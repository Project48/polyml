/*
    Title:      Thread functions
    Author:     David C.J. Matthews

    Copyright (c) 2007,2008 David C.J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

#ifdef HAVE_PROCESS_H
#include <process.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h> // Want unistd for _SC_NPROCESSORS_ONLN at least
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif

#if ((!defined(WIN32) || defined(__CYGWIN__)) && defined(HAVE_LIBPTHREAD) && defined(HAVE_PTHREAD_H))
#define HAVE_PTHREAD 1
#include <pthread.h>
#endif

#ifdef HAVE_SYS_SYSCTL_H
// Used determine number of processors in Mac OS X.
#include <sys/sysctl.h>
#endif

#include <new>

/************************************************************************
 *
 * Include runtime headers
 *
 ************************************************************************/

#include "globals.h"
#include "gc.h"
#include "mpoly.h"
#include "arb.h"
#include "machine_dep.h"
#include "diagnostics.h"
#include "processes.h"
#include "run_time.h"
#include "sys.h"
#include "sighandler.h"
#include "scanaddrs.h"
#include "save_vec.h"
#include "rts_module.h"
#include "noreturn.h"
#include "memmgr.h"
#include "locking.h"
#include "profiling.h"
#include "sharedata.h"
#include "exporter.h"

#ifdef WINDOWS_PC
#include "Console.h"
#endif

#define SAVE(x) taskData->saveVec.push(x)
#define SIZEOF(x) (sizeof(x)/sizeof(PolyWord))

// These values are stored in the second word of thread id object as
// a tagged integer.  They may be set and read by the thread in the ML
// code.  
#define PFLAG_BROADCAST     1   // If set, accepts a broadcast
// How to handle interrrupts
#define PFLAG_IGNORE        0   // Ignore interrupts completely
#define PFLAG_SYNCH         2   // Handle synchronously
#define PFLAG_ASYNCH        4   // Handle asynchronously
#define PFLAG_ASYNCH_ONCE   6   // First handle asynchronously then switch to synch.
#define PFLAG_INTMASK       6   // Mask of the above bits


// Other threads may make requests to a thread.
typedef enum {
    kRequestNone = 0, // Increasing severity
    kRequestInterrupt = 1,
    kRequestKill = 2
} ThreadRequests;

class ProcessTaskData: public TaskData
{
public:
    ProcessTaskData();
    ~ProcessTaskData();

    virtual void Lock(void) {}
    virtual void Unlock(void) {}

    virtual void GarbageCollect(ScanAddress *process);

    // If a thread has to block it will block on this.
    PCondVar threadLock;
    // External requests made are stored here until they
    // can be actioned.
    ThreadRequests requests;
    // Pointer to the mutex when blocked. Set to NULL when it doesn't apply.
    PolyObject *blockMutex;
    // This is set to false when a thread blocks or enters foreign code,
    // While it is true the thread can manipulate ML memory so no other
    // thread can garbage collect.
    bool inMLHeap;

    // In Linux, at least, we need to run a separate timer in each thread
    bool runningProfileTimer;

#ifdef HAVE_WINDOWS_H
    LONGLONG lastCPUTime; // Used for profiling
#endif
#ifdef HAVE_PTHREAD
    pthread_t pthreadId;
#endif
#ifdef HAVE_WINDOWS_H
    HANDLE threadHandle;
#endif
};

class Processes: public ProcessExternal, public RtsModule
{
public:
    Processes();
    virtual void Init(void);
    virtual void Uninit(void);
    virtual void Reinit(void);
    void GarbageCollect(ScanAddress *process);
public:
    void BroadcastInterrupt(void);
    void BeginRootThread(PolyObject *rootFunction);
    void Exit(int n); // Request all ML threads to exit and set the process result code.
    // Called when a thread has completed - doesn't return.
    virtual NORETURNFN(void ThreadExit(TaskData *taskData));

    void BlockAndRestart(TaskData *taskData, Waiter *pWait, bool posixInterruptable, int ioCall);
    // Called when a thread may block.  Returns some time later when perhaps
    // the input is available.
    virtual void ThreadPauseForIO(TaskData *taskData, Waiter *pWait);
    // Return the task data for the current thread.
    virtual TaskData *GetTaskDataForThread(void);
    // ForkFromRTS.  Creates a new thread from within the RTS.
    virtual bool ForkFromRTS(TaskData *taskData, Handle proc, Handle arg);
    // Create a new thread.  The "args" argument is only used for threads
    // created in the RTS by the signal handler.
    Handle ForkThread(ProcessTaskData *taskData, Handle threadFunction,
                    Handle args, PolyWord flags);
    // Process general RTS requests from ML.
    Handle ThreadDispatch(TaskData *taskData, Handle args, Handle code);

    virtual void ThreadUseMLMemory(TaskData *taskData); 
    virtual void ThreadReleaseMLMemory(TaskData *taskData);

    // If the schedule lock is already held we need to use these functions.
    void ThreadUseMLMemoryWithSchedLock(TaskData *taskData);
    void ThreadReleaseMLMemoryWithSchedLock(TaskData *taskData);

    // Requests from the threads for actions that need to be performed by
    // the root thread. Make the request and wait until it has completed.
    virtual void MakeRootRequest(TaskData *taskData, MainThreadRequest *request);

    // Deal with any interrupt or kill requests.
    virtual bool ProcessAsynchRequests(TaskData *taskData);
    // Process an interrupt request synchronously.
    virtual void TestSynchronousRequests(TaskData *taskData);
    // Process any events, synchronous or asynchronous.
    virtual void TestAnyEvents(TaskData *taskData);

    // Set a thread to be interrupted or killed.  Wakes up the
    // thread if necessary.  MUST be called with taskArrayLock held.
    void MakeRequest(ProcessTaskData *p, ThreadRequests request);

    // Profiling control.
    virtual void StartProfiling(void);
    virtual void StopProfiling(void);

#ifdef HAVE_WINDOWS_H
    // Windows: Called every millisecond while profiling is on.
    void ProfileInterrupt(void);
#else
    // Unix: Start a profile timer for a thread.
    void StartProfilingTimer(void);
#endif
    // Memory allocation.  Tries to allocate space.  If the allocation succeeds it
    // may update the allocation values in the taskData object.  If the heap is exhausted
    // it may set this thread (or other threads) to raise an exception.
    PolyWord *FindAllocationSpace(TaskData *taskData, POLYUNSIGNED words, bool alwaysInSeg);

    // Find a task that matches the specified identifier and returns
    // it if it exists.  MUST be called with taskArrayLock held.
    ProcessTaskData *TaskForIdentifier(Handle taskId);

    // Signal handling support.  The ML signal handler thread blocks until it is
    // woken up by the signal detection thread.
    virtual bool WaitForSignal(TaskData *taskData, PLock *sigLock);
    virtual void SignalArrived(void);

    virtual void SetSingleThreaded(void) { singleThreaded = true; }


    // Generally, the system runs with multiple threads.  After a
    // fork, though, there is only one thread.
    bool singleThreaded;

    // Each thread has an entry in this array.
    ProcessTaskData **taskArray;
    unsigned taskArraySize; // Current size of the array.

    /* schedLock: This lock must be held when making scheduling decisions.
       It must also be held before adding items to taskArray, removing
       them or scanning the array.
       It must also be held before deleting a TaskData object
       or using it in a thread other than the "owner"  */
    PLock schedLock;
#ifdef HAVE_PTHREAD
    pthread_key_t tlsId;
#elif defined(HAVE_WINDOWS_H)
    DWORD tlsId;
#endif

    // We make an exception packet for Interrupt and store it here.
    // This exception can be raised if we run out of store so we need to
    // make sure we have the packet before we do.
    poly_exn *interrupt_exn;

    /* initialThreadWait: The initial thread waits on this for
       wake-ups from the ML threads requesting actions such as GC or
       close-down. */
    PCondVar initialThreadWait;
    // A requesting thread sets this to indicate the request.  This value
    // is only reset once the request has been satisfied.
    MainThreadRequest *threadRequest;

    PCondVar mlThreadWait;  // All the threads block on here until the request has completed.

    int exitResult;
    bool exitRequest;
    // Shutdown locking.
    void CrowBarFn(void);
    PLock shutdownLock;
    PCondVar crowbarLock, crowbarStopped;
    bool crowbarRunning;

#ifdef HAVE_WINDOWS_H
    // Used in profiling
    HANDLE hStopEvent; /* Signalled to stop all threads. */
    HANDLE profilingHd;
    HANDLE mainThreadHandle; // The same as hMainThread except on Cygwin
    LONGLONG lastCPUTime; // CPU used by main thread.
#endif

    ProcessTaskData *sigTask;  // Pointer to current signal task.
};

// Global process data.
static Processes processesModule;
ProcessExternal *processes = &processesModule;

Processes::Processes(): singleThreaded(false), taskArray(0), taskArraySize(0), interrupt_exn(0),
    threadRequest(0), exitResult(0), exitRequest(false),
    crowbarRunning(false), sigTask(0)
{
#ifdef HAVE_WINDOWS_H
    Waiter::hWakeupEvent = NULL;
    hStopEvent = NULL;
    profilingHd = NULL;
    lastCPUTime = 0;
    mainThreadHandle = NULL;
#endif
}

enum _mainThreadPhase mainThreadPhase = MTP_USER_CODE;

// Get the attribute flags.
static POLYUNSIGNED ThreadAttrs(TaskData *taskData)
{
    return UNTAGGED_UNSIGNED(taskData->threadObject->flags);
}

// As far as possible we want locking and unlocking an ML mutex to be fast so
// we try to implement the code in the assembly code using appropriate
// interlocked instructions.  That does mean that if we need to lock and
// unlock an ML mutex in this code we have to use the same, machine-dependent,
// code to do it.  These are defaults that are used where there is no
// machine-specific code.

// Increment the value contained in the first word of the mutex.
// On most platforms this code will be done with a piece of assembly code.
PLock mutexLock;

Handle MachineDependent::AtomicIncrement(TaskData *taskData, Handle mutexp)
{
    mutexLock.Lock();
    PolyObject *p = DEREFHANDLE(mutexp);
    // A thread can only call this once so the values will be short
    PolyWord newValue = TAGGED(UNTAGGED(p->Get(0))+1);
    p->Set(0, newValue);
    mutexLock.Unlock();
    return SAVE(newValue);
}

// Decrement the value contained in the first word of the mutex.
Handle MachineDependent::AtomicDecrement(TaskData *taskData, Handle mutexp)
{
    mutexLock.Lock();
    PolyObject *p = DEREFHANDLE(mutexp);
    PolyWord newValue = TAGGED(UNTAGGED(p->Get(0))-1);
    p->Set(0, newValue);
    mutexLock.Unlock();
    return SAVE(newValue);
}

// Release a mutex and if necessary apply a write barrier.
// This is the default code for platforms that don't need a barrier.
void MachineDependent::SetToReleased(TaskData * /*taskData*/, Handle mutexp)
{
    DEREFHANDLE(mutexp)->Set(0, TAGGED(1)); // Set this to released.
}

// Called from interface vector.  Generally the assembly code will be
// used instead of this.
Handle AtomicIncrement(TaskData *taskData, Handle mutexp)
{
    return machineDependent->AtomicIncrement(taskData, mutexp);
}

// Called from interface vector.  Generally the assembly code will be
// used instead of this.
Handle AtomicDecrement(TaskData *taskData, Handle mutexp)
{
    return machineDependent->AtomicDecrement(taskData, mutexp);
}

// Return the thread object for the current thread.
// On most platforms this will be done with a piece of assembly code.
Handle ThreadSelf(TaskData *taskData)
{
    return SAVE(taskData->threadObject);
}

// Called from interface vector.  This is the normal entry point for
// the thread functions.
Handle ThreadDispatch(TaskData *taskData, Handle args, Handle code)
{
    return processesModule.ThreadDispatch(taskData, args, code);
}

Handle Processes::ThreadDispatch(TaskData *taskData, Handle args, Handle code)
{
    int c = get_C_long(taskData, DEREFWORDHANDLE(code));
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;
    switch (c)
    {
    case 1: /* A mutex was locked i.e. the count was ~1 or less.  We will have set it to
               ~1. This code blocks if the count is still ~1.  It does actually return
               if another thread tries to lock the mutex and hasn't yet set the value
               to ~1 but that doesn't matter since whenever we return we simply try to
               get the lock again. */
        {
            schedLock.Lock();
            // We have to check the value again with schedLock held rather than
            // simply waiting because otherwise the unlocking thread could have
            // set the variable back to 1 (unlocked) and signalled any waiters
            // before we actually got to wait.  
            if (UNTAGGED(DEREFHANDLE(args)->Get(0)) < 0)
            {
                // Set this so we can see what we're blocked on.
                ptaskData->blockMutex = DEREFHANDLE(args);
                // Now release the ML memory.  A GC can start.
                ThreadReleaseMLMemoryWithSchedLock(ptaskData);
                // Wait until we're woken up.  We mustn't block if we have been
                // interrupted, and are processing interrupts asynchronously, or
                // we've been killed.
                switch (ptaskData->requests)
                {
                case kRequestKill:
                    // We've been killed.  Handle this later.
                    break;
                case kRequestInterrupt:
                    {
                        // We've been interrupted.  
                        POLYUNSIGNED attrs = ThreadAttrs(ptaskData) & PFLAG_INTMASK;
                        if (attrs == PFLAG_ASYNCH || attrs == PFLAG_ASYNCH_ONCE)
                            break;
                        // If we're ignoring interrupts or handling them synchronously
                        // we don't do anything here.
                    }
                case kRequestNone:
                    ptaskData->threadLock.Wait(&schedLock);
                }
                ptaskData->blockMutex = 0; // No longer blocked.
                ThreadUseMLMemoryWithSchedLock(ptaskData);
            }
            // Return and try and get the lock again.
            schedLock.Unlock();
            // Test to see if we have been interrupted and if this thread
            // processes interrupts asynchronously we should raise an exception
            // immediately.  Perhaps we do that whenever we exit from the RTS.
            return SAVE(TAGGED(0));
       }

    case 2: /* Unlock a mutex.  Called after incrementing the count and discovering
               that at least one other thread has tried to lock it.  We may need
               to wake up threads that are blocked. */
        {
            // The caller has already set the variable to 1 (unlocked).
            // We need to acquire schedLock so that we can
            // be sure that any thread that is trying to lock sees either
            // the updated value (and so doesn't wait) or has successfully
            // waited on its threadLock (and so will be woken up).
            schedLock.Lock();
            // Unlock any waiters.
            for (unsigned i = 0; i < taskArraySize; i++)
            {
                ProcessTaskData *p = taskArray[i];
                // If the thread is blocked on this mutex we can signal the thread.
                if (p && p->blockMutex == DEREFHANDLE(args))
                    p->threadLock.Signal();
            }
            schedLock.Unlock();
            return SAVE(TAGGED(0));
       }

    case 3: // Atomically drop a mutex and wait for a wake up.  
        {
            // The argument is a pair of a mutex and the time to wake up.  The time
            // may be zero to indicate an infinite wait.  The return value is unit.
            // It WILL NOT RAISE AN EXCEPTION unless it is set to handle exceptions
            // asynchronously (which it shouldn't do if the ML caller code is correct).
            // It may return as a result of any of the following:
            //      an explicit wake up.
            //      an interrupt, either direct or broadcast
            //      a trap i.e. a request to handle an asynchronous event.
            Handle mutexH = SAVE(args->WordP()->Get(0));
            Handle wakeTime = SAVE(args->WordP()->Get(1));
            // We pass zero as the wake time to represent infinity.
            bool isInfinite = compareLong(taskData, wakeTime, SAVE(TAGGED(0))) == 0;

            // Convert the time into the correct format for WaitUntil before acquiring
            // schedLock.  div_longc could do a GC which requires schedLock.
#ifdef HAVE_PTHREAD
            struct timespec tWake;
            if (! isInfinite)
            {
                // On Unix we represent times as a number of microseconds.
                Handle hMillion = Make_arbitrary_precision(taskData, 1000000);
                tWake.tv_sec =
                    get_C_ulong(taskData, DEREFWORDHANDLE(div_longc(taskData, hMillion, wakeTime)));
                tWake.tv_nsec =
                    1000*get_C_ulong(taskData, DEREFWORDHANDLE(rem_longc(taskData, hMillion, wakeTime)));
            }
#elif defined(HAVE_WINDOWS_H)
            // On Windows it is the number of 100ns units since the epoch
            FILETIME tWake;
            if (! isInfinite)
            {
                get_C_pair(taskData, DEREFWORDHANDLE(wakeTime),
                    (unsigned long*)&tWake.dwHighDateTime, (unsigned long*)&tWake.dwLowDateTime);
            }
#else
            int tWake = 0; // Not actually used in single-threaded.
#endif
            schedLock.Lock();
            // Atomically release the mutex.  This is atomic because we hold schedLock
            // so no other thread can call signal or broadcast.
            Handle decrResult = machineDependent->AtomicIncrement(taskData, mutexH);
            if (UNTAGGED(decrResult->Word()) != 1)
            {
                machineDependent->SetToReleased(taskData, mutexH);
                // The mutex was locked so we have to release any waiters.
                // Unlock any waiters.
                for (unsigned i = 0; i < taskArraySize; i++)
                {
                    ProcessTaskData *p = taskArray[i];
                    // If the thread is blocked on this mutex we can signal the thread.
                    if (p && p->blockMutex == DEREFHANDLE(mutexH))
                        p->threadLock.Signal();
                }
            }
            // Wait until we're woken up.  Don't block if we have been interrupted
            // or killed.
            if (ptaskData->requests == kRequestNone)
            {
                // Now release the ML memory.  A GC can start.
                ThreadReleaseMLMemoryWithSchedLock(ptaskData);
                // We pass zero as the wake time to represent infinity.
                if (isInfinite)
                    ptaskData->threadLock.Wait(&schedLock);
                else (void)ptaskData->threadLock.WaitUntil(&schedLock, &tWake);
                // We want to use the memory again.
                ThreadUseMLMemoryWithSchedLock(ptaskData);
            }
            schedLock.Unlock();
            return SAVE(TAGGED(0));
        }

    case 4: // Wake up the specified thread.  Returns false (0) if the thread has
        // already been interrupted and is not ignoring interrupts or if the thread
        // does not exist (i.e. it's been killed while waiting).  Returns true
        // if it successfully woke up the thread.  The thread may subsequently
        // receive an interrupt but we need to know whether we woke the thread
        // up before that happened.
        {
            int result = 0; // Default to failed.
            // Acquire the schedLock first.  This ensures that this is
            // atomic with respect to waiting.
            schedLock.Lock();
            ProcessTaskData *p = TaskForIdentifier(args);
            if (p && p->threadObject == args->WordP())
            {
                POLYUNSIGNED attrs = ThreadAttrs(p) & PFLAG_INTMASK;
                if (p->requests == kRequestNone ||
                    (p->requests == kRequestInterrupt && attrs == PFLAG_IGNORE))
                {
                    p->threadLock.Signal();
                    result = 1;
                }
            }
            schedLock.Unlock();
            return SAVE(TAGGED(result));
        }

        // 5 and 6 are no longer used.

    case 7: // Fork a new thread.  The arguments are the function to run and the attributes.
            return ForkThread(ptaskData, SAVE(args->WordP()->Get(0)),
                        (Handle)0, args->WordP()->Get(1));

    case 8: // Test if a thread is active
        {
            schedLock.Lock();
            ProcessTaskData *p = TaskForIdentifier(args);
            schedLock.Unlock();
            return SAVE(TAGGED(p != 0));
        }

    case 9: // Send an interrupt to a specific thread
        {
            schedLock.Lock();
            ProcessTaskData *p = TaskForIdentifier(args);
            if (p) MakeRequest(p, kRequestInterrupt);
            schedLock.Unlock();
            if (p == 0)
                raise_exception_string(taskData, EXC_thread, "Thread does not exist");
            return SAVE(TAGGED(0));
        }

    case 10: // Broadcast an interrupt to all threads that are interested.
        BroadcastInterrupt();
        return SAVE(TAGGED(0));

    case 11: // Interrupt this thread now if it has been interrupted
        TestSynchronousRequests(taskData);
        // Also process any asynchronous requests that may be pending.
        // These will be handled "soon" but if we have just switched from deferring
        // interrupts this guarantees that any deferred interrupts will be handled now.
        if (ProcessAsynchRequests(taskData))
            throw IOException(EXC_EXCEPTION);
        return SAVE(TAGGED(0));

    case 12: // Kill a specific thread
        {
            schedLock.Lock();
            ProcessTaskData *p = TaskForIdentifier(args);
            if (p) MakeRequest(p, kRequestKill);
            schedLock.Unlock();
            if (p == 0)
                raise_exception_string(taskData, EXC_thread, "Thread does not exist");
            return SAVE(TAGGED(0));
        }

    case 13: // Return the number of processors.
        // Returns 1 if there is any problem.
        return Make_unsigned(taskData, NumberOfProcessors());

    default:
        {
            char msg[100];
            sprintf(msg, "Unknown thread function: %d", c);
            raise_fail(taskData, msg);
			return 0;
        }
    }
}

TaskData::TaskData(): mdTaskData(0), allocPointer(0), allocLimit(0), allocSize(MIN_HEAP_SIZE), allocCount(0),
        stack(0), threadObject(0), signalStack(0), pendingInterrupt(false)
{
}

TaskData::~TaskData()
{
    if (signalStack) free(signalStack);
    if (stack) gMem.DeleteStackSpace(stack);
    delete(mdTaskData);
}


// Fill unused allocation space with a dummy object to preserve the invariant
// that memory is always valid.
void TaskData::FillUnusedSpace(void)
{
    if (allocPointer > allocLimit)
        gMem.FillUnusedSpace(allocLimit, allocPointer-allocLimit); 
}

ProcessTaskData::ProcessTaskData(): requests(kRequestNone), blockMutex(0), inMLHeap(false),
        runningProfileTimer(false)
{
#ifdef HAVE_WINDOWS_H
    lastCPUTime = 0;
#endif
#ifdef HAVE_PTHREAD
    pthreadId = 0;
#endif
#ifdef HAVE_WINDOWS_H
    threadHandle = 0;
#endif
}

ProcessTaskData::~ProcessTaskData()
{
#ifdef HAVE_WINDOWS_H
    if (threadHandle) CloseHandle(threadHandle);
#endif
}


// Find a task that matches the specified identifier and returns
// it if it exists.  MUST be called with taskArrayLock held.
ProcessTaskData *Processes::TaskForIdentifier(Handle taskId)
{
    // The index is in the first word of the thread object.
    unsigned index = UNTAGGED_UNSIGNED(taskId->WordP()->Get(0));
    // Check the index is valid and matches the object stored in the table.
    if (index < taskArraySize)
    {
        ProcessTaskData *p = taskArray[index];
        if (p && p->threadObject == taskId->WordP())
            return p;
    }
    return 0;
}
// Broadcast an interrupt to all relevant threads.
void Processes::BroadcastInterrupt(void)
{
    // If a thread is set to accept broadcast interrupts set it to
    // "interrupted".
    schedLock.Lock();
    for (unsigned i = 0; i < taskArraySize; i++)
    {
        ProcessTaskData *p = taskArray[i];
        if (p)
        {
            POLYUNSIGNED attrs = ThreadAttrs(p);
            if (attrs & PFLAG_BROADCAST)
                MakeRequest(p, kRequestInterrupt);
        }
    }
    schedLock.Unlock();
}

// Set the asynchronous request variable for the thread.  Must be called
// with the schedLock held.  Tries to wake the thread up if possible.
void Processes::MakeRequest(ProcessTaskData *p, ThreadRequests request)
{
    // We don't override a request to kill by an interrupt request.
    if (p->requests < request)
    {
        p->requests = request;
        machineDependent->InterruptCode(p);
        p->threadLock.Signal();
        // Set the value in the ML object as well so the ML code can see it
        p->threadObject->requestCopy = TAGGED(request);
    }
#ifdef HAVE_WINDOWS_H
    // Wake any threads waiting for IO
    PulseEvent(Waiter::hWakeupEvent);
#endif
}

void Processes::ThreadExit(TaskData *taskData)
{
    if (debugOptions & DEBUG_THREADS)
        Log("THREAD: Thread %p exiting\n", taskData);

    if (singleThreaded) finish(0);

    schedLock.Lock();
    ThreadReleaseMLMemoryWithSchedLock(taskData); // Allow a GC if it was waiting for us.
    // Remove this from the taskArray
    unsigned index = UNTAGGED(taskData->threadObject->index);
    ASSERT(index < taskArraySize && taskArray[index] == taskData);
    taskArray[index] = 0;
    delete(taskData);
    initialThreadWait.Signal(); // Tell it we've finished.
    schedLock.Unlock();
#ifdef HAVE_PTHREAD
    pthread_exit(0);
#elif defined(HAVE_WINDOWS_H)
    ExitThread(0);
#endif
}

// These two functions are used for calls from outside where
// the lock has not yet been acquired.
void Processes::ThreadUseMLMemory(TaskData *taskData)
{
    // Trying to acquire the lock here may block if a GC is in progress
    schedLock.Lock();
    ThreadUseMLMemoryWithSchedLock(taskData);
    schedLock.Unlock();
}

void Processes::ThreadReleaseMLMemory(TaskData *taskData)
{
    schedLock.Lock();
    ThreadReleaseMLMemoryWithSchedLock(taskData);
    schedLock.Unlock();
}

// Called when a thread wants to resume using the ML heap.  That could
// be after a wait for some reason or after executing some foreign code.
// Since there could be a GC in progress already at this point we may either
// be blocked waiting to acquire schedLock or we may need to wait until
// we are woken up at the end of the GC.
void Processes::ThreadUseMLMemoryWithSchedLock(TaskData *taskData)
{
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;
    // If there is a request outstanding we have to wait for it to
    // complete.  We notify the root thread and wait for it.
    while (threadRequest != 0)
    {
        initialThreadWait.Signal();
        // Wait for the GC to happen
        mlThreadWait.Wait(&schedLock);
    }
    ASSERT(! ptaskData->inMLHeap);
    ptaskData->inMLHeap = true;
}

// Called to indicate that the thread has temporarily finished with the
// ML memory either because it is going to wait for something or because
// it is going to run foreign code.  If there is an outstanding GC request
// that can proceed.
void Processes::ThreadReleaseMLMemoryWithSchedLock(TaskData *taskData)
{
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;
    ASSERT(ptaskData->inMLHeap);
    ptaskData->inMLHeap = false;
    // Put a dummy object in any unused space.  This maintains the
    // invariant that the allocated area is filled with valid objects.
    ptaskData->FillUnusedSpace();
    //
    if (threadRequest != 0)
        initialThreadWait.Signal();
}


// Make a request to the root thread.
void Processes::MakeRootRequest(TaskData *taskData, MainThreadRequest *request)
{
    if (singleThreaded)
    {
        mainThreadPhase = request->mtp;
        request->Perform();
        mainThreadPhase = MTP_USER_CODE;
    }
    else
    {
        PLocker locker(&schedLock);

        // Wait for any other requests. 
        while (threadRequest != 0)
        {
            // Deal with any pending requests.
            ThreadReleaseMLMemoryWithSchedLock(taskData);
            ThreadUseMLMemoryWithSchedLock(taskData); // Drops schedLock while waiting.
        }
        // Now the other requests have been dealt with (and we have schedLock).
        request->completed = false;
        threadRequest = request;
        // Wait for it to complete.
        while (! request->completed)
        {
            ThreadReleaseMLMemoryWithSchedLock(taskData);
            ThreadUseMLMemoryWithSchedLock(taskData); // Drops schedLock while waiting.
        }
    }
}

// Find space for an object.  Returns a pointer to the start.  "words" must include
// the length word and the result points at where the length word will go.
PolyWord *Processes::FindAllocationSpace(TaskData *taskData, POLYUNSIGNED words, bool alwaysInSeg)
{
    bool triedInterrupt = false;

    while (1)
    {
        // After a GC allocPointer and allocLimit are zero and when allocating the
        // heap segment we request a minimum of zero words.
        if (taskData->allocPointer != 0 && taskData->allocPointer >= taskData->allocLimit + words)
        {
            // There's space in the current segment,
            taskData->allocPointer -= words;
            return taskData->allocPointer;
        }
        else // Insufficient space in this area. 
        {
            if (words > taskData->allocSize && ! alwaysInSeg)
            {
                // If the object we want is larger than the heap segment size
                // we allocate it separately rather than in the segment.
                PolyWord *foundSpace = gMem.AllocHeapSpace(words);
                if (foundSpace) return foundSpace;
            }
            else
            {
                // Fill in any unused space in the existing segment
                taskData->FillUnusedSpace();
                // Get another heap segment with enough space for this object.
                POLYUNSIGNED requestSpace = taskData->allocSize+words;
                POLYUNSIGNED spaceSize = requestSpace;
                // Get the space and update spaceSize with the actual size.
                PolyWord *space = gMem.AllocHeapSpace(words, spaceSize);
                if (space)
                {
                    // Double the allocation size for the next time if
                    // we succeeded in allocating the whole space.
                    taskData->allocCount++; 
                    if (spaceSize == requestSpace) taskData->allocSize = taskData->allocSize*2;
                    taskData->allocLimit = space;
                    taskData->allocPointer = space+spaceSize;
                    // Actually allocate the object
                    taskData->allocPointer -= words;
                    return taskData->allocPointer;
                }
            }

            // Try garbage-collecting.  If this failed return 0.
            if (! QuickGC(taskData, words))
            {
                if (! triedInterrupt)
                {
                    triedInterrupt = true;
                    fprintf(stderr,"Run out of store - interrupting threads\n");
                    if (debugOptions & DEBUG_THREADS)
                        Log("THREAD: Run out of store, interrupting threads\n");
                    BroadcastInterrupt();
                    if (ProcessAsynchRequests(taskData))
                        return 0; // Has been interrupted.
                    // Not interrupted: pause this thread to allow for other
                    // interrupted threads to free something.
#if defined(WINDOWS_PC)
                    Sleep(5000);
#else
                    sleep(5);
#endif
                    // Try again.
                }
                else {
                    // That didn't work.  Exit.
                    fprintf(stderr,"Failed to recover - exiting\n");
                    Exit(1);
                }
             }
            // Try again.  There should be space now.
        }
    }
}


Handle exitThread(TaskData *taskData)
/* A call to this is put on the stack of a new thread so when the
   thread function returns the thread goes away. */  
{
    processesModule.ThreadExit(taskData);
}

/* Called when a thread is about to block, usually because of IO.
   If this is interruptable (currently only used for Posix functions)
   the process will be set to raise an exception if any signal is handled.
   It may also raise an exception if another thread has called
   broadcastInterrupt. */
void Processes::ThreadPauseForIO(TaskData *taskData, Waiter *pWait)
{
    TestAnyEvents(taskData); // Consider this a blocking call that may raise Interrupt
    ThreadReleaseMLMemory(taskData);
    pWait->Wait(1000); // Wait up to a second
    ThreadUseMLMemory(taskData);
    TestAnyEvents(taskData); // Check if we've been interrupted.
}

// This is largely a legacy of the old single-thread version.  In that version there
// was only a single C thread managing multiple ML threads (processes) so if an ML
// thread blocked it was necessary to switch the thread and then for the C function
// call to raise an exception to get back to ML.  
// TODO: There's actually a race here if we have posixInterruptible set.  We
// repeatedly come back here and if a signal happens while we're in
// ThreadPauseForIO we will raise the exception.  If the signal happens at
// another point we won't.
void Processes::BlockAndRestart(TaskData *taskData, Waiter *pWait, bool posixInterruptable, int ioCall)
{
    if (pWait == NULL) pWait = Waiter::defaultWaiter;
    machineDependent->SetForRetry(taskData, ioCall);
    unsigned lastSigCount = receivedSignalCount;
    ThreadPauseForIO(taskData, pWait);
    // If this is an interruptible Posix function we raise an exception if
    // there has been a signal.
    if (posixInterruptable && lastSigCount != receivedSignalCount)
        raise_syscall(taskData, "Call interrupted by signal", EINTR);
    throw IOException(EXC_EXCEPTION);
    /* NOTREACHED */
}

// Default waiter: simply wait for the time.  In the case of Windows it
// is also woken up if the event is signalled.  In Unix it may be woken
// up by a signal.
void Waiter::Wait(unsigned maxMillisecs)
{
    // Since this is used only when we can't monitor the source directly
    // we set this to 10ms so that we're not waiting too long.
    if (maxMillisecs > 10) maxMillisecs = 10;
#ifdef WINDOWS_PC
    /* We seem to need to reset the queue before calling
       MsgWaitForMultipleObjects otherwise it frequently returns
       immediately, often saying there is a message with a message ID
       of 0x118 which doesn't correspond to any listed message.
       While calling PeekMessage afterwards might be better this doesn't
       seem to work properly.  We need to use MsgWaitForMultipleObjects
       here so that we get a reasonable response with the Windows GUI. */
    MSG msg;
    // N.B.  It seems that calling PeekMessage may result in a callback
    // to a window proc directly without a call to DispatchMessage.  That
    // could result in a recursive call here if we have installed an ML
    // window proc.
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);
    // Wait until we get input or we're woken up.
    MsgWaitForMultipleObjects(1, &hWakeupEvent, FALSE, maxMillisecs, QS_ALLINPUT);
#else
    // Unix
    fd_set read_fds, write_fds, except_fds;
    struct timeval toWait = { 0, 0 };
    toWait.tv_sec = maxMillisecs / 1000;
    toWait.tv_usec = (maxMillisecs % 1000) * 1000;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
    select(FD_SETSIZE, &read_fds, &write_fds, &except_fds, &toWait);
#endif
}

static Waiter defWait;
Waiter *Waiter::defaultWaiter = &defWait;

#ifdef HAVE_WINDOWS_H
// Windows and Cygwin
HANDLE Waiter::hWakeupEvent; // Pulsed to wake up any threads waiting for IO.

// Wait for the specified handle to be signalled.
void WaitHandle::Wait(unsigned maxMillisecs)
{
    MSG msg;
    PeekMessage(&msg, 0, 0, 0, PM_NOREMOVE);

    HANDLE hEvents[2];
    DWORD dwEvents = 0;
    hEvents[dwEvents++] = Waiter::hWakeupEvent;
    if (m_Handle != NULL)
        hEvents[dwEvents++] = m_Handle;
    // Wait until we get input or we're woken up.
    MsgWaitForMultipleObjects(dwEvents, hEvents, FALSE, maxMillisecs, QS_ALLINPUT);
}
#endif

#ifndef WINDOWS_PC
// Unix and Cygwin: Wait for a file descriptor on input.
void WaitInputFD::Wait(unsigned maxMillisecs)
{
    fd_set read_fds, write_fds, except_fds;
    struct timeval toWait = { 0, 0 };
    toWait.tv_sec = maxMillisecs / 1000;
    toWait.tv_usec = (maxMillisecs % 1000) * 1000;
    FD_ZERO(&read_fds);
    if (m_waitFD >= 0) FD_SET(m_waitFD, &read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
    select(FD_SETSIZE, &read_fds, &write_fds, &except_fds, &toWait);
}
#endif

// Get the task data for the current thread.  This is held in
// thread-local storage.  Normally this is passed in taskData but
// in a few cases this isn't available.
TaskData *Processes::GetTaskDataForThread(void)
{
#ifdef HAVE_PTHREAD
    return (TaskData *)pthread_getspecific(tlsId);
#elif defined(HAVE_WINDOWS_H)
    return (TaskData *)TlsGetValue(tlsId);
#else
    // If there's no threading.
    return taskArray[0];
#endif
}

// This function is run when a new thread has been forked.  The
// parameter is the taskData value for the new thread.  This function
// is also called directly for the main thread.
#ifdef HAVE_PTHREAD
static void *NewThreadFunction(void *parameter)
{
    ProcessTaskData *taskData = (ProcessTaskData *)parameter;
#ifdef HAVE_WINDOWS_H
    // Cygwin: Get the Windows thread handle in case it's needed for profiling.
    HANDLE thisProcess = GetCurrentProcess();
    DuplicateHandle(thisProcess, GetCurrentThread(), thisProcess, 
        &(taskData->threadHandle), THREAD_ALL_ACCESS, FALSE, 0);
#endif
    initThreadSignals(taskData);
    pthread_setspecific(processesModule.tlsId, taskData);
    taskData->saveVec.init(); // Removal initial data
    processes->ThreadUseMLMemory(taskData);
    try {
        (void)EnterPolyCode(taskData); // Will normally (always?) call ExitThread.
    }
    catch (KillException) {
        processesModule.ThreadExit(taskData);
    }

    return 0;
}
#elif defined(HAVE_WINDOWS_H)
static DWORD WINAPI NewThreadFunction(void *parameter)
{
    ProcessTaskData *taskData = (ProcessTaskData *)parameter;
    TlsSetValue(processesModule.tlsId, taskData);
    taskData->saveVec.init(); // Removal initial data
    processes->ThreadUseMLMemory(taskData);
    try {
        (void)EnterPolyCode(taskData);
    }
    catch (KillException) {
        processesModule.ThreadExit(taskData);
    }
    return 0;
}
#else
static void NewThreadFunction(void *parameter)
{
    ProcessTaskData *taskData = (ProcessTaskData *)parameter;
    initThreadSignals(taskData);
    taskData->saveVec.init(); // Removal initial data
    processes->ThreadUseMLMemory(taskData);
    try {
        (void)EnterPolyCode(taskData);
    }
    catch (KillException) {
        processesModule.ThreadExit(taskData);
    }
}
#endif

// Sets up the initial thread from the root function.  This is run on
// the initial thread of the process so it will work if we don't
// have pthreads.
// When multithreading this thread also deals with all garbage-collection
// and similar operations and the ML threads send it requests to deal with
// that.  These require all the threads to pause until the operation is complete
// since they affect all memory but they are also sometimes highly recursive.
// On Mac OS X and on Linux if the stack limit is set to unlimited only the
// initial thread has a large stack and newly created threads have smaller
// stacks.  We need to make sure that any significant stack usage occurs only
// on the inital thread.
void Processes::BeginRootThread(PolyObject *rootFunction)
{
    if (taskArraySize < 1)
    {
        taskArray = (ProcessTaskData **)realloc(taskArray, sizeof(ProcessTaskData *));
        if (taskArray == 0) ::Exit("Unable to create the initial thread - insufficient memory");
        taskArraySize = 1;
    }

    try {
        // We can't use ForkThread because we don't have a taskData object before we start
        ProcessTaskData *taskData = new ProcessTaskData;
        taskData->mdTaskData = machineDependent->CreateTaskData();
        taskData->threadObject = (ThreadObject*)alloc(taskData, 4, F_MUTABLE_BIT);
        taskData->threadObject->index = TAGGED(0); // Index 0
        // The initial thread is set to accept broadcast interrupt requests
        // and handle them synchronously.  This is for backwards compatibility.
        taskData->threadObject->flags = TAGGED(PFLAG_BROADCAST|PFLAG_ASYNCH); // Flags
        taskData->threadObject->threadLocal = TAGGED(0); // Empty thread-local store
        taskData->threadObject->requestCopy = TAGGED(0); // Cleared interrupt state
#ifdef HAVE_PTHREAD
        taskData->pthreadId = pthread_self();
#elif defined(HAVE_WINDOWS_H)
        taskData->threadHandle = hMainThread;
#endif
        taskArray[0] = taskData;

        taskData->stack = gMem.NewStackSpace(machineDependent->InitialStackSize());
        if (taskData->stack == 0)
            ::Exit("Unable to create the initial thread - insufficient memory");

        machineDependent->InitStackFrame(taskData, taskData->stack,
                taskData->saveVec.push(rootFunction), (Handle)0);

        // Create a packet for the Interrupt exception once so that we don't have to
        // allocate when we need to raise it.
        // We can only do this once the taskData object has been created.
        if (interrupt_exn == 0)
            interrupt_exn =
                DEREFEXNHANDLE(make_exn(taskData, EXC_interrupt, taskData->saveVec.push(TAGGED(0))));


        if (singleThreaded)
        {
            // If we don't have threading enter the code as if this were a new thread.
            // This will call finish so will never return.
            NewThreadFunction(taskData);
        }

        schedLock.Lock();
        int errorCode = 0;
#ifdef HAVE_PTHREAD
        // Create a thread that isn't joinable since we don't want to wait
        // for it to finish.
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&taskData->pthreadId, &attrs, NewThreadFunction, taskData) != 0)
            errorCode = errno;
        pthread_attr_destroy(&attrs);
#elif defined(HAVE_WINDOWS_H)
        DWORD dwThrdId; // Have to provide this although we don't use it.
        taskData->threadHandle =
            CreateThread(NULL, 0, NewThreadFunction, taskData, 0, &dwThrdId);
        if (taskData->threadHandle == NULL) errorCode = -GetLastError();
#endif
        if (errorCode != 0)
        {
            // Thread creation failed.
            taskArray[0] = 0;
            delete(taskData);
            ExitWithError("Unable to create initial thread:", errorCode);
        }

        if (debugOptions & DEBUG_THREADS)
            Log("THREAD: Forked initial root thread %p\n", taskData);
    }
    catch (std::bad_alloc a) {
        ::Exit("Unable to create the initial thread - insufficient memory");
    }

    // Wait until the threads terminate or make a request.
    // We only release schedLock while waiting.
    while (1)
    {
        // Look at the threads to see if they are running.
        bool allStopped = true;
        bool noUserThreads = true;
        bool signalThreadRunning = false;
        for (unsigned i = 0; i < taskArraySize; i++)
        {
            ProcessTaskData *p = taskArray[i];
            if (p)
            {
                if (p == sigTask) signalThreadRunning = true;
                else noUserThreads = false;
            }
            if (p && p->inMLHeap)
            {
                allStopped = false;
                // It must be running - interrupt it if we are waiting.
                if (threadRequest != 0)
                    machineDependent->InterruptCode(p);
            }
        }
        if (noUserThreads)
        {
            // If all threads apart from the signal thread have exited then
            // we can finish but we must make sure that the signal thread has
            // exited before we finally finish and deallocate the memory.
            if (signalThreadRunning) exitRequest = true;
            else break; // Really no threads.
        }

        if (allStopped && threadRequest != 0)
        {
            mainThreadPhase = threadRequest->mtp;
            threadRequest->Perform();
            mainThreadPhase = MTP_USER_CODE;
            threadRequest->completed = true;
            threadRequest = 0; // Allow a new request.
            mlThreadWait.Signal();
        }

        // Have we had a request to stop?  This may have happened while in the GC.
        if (exitRequest)
        {
            // Set this to kill the threads.
            for (unsigned i = 0; i < taskArraySize; i++)
            {
                ProcessTaskData *taskData = taskArray[i];
                if (taskData)
                    MakeRequest(taskData, kRequestKill);
            }
            exitRequest = false; // Don't need to repeat this.
        }

        // Now release schedLock and wait for a thread
        // to wake us up.  Use a timed wait to avoid the race with
        // setting exitRequest.
        initialThreadWait.WaitFor(&schedLock, 2000);
    }
    schedLock.Unlock();
    // We are about to return normally.  Stop any crowbar function
    // and wait until it stops.
    shutdownLock.Lock();
    if (crowbarRunning)
    {
        crowbarLock.Signal();
        crowbarStopped.Wait(&shutdownLock);
    }
    finish(exitResult); // Close everything down and exit.
}

// Create a new thread.  Returns the ML thread identifier object if it succeeds.
// May raise an exception.
Handle Processes::ForkThread(ProcessTaskData *taskData, Handle threadFunction,
                           Handle args, PolyWord flags)
{
    if (singleThreaded)
        raise_exception_string(taskData, EXC_thread, "Threads not available");

    try {
        // Create a taskData object for the new thread
        ProcessTaskData *newTaskData = new ProcessTaskData;
        newTaskData->mdTaskData = machineDependent->CreateTaskData();
        // We allocate the thread object in the PARENT's space
        Handle threadId = alloc_and_save(taskData, 4, F_MUTABLE_BIT);
        newTaskData->threadObject = (ThreadObject*)DEREFHANDLE(threadId);
        newTaskData->threadObject->index = TAGGED(0);
        newTaskData->threadObject->flags = flags; // Flags
        newTaskData->threadObject->threadLocal = TAGGED(0); // Empty thread-local store
        newTaskData->threadObject->requestCopy = TAGGED(0); // Cleared interrupt state

        unsigned thrdIndex;
        schedLock.Lock();
        // Before forking a new thread check to see whether we have been asked
        // to exit.  Processes::Exit sets the current set of threads to exit but won't
        // see a new thread.
        if (taskData->requests == kRequestKill)
        {
            schedLock.Unlock();
            // Raise an exception although the thread may exit before we get there.
            raise_exception_string(taskData, EXC_thread, "Thread is exiting");
        }

        // See if there's a spare entry in the array.
        for (thrdIndex = 0;
             thrdIndex < taskArraySize && taskArray[thrdIndex] != 0;
             thrdIndex++);

        if (thrdIndex == taskArraySize) // Need to expand the array
        {
            ProcessTaskData **newArray =
                (ProcessTaskData **)realloc(taskArray, sizeof(ProcessTaskData *)*(taskArraySize+1));
            if (newArray)
            {
                taskArray = newArray;
                taskArraySize++;
            }
            else
            {
                delete(newTaskData);
                schedLock.Unlock();
                raise_exception_string(taskData, EXC_thread, "Too many threads");
            }
        }
        // Add into the new entry
        taskArray[thrdIndex] = newTaskData;
        newTaskData->threadObject->index = TAGGED(thrdIndex); // Set to the index
        schedLock.Unlock();

        newTaskData->stack = gMem.NewStackSpace(machineDependent->InitialStackSize());
        if (newTaskData->stack == 0)
        {
            delete(newTaskData);
            raise_exception_string(taskData, EXC_thread, "Unable to allocate thread stack");
        }

        // Allocate anything needed for the new stack in the parent's heap.
        // The child still has inMLHeap set so mustn't GC.
        machineDependent->InitStackFrame(taskData, newTaskData->stack, threadFunction, args);

        // Now actually fork the thread.
        bool success = false;
        schedLock.Lock();
#ifdef HAVE_PTHREAD
        // Create a thread that isn't joinable since we don't want to wait
        // for it to finish.
        pthread_attr_t attrs;
        pthread_attr_init(&attrs);
        pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
        success = pthread_create(&newTaskData->pthreadId, &attrs, NewThreadFunction, newTaskData) == 0;
        pthread_attr_destroy(&attrs);
#elif defined(HAVE_WINDOWS_H)
        DWORD dwThrdId; // Have to provide this although we don't use it.
        newTaskData->threadHandle =
            CreateThread(NULL, 0, NewThreadFunction, newTaskData, 0, &dwThrdId);
        success = newTaskData->threadHandle != NULL;
#endif
        if (success)
        {
            schedLock.Unlock();

            if (debugOptions & DEBUG_THREADS)
                Log("THREAD: Forking new thread %p from thread %p\n", newTaskData, taskData);

            return threadId;
        }
        // Thread creation failed.
        taskArray[thrdIndex] = 0;
        delete(newTaskData);
        schedLock.Unlock();

        if (debugOptions & DEBUG_THREADS)
            Log("THREAD: Fork from thread %p failed\n", taskData);

        raise_exception_string(taskData, EXC_thread, "Thread creation failed");
    }
    catch (std::bad_alloc a) {
            raise_exception_string(taskData, EXC_thread, "Insufficient memory");
    }
}

// ForkFromRTS.  Creates a new thread from within the RTS.  This is currently used
// only to run a signal function.
bool Processes::ForkFromRTS(TaskData *taskData, Handle proc, Handle arg)
{
    try {
        (void)ForkThread((ProcessTaskData*)taskData, proc, arg, TAGGED(PFLAG_SYNCH));
        return true;
    } catch (IOException)
    {
        // If it failed
        return false;
    }
}

// Deal with any interrupt or kill requests.
bool Processes::ProcessAsynchRequests(TaskData *taskData)
{
    bool wasInterrupted = false;
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;

    schedLock.Lock();

    switch (ptaskData->requests)
    {
    case kRequestNone:
        schedLock.Unlock();
        break;

    case kRequestInterrupt:
        {
            // Handle asynchronous interrupts only.
            // We've been interrupted.  
            POLYUNSIGNED attrs = ThreadAttrs(ptaskData);
            POLYUNSIGNED intBits = attrs & PFLAG_INTMASK;
            if (intBits == PFLAG_ASYNCH || intBits == PFLAG_ASYNCH_ONCE)
            {
                if (intBits == PFLAG_ASYNCH_ONCE)
                {
                    // Set this so from now on it's synchronous.
                    // This word is only ever set by the thread itself so
                    // we don't need to synchronise.
                    attrs = (attrs & (~PFLAG_INTMASK)) | PFLAG_SYNCH;
                    ptaskData->threadObject->flags = TAGGED(attrs);
                }
                ptaskData->requests = kRequestNone; // Clear this
                ptaskData->threadObject->requestCopy = TAGGED(0); // And in the ML copy
                schedLock.Unlock();
                // Don't actually throw the exception here.
                machineDependent->SetException(taskData, interrupt_exn);
                wasInterrupted = true;
            }
            else schedLock.Unlock();
        }
        break;

    case kRequestKill: // The thread has been asked to stop.
        schedLock.Unlock();
        throw KillException();
        // Doesn't return.
    }

#ifndef HAVE_WINDOWS_H
    // Start the profile timer if needed.
    if (profileMode == kProfileTime)
    {
        if (! ptaskData->runningProfileTimer)
        {
            ptaskData->runningProfileTimer = true;
            StartProfilingTimer();
        }
    }
    else ptaskData->runningProfileTimer = false;
    // The timer will be stopped next time it goes off.
#endif
    return wasInterrupted;
}

// If this thread is processing interrupts synchronously and has been
// interrupted clear the interrupt and raise the exception.  This is
// called from IO routines which may block.
void Processes::TestSynchronousRequests(TaskData *taskData)
{
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;
    schedLock.Lock();
    switch (ptaskData->requests)
    {
    case kRequestNone:
        schedLock.Unlock();
        break;

    case kRequestInterrupt:
        {
            // Handle synchronous interrupts only.
            // We've been interrupted.  
            POLYUNSIGNED attrs = ThreadAttrs(ptaskData);
            POLYUNSIGNED intBits = attrs & PFLAG_INTMASK;
            if (intBits == PFLAG_SYNCH)
            {
                ptaskData->requests = kRequestNone; // Clear this
                ptaskData->threadObject->requestCopy = TAGGED(0);
                schedLock.Unlock();
                machineDependent->SetException(taskData, interrupt_exn);
                throw IOException(EXC_EXCEPTION);
            }
            else schedLock.Unlock();
        }
        break;

    case kRequestKill: // The thread has been asked to stop.
        schedLock.Unlock();
        throw KillException();
        // Doesn't return.
    }
}

// Check for asynchronous or synchronous events
void Processes::TestAnyEvents(TaskData *taskData)
{
    TestSynchronousRequests(taskData);
    if (ProcessAsynchRequests(taskData))
        throw IOException(EXC_EXCEPTION);
}

// Stop.  Usually called by one of the threads but
// in the Windows version can also be called by the GUI or
// it can be called from the default console interrupt handler.
// This is more complicated than it seems.  We must avoid
// calling exit while there are other threads running because
// exit will finalise the modules and deallocate memory etc.
// However some threads may be deadlocked or we may be in the
// middle of a very slow GC and we just want it to stop.
void Processes::CrowBarFn(void)
{
#if (defined(HAVE_PTHREAD) || defined(HAVE_WINDOWS_H))
    shutdownLock.Lock();
    crowbarRunning = true;
    if (crowbarLock.WaitFor(&shutdownLock, 20000)) // Wait for 20s
    {
        // We've been woken by the main thread.  Let it do the shutdown.
        crowbarStopped.Signal();
        shutdownLock.Unlock();
    }
    else
    {
#if defined(HAVE_WINDOWS_H)
        ExitProcess(1);
#else
        _exit(1); // Something is stuck.  Get out without calling destructors.
#endif
    }
#endif
}

#ifdef HAVE_PTHREAD
static void *crowBarFn(void*)
{
    processesModule.CrowBarFn();
    return 0;
}
#elif defined(HAVE_WINDOWS_H)
static DWORD WINAPI crowBarFn(LPVOID arg)
{
    processesModule.CrowBarFn();
    return 0;
}
#endif

void Processes::Exit(int n)
{
    if (singleThreaded)
        finish(n);

    // Start a crowbar thread.  This will stop everything if the main thread
    // does not reach the point of stopping within 5 seconds.
#if (defined(HAVE_PTHREAD))
    // Create a thread that isn't joinable since we don't want to wait
    // for it to finish.
    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);
    pthread_t threadId;
    (void)pthread_create(&threadId, &attrs, crowBarFn, 0);
    pthread_attr_destroy(&attrs);
#elif defined(HAVE_WINDOWS_H)
    DWORD dwThrdId;
    HANDLE hCrowBarThread = CreateThread(NULL, 0, crowBarFn, 0, 0, &dwThrdId);
    CloseHandle(hCrowBarThread); // Not needed
#endif
    // We may be in an interrupt handler with schedLock held.
    // Just set the exit request and go.
    exitResult = n;
    exitRequest = true;
    initialThreadWait.Signal(); // Wake it if it's sleeping.
}

/******************************************************************************/
/*                                                                            */
/*      catchVTALRM - handler for alarm-clock signal                          */
/*                                                                            */
/******************************************************************************/
#if !defined(HAVE_WINDOWS_H)
// N.B. This may be called either by an ML thread or by the main thread.
// On the main thread taskData will be null.
static void catchVTALRM(SIG_HANDLER_ARGS(sig, context))
{
    ASSERT(sig == SIGVTALRM);
    if (profileMode != kProfileTime)
    {
        // We stop the timer for this thread on the next signal after we end profile
        static struct itimerval stoptime = {{0, 0}, {0, 0}};
        /* Stop the timer */
        setitimer(ITIMER_VIRTUAL, & stoptime, NULL);
    }
    else {
        TaskData *taskData = processes->GetTaskDataForThread();
        handleProfileTrap(taskData, (SIGNALCONTEXT*)context);
    }
}

#else /* Windows including Cygwin */
// This runs as a separate thread.  Every millisecond it checks the CPU time used
// by each ML thread and increments the count for each thread that has used a
// millisecond of CPU time.

static bool testCPUtime(HANDLE hThread, LONGLONG &lastCPUTime)
{
    FILETIME cTime, eTime, kTime, uTime;
    // Try to get the thread CPU time if possible.  This isn't supported
    // in Windows 95/98 so if it fails we just include this thread anyway.
    if (GetThreadTimes(hThread, &cTime, &eTime, &kTime, &uTime))
    {
        LONGLONG totalTime = 0;
        LARGE_INTEGER li;
        li.LowPart = kTime.dwLowDateTime;
        li.HighPart = kTime.dwHighDateTime;
        totalTime += li.QuadPart;
        li.LowPart = uTime.dwLowDateTime;
        li.HighPart = uTime.dwHighDateTime;
        totalTime += li.QuadPart;
        if (totalTime - lastCPUTime >= 10000)
        {
            lastCPUTime = totalTime;
            return true;
        }
        return false;
    }
    else return true; // Failed to get thread time, maybe Win95.
 }


void Processes::ProfileInterrupt(void)
{               
    // Wait for millisecond or until the stop event is signalled.
    while (WaitForSingleObject(hStopEvent, 1) == WAIT_TIMEOUT)        
    {
        // We need to hold schedLock to examine the taskArray but
        // that is held during garbage collection.
        if (schedLock.Trylock())
        {
            for (unsigned i = 0; i < taskArraySize; i++)
            {
                ProcessTaskData *p = taskArray[i];
                if (p && p->threadHandle)
                {
                    if (testCPUtime(p->threadHandle, p->lastCPUTime))
                    {
                        CONTEXT context;
                        SuspendThread(p->threadHandle);
                        context.ContextFlags = CONTEXT_CONTROL; /* Get Eip and Esp */
                        if (GetThreadContext(p->threadHandle, &context))
                        {
                            handleProfileTrap(p, &context);
                        }
                        ResumeThread(p->threadHandle);
                    }
                }
            }
            schedLock.Unlock();
        }
        // Check the CPU time used by the main thread.  This is used for GC
        // so we need to check that as well.
        if (testCPUtime(mainThreadHandle, lastCPUTime))
            handleProfileTrap(NULL, NULL);
    }
}

DWORD WINAPI ProfilingTimer(LPVOID parm)
{
    processesModule.ProfileInterrupt();
    return 0;
}

#endif

// Profiling control.  Called by the root thread.
void Processes::StartProfiling(void)
{
#ifdef HAVE_WINDOWS_H
    DWORD threadId;
    if (profilingHd)
        return;
    ResetEvent(hStopEvent);
    profilingHd = CreateThread(NULL, 0, ProfilingTimer, NULL, 0, &threadId);
    if (profilingHd == NULL)
        fputs("Creating ProfilingTimer thread failed.\n", stdout); 
    /* Give this a higher than normal priority so it pre-empts the main
       thread.  Without this it will tend only to be run when the main
       thread blocks for some reason. */
    SetThreadPriority(profilingHd, THREAD_PRIORITY_ABOVE_NORMAL);
#else
    // In Linux, at least, we need to run a timer in each thread.
    // We request each to enter the RTS so that it will start the timer.
    // Since this is being run by the main thread while all the ML threads
    // are paused this may not actually be necessary.
    for (unsigned i = 0; i < taskArraySize; i++)
    {
        ProcessTaskData *taskData = taskArray[i];
        if (taskData)
        {
            machineDependent->InterruptCode(taskData);
        }
    }
    StartProfilingTimer(); // Start the timer in the root thread.
#endif
}

void Processes::StopProfiling(void)
{
#ifdef HAVE_WINDOWS_H
    if (hStopEvent) SetEvent(hStopEvent);
    // Wait for the thread to stop
    if (profilingHd) WaitForSingleObject(profilingHd, 10000);
    CloseHandle(profilingHd);
    profilingHd = NULL;
#endif
}

// Called by the ML signal handling thread.  It blocks until a signal
// arrives.  There should only be a single thread waiting here.
bool Processes::WaitForSignal(TaskData *taskData, PLock *sigLock)
{
    ProcessTaskData *ptaskData = (ProcessTaskData *)taskData;
    // We need to hold the signal lock until we have acquired schedLock.
    schedLock.Lock();
    sigLock->Unlock();
    if (sigTask != 0)
    {
        schedLock.Unlock();
        return false;
    }
    sigTask = ptaskData;

    if (ptaskData->requests == kRequestNone)
    {
        // Now release the ML memory.  A GC can start.
        ThreadReleaseMLMemoryWithSchedLock(ptaskData);
        ptaskData->threadLock.Wait(&schedLock);
        // We want to use the memory again.
        ThreadUseMLMemoryWithSchedLock(ptaskData);
    }

    sigTask = 0;
    schedLock.Unlock();
    return true;
}

// Called by the signal detection thread to wake up the signal handler
// thread.  Must be called AFTER releasing sigLock.
void Processes::SignalArrived(void)
{
    PLocker locker(&schedLock);
    if (sigTask)
        sigTask->threadLock.Signal();
}


void Processes::Init(void)
{
#ifdef HAVE_WINDOWS_H
    // Create event to wake up from IO sleeping.
    Waiter::hWakeupEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
#endif

#ifdef HAVE_PTHREAD
    pthread_key_create(&tlsId, NULL);
#elif defined(HAVE_WINDOWS_H)
    tlsId = TlsAlloc();
#else
    singleThreaded = true;
#endif

#if defined(HAVE_WINDOWS_H) /* Windows including Cygwin. */
    // Create stop event for time profiling.
    hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    // Get the thread handle for this thread.  It's the same as
    // hMainThread except that we don't have that in the Cygwin version.
    HANDLE thisProcess = GetCurrentProcess();
    DuplicateHandle(thisProcess, GetCurrentThread(), thisProcess, 
        &mainThreadHandle, THREAD_ALL_ACCESS, FALSE, 0);
#else
    // Set up a signal handler.  This will be the same for all threads.
    markSignalInuse(SIGVTALRM);
    setSignalHandler(SIGVTALRM, catchVTALRM);
#endif
}

#ifndef HAVE_WINDOWS_H
// On Linux, at least, each thread needs to run this.
void Processes::StartProfilingTimer(void)
{
    // set virtual timer to go off every millisecond
    struct itimerval starttime;
    starttime.it_interval.tv_sec = starttime.it_value.tv_sec = 0;
    starttime.it_interval.tv_usec = starttime.it_value.tv_usec = 1000;
    setitimer(ITIMER_VIRTUAL,&starttime,NULL);
}
#endif

void Processes::Reinit(void)
{
}

void Processes::Uninit(void)
{     
#ifdef HAVE_WINDOWS_H
    if (Waiter::hWakeupEvent) SetEvent(Waiter::hWakeupEvent);
#endif

#ifdef HAVE_WINDOWS_H
    if (Waiter::hWakeupEvent) CloseHandle(Waiter::hWakeupEvent);
    Waiter::hWakeupEvent = NULL;
#endif

#ifdef HAVE_PTHREAD
    pthread_key_delete(tlsId);
#elif defined(HAVE_WINDOWS_H)
    TlsFree(tlsId);
#endif

#if defined(HAVE_WINDOWS_H)
    /* Stop the timer and profiling threads. */
    if (hStopEvent) SetEvent(hStopEvent);
    if (profilingHd)
    {
        WaitForSingleObject(profilingHd, 10000);
        CloseHandle(profilingHd);
        profilingHd = NULL;
    }
    if (hStopEvent) CloseHandle(hStopEvent);
    hStopEvent = NULL;
    if (mainThreadHandle) CloseHandle(mainThreadHandle);
    mainThreadHandle = NULL;
#else
    profileMode = kProfileOff;
    // Make sure the timer is not running
    struct itimerval stoptime;
    memset(&stoptime, 0, sizeof(stoptime));
    setitimer(ITIMER_VIRTUAL, &stoptime, NULL);
#endif
}

void Processes::GarbageCollect(ScanAddress *process)
/* Ensures that all the objects are retained and their addresses updated. */
{   
    /* The interrupt exn */
    if (interrupt_exn != 0) {
        PolyObject *p = interrupt_exn;
        process->ScanRuntimeAddress(&p, ScanAddress::STRENGTH_STRONG);
        interrupt_exn = (PolyException*)p;
    }
    for (unsigned i = 0; i < taskArraySize; i++)
    {
        if (taskArray[i])
            taskArray[i]->GarbageCollect(process);
    }
}

void ProcessTaskData::GarbageCollect(ScanAddress *process)
{
    saveVec.gcScan(process);
    if (stack != 0) process->ScanAddressesInStack(stack);

    if (threadObject != 0)
    {
        PolyObject *p = threadObject;
        process->ScanRuntimeAddress(&p, ScanAddress::STRENGTH_STRONG);
        threadObject = (ThreadObject*)p;
    }
    if (blockMutex != 0)
        process->ScanRuntimeAddress(&blockMutex, ScanAddress::STRENGTH_STRONG);
    // The allocation spaces are no longer valid.
    allocPointer = 0;
    allocLimit = 0;
    // Divide the allocation size by four. If we have made a single allocation
    // since the last GC the size will have been doubled after the allocation.
    // On average for each thread, apart from the one that ran out of space
    // and requested the GC, half of the space will be unused so reducing by
    // four should give a good estimate for next time.
    if (allocCount != 0)
    { // Do this only once for each GC.
        allocCount = 0;
        allocSize = allocSize/4;
        if (allocSize < MIN_HEAP_SIZE)
            allocSize = MIN_HEAP_SIZE;
    }
}

// Return the number of processors.  Used when configuring multi-threaded GC.
extern unsigned NumberOfProcessors(void)
{
#ifdef WIN32
        SYSTEM_INFO info;
        memset(&info, 0, sizeof(info));
        GetSystemInfo(&info);
        if (info.dwNumberOfProcessors == 0) // Just in case
            info.dwNumberOfProcessors = 1;
        return info.dwNumberOfProcessors;
#elif(defined(_SC_NPROCESSORS_ONLN))
        long res = sysconf(_SC_NPROCESSORS_ONLN);
        if (res <= 0) res = 1;
        return res;
#elif(defined(HAVE_SYSCTL) && defined(CTL_HW) && defined(HW_NCPU))
        static int mib[2] = { CTL_HW, HW_NCPU };
        int nCPU = 1;
        size_t len = sizeof(nCPU);
        if (sysctl(mib, 2, &nCPU, &len, NULL, 0) == 0 && len == sizeof(nCPU))
             return nCPU;
        else return 1;
#else
        // Can't determine.
        return 1;
#endif
}
