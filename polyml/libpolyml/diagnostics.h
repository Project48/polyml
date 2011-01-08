/*
    Title:  Header for diagnostics.c

    Copyright (c) 2011 David C.J. Matthews

    Copyright (c) 2000
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

#ifndef _DIAGNOSTICS_H_USED
#define _DIAGNOSTICS_H_USED
#include "noreturn.h"

NORETURNFN(extern void Exit(const char *, ...));
NORETURNFN(extern void Crash(const char *, ...));

NORETURNFN(extern void ExitWithError(const char *, int err));

extern void SetLogFile(const char *fileName);
extern void Log(const char *, ...);

extern unsigned    debugOptions; // debugging  flags

// Values for debugging flags
#define DEBUG_CHECK_OBJECTS 0x001       // Check that addresses are valid.
#define DEBUG_GC            0x002       // Summary GC debugging output
#define DEBUG_MEMMGR        0x004       // Memory manager output
#define DEBUG_GC_DETAIL     0x008       // Detailed GC debugging output
#define DEBUG_THREADS       0x010       // Thread-related output
#define DEBUG_GCTASKS       0x020       // GC task farm
#define DEBUG_X             0x080       // X-Windows debugging

#endif
