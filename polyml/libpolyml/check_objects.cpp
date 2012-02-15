/*
    Title:      Validate addresses in objects.

    Copyright (c) 2006
        David C.J. Matthews

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

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x)   assert(x)
#else
#define ASSERT(x) 
#endif


#include "globals.h"
#include "diagnostics.h"
#include "machine_dep.h"
#include "scanaddrs.h"
#include "memmgr.h"

#define INRANGE(val,start,end)\
  (start <= val && val < end)

static void CheckAddress(PolyWord *pt)
{
    MemSpace *space = gMem.SpaceForAddress(pt);
    if (space == 0)
        Crash ("Bad pointer 0x%08x found", pt);
    PolyObject *obj = (PolyObject*)pt;
    POLYUNSIGNED length;
    // At the start of a full GC we can have forwarding
    // pointers left over.
    if (obj->ContainsForwardingPtr())
        length = obj->GetForwardingPtr()->Length();
    else length = obj->Length();
    ASSERT(pt+length <= space->top);
}

void DoCheck (const PolyWord pt)
{
    if (pt == PolyWord::FromUnsigned(0)) return;

    if (pt.IsTagged()) return;

    ASSERT (pt.IsDataPtr());
    CheckAddress(pt.AsStackAddr());
} 

class ScanCheckAddress: public ScanAddress
{
public:
    virtual PolyObject *ScanObjectAddress(PolyObject *pt) { DoCheck(pt); return pt; }
};

void DoCheckObject (const PolyObject *base, POLYUNSIGNED L)
{

    PolyWord *pt  = (PolyWord*)base;
    CheckAddress(pt);
    MemSpace *space = gMem.SpaceForAddress(pt);
    if (space == 0)
        Crash ("Bad pointer 0x%08x found", pt);

    ASSERT (OBJ_IS_LENGTH(L));

    POLYUNSIGNED n   = OBJ_OBJECT_LENGTH(L);
    if (n == 0) return;

    ASSERT (n > 0);
    ASSERT(pt-1 >= space->bottom && pt+n <= space->top);

    byte flags = GetTypeBits(L);  /* discards GC flag and mutable bit */

    if (flags == F_BYTE_OBJ) /* possibly signed byte object */
        return; /* Nothing more to do */

    if (flags == F_CODE_OBJ) /* code object */
    {
        ScanCheckAddress checkAddr;
        /* We flush the instruction cache here in case we change any of the
          instructions when we update addresses. */
        machineDependent->FlushInstructionCache(pt, (n + 1) * sizeof(PolyWord));
        machineDependent->ScanConstantsWithinCode((PolyObject *)base, (PolyObject *)base, n, &checkAddr);
        /* Skip to the constants. */
        base->GetConstSegmentForCode(n, pt, n);
    }
    else ASSERT (flags == 0); /* ordinary word object */

    while (n--) DoCheck (*pt++);
}

void DoCheckPointer (const PolyWord pt)
{
    if (pt == PolyWord::FromUnsigned(0)) return;

    if (OBJ_IS_AN_INTEGER(pt)) return;

    if (gMem.IsIOPointer(pt.AsAddress())) return;

    DoCheck (pt);

    if (pt.IsDataPtr())
    {
        PolyObject *obj = pt.AsObjPtr();
        DoCheckObject (obj, obj->LengthWord());
    }
}

// Check all the objects in the memory.  Used to check the garbage collector
//
void DoCheckMemory()
{
    ScanCheckAddress memCheck;
    // Scan the allocation areas.  This is where new objects are created.
    // The other local areas are only modified by the GC.
    for (unsigned i = 0; i < gMem.nlSpaces; i++)
    {
        LocalMemSpace *space = gMem.lSpaces[i];
        if (space->allocationSpace)
        {
            memCheck.ScanAddressesInRegion(space->bottom, space->lowerAllocPtr);
            memCheck.ScanAddressesInRegion(space->upperAllocPtr, space->top);
        }
    }
    // Scan the permanent mutable areas.
    for (unsigned j = 0; j < gMem.npSpaces; j++)
    {
        PermanentMemSpace *space = gMem.pSpaces[j];
        if (space->isMutable && ! space->byteOnly)
            memCheck.ScanAddressesInRegion(space->bottom, space->top);
    }
}
