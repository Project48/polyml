/*
    Title:      Multi-Threaded Garbage Collector

    Copyright (c) 2010-12 David C. J. Matthews

    Based on the original garbage collector code
        Copyright 2000-2008
        Cambridge University Technical Services Limited

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
#elif defined(_WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x)   assert(x)
#else
#define ASSERT(x)
#endif

#include "globals.h"
#include "run_time.h"
#include "machine_dep.h"
#include "diagnostics.h"
#include "processes.h"
#include "timing.h"
#include "gc.h"
#include "scanaddrs.h"
#include "check_objects.h"
#include "osmem.h"
#include "bitmap.h"
#include "rts_module.h"
#include "memmgr.h"
#include "gctaskfarm.h"
#include "mpoly.h"
#include "statistics.h"
#include "profiling.h"
#include "heapsizing.h"

static GCTaskFarm gTaskFarm; // Global task farm.
GCTaskFarm *gpTaskFarm = &gTaskFarm;

// If the GC converts a weak ref from SOME to NONE it sets this ref.  It can be
// cleared by the signal handler thread.  There's no need for a lock since it
// is only set during GC and only cleared when not GCing.
bool convertedWeak = false;

/*
    How the garbage collector works.
    The GC has two phases.  The minor (quick) GC is a copying collector that
    copies data from the allocation area into the mutable and immutable area.
    The major collector is started when either the mutable or the immutable
    area is full.  The major collector uses a mark/sweep scheme.
    The GC has three phases:

    1.  Mark phase.
    Working from the roots; which are the the permanent mutable segments and
    the RTS roots (e.g. thread stacks), mark all reachable cells.
    Marking involves setting bits in the bitmap for reachable words.

    2. Compact phase.
    Marked objects are copied to try to compact, upwards, the heap segments.  When
    an object is moved the length word of the object in the old location is set as
    a tombstone that points to its new location.  In particular this means that we
    cannot reuse the space where an object previously was during the compaction phase.
    Immutable objects are moved into immutable segments.  When an object is moved
    to a new location the bits are set in the bitmap as though the object had been
    marked at that location.

    3. Update phase.
    The roots and objects marked during the first two phases are scanned and any
    addresses for moved objects are updated.  The lowest address used in the area
    then becomes the base of the area for future allocations.

    There is a sharing phase which may be performed before the mark phase.  This
    merges immutable cells with the same contents with the aim of reducing the
    size of the live data.  It is expensive so is not performed by default.

    Updated DCJM 12/06/12

*/
static bool doGC(const POLYUNSIGNED wordsRequiredToAllocate)
{
    unsigned j;

    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeStart);
    globalStats.incCount(PSC_GC_FULLGC);

    if (debugOptions & DEBUG_GC)
        Log("GC: Full GC, %lu words required %u spaces\n", wordsRequiredToAllocate, gMem.nlSpaces);
    
    if (debugOptions & DEBUG_HEAPSIZE)
        gMem.reportHeapSpaceUsage();
    
    // Experimental data sharing pass.
    if (gHeapSizeParameters.PerformSharingPass())
        GCSharingPhase();
/*
 * There is a really weird bug somewhere.  An extra bit may be set in the bitmap during
 * the mark phase.  It seems to be related to heavy swapping activity.  Duplicating the
 * bitmap causes it to occur only in one copy and write-protecting the bitmap apart from
 * when it is actually being updated does not result in a seg-fault.  So far I've only
 * seen it on 64-bit Linux but it may be responsible for other crashes.  The work-around
 * is to check the number of bits set in the bitmap and repeat the mark phase if it does
 * not match.
 */
    
    for (unsigned p = 3; p > 0; p--)
    {
        for(j = 0; j < gMem.nlSpaces; j++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[j];
            ASSERT (lSpace->top >= lSpace->upperAllocPtr);
            ASSERT (lSpace->upperAllocPtr >= lSpace->lowerAllocPtr);
            ASSERT (lSpace->lowerAllocPtr >= lSpace->bottom);
            // Set upper and lower limits of weak refs.
            lSpace->highestWeak = lSpace->bottom;
            lSpace->lowestWeak = lSpace->top;
            lSpace->fullGCLowerLimit = lSpace->top;
            // Put dummy objects in the unused space.  This allows
            // us to scan over the whole of the space.
            gMem.FillUnusedSpace(lSpace->lowerAllocPtr,
                lSpace->upperAllocPtr-lSpace->lowerAllocPtr);
        }

        // Set limits of weak refs.
        for (j = 0; j < gMem.npSpaces; j++)
        {
            PermanentMemSpace *pSpace = gMem.pSpaces[j];
            pSpace->highestWeak = pSpace->bottom;
            pSpace->lowestWeak = pSpace->top;
        }

        /* Mark phase */
        GCMarkPhase();
        
        POLYUNSIGNED bitCount = 0, markCount = 0;
        
        for (j = 0; j < gMem.nlSpaces; j++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[j]; 
            markCount += lSpace->i_marked + lSpace->m_marked;
            bitCount += lSpace->bitmap.CountSetBits(lSpace->spaceSize());
        }
        
        if (markCount == bitCount)
            break;
        else
        {
            // Report an error.  If this happens again we crash.
            Log("GC: Count error for space %u - mark count %lu, bitCount %lu\n", j, markCount, bitCount);
            if (p == 1)
            {
                ASSERT(markCount == bitCount);
            }
        }
    }
    for(j = 0; j < gMem.nlSpaces; j++)
    {
        LocalMemSpace *lSpace = gMem.lSpaces[j];
        // Reset the allocation pointers.  They will be set to the
        // limits of the retained data.
        lSpace->lowerAllocPtr = lSpace->bottom;
        lSpace->upperAllocPtr = lSpace->top;
    }

    if (debugOptions & DEBUG_GC) Log("GC: Check weak refs\n");
    /* Detect unreferenced streams, windows etc. */
    GCheckWeakRefs();

    // Check that the heap is not overfull.  We make sure the marked
    // mutable and immutable data is no more than 90% of the
    // corresponding areas.  This is a very coarse adjustment.
    {
        POLYUNSIGNED iMarked = 0, mMarked = 0;
        POLYUNSIGNED iSpace = 0, mSpace = 0;
        for (unsigned i = 0; i < gMem.nlSpaces; i++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[i];
            iMarked += lSpace->i_marked;
            mMarked += lSpace->m_marked;
            if (! lSpace->allocationSpace)
            {
                if (lSpace->isMutable)
                    mSpace += lSpace->spaceSize();
                else
                    iSpace += lSpace->spaceSize();
            }
        }
        // Add space if necessary and possible.
        while (iMarked > iSpace - iSpace/10 && gHeapSizeParameters.AddSpaceBeforeCopyPhase(false) != 0)
            iSpace += gMem.DefaultSpaceSize();
        while (mMarked > mSpace - mSpace/10 && gHeapSizeParameters.AddSpaceBeforeCopyPhase(true) != 0)
            mSpace += gMem.DefaultSpaceSize();
    }

    /* Compact phase */
    GCCopyPhase();

    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeIntermediate, "Copy");

    // Update Phase.
    if (debugOptions & DEBUG_GC) Log("GC: Update\n");
    GCUpdatePhase();

    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeIntermediate, "Update");

    {
        POLYUNSIGNED iUpdated = 0, mUpdated = 0, iMarked = 0, mMarked = 0;
        for(j = 0; j < gMem.nlSpaces; j++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[j];
            iMarked += lSpace->i_marked;
            mMarked += lSpace->m_marked;
            if (lSpace->isMutable)
                mUpdated += lSpace->updated;
            else
                iUpdated += lSpace->updated;
        }
        ASSERT(iUpdated+mUpdated == iMarked+mMarked);
    }

    // Delete empty spaces.
    gMem.RemoveEmptyLocals();

    // The allocation areas should have been emptied and deallocated.  We
    // want to create one that is big enough for the required data
    // to check that it's possible.  If it isn't then we've run out of
    // memory.
    bool haveSpace = true;
    if (wordsRequiredToAllocate != 0)
    {
        POLYUNSIGNED needed = gMem.DefaultSpaceSize();
        if (wordsRequiredToAllocate > needed)
            needed = wordsRequiredToAllocate;
        haveSpace = gMem.CreateAllocationSpace(needed) != 0;
    }

    if (debugOptions & DEBUG_GC)
    {
        for(j = 0; j < gMem.nlSpaces; j++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[j];
            Log("GC: %s space %p %d free in %d words %2.1f%% full\n", lSpace->spaceTypeString(),
                lSpace, lSpace->freeSpace(), lSpace->spaceSize(),
                ((float)lSpace->allocatedSpace()) * 100 / (float)lSpace->spaceSize());
        }
    }

    // Compute values for statistics
    globalStats.setSize(PSS_AFTER_LAST_GC, 0);
    globalStats.setSize(PSS_AFTER_LAST_FULLGC, 0);
    globalStats.setSize(PSS_ALLOCATION, 0);
    globalStats.setSize(PSS_ALLOCATION_FREE, 0);

    for (j = 0; j < gMem.nlSpaces; j++)
    {
        LocalMemSpace *space = gMem.lSpaces[j];
        POLYUNSIGNED free = space->freeSpace();
        globalStats.incSize(PSS_AFTER_LAST_GC, free*sizeof(PolyWord));
        globalStats.incSize(PSS_AFTER_LAST_FULLGC, free*sizeof(PolyWord));
        if (space->allocationSpace)
        {
            globalStats.incSize(PSS_ALLOCATION, free*sizeof(PolyWord));
            globalStats.incSize(PSS_ALLOCATION_FREE, free*sizeof(PolyWord));
        }
#ifdef FILL_UNUSED_MEMORY
        memset(space->bottom, 0xaa, (char*)space->upperAllocPtr - (char*)space->bottom);
#endif
        if (debugOptions & DEBUG_GC)
            Log("GC: %s space %p %d free in %d words %2.1f%% full\n", space->spaceTypeString(),
                space, space->freeSpace(), space->spaceSize(),
                ((float)space->allocatedSpace()) * 100 / (float)space->spaceSize());
    }

    // End of garbage collection
    gHeapSizeParameters.RecordGCTime(HeapSizeParameters::GCTimeEnd);

    // Now we've finished we can adjust the heap sizes.
    gHeapSizeParameters.AdjustSizeAfterMajorGC();
    gHeapSizeParameters.resetMajorTimingData();

    // Invariant: the bitmaps are completely clean.
    if (debugOptions & DEBUG_GC)
    {
        if (haveSpace)
            Log("GC: Completed successfully\n");
        else Log("GC: Completed with insufficient space\n");
    }

    if (debugOptions & DEBUG_HEAPSIZE)
        gMem.reportHeapSpaceUsage();

    if (profileMode == kProfileLiveData || profileMode == kProfileLiveMutables)
        printprofile();

    CheckMemory();

    return haveSpace; // Completed
}

// Create the initial heap.  hsize, isize and msize are the requested heap sizes
// from the user arguments in units of kbytes.
// Fills in the defaults and attempts to allocate the heap.  If the heap size
// is too large it allocates as much as it can.  The default heap size is half the
// physical memory.
void CreateHeap()
{
    // Create an initial allocation space.
    if (gMem.CreateAllocationSpace(gMem.DefaultSpaceSize()) == 0)
        Exit("Insufficient memory to allocate the heap");

    // Create the task farm if required
    if (userOptions.gcthreads != 1)
    {
        if (! gTaskFarm.Initialise(userOptions.gcthreads, 100))
            Crash("Unable to initialise the GC task farm");
    }
    // Set up the stacks for the mark phase.
    initialiseMarkerTables();
}

class FullGCRequest: public MainThreadRequest
{
public:
    FullGCRequest(): MainThreadRequest(MTP_GCPHASEMARK) {}
    virtual void Perform()
    {
        doGC (0);
    }
};

class QuickGCRequest: public MainThreadRequest
{
public:
    QuickGCRequest(POLYUNSIGNED words): MainThreadRequest(MTP_GCPHASEMARK), wordsRequired(words) {}

    virtual void Perform()
    {
        result =
#ifndef DEBUG_ONLY_FULL_GC
// If DEBUG_ONLY_FULL_GC is defined then we skip the partial GC.
            RunQuickGC() ||
#endif
            doGC (wordsRequired);
    }

    bool result;
    POLYUNSIGNED wordsRequired;
};

// Perform a full garbage collection.  This is called either from ML via the full_gc RTS call
// or from various RTS functions such as open_file to try to recover dropped file handles.
void FullGC(TaskData *taskData)
{
    FullGCRequest request;
    processes->MakeRootRequest(taskData, &request);

    if (convertedWeak)
        // Notify the signal thread to broadcast on the condition var when
        // the GC is complete.  We mustn't call SignalArrived within the GC
        // because it locks schedLock and the main GC thread already holds schedLock.
        processes->SignalArrived();
}

// This is the normal call when memory is exhausted and we need to garbage collect.
bool QuickGC(TaskData *taskData, POLYUNSIGNED wordsRequiredToAllocate)
{
    QuickGCRequest request(wordsRequiredToAllocate);
    processes->MakeRootRequest(taskData, &request);

    if (convertedWeak)
        processes->SignalArrived();

    return request.result;
}

