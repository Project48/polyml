/*
    Title:  memmgr.cpp   Memory segment manager

    Copyright (c) 2006 David C. J. Matthews

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
#ifdef _WIN32_WCE
#include "winceconfig.h"
#else
#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x)   assert(x)
#else
#define ASSERT(x)
#endif

#include "globals.h"
#include "memmgr.h"
#include "osmem.h"
#include "scanaddrs.h"
#include "bitmap.h"

MemSpace::MemSpace()
{
    spaceType = ST_PERMANENT;
    isMutable = false;
    bottom = 0;
    top = 0;
}

LocalMemSpace::LocalMemSpace()
{
    spaceType = ST_LOCAL;
    gen_top = 0; 
    gen_bottom = 0;
    highest = 0;
    pointer = 0;
    for (unsigned i = 0; i < NSTARTS; i++)
        start[i] = 0;
    start_index = 0;
    i_marked = m_marked = copied = updated = 0;
}

LocalMemSpace::~LocalMemSpace()
{
    if (bottom != 0)
        osMemoryManager->Free(bottom, (char*)top - (char*)bottom);
}

bool LocalMemSpace::InitSpace(POLYUNSIGNED size, bool mut)
{
    isMutable = mut;
    // Allocate the heap itself.
    size_t iSpace = size*sizeof(PolyWord);
    bottom  =
        (PolyWord*)osMemoryManager->Allocate(iSpace, PERMISSION_READ|PERMISSION_WRITE|PERMISSION_EXEC);

    if (bottom == 0)
        return false;

    // The size may have been rounded up to a block boundary.
    size = iSpace/sizeof(PolyWord);

    top = bottom + size;
    gen_top = top;
    pointer = top;
    gen_bottom = top;
    
    // Bitmap for the space.
    return bitmap.Create(size);
}

ExportMemSpace::ExportMemSpace()
{
    pointer = 0;
}

MemMgr::MemMgr()
{
    npSpaces = nlSpaces = 0;
    minLocal = maxLocal = 0;
    pSpaces = 0;
    lSpaces = 0;
    eSpaces = 0;
}

MemMgr::~MemMgr()
{
    unsigned i;
    for (i = 0; i < npSpaces; i++)
        delete(pSpaces[i]);
    for (i = 0; i < nlSpaces; i++)
        delete(lSpaces[i]);
    for (i = 0; i < neSpaces; i++)
        delete(eSpaces[i]);
}

// Create and initialise a new local space and add it to the table.
LocalMemSpace* MemMgr::NewLocalSpace(POLYUNSIGNED size, bool mut)
{
    LocalMemSpace *space = new LocalMemSpace;
    if (! space->InitSpace(size, mut))
    {
        delete space;
        return 0;
    }
    // Compute the maximum and minimum local addresses.  The idea
    // is to speed up LocalSpaceForAddress which is likely to be a hot-spot
    // in the GC.
    if (nlSpaces == 0)
    {
        // First space
        minLocal = space->bottom;
        maxLocal = space->top;
    }
    else
    {
        if (space->bottom < minLocal)
            minLocal = space->bottom;
        if (space->top > maxLocal)
            maxLocal = space->top;
    }

    // Add to the table.
    LocalMemSpace **table = (LocalMemSpace **)realloc(lSpaces, (nlSpaces+1) * sizeof(LocalMemSpace *));
    if (table == 0)
    {
        delete space;
        return 0;
    }
    lSpaces = table;
    lSpaces[nlSpaces++] = space;
    return space;
}

// Create an entry for a permanent space.
MemSpace* MemMgr::NewPermanentSpace(PolyWord *base, POLYUNSIGNED words, bool mut)
{
    MemSpace *space = new MemSpace;
    space->bottom = base;
    space->top = space->bottom + words;
    space->spaceType = ST_PERMANENT;
    space->isMutable = mut;

    // Extend the permanent memory table and add this space to it.
    MemSpace **table = (MemSpace **)realloc(pSpaces, (npSpaces+1) * sizeof(MemSpace *));
    if (table == 0)
    {
        delete space;
        return 0;
    }
    pSpaces = table;
    pSpaces[npSpaces++] = space;
    return space;
}

// Delete a local space and remove it from the table.
bool MemMgr::DeleteLocalSpace(LocalMemSpace *sp)
{
    for (unsigned i = 0; i < nlSpaces; i++)
    {
        if (lSpaces[i] == sp)
        {
            delete sp;
            nlSpaces--;
            while (i < nlSpaces)
            {
                lSpaces[i] = lSpaces[i+1];
                i++;
            }
            return true;
        }
    }
    return false;
}

// Create an entry for the IO space.
MemSpace* MemMgr::InitIOSpace(PolyWord *base, POLYUNSIGNED words)
{
    ioSpace.bottom = base;
    ioSpace.top = ioSpace.bottom + words;
    ioSpace.spaceType = ST_IO;
    ioSpace.isMutable = false;
    return &ioSpace;
}


// Create and initialise a new export space and add it to the table.
ExportMemSpace* MemMgr::NewExportSpace(POLYUNSIGNED size, bool mut)
{
    ExportMemSpace *space = new ExportMemSpace;
    space->spaceType = ST_EXPORT;
    space->isMutable = mut;
    // Allocate the memory itself.
    size_t iSpace = size*sizeof(PolyWord);
    space->bottom  =
        (PolyWord*)osMemoryManager->Allocate(iSpace, PERMISSION_READ|PERMISSION_WRITE|PERMISSION_EXEC);

    if (space->bottom == 0)
    {
        delete space;
        return 0;
    }
 
    // The size may have been rounded up to a block boundary.
    size = iSpace/sizeof(PolyWord);
    space->top = space->bottom + size;
    space->pointer = space->top;

    // Add to the table.
    ExportMemSpace **table = (ExportMemSpace **)realloc(eSpaces, (neSpaces+1) * sizeof(ExportMemSpace *));
    if (table == 0)
    {
        delete space;
        return 0;
    }
    eSpaces = table;
    eSpaces[neSpaces++] = space;
    return space;
}

void MemMgr::DeleteExportSpaces(void)
{
    while (neSpaces > 0)
        delete(eSpaces[--neSpaces]);
}


void MemMgr::OpOldMutables(ScanAddress *process) // Scan permanent mutable areas
{
    for (unsigned i = 0; i < npSpaces; i++)
    {
        MemSpace *space = pSpaces[i];
        ASSERT(space->spaceType == ST_PERMANENT);
        if (space->isMutable)
            process->ScanAddressesInRegion(space->bottom, space->top - space->bottom);
    }
}

// Return the space the address is in or NULL if none.
// We have to check here against the bottom of the area rather
// than the allocation because, when called from CopyObject,
// the "pointer" field points to the top of the area.
// N.B.  By checking for pt < space->top we are assuming that we don't have
// zero length objects.  A zero length object at the top of the space would
// have its length word as the last word in the space and the address of the
// object would be == space->top.
MemSpace *MemMgr::SpaceForAddress(const void *pt)
{
    unsigned i;
    for (i = 0; i < nlSpaces; i++)
    {
        MemSpace *space = lSpaces[i];
        if (pt >= space->bottom && pt < space->top)
            return space;
    }
    for (i = 0; i < npSpaces; i++)
    {
        MemSpace *space = pSpaces[i];
        if (pt >= space->bottom && pt < space->top)
            return space;
    }
    for (i = 0; i < neSpaces; i++)
    {
        MemSpace *space = eSpaces[i];
        if (pt >= space->bottom && pt < space->top)
            return space;
    }
    if (pt >= ioSpace.bottom && pt < ioSpace.top)
        return &ioSpace;
    return 0; // Not in any space
}

bool MemMgr::IsPermanentMemoryPointer(const void *pt)
{
    for (unsigned i = 0; i < npSpaces; i++)
    {
        MemSpace *space = pSpaces[i];
        if (pt >= space->bottom && pt < space->top)
            return true;
    }
    return false;
}

// Return a mutable local space with at least enough room to satisfy
// the request or 0 if there isn't one.
LocalMemSpace *MemMgr::GetAllocSpace(POLYUNSIGNED words)
{
    for (unsigned j = 0; j < gMem.nlSpaces; j++)
    {
        LocalMemSpace *space = gMem.lSpaces[j];
        if (space->isMutable)
        {
            POLYUNSIGNED available = space->pointer - space->bottom;
            if (available >= words)
                return space;
        }
    }
    return 0; // There isn't one.
}

// Get the largest mutable local space with at least enough room
LocalMemSpace *MemMgr::GetLargestSpace(POLYUNSIGNED words)
{
    LocalMemSpace *result = 0;
    for (unsigned j = 0; j < gMem.nlSpaces; j++)
    {
        LocalMemSpace *space = gMem.lSpaces[j];
        if (space->isMutable)
        {
            POLYUNSIGNED available = space->pointer - space->bottom;
            if (available >= words)
            { // Large enough and the largest so far
                result = space;
                words = available+1; // Only larger areas will match
            }
        }
    }
    return result;
}


MemMgr gMem; // The one and only memory manager object
