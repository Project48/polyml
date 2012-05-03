/*
    Title:      Quick copying garbage collector

    Copyright (c) 2011-12 David C. J. Matthews

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
/*
This is a quick copying garbage collector that moves all the data out of
the allocation areas and into the mutable and immutable areas.  If either of
these has filled up it fails and a full garbage collection must be done.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(_WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H 
#include <string.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x)   assert(x)
#else
#define ASSERT(x)
#endif

#include "globals.h"
#include "processes.h"
#include "gc.h"
#include "scanaddrs.h"
#include "check_objects.h"
#include "bitmap.h"
#include "memmgr.h"
#include "diagnostics.h"
#include "timing.h"
#include "gctaskfarm.h"
#include "statistics.h"

// This protects access to the gMem.lSpace table.
static PLock localTableLock("Minor GC tables");

static bool succeeded = true;

class QuickGCScanner: public ScanAddress
{
public:
    QuickGCScanner(bool r): rootScan(r) {}
    virtual ~QuickGCScanner() {}

    // Overrides for ScanAddress class
    virtual POLYUNSIGNED ScanAddressAt(PolyWord *pt);
    virtual PolyObject *ScanObjectAddress(PolyObject *base);
private:
    PolyObject *FindNewAddress(PolyObject *obj, POLYUNSIGNED L, LocalMemSpace *srcSpace);
    virtual LocalMemSpace *FindSpace(POLYUNSIGNED length, bool isMutable) = 0;
protected:
    bool objectCopied;
    bool rootScan;
};

class RootScanner: public QuickGCScanner
{
public:
    RootScanner(): QuickGCScanner(true), mutableSpace(0), immutableSpace(0) {}
private:
    virtual LocalMemSpace *FindSpace(POLYUNSIGNED length, bool isMutable);
    LocalMemSpace *mutableSpace, *immutableSpace;
};

class ThreadScanner: public QuickGCScanner
{
public:
    ThreadScanner(GCTaskId* id): QuickGCScanner(false), taskID(id), mutableSpace(0), immutableSpace(0),
        spaceTable(0), nOwnedSpaces(0) {}
    virtual ~ThreadScanner() { free(spaceTable); }

    void ScanOwnedAreas(void);
private:
    virtual LocalMemSpace *FindSpace(POLYUNSIGNED length, bool isMutable);
    bool TakeOwnership(LocalMemSpace *space);

    GCTaskId *taskID;
    LocalMemSpace *mutableSpace, *immutableSpace;
    LocalMemSpace **spaceTable;
    unsigned nOwnedSpaces;
};

// This uses the conditional exchange instruction to check and update
// the forwarding pointer.  It uses a lock prefix so that if another
// thread has updated it in the meantime it will not set it.
// Using the assembly code provides a very small speed-up so may not
// be worth-while. 
#if defined(_MSC_VER) && (_MSC_VER >= 1600)
// In later versions of MS C we can use the intrinsic.
// 1600 is Visual Studio 2010.  It may well work in older versions
#   include <intrin.h>
#   pragma intrinsic(_InterlockedCompareExchange)
#   if (SIZEOF_VOIDP == 8)
#       define InterlockedCompareExchange64 _InterlockedCompareExchange64
#   else
#       define InterlockedCompareExchange   _InterlockedCompareExchange
#   endif
#endif

static bool atomiclySetForwarding(LocalMemSpace *space, POLYUNSIGNED *pt,
                                  POLYUNSIGNED testVal, POLYUNSIGNED update)
{
#ifdef _MSC_VER
# if (SIZEOF_VOIDP == 8)
    LONGLONG *address = (LONGLONG*)(pt-1);
    POLYUNSIGNED result = InterlockedCompareExchange64(address, update, testVal);
    return result == testVal;
# else
    LONG *address = (LONG*)(pt-1);
    POLYUNSIGNED result = InterlockedCompareExchange(address, update, testVal);
    return result == testVal;
# endif
#elif(defined(HOSTARCHITECTURE_X86) && defined(__GNUC__))
    POLYUNSIGNED result;
    __asm__ __volatile__ (
        "lock; cmpxchgl %1,%2"
        :"=a"(result)
        :"r"(update),"m"(pt[-1]),"0"(testVal)
        :"memory", "cc"
    );
    return result == testVal;
#elif(defined(HOSTARCHITECTURE_X86_64) && defined(__GNUC__))
    POLYUNSIGNED result;
    __asm__ __volatile__ (
        "lock; cmpxchgq %1,%2"
        :"=a"(result)
        :"r"(update),"m"(pt[-1]),"0"(testVal)
        :"memory", "cc"
    );
    return result == testVal;
#else
    // Fallback on other targets.
    PLocker lock(&space->spaceLock);
    if (pt[-1] == testVal)
    {
        pt[-1] = update;
        return true;
    }
    return false;
#endif
}

PolyObject *QuickGCScanner::FindNewAddress(PolyObject *obj, POLYUNSIGNED L, LocalMemSpace *srcSpace)
{
    bool isMutable = OBJ_IS_MUTABLE_OBJECT(L);
    POLYUNSIGNED n = OBJ_OBJECT_LENGTH(L);
    LocalMemSpace *lSpace = FindSpace(n, isMutable);
    if (lSpace == 0)
        return 0; // Unable to move it.
    PolyObject *newObject = (PolyObject*)(lSpace->lowerAllocPtr+1);

    // It's possible that another thread may have actually copied the 
    // object since we loaded the length word so we check it again.
    // If this is a mutable we must ensure that checking the forwarding
    // pointer here and updating it if necessary is atomic.  We don't need
    // to do that for immutable data so there is a small chance that an
    // object may be copied twice.  That's not a problem for immutable data.
    // Also lock this if it's code.  This may not be necessary but code objects
    // are rare. Updating the addresses in code objects is complicated and
    // it's possible that there are assumptions somewhere that there's only one
    // copy.
    // Avoiding locking for immutables provides only a small speed-up so may not
    // be worth-while.
    if (isMutable || OBJ_IS_CODE_OBJECT(L))
    {
        if (! atomiclySetForwarding(srcSpace, (POLYUNSIGNED*)obj, L, OBJ_SET_POINTER(newObject)))
        {
            newObject = obj->GetForwardingPtr();
            if (debugOptions & DEBUG_GC_DETAIL)
                Log("GC: Quick: %p %lu %u has already moved to %p\n", obj, n, GetTypeBits(L), newObject);
            objectCopied = false;
            return newObject;
        }
    }
    else
    {
        if (obj->ContainsForwardingPtr())
        {
            newObject = obj->GetForwardingPtr();
            if (debugOptions & DEBUG_GC_DETAIL)
                Log("GC: Quick: %p %lu %u has already moved to %p\n", obj, n, GetTypeBits(L), newObject);
            objectCopied = false;
            return newObject;
        }
        else obj->SetForwardingPtr(newObject);
    }

    lSpace->lowerAllocPtr += n+1;
    CopyObjectToNewAddress(obj, newObject, L);
    objectCopied = true;
    return newObject;
}

// When scanning the roots we want to distribute the data among the immutable and mutable areas
// so that the work is distributed for the scanning threads.
LocalMemSpace *RootScanner::FindSpace(POLYUNSIGNED n, bool isMutable)
{
    POLYUNSIGNED spaceAllocated = 0;
    LocalMemSpace *lSpace = isMutable ? mutableSpace : immutableSpace;

    if (lSpace != 0)
    {
        // See if there's space in the existing area.
        if (lSpace->freeSpace() > n /* At least n+1*/)
            return lSpace;
    }

    // Find the space with the largest free area.
    for (unsigned i = 0; i < gMem.nlSpaces; i++)
    {
        LocalMemSpace *sp = gMem.lSpaces[i];
        if (sp->isMutable == isMutable && !sp->allocationSpace &&
                (lSpace == 0 || sp->freeSpace() > lSpace->freeSpace()))
            lSpace = sp;
    }

    if (lSpace != 0 && lSpace->freeSpace() > n)
    {
        if (isMutable) mutableSpace = lSpace; else immutableSpace = lSpace;
        return lSpace;
    }

    if (spaceAllocated < gMem.SpaceBeforeMajorGC())
    {
        // Allocate a new space for this.
        POLYUNSIGNED spaceSize = gMem.DefaultSpaceSize();
        if (n+1 > spaceSize) spaceSize = n+1;
        LocalMemSpace *lSpace = gMem.NewLocalSpace(spaceSize, isMutable);
        if (lSpace == 0)
            return 0; // Couldn't allocate it.
        lSpace->partialGCTop = lSpace->upperAllocPtr;
        lSpace->partialGCScan = lSpace->lowerAllocPtr;
        lSpace->spaceOwner = 0;
        return lSpace;
    }
    return 0; // Insufficient space
}

// When scanning within a thread we don't want to be searching the space table.
LocalMemSpace *ThreadScanner::FindSpace(POLYUNSIGNED n, bool isMutable)
{
    LocalMemSpace *lSpace = isMutable ? mutableSpace : immutableSpace;

    if (lSpace != 0)
    {
        // See if there's space in the existing area.
        if (lSpace->freeSpace() > n /* At least n+1*/)
            return lSpace;
    }

    for (unsigned i = 0; i < nOwnedSpaces; i++)
    {
        lSpace = spaceTable[i];
        if (lSpace->isMutable == isMutable &&
            ! lSpace->allocationSpace && lSpace->freeSpace() > n /* At least n+1*/)
        {
            if (n < 10)
            {
                // We use this space for further allocations unless we are trying to
                // allocate a "large" object.
                if (isMutable) mutableSpace = lSpace; else immutableSpace = lSpace;
            }
            return lSpace;
        }
    }

    PLocker l(&localTableLock);
    // Another thread may allocate a new area, reallocating gMem.lSpaces so we
    // we need a lock here.
    if (taskID != 0)
    {
        // See if we can take a space that is currently unused.
        for (unsigned i = 0; i < gMem.nlSpaces; i++)
        {
            lSpace = gMem.lSpaces[i];
            if (lSpace->spaceOwner == 0 && lSpace->isMutable == isMutable &&
                ! lSpace->allocationSpace && lSpace->freeSpace() > n /* At least n+1*/)
            {
                if (debugOptions & DEBUG_GC)
                    Log("GC: Quick: Thread %p is taking ownership of space %p\n", taskID, lSpace);
                if (! TakeOwnership(lSpace))
                    return 0;
                return lSpace;
            }
        }
    }

    POLYUNSIGNED spaceAllocated = 0;
    for (unsigned k = 0; k < gMem.nlSpaces; k++)
    {
        LocalMemSpace *space = gMem.lSpaces[k];
        if (! space->allocationSpace)
            spaceAllocated += space->spaceSize();
    }

    if (spaceAllocated < gMem.SpaceBeforeMajorGC())
    {
        // Allocate a new space for this.
        POLYUNSIGNED spaceSize = gMem.DefaultSpaceSize();
        if (n+1 > spaceSize) spaceSize = n+1;
        lSpace = gMem.NewLocalSpace(spaceSize, isMutable);
        if (lSpace == 0)
            return 0; // Couldn't allocate it.
        lSpace->partialGCTop = lSpace->upperAllocPtr;
        lSpace->partialGCScan = lSpace->lowerAllocPtr;
        lSpace->spaceOwner = 0;
        if (! TakeOwnership(lSpace))
            return 0;
        return lSpace;
    }

    return 0; // Insufficient space
}

// Copy all the objects.
POLYUNSIGNED QuickGCScanner::ScanAddressAt(PolyWord *pt)
{
    POLYUNSIGNED n = 1; // Set up the loop to process one word at *pt
    pt++;
    
    while (n-- != 0)
    {
        PolyWord val = *(--pt);
        if (! val.IsTagged())
        {
            LocalMemSpace *space = gMem.LocalSpaceForAddress(val.AsAddress());

            // We only copy it if it is in a local allocation space and not in the
            // "overflow" area of data that could not copied by the last full GC.
            if (space != 0 && space->allocationSpace && val.AsAddress() <= space->upperAllocPtr)
            {
                // We shouldn't get code addresses since we handle code
                // segments separately so if this isn't an integer it must be an object address.
                ASSERT(OBJ_IS_DATAPTR(val));

                PolyObject *obj = val.AsObjPtr();
                // Load the length word without any interlock.  We can't assume that
                // another thread won't also copy this at the same time.
                POLYUNSIGNED L = obj->LengthWord();

                // Has it been moved already? N.B.  Another thread may be in the process of
                // moving it so the new object may not be fully copied.
                if (OBJ_IS_POINTER(L))
                    *pt = OBJ_GET_POINTER(L);
                else
                {
                    // We need to copy this object.
                    PolyObject *newObject = FindNewAddress(obj, L, space); // New address of object.

                    if (newObject == 0) { // Couldn't copy it - not enough space.
                        succeeded = false;

                        if (debugOptions & DEBUG_GC_DETAIL)
                            Log("GC: Quick: Insufficient space to move %p %lu %u\n",
                                obj, OBJ_OBJECT_LENGTH(L), GetTypeBits(L));

                        return 0;
                    }

                    *pt = newObject; // Update the pointer to the object
                    // N.B.  If another thread has just copied it "newObject" may actually
                    // be an address in another thread's space.  In that case "objectCopied"
                    // will be false.

                    if (debugOptions & DEBUG_GC_DETAIL)
                        Log("GC: Quick: %p %lu %u moved to %p\n", obj, OBJ_OBJECT_LENGTH(L), GetTypeBits(L), newObject);

                    // Stop now unless this is a simple word object we have been able to move.
                    // Also stop if we're just scanning the roots.
                    if (! rootScan && newObject != obj && ! OBJ_IS_MUTABLE_OBJECT(L) && 
                        GetTypeBits(L) == 0 && objectCopied)
                    {
                        // We can simply return zero in which case this performs a breadth-first scan.
                        // A breadth-first scan distributes the objects through the memory so
                        // to retain some degree of locality we try to copy some object pointed at
                        // by this one.  We work from the end back so that we follow the tail pointers
                        // for lists.
                        n = OBJ_OBJECT_LENGTH(L); // Object length
                        pt = (PolyWord*)newObject + n;
                    }
                }
            }
        }
    }
    // We've reached the end without finding a pointer to follow
    return 0;
}

// The initial entry to process the roots.  Also used when processing the addresses
// in objects that can't be handled by ScanAddressAt.
PolyObject *QuickGCScanner::ScanObjectAddress(PolyObject *base)
{
    PolyWord val = base;
    // Scan this as an address.
    (void)QuickGCScanner::ScanAddressAt(&val);
    // Ignore the result of ScanAddressAt which is always zero and
    // just return the updated address.
    return val.AsObjPtr();
}

// Add this to the set of spaces we own.  Must be called with the
// localTableLock held.
bool ThreadScanner::TakeOwnership(LocalMemSpace *space)
{
    ASSERT(space->spaceOwner == 0);
    LocalMemSpace **v = (LocalMemSpace**)realloc(spaceTable, (nOwnedSpaces+1)*sizeof(LocalMemSpace*));
    if (v == 0)
        return false;
    spaceTable = v;
    space->spaceOwner = taskID;
    spaceTable[nOwnedSpaces++] = space;
    return true;
}

// Thread function to scan an area.  It scans the addresses in the region
// copying any objects from the allocation area into mutable or immutable
// areas it owns.  It then processes all the areas it owns until there
// are no further addresses to scan.
static void scanArea(GCTaskId *id, void *arg1, void *arg2)
{
    ThreadScanner marker(id);
    marker.ScanAddressesInRegion((PolyWord*)arg1, (PolyWord*)arg2);
    marker.ScanOwnedAreas();
}

void ThreadScanner::ScanOwnedAreas()
{
    while (true)
    {
        bool allDone = true;
        // We're finished when there is no unscanned data in any space we own.
        for (unsigned k = 0; k < nOwnedSpaces && allDone; k++)
        {
            LocalMemSpace *space = spaceTable[k];
            allDone = space->partialGCScan == space->lowerAllocPtr;
        }
        if (allDone)
            break;

        // Scan each area that has had data added to it.
        for (unsigned l = 0; l < nOwnedSpaces; l++)
        {
            LocalMemSpace *space = spaceTable[l];
            // Scan the area.  This may well result in more data being added
            while (space->partialGCScan < space->lowerAllocPtr)
            {
                // Is the queue draining?  If so it's probably worth creating
                // some spare work.
                if (gpTaskFarm->Draining())
                {
                    PolyWord *mid =
                        space->partialGCScan + (space->lowerAllocPtr - space->partialGCScan)/2;
                    // Split the space in two.
                    PolyWord *p = space->partialGCScan;
                    while (p < mid)
                    {
                        PolyObject *o = (PolyObject*)(p+1);
                        ASSERT(o->ContainsNormalLengthWord());
                        p += o->Length()+1;
                    }
                    // Start a new task to scan the area up to the half-way point.
                    // Because we round up to the end of the next object we may
                    // include the whole area but that's probably better because
                    // we may have other areas to scan.
                    if (gpTaskFarm->AddWork(scanArea, space->partialGCScan, p))
                    {
                        space->partialGCScan = p;
                        if (space->lowerAllocPtr == space->partialGCScan)
                            break;
                    }
                }
                PolyObject *obj = (PolyObject*)(space->partialGCScan+1);
                ASSERT(obj->ContainsNormalLengthWord());
                POLYUNSIGNED length = obj->Length();
                ASSERT(space->partialGCScan+length+1 <= space->lowerAllocPtr);
                space->partialGCScan += length+1;
                if (length != 0)
                    ScanAddressesInObject(obj);
                // If any thread has run out of space we should stop.
                if (! succeeded)
                    return;
            }
        }
    }
    // Release the spaces we're holding in case another thread wants to use them.
    for (unsigned m = 0; m < nOwnedSpaces; m++)
    {
        LocalMemSpace *space = spaceTable[m];
        space->spaceOwner = 0;
    }
    nOwnedSpaces = 0;
}

// Called after a minor GC.  Currently does nothing.
// See also adjustHeapSize for adjustments after a major GC.
static void adjustHeapSizeAfterMinorGC(POLYUNSIGNED spaceCopiedOut)
{
    TIMEDATA gc, total;
    gc.add(gcTimeData.minorGCSystemCPU);
    gc.add(gcTimeData.minorGCUserCPU);
    total.add(gc);
    total.add(gcTimeData.minorNonGCSystemCPU);
    total.add(gcTimeData.minorNonGCUserCPU);
    float g = gc.toSeconds() / total.toSeconds();
    // In this case we compute l as the live memory in the allocation area
    // divided by the size of the allocation area.
    float l = (float)spaceCopiedOut / (float)gMem.SpaceBeforeMinorGC();
    if (debugOptions & DEBUG_HEAPSIZE)
        Log("Heap: Minor resizing factors g = %f, l = %f\n", g, l);

}


bool RunQuickGC(void)
{
    gcTimeData.RecordGCTime(GcTimeData::GCTimeStart);
    globalStats.incCount(PSC_GC_PARTIALGC);
    mainThreadPhase = MTP_GCQUICK;
    succeeded = true;

    if (debugOptions & DEBUG_GC)
        Log("GC: Beginning quick GC\n");

    POLYUNSIGNED spaceBeforeGC = 0;

    for(unsigned k = 0; k < gMem.nlSpaces; k++)
    {
        LocalMemSpace *lSpace = gMem.lSpaces[k];
        ASSERT (lSpace->top >= lSpace->upperAllocPtr);
        ASSERT (lSpace->upperAllocPtr >= lSpace->lowerAllocPtr);
        ASSERT (lSpace->lowerAllocPtr >= lSpace->bottom);
        // Remember the top before we started this GC.  It's
        // only relevant for mutable areas.  It avoids us rescanning
        // objects that may have been added to the space as a result of
        // scanning another space.
        if (lSpace->isMutable)
            lSpace->partialGCTop = lSpace->upperAllocPtr;
        else lSpace->partialGCTop = lSpace->top;
        // If we're scanning a space this is where we start.
        // For immutable areas this only includes newly added
        // data but for mutable areas we have to scan data added
        // by previous GCs.
        if (lSpace->isMutable && ! lSpace->allocationSpace)
            lSpace->partialGCScan = lSpace->bottom;
        else lSpace->partialGCScan = lSpace->lowerAllocPtr;
        lSpace->spaceOwner = 0; // Not currently owned
        // Add up the space in the mutable and immutable areas
        if (! lSpace->allocationSpace)
            spaceBeforeGC += lSpace->allocatedSpace();
    }

    // First scan the roots, copying the data into the mutable and immutable areas.
    RootScanner rootScan;
    // Scan the permanent mutable areas.  This could be parallelised but it doesn't
    // appear to be worthwhile at the moment.
    for (unsigned j = 0; j < gMem.npSpaces; j++)
    {
        PermanentMemSpace *space = gMem.pSpaces[j];
        if (space->isMutable && ! space->byteOnly)
            rootScan.ScanAddressesInRegion(space->bottom, space->top);
    }

    // Scan RTS addresses.  This will include the thread stacks.
    GCModules(&rootScan);

    // At this point the immutable and mutable areas will have some root objects
    // in the space between partialGCScan (the old value of lowerAllocPtr) and
    // lowerAllocPtr.  These will contain the addresses of objects in the allocation
    // areas.  We need to scan these root objects and then any new objects we copy
    // until there are no objects left to scan.
    // We also need to scan local mutable areas since these are roots as well.
    // They have data between partialGCTop and top.  Parallelising this appears
    // to be a significant gain.
    {
        unsigned l = 0;
        while (true)
        {
            LocalMemSpace *space;
            {
                // There is a chance that a thread that has already been forked may
                // allocate a new space and realloc gMem.lSpaces.  We have to drop
                // the lock before calling AddWorkOrRunNow in case we "run now".
                PLocker lock(&localTableLock);
                if (l >= gMem.nlSpaces)
                    break;
                space = gMem.lSpaces[l++];
            }
            if (space->partialGCScan != space->lowerAllocPtr)
                gpTaskFarm->AddWorkOrRunNow(scanArea, space->partialGCScan, space->lowerAllocPtr);
            if (space->partialGCTop != space->top)
                gpTaskFarm->AddWorkOrRunNow(scanArea, space->partialGCTop, space->top);
        }
    }

    gpTaskFarm->WaitForCompletion();

    if (succeeded)
    {

        globalStats.setSize(PSS_AFTER_LAST_GC, 0);
        globalStats.setSize(PSS_ALLOCATION, 0);
        globalStats.setSize(PSS_ALLOCATION_FREE, 0);
        // If it succeeded the allocation areas are now empty.
        POLYUNSIGNED spaceAfterGC = 0;
        for(unsigned l = 0; l < gMem.nlSpaces; l++)
        {
            LocalMemSpace *lSpace = gMem.lSpaces[l];
            POLYUNSIGNED free;
            if (lSpace->allocationSpace)
            {
                lSpace->lowerAllocPtr = lSpace->bottom;
                free = lSpace->freeSpace();
#ifdef FILL_UNUSED_MEMORY
                // This provides extra checking if we have dangling pointers
                memset(lSpace->bottom, 0xaa, (char*)lSpace->upperAllocPtr - (char*)lSpace->bottom);
#endif
                globalStats.incSize(PSS_ALLOCATION, free*sizeof(PolyWord));
                globalStats.incSize(PSS_ALLOCATION_FREE, free*sizeof(PolyWord));
            }
            else free = lSpace->freeSpace();
            if (debugOptions & DEBUG_GC)
                Log("GC: %s space %p %d free in %d words %2.1f%% full\n", lSpace->spaceTypeString(),
                    lSpace, lSpace->freeSpace(), lSpace->spaceSize(),
                    ((float)lSpace->allocatedSpace()) * 100 / (float)lSpace->spaceSize());
            globalStats.incSize(PSS_AFTER_LAST_GC, free*sizeof(PolyWord));
            spaceAfterGC += lSpace->allocatedSpace();
        }

        gcTimeData.RecordGCTime(GcTimeData::GCTimeEnd);

        adjustHeapSizeAfterMinorGC(spaceAfterGC-spaceBeforeGC); // Adjust the allocation size.
        gcTimeData.resetMinorTimingData();
        // Remove allocation spaces that are larger than the default
        // and any excess over the current size of the allocation area.
        gMem.RemoveExcessAllocation();

        if (debugOptions & DEBUG_GC)
            Log("GC: Completed successfully\n");

        CheckMemory();
    }
    else
    {
        // There was insufficient room to copy everything.  We will need to
        // run a full GC.
        gcTimeData.RecordGCTime(GcTimeData::GCTimeEnd);
        if (debugOptions & DEBUG_GC)
            Log("GC: Quick GC failed\n");
    }

    return succeeded;
}
