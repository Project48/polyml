/*
    Title:      Foreign function interface
    Author:     Nick Chapman

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

#ifdef _WIN32_WCE
#include "winceconfig.h"
#include "wincelib.h"
#else
#ifdef WIN32
#include "winconfig.h"
#else
#include "config.h"
#endif
#endif

#if (defined(WIN32) || (defined(HAVE_DLOPEN)))
// Then we can use the foreign function interface.

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_ALLOCA_H
#include <alloca.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h> /* sleep */
#endif
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#else
// Earlier versions of Mac OS X had dlopen but no /usr/include/dlfcn.h
extern "C" {
     void *dlopen(const char *, int);
     int dlclose(void *);
     void *dlsym(void *, const char *);
     const char *dlerror(void);
}
#endif

#if defined(WIN32)
#include <windows.h>
#include "Console.h" /* For hApplicationInstance. */
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) 0
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif

#ifdef HAVE_TCHAR_H
#include <tchar.h>
#else
typedef char TCHAR;
#define _T(x) x
#endif

#include "globals.h"
#include "arb.h"
#include "reals.h"
#include "foreign.h"
#include "diagnostics.h"
#include "run_time.h"
#include "sys.h"
#include "machine_dep.h"
#include "mpoly.h"
#include "processes.h"
#include "scanaddrs.h"
#include "polystring.h"
#include "save_vec.h"
#include "rts_module.h"


/**********************************************************************
 *
 *  Stuff...
 *   
 **********************************************************************/
    
#define DLOPENFLAGS 1

#define Head(p) (((ML_Cons_Cell*)(p).AsObjPtr())->h)
#define Tail(p) (((ML_Cons_Cell*)(p).AsObjPtr())->t)

#define UNHANDLE(h)     DEREFWORDHANDLE(h)
#define SAVE(x)         gSaveVec->push(x)

int in_foreign_code = 0; /* Start off not in foreign code */


/**********************************************************************
 *
 *  Foreign Debug
 *   
 **********************************************************************/


static int foreign_debug = 0;

/* Debug levels:
   0 - (no-debug)
   1 - (debug)    Bug specific tracing.
   2 - (info)     Important info, e.g. the actual calls to foreign functions
   3 - (trace)    Trace every c-function
   4 - (mes)      Any old rubbish
   */

/*
   Level 1 can be used to track different bugs when they appear.
   For example, used it to watch Volatile GC when
   tracking `Invalid volatile' bug.
*/   


/***
 In each of the following macros, ARGS must be a bracketed arg list
 suitable for printf. This means usage will look like the following:
 (Note the double open/close brackets.)
 
       info(("example message with a parameter <%d>\n",parameter));
***/       

/* Give macro `__FUNCTION__' a defintion if we are not using GCC */
#if ! defined(__GNUC__) && !defined(__FUNCTION__)
#define __FUNCTION__        __FILE__ 
#endif

#define mesN(N,ARGS) \
    { \
      if (foreign_debug>=(N)) \
      { printf("%s:%4i (%s) ", __FILE__, __LINE__, __FUNCTION__); \
        printf ARGS; \
      } \
    }

#define show(ARGS) mesN(0,ARGS)
#define mes1(ARGS) mesN(1,ARGS)
#define mes2(ARGS) mesN(2,ARGS)
#define mes3(ARGS) mesN(3,ARGS)
#define mes4(ARGS) mesN(4,ARGS)


#define debug   mes1
#define info    mes2
#define trace   mes3
#define TRACE   mes3(("\n"))
#define mes     mes4


static Handle get_foreign_debug (Handle ignored)
{
  TRACE;
  return Make_arbitrary_precision(foreign_debug);
}

static Handle set_foreign_debug (Handle h)
{
  TRACE;
  foreign_debug = get_C_long(DEREFWORD(h));
  return h;
}
 

#define RAISE_EXN(string) raise_exception_string(EXC_foreign,(string))



static char *stringOfCtype(Ctype c)
{
  switch (c) {
  case Cchar    : return "Cchar";
  case Cdouble  : return "Cdouble";
  case Cfloat   : return "Cfloat";
  case Cint     : return "Cint";
  case Clong    : return "Clong";
  case Cpointer : return "Cpointer";
  case Cshort   : return "Cshort";
  case Cstruct  : return "Cstruct";
  case Cuint    : return "Cuint";
  default       : {
    static char buf[100];
    sprintf(buf,"Bad Ctype <%d>", c);
    return buf;
  }
  }
}


/**********************************************************************
 *
 * Volatiles ...
 * 
 **********************************************************************/

#define INITIAL_NUM_VOLS 200

/*typedef enum {False,True} Bool;*/
typedef int Bool; /* record size for malloc/free wrappers */

class PolyVolData;

typedef struct {
    PolyVolData* ML_pointer;     /* Pointer to ML token object. */
    void* C_pointer;      /* Pointer to C storage. */
    Bool Own_C_space;     /* Size if this is the owner of storage. */
} Volatile;


static Volatile *vols;

#define FIRST_VOL 0

static POLYUNSIGNED num_vols = 0;
static POLYUNSIGNED next_vol = FIRST_VOL;


/**********************************************************************
 *
 *  Expand Volatile array
 *   
 **********************************************************************/

static void expand_vol_array(void)
{
  POLYUNSIGNED new_num_vols = (num_vols==0) ? INITIAL_NUM_VOLS : num_vols*2;
  info(("<%lu> ---> <%lu>\n", num_vols, new_num_vols));
  {
    Volatile *new_vols = (Volatile*)malloc(sizeof(Volatile)*new_num_vols);
    if (new_vols == NULL) {
      RAISE_EXN("Can't Enlarge Volatile Array");
      /*NOTREACHED*/
    }
    memcpy(new_vols,vols,num_vols*sizeof(Volatile));
    free(vols);
    vols = new_vols;
    num_vols = new_num_vols;
  }
}


/* This table contains all the callback functions that have been created.  Once a callback
   has been set up it remains in existence for the rest of the session. */
static struct _cbStructEntry {
    PolyObject  *mlFunction;        /* The corresponding ML function. */
    PolyObject  *argType;           /* The argument type information. */
    unsigned char *cFunction;       /* The C function "stub" code. */
} *callbackTable;
static unsigned callBackEntries = 0;


// Recursive call stack.  This is needed to handle callbacks.
#define RECURSIVECALLSTACKSIZE  40  // Unlikely to be more than 1 or 2
static PolyObject *recursiveCallStack[RECURSIVECALLSTACKSIZE];
static unsigned recursiveCallStackPtr = 0;



/**********************************************************************
 *
 *  Malloc / Free Wrappers
 *   
 **********************************************************************/

static POLYUNSIGNED malloc_count = 0;
#if 0
#define Vmalloc(where,size) {where = malloc(size); printf("malloc: %p,%d\n",where,size); fflush(stdout); malloc_count++;}
#else
#define Vmalloc(where,size) {where = malloc(size); malloc_count++;}
#endif
#define Vfree(p) { free(p);  malloc_count--;}


/**********************************************************************
 *
 *  Volatile Allocation
 *   
 **********************************************************************/

/* With magic numbers */

// This is the ML object that represents a "vol"
class PolyVolData: public PolyObject {
public:
    POLYUNSIGNED    volmagic;
    POLYUNSIGNED    volindex;
};

#define VOL_BOX_SIZE            (sizeof(PolyVolData)/sizeof(PolyWord))
#define V_INDEX(v)              ((v))->volindex
#define V_MAGIC(v)              ((v))->volmagic
#define VOL_MAGIC_NUMBER        65169
#define MakeVolMagic(v)         V_MAGIC((v)) = VOL_MAGIC_NUMBER
#define IsVolMagic(v)           (V_MAGIC((v)) == VOL_MAGIC_NUMBER)

#define ML_POINTER(v)           (vols[V_INDEX(v)].ML_pointer)
#define C_POINTER(v)            (vols[V_INDEX(v)].C_pointer)
#define OWN_C_SPACE(v)          (vols[V_INDEX(v)].Own_C_space)

#define UNVOLHANDLE(_x)          ((PolyVolData*)DEREFHANDLE(_x))


/* Allocate a new "vol" in the table and in ML space and returns a pointer to the ML "vol". */
static Handle vol_alloc (void)
{
    PolyVolData* v = (PolyVolData*)alloc(VOL_BOX_SIZE, F_MUTABLE_BIT|F_BYTE_BIT);
    Handle result = SAVE(v);
    
    trace(("index=<%lu>\n",next_vol));
    if (next_vol >= num_vols) expand_vol_array();
    V_INDEX(v) = next_vol++;
    MakeVolMagic(v);
    ML_POINTER(v) = v;
    C_POINTER(v) = NULL;
    OWN_C_SPACE(v) = /*False*/0;
    
    return result;
}

/* Allocate a new "vol" in the table which points to a C object of size "size". */
static Handle vol_alloc_with_c_space (POLYUNSIGNED size)
{
    Handle res = vol_alloc();
    trace(("size= %lu\n",size));
    Vmalloc( C_POINTER(UNVOLHANDLE(res)), size );
    OWN_C_SPACE(UNVOLHANDLE(res)) = /*True*/size;
    return res;
}


/**********************************************************************
 *
 *  Volatile Dereferencing --- A safe version of C_POINTER
 *   
 **********************************************************************/

/* Returns the C-pointer component corresponding to the "vol" argument. */
static void* DEREFVOL (PolyWord v)
{ TRACE; {
    PolyVolData *vol = (PolyVolData*)v.AsObjPtr();
    
    POLYUNSIGNED index = V_INDEX(vol);
    trace(("<%lu>\n",index));
    
    if (!(IsVolMagic(vol))) {
        info (("Invalid volatile -- bad magic number, index=<%lu>\n", index));
        RAISE_EXN("Bad volatile magic number");
    }
    
    if (index < num_vols) {
        if (vols[index].ML_pointer == v.AsObjPtr()) {
            /* everything is okay */
            return vols[index].C_pointer;
            
        } else {
            info(("Invalid volatile -- backpointer is wrong <%lu>: <%p> != <%p>\n",
                index, vol, (void *)vols[index].ML_pointer));
        }
    } else {
        info(("Invalid volatile -- no such vol index <%lu>\n", index));
    }
    RAISE_EXN("Invalid volatile");
    /*NOTREACHED*/
	return 0;
}}

/**********************************************************************
 *
 *  Volatile Sanity Checking (debug only)
 *   
 **********************************************************************/
#if 0
static void sanity_check_vols(void)
{ TRACE; {
  POLYUNSIGNED v, v2;
  for (v=FIRST_VOL; v < next_vol; v++) {
    if (vols[v].ML_pointer == NULL) {
      trace(("vol <%lu> has nulled ML_pointer\n",v));
    } else {
      v2 = V_INDEX(vols[v].ML_pointer);
      if ( v2 != v ) {
    info(("vol <%d> has been corrupted to <%d>\n",v,v2));
      }
    }
  }
}}
#endif

/**********************************************************************
 *
 *  C Programming Primitives
 *   
 **********************************************************************/

/* This is roughly equivalent to calling malloc via the FFI, except that the vol it
   returns refers to the newly allocated store.  Typically we need a pointer to the
   store so we need to call address(alloc s). */
static Handle allocate (Handle h)
{ TRACE; {
  POLYUNSIGNED size = get_C_ulong(DEREFWORD(h)); /* bytes */

  Handle space = vol_alloc_with_c_space(size);
  return space;
}}

/* Constructs a one-word C object whose value is the C-pointer of the argument. */
static Handle address (Handle h)
{ TRACE; {
  Handle res = vol_alloc_with_c_space(sizeof(void*));
  *(void**)C_POINTER(UNVOLHANDLE(res)) = DEREFVOL(UNVOLHANDLE(h));
  return res;
}}

/* Returns a vol containing the value at the address given in its argument. */
static Handle deref (Handle h)
{ TRACE; {
  Handle res = vol_alloc();
  C_POINTER(UNVOLHANDLE(res))= *(void**)DEREFVOL(UNVOLHANDLE(h));
  return res;
}}


/******
 * offset is intended for accessing structure components.
 * pointer_add can be written in terms offset,address & deref:
 *   pointer_add p n == address (offset (deref p) n)
 ******/
/* This seems odd to me (DCJM) but it makes sense when you realise that a pointer is
   a vol whose C-pointer value contains the address of the vol containing the storage.
   There's always one more level of indirection than you think.  DCJM 12/4/04. */
static Handle offset (Handle h)
{ TRACE; {
    Handle res = vol_alloc();
    PolyWord structure  = UNHANDLE(h)->Get(0);
    int num_bytes = get_C_long(DEREFWORDHANDLE(h)->Get(1));
    
    C_POINTER(UNVOLHANDLE(res)) = (char*)DEREFVOL(structure) + num_bytes;
    return res;
}}
  

static Handle assign (Handle h)
{ TRACE; {
    
    PolyVolData *left  = (PolyVolData *)(UNHANDLE(h)->Get(0).AsObjPtr());
    PolyVolData *right = (PolyVolData *)(UNHANDLE(h)->Get(1).AsObjPtr());
    POLYSIGNED size = get_C_long(DEREFWORDHANDLE(h)->Get(2)); /* bytes */
    POLYSIGNED i;
    
    for (i=0; i<size; i++) {
        ((char*)C_POINTER(left))[i] = ((char*)DEREFVOL(right))[i];
    }
    
    return h; /* to be ignored */
}}


static Handle c_sizeof (Handle h)
{ TRACE; {
    PolyWord v = UNHANDLE(h);
    
    if (!(IS_INT(v))) {
        int size = get_C_long(v.AsObjPtr()->Get(0));
        trace(("Cstruct, size <%d>\n", size));
        return Make_arbitrary_precision(size);
    }
    else {
        Ctype ctype = (Ctype)UNTAGGED(v);
        trace(("<%s>\n", stringOfCtype(ctype)));
        switch (ctype) {
        case Cchar    : return Make_arbitrary_precision(sizeof(char));
        case Cdouble  : return Make_arbitrary_precision(sizeof(double));
        case Cfloat   : return Make_arbitrary_precision(sizeof(float));
        case Cint     : return Make_arbitrary_precision(sizeof(int));
        case Clong    : return Make_arbitrary_precision(sizeof(long));
        case Cpointer : return Make_arbitrary_precision(sizeof(void*));
        case Cshort   : return Make_arbitrary_precision(sizeof(short));
        case Cuint    : return Make_arbitrary_precision(sizeof(unsigned));
        default: {
            char buf[100];
            sprintf(buf, "Unknown ctype <%s>", stringOfCtype(ctype));
            RAISE_EXN(buf);
            /*NOTREACHED*/
            /* Keep -Wall happy */ return (Handle)0;
                 }
        }
    }
}}


static Handle alignment (Handle h)
{ TRACE; {
    PolyWord v = UNHANDLE(h);
    
    if (!(IS_INT(v))) {
        RAISE_EXN("alignment of structure"); 
    }
    else {
        Ctype ctype = (Ctype)UNTAGGED(v);
        trace(("<%s>\n", stringOfCtype(ctype)));
        switch (ctype) {
#ifdef __GNUC__ 
        case Cchar    : return Make_arbitrary_precision(__alignof__(char));
        case Cdouble  : return Make_arbitrary_precision(__alignof__(double));
        case Cfloat   : return Make_arbitrary_precision(__alignof__(float));
        case Cint     : return Make_arbitrary_precision(__alignof__(int));
        case Clong    : return Make_arbitrary_precision(__alignof__(long));
        case Cpointer : return Make_arbitrary_precision(__alignof__(void*));
        case Cshort   : return Make_arbitrary_precision(__alignof__(short));
        case Cuint    : return Make_arbitrary_precision(__alignof__(unsigned));
#else
            /* Take a guess... */
            /* Use "sizeof" here.  DCJM 19/4/01. */
        case Cchar    : return Make_arbitrary_precision(sizeof(char));
        case Cdouble  : return Make_arbitrary_precision(sizeof(double));
        case Cfloat   : return Make_arbitrary_precision(sizeof(float));
        case Cint     : return Make_arbitrary_precision(sizeof(int));
        case Clong    : return Make_arbitrary_precision(sizeof(long));
        case Cpointer : return Make_arbitrary_precision(sizeof(void*));
        case Cshort   : return Make_arbitrary_precision(sizeof(short));
        case Cuint    : return Make_arbitrary_precision(sizeof(unsigned));
#endif  
        default: {
            char buf[100];
            sprintf(buf, "Unknown ctype <%s>", stringOfCtype(ctype));
            RAISE_EXN(buf);
                 }
        }
    }
    /*NOTREACHED*/
    /* Keep -Wall happy */ return (Handle)0;
}}


/**********************************************************************
 *
 *  Volatile Garbage Collection
 *   
 **********************************************************************

During GC this function is called three times, with "op" taking the values
    1 -- MarkRuntimeObject
    2 -- ReferenceRuntimeObject
    3 -- UpdateRuntimeObject

Pass 1 is irrelevant. We call "op" (MarkRuntimeObject) with the second
parameter equal to 1.  This indicates the ML pointers held by the
array "vols" are weak references and so "op" does nothing and returns
immediately.

During Pass 2, the condition "(vols[from].ML_pointer != NULL)" is
always true, so we call "op" (ReferenceRuntimeObject) with the address
of each ML pointer cell contained in the array "vols".  Any cell that
holds a heap pointer that is not marked is overwritten by "op" to be
NULL. After the call to "op" we test if the cell has been nulled, if
so we deallocate the malloced space pointed to by the corresponding C
pointer.

Also, because the condition "(vols[from].ML_pointer != NULL)" is true
on every iteration of the loop, the counter "to" always keeps in step
with the loop counter "from". This prevents us from entering the code
guarded by the condition "(from>to)" (intended for pass 3).

During the Pass 3, we only call "op" (UpdateRuntimeObject) with the
address of those ML pointers that were not nulled on the previous
stage. The ML pointers are overwritten by "op" with their new location
in the heap.

Also, on this pass we compact the array "vols". The "to" counter is
only incremented when we encounter an ML pointer that is not NULL,
whereas the "from" counter is incremented on every iteration.  If
"from" > "to" we shift the pair of pointers (ML and C) leftwards in the
array from index "from" to index "to" . This requires us to update the
index held by the ML heap cell to be "to".

Finally we set "next_vol" to be "to", the index of the next volatile
to be allocated.  Although this occurs on every pass, it only has
effect on Pass 3.

**********************************************************************/

class Foreign: public RtsModule
{
public:
    void GarbageCollect(ScanAddress *process);
};

// Declare this.  It will be automatically added to the table.
static Foreign foreignModule;

void Foreign::GarbageCollect(ScanAddress *process)
{ TRACE; {
    POLYUNSIGNED to,from;
    
    for (from=FIRST_VOL, to=FIRST_VOL; from < next_vol; from++) {
        mes(("to=<%lu> from=<%lu>\n",to,from));
        
        if (vols[from].ML_pointer != NULL) {
            PolyObject *p = vols[from].ML_pointer;
            process->ScanRuntimeAddress(&p, ScanAddress::STRENGTH_WEAK);
            vols[from].ML_pointer = (PolyVolData*)p;
            
            if (vols[from].ML_pointer == NULL) { /* It's no longer reachable. */
                if (vols[from].Own_C_space) {
                    
                    mes(("Trashing malloc space of <%lu>\n",from));
                    {int i; for (i=0; i<vols[from].Own_C_space; i++) {
                        ((char*)vols[from].C_pointer)[i] = 0;
                    }}
                    
                    trace(("Freeing malloc space of <%lu>\n",from));
                    Vfree(vols[from].C_pointer);
                }
            }
            
            if (from>to) {
                trace(("Shifting volatile <%lu> ---> <%lu>\n",from,to));
                vols[to] = vols[from];
                V_INDEX(vols[to].ML_pointer) = to;
            }
            to++;
        }
    }
    next_vol = to;
    info(("unfreed mallocs=<%lu> next_vol=<%lu>\n", malloc_count, next_vol));
    
    /* Callback table.  Added DCJM 12/4/04.  We always process these as strong references.
    For the time being at any rate we treat these as permanent entries so that once a
    callback is set up it cannot be garbage-collected. */
    for (unsigned i = 0; i < callBackEntries; i++)
    {
        if (callbackTable[i].mlFunction != NULL)
        {
            process->ScanRuntimeAddress (&(callbackTable[i].mlFunction), ScanAddress::STRENGTH_STRONG);
            process->ScanRuntimeAddress (&(callbackTable[i].argType), ScanAddress::STRENGTH_STRONG);
        }
    }

    // Recursive call stack
    for (unsigned j = 0; j < recursiveCallStackPtr; j++)
        process->ScanRuntimeAddress (&(recursiveCallStack[j]), ScanAddress::STRENGTH_STRONG);
}}


/**********************************************************************
 *
 *  Load a Dynamic Library.
 *   
 **********************************************************************/

static Handle load_lib (Handle string)
{
	TCHAR name[500];
	
	Poly_string_to_C(DEREFWORD(string), name, sizeof(name)/sizeof(TCHAR));
	info(("<%s>\n", name));
	
#if defined(WINDOWS_PC)
	{
		HINSTANCE lib = LoadLibrary(name);
		if (lib == NULL) 
		{
			char buf[256];
			sprintf(buf, "load_lib <%s> : %i", name, GetLastError());
			RAISE_EXN(buf);
		}
		
		{
			Handle res = vol_alloc_with_c_space(sizeof(void*));
			*(void**)DEREFVOL(UNHANDLE(res)) = lib;
			return res;
		}
	}
#else  /* UNIX version */
	{
		void *lib = dlopen(name,DLOPENFLAGS);
		if (!lib)
		{
			char buf[256];
			sprintf(buf, "load_lib <%s> : %s", name, dlerror());
			RAISE_EXN(buf);
		}
		
		{
			Handle res = vol_alloc_with_c_space(sizeof(void*));
			*(void**)DEREFVOL(UNHANDLE(res)) = lib;
			return res;
		}
	}
#endif
}


/**********************************************************************
 *
 *  Load Symbol from a Dynamic Library
 *   
 **********************************************************************/

static Handle load_sym (Handle h)
{
	TCHAR name[500];
	
	Poly_string_to_C(DEREFHANDLE(h)->Get(1), name, sizeof(name)/sizeof(TCHAR));
	info(("<%s>\n", name));
	
#if defined(WINDOWS_PC)
	{
		FARPROC sym = GetProcAddress( *(HINSTANCE*)DEREFVOL(DEREFHANDLE(h)->Get(0)), name);
		
		if (sym == NULL) 
		{
			char buf[256];
			sprintf(buf, "load_sym <%s> : %i", name,GetLastError());
			RAISE_EXN(buf);
		}
		
		{
			Handle res = vol_alloc_with_c_space(sizeof(void*));
			*(void**)DEREFVOL(UNHANDLE(res)) = sym;
			return res;
		}
	}
	
#else /* UNIX version */
	{
		void *sym = dlsym( *(void**)DEREFVOL(DEREFHANDLE(h)->Get(0)), name );
		
		if (!sym)
		{
			char buf[256];
			sprintf(buf, "load_sym <%s> : %s", name, dlerror());
			RAISE_EXN(buf);
		}
		
		{
			Handle res = vol_alloc_with_c_space(sizeof(void*));
			*(void**)DEREFVOL(UNHANDLE(res)) = sym;
			return res;
		}
	}
#endif
}


/**********************************************************************
 *
 *  Call a symbol with a list of conversion/argument pairs
 *   
 **********************************************************************/


typedef void*   (*ftype)(...);


#define STRUCT(n) struct {char xyz[n];}

#define MAX_STRUCT_SIZE 1024
typedef STRUCT(MAX_STRUCT_SIZE) STRUCT_MAX;

static void print_call
(
  void* a1,  void* a2,  void* a3,  void* a4,  void* a5,
  void* a6,  void* a7,  void* a8,  void* a9,  void* a10,
  void* a11, void* a12, void* a13, void* a14, void* a15,
  void* b1,  void* b2,  void* b3,  void* b4,  void* b5,
  void* b6,  void* b7,  void* b8,  void* b9,  void* b10,
  void* b11, void* b12, void* b13, void* b14, void* b15
)
{
  info (("<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p>"
         "<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p>"
         "<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p>\n", 
         a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,
         b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15));
}

/* Macro to call a function and get a particular argument type.
   Note: We only allocate the result handle once the function has
   returned.  If the C function makes a call back to ML the save
   vector will change. */

// N.B.  This has a potential nasty in Windows.  Normal Windows API calls use
// "pascal" (callee removes arguments) calling conventions but we're calling the
// function with C (caller removes arguments) conventions.  The result will be
// that the stack is reset twice.  The "space" variable is intended to make sure
// we don't overwrite anything important but it's really a mess.

// The only really satisfactory solution to this would be to compile specific
// call and return code depending on the types of the arguments and the result.

#ifdef WINDOWS_PC
#define CALL_TYPED(TYPE)    \
    do { \
        int space[20];\
        space[0] = 0;\
        in_foreign_code = 1;\
        TYPE result = ((TYPE(*)(...))fun)(a1,\
            a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,\
            b7,b8,b9,b10,b11,b12,b13,b14,b15);\
        in_foreign_code = 0;\
        Handle res = vol_alloc_with_c_space(sizeof(TYPE));\
        *(TYPE*)DEREFVOL(UNHANDLE(res)) = result;\
        return res;\
    } while (0)
#else
#define CALL_TYPED(TYPE)    \
    do { \
        in_foreign_code = 1;\
        TYPE result = ((TYPE(*)(...))fun)(a1,\
            a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,\
            b7,b8,b9,b10,b11,b12,b13,b14,b15);\
        in_foreign_code = 0;\
        Handle res = vol_alloc_with_c_space(sizeof(TYPE));\
        *(TYPE*)DEREFVOL(UNHANDLE(res)) = result;\
        return res;\
    } while (0)
#endif

/* current version - all platforms */
// TODO: For 64-bit platforms this won't necessarily work.  It all depends on the relative
// sizes of the various arguments and the size of void *.  The only safe way is probably
// to get "configure" to give us some idea of the space used on the stack to pass various
// arguments. 
static Handle apply_rec (int iter, ftype fun, PolyWord* conv,PolyWord ret_conv, PolyWord *args,
                         void *a1,void *a2,void *a3,void *a4,void *a5,
                         void *a6,void *a7,void *a8,void *a9,void *a10,
                         void *a11,void *a12,void *a13,void *a14,void *a15,
                         void *b1,void *b2,void *b3,void *b4,void *b5,
                         void *b6,void *b7,void *b8,void *b9,void *b10,
                         void *b11,void *b12,void *b13,void *b14,void *b15)
{
    mes(("iter = <%d> args = "
        "<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> ,%p>"
        "<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> ,%p>"
        "<%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> <%p> ,%p>\n",
        iter, 
        a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,
        b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15));
    
    if (iter < 0) {
        mes(("Calling foreign function\n"));
        print_call(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,
            b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15);
        
        if (!(IS_INT(ret_conv)))
        {
            /* Structs as results.  Now that I've removed structs as arguments
            I'm strongly tempted to forbid structs as results as well.  I guess
            there's more of a case for allowing structs as results because it's
            not possible to return more than one result any other way.
            DCJM 14/4/04. */
            typedef struct { int x0;  } small_struct1;
            typedef struct { int x0; int x1; } small_struct2;
            typedef struct { int x0; int x1; int x2; } small_struct3;
            typedef struct { int x0; int x1; int x2; int x3; } small_struct4;
            
            POLYSIGNED size = get_C_long(ret_conv.AsObjPtr()->Get(0));
            info(("Expecting return type Cstruct, size <%lu>\n", size));
            
            /* We have to treat small structures specially, because GCC does.
            It would be nice to insist that the library we are calling
            should be compiled with "-fpcc-struct-return" but that's
            not practical at the moment for the Siemens BDD libraries,
            which we need to access. Hopefully, these special cases
            should give us a minor performance boost too.
            SPF 31/3/1998
            */
            /* It seems that two-word structures, at least, are implemented
            using registers in Visual C++ V5 so some of these special
            cases are needed in the Windows version.  There's no harm
            in treating several other of the cases specially as well.
            DCJM 8/10/1999
            */
            
            if (size == sizeof(small_struct1)) CALL_TYPED(small_struct1);
            else if (size == sizeof(small_struct2)) CALL_TYPED(small_struct2);
            else if (size == sizeof(small_struct3)) CALL_TYPED(small_struct3);
            else if (size == sizeof(small_struct4)) CALL_TYPED(small_struct4);
            
            else if (size > (int)sizeof(STRUCT_MAX))
            {
                char buf[100];
                sprintf(buf, "Required size of return structure <%lu> is too large", size);
                raise_exception_string(EXC_foreign, buf);
            }
            else
            {
                /* We call the function saying that it will return a STRUCT_MAX.
                It's OK that the function probably returns something smaller,
                because space is allocated by the caller, not the callee.
                Then we have to copy the result into a STRUCT_MAX.
                Finally, we have to extract the part we're really interested
                in and copy that into the volatile. 
            
                  Note that
              
                (1) We copy the result twice - yeuch.
                (2) The whole scheme falls over completely if the
                callee passes the result back in registers
                rather than using the pre-allocated space.
                (3) Compiling the callee with -fpcc-struct-return
                would solve this problem (for GCC) but wouldn't
                work if the callee needs to call some other code
                that is compiled use -freg-struct-return. This
                is currently an issue for the HP version of the
                makefsm libraries.
                (4) That's why we treat small structures specially!
                
                  SPF 31/3/1998
                */
                
                Handle res = vol_alloc_with_c_space(size);
                in_foreign_code = 1;
                
                {
                    STRUCT_MAX temp = 
                        ((STRUCT_MAX(*)(...))fun)
                        (a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,
                        b1,b2,b3,b4,b5,b6,b7,b8,b9,b10,b11,b12,b13,b14,b15);
                    
                    memcpy(DEREFVOL(UNHANDLE(res)), &temp, size);
                }
                
                
                in_foreign_code = 0;
                mes(("Returning from foreign function\n"));
                return res;
            }
        }
        else {
            Ctype ctype = (Ctype)UNTAGGED(ret_conv);
            
            info(("Expecting return type <%s>\n", stringOfCtype(ctype)));
            switch (ctype) {
            case Cchar: CALL_TYPED(char);
            case Cdouble: CALL_TYPED(double);
            case Cfloat: CALL_TYPED(float);
            case Cint: CALL_TYPED(int);
            case Clong: CALL_TYPED(long);
            case Cpointer: CALL_TYPED(void*);
            case Cshort: CALL_TYPED(short);
            case Cuint: CALL_TYPED(unsigned);
            default:
                {
                    char buf[100];
                    sprintf(buf, "Unknown return convention <%s>", stringOfCtype(ctype));
                    raise_exception_string(EXC_foreign, buf);
                }
            }
        }
    }
    else { // iter == 0
        void* arg = DEREFVOL(args[iter]);
      
        if (!(IS_INT(conv[iter]))) {
           /* This code previously created a temporary struct, copied
              the argument in there and then passed the address of this
              as the argument.  Of the machines and systems I have here
              the only one to do that is Sparc/Solaris.  GCC on i386
              (Linux and FreeBSD), GCC on PPC (Mac OS X) and MS VS 6
              all pass structs by copying the words and passing them
              as actual arguments.  Because of this uncertainty I've
              decided to forbid structs as arguments and raise an
              exception here.  DCJM 14/4/04. */
            RAISE_EXN("Structs as function arguments are not supported");
        }
        else {
            Ctype ctype = (Ctype)UNTAGGED(conv[iter]);
            info(("<%s>\n", stringOfCtype(ctype)));
            switch (ctype) {
              
            case Cchar:
              return apply_rec(iter-1, fun, conv, ret_conv, args, (void*)(long)(*(char*)arg), 
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Cdouble: /* Double is two words -- pass them separately */
#ifdef X86_64
                // This won't work on X86_64.  Floating point arguments are not passed
                // on the stack.
                RAISE_EXN("Floating point arguments are not supported on 64-bit platform");
#endif
              /* This is a mess!! It all depends on the relative size of double and void * */
                return apply_rec(iter-1, fun, conv, ret_conv, args, (void*)((void**)arg)[0], (void*)((void**)arg)[1] ,  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13);
              
            case Cfloat:
#ifdef X86_64
                RAISE_EXN("Floating point arguments are not supported on 64-bit platform");
#endif
              return apply_rec(iter-1, fun, conv, ret_conv, args, *(void**)arg,  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Cint:
              return apply_rec(iter-1, fun, conv, ret_conv, args, *(void**)arg,  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Clong:
              return apply_rec(iter-1, fun, conv, ret_conv, args, (void*)(*(long*)arg),  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Cpointer:
              return apply_rec(iter-1, fun, conv, ret_conv, args, *(void**)arg,  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Cshort:
              return apply_rec(iter-1, fun, conv, ret_conv, args, (void*)(long)(*(short*)arg),  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            case Cuint:
              return apply_rec(iter-1, fun, conv, ret_conv, args, (void*)(*(unsigned*)arg),  
                  a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11,a12,a13,a14,a15,b1,b2,b3,b4,b5,b6,
                  b7,b8,b9,b10,b11,b12,b13,b14);
              
            default:
                {
                  char buf[100];
                  sprintf(buf, "Unknown calling convention <%s>", stringOfCtype(ctype));
                  raise_exception_string(EXC_foreign, buf);
                }
            }
        }
    }
  
    /*NOTREACHED*/
    /* Keep -Wall happy */ return (Handle)0;
}


static POLYUNSIGNED length_list (PolyWord p)
{
  TRACE; {
      return ML_Cons_Cell::IsNull(p) ? 0 : 1 + length_list (Tail(p));
  }
}

static Handle call_sym (Handle symH, Handle argsH, Handle retCtypeH)
{
    TRACE;
    ftype sym               = *(ftype*)DEREFVOL(symH->Word());
    PolyWord arg_list       = argsH->Word();
    POLYUNSIGNED num_args   = length_list(arg_list);
    PolyWord ret_conv       = retCtypeH->Word();
    
    
    if (num_args > 15) /* was 9, but increased for WNT calls. SPF 3/5/95 */
        RAISE_EXN("Too many args\n");
    
    PolyWord* arg_tuple = (PolyWord*)alloca(num_args * sizeof(PolyWord));
    PolyWord* conv      = (PolyWord*)alloca(num_args * sizeof(PolyWord));
    
    // The argument list is a list of pairs.
    PolyWord p = arg_list;
    for (POLYUNSIGNED i=0; i<num_args; i++,p=Tail(p))
    {
        conv[i]      = Head(p).AsObjPtr()->Get(0);
        arg_tuple[i] = Head(p).AsObjPtr()->Get(1);
    }
    
    Handle res = 
        apply_rec (num_args-1, sym, conv, ret_conv, arg_tuple, 
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
    
    return res;
}


/**********************************************************************
 *
 *  Char Conversion
 *   
 **********************************************************************/

/* This is backwards compatible with the old code in which toCchar and fromCchar
   worked on "string" rather than "char".  Since single character strings and
   chars have the same representation there's no need to change this.
   DCJM 27/6/01. */
static Handle toCchar (Handle h)
{
  char s[2];
  Poly_string_to_C(DEREFWORD(h),s,2);
  mes(("<%c>\n", s[0]));
  {
    Handle res = vol_alloc_with_c_space(sizeof(char));
    *(char*)DEREFVOL(UNHANDLE(res)) = s[0];
    return res;
  }
}

static Handle fromCchar (Handle h)
{
  char c;
  c = *(char*)DEREFVOL(UNHANDLE(h));
  mes(("<%c>\n", c));
  return SAVE(Buffer_to_Poly(&c,1));
}


/**********************************************************************
 *
 *  Double Conversion
 *   
 **********************************************************************/

static Handle toCdouble (Handle h)
{
  double d = real_arg(h);
  mes(("<%f>\n", d));
  {
    Handle res = vol_alloc_with_c_space(sizeof(double));
    *(double*)DEREFVOL(UNHANDLE(res)) = d;
    return res;
  }
}

static Handle fromCdouble (Handle h)
{
  double d = *(double*)DEREFVOL(UNHANDLE(h));
  mes(("<%f>\n", d));
  return real_result(d);
}


/**********************************************************************
 *
 *  Float Conversion
 *   
 **********************************************************************/

static Handle toCfloat (Handle h)
{
  float f = (float)real_arg(h);
  mes(("<%f>\n", f));
  {
    Handle res = vol_alloc_with_c_space(sizeof(float));
    *(float*)DEREFVOL(UNHANDLE(res)) = f;
    return res;
  }
}

static Handle fromCfloat (Handle h)
{
  float f = *(float*)DEREFVOL(UNHANDLE(h));
  mes(("<%f>\n", f));
  return real_result((double)f);
}


/**********************************************************************
 *
 *  Int Conversion
 *   
 **********************************************************************/

static Handle toCint (Handle h)
{
    int i = get_C_long(UNHANDLE(h));
    mes(("value = %d\n", i));
    {
        Handle res = vol_alloc_with_c_space(sizeof(int));
        *(int*)DEREFVOL(UNHANDLE(res)) = i;
        return res;
    }
}

static Handle fromCint (Handle h)
{
    int i = *(int*)DEREFVOL(UNHANDLE(h));
    mes(("<%d>\n", i));
    return Make_arbitrary_precision(i);
}


/**********************************************************************
 *
 *  Long Conversion
 *   
 **********************************************************************/

static Handle toClong (Handle h)
{
    long i = get_C_long(UNHANDLE(h));
    mes(("value = %d\n", (int)i));
    {
        Handle res = vol_alloc_with_c_space(sizeof(long));
        *(long*)DEREFVOL(UNHANDLE(res)) = i;
        return res;
    }
}

static Handle fromClong (Handle h)
{
    long i = *(long*)DEREFVOL(UNHANDLE(h));
    mes(("<%d>\n", (int)i));
    return Make_arbitrary_precision(i);
}


/**********************************************************************
 *
 *  Short Conversion
 *   
 **********************************************************************/

static Handle toCshort (Handle h)
{
  short i = (short)get_C_long(UNHANDLE(h));
  mes(("<%d>\n", (int)i));
  {
    Handle res = vol_alloc_with_c_space(sizeof(short));
    *(short*)DEREFVOL(UNHANDLE(res)) = i;
    return res;
  }
}

static Handle fromCshort (Handle h)
{
  short i = *(short*)DEREFVOL(UNHANDLE(h));
  mes(("<%d>\n", (int)i));
  return Make_arbitrary_precision(i);
}


/**********************************************************************
 *
 *  Unsigned int Conversion
 *   
 **********************************************************************/

static Handle toCuint (Handle h)
{
  unsigned i = get_C_ulong(UNHANDLE(h));
  mes(("value = %d\n", (int)i));
  {
    Handle res = vol_alloc_with_c_space(sizeof(unsigned));
    *(unsigned*)DEREFVOL(UNHANDLE(res)) = i;
    return res;
  }
}

static Handle fromCuint (Handle h)
{
  unsigned i = *(unsigned*)DEREFVOL(UNHANDLE(h));
  mes(("<%d>\n", (int)i));
  return Make_unsigned(i);
}

/**********************************************************************
 *
 *  String Conversion
 *   
 **********************************************************************/

#define PSTRING_LENGTH(pstr) \
    (IS_INT((pstr)) ? 1 : (pstr)->length)

static Handle fillCstring (Handle h)
{ TRACE; {
    POLYUNSIGNED size;
    PolyWord str = DEREFHANDLE(h)->Get(1);
    PolyVolData* cArg = (PolyVolData*)(DEREFHANDLE(h)->Get(0).AsObjPtr());

    if (str.IsTagged()) size = 1;
    else size = ((PolyStringObject*)str.AsObjPtr())->length;
    size += 1; // For the terminating zero

    Poly_string_to_C(str, (char*)DEREFVOL(cArg), size);
    mes(("<%s>\n", (char*)C_POINTER(cArg)));
    return h; /* to be ignored */
}}


static Handle toCstring (Handle h)
{ TRACE; {
    POLYUNSIGNED size;
    {
        PolyWord str = DEREFWORD(h);
        if (str.IsTagged()) size = 1;
        else size = ((PolyStringObject*)str.AsObjPtr())->length;
    }
    size += 1; // For the terminating zero
    
    /* Allocate c-space for both the string & a pointer to the string,
    which is owned by the same vol. */
    Handle res = vol_alloc_with_c_space(sizeof(char*)+size);
    
    /* Make the first word of the c-space point to the second word */
    *(void**)C_POINTER(UNVOLHANDLE(res)) = 1 + (void**)C_POINTER(UNVOLHANDLE(res));
    
    /* Copy the string into the c-space starting at the second word */
    Poly_string_to_C(DEREFWORD(h), (char*)(1 + (void**)C_POINTER(UNVOLHANDLE(res))), size);
    return res;
}}

static Handle fromCstring (Handle h)
{ TRACE; {
    char* str = *(char**)DEREFVOL(UNHANDLE(h));
    mes(("<%s>\n", str));
    return SAVE(C_string_to_Poly(str));
}}

/* Byte vector functions.  The representation is the same as a string but
   the values may include zero bytes.  For fromCbytes the length
   therefore has to be passed as an argument.   Added DCJM 29/6/01. */
static Handle toCbytes (Handle h)
{ TRACE; {
    POLYUNSIGNED size;
    {
        PolyWord str = DEREFWORD(h);
        if (str.IsTagged()) size = 1;
        else size = ((PolyStringObject*)str.AsObjPtr())->length;
    }
    // No terminating null here unlike strings
    
    /* Allocate c-space for both the string & a pointer to the string,
       which is owned by the same vol. */
    Handle res = vol_alloc_with_c_space(sizeof(char*)+size);
    char  **p = (char**)C_POINTER(UNVOLHANDLE(res));
    
    /* Make the first word of the c-space point to the second word */
    *(char***)p = p + 1;
    
    /* Copy the string into the c-space starting at the second word */
    if (size == 1) **p = (char)UNTAGGED(DEREFHANDLE(h));
    else memcpy(*p, ((PolyStringObject*)h->WordP())->chars, size);
    
    return res;
}}

static Handle fromCbytes (Handle h)
{ TRACE; {
    char* str = *(char**)DEREFVOL(DEREFHANDLE(h)->Get(0));
    int size = get_C_long(DEREFHANDLE(h)->Get(1));
    if (str == NULL) return SAVE(EmptyString());
    else return SAVE(Buffer_to_Poly(str, size));
}}


/**********************************************************************
 *
 *  call_sym_and_convert
 *   
 **********************************************************************/

typedef enum {
    directedArg_In = 1,
    directedArg_Out = 2
} DirectedArgs;

/*
    datatype 'a union =
        Char    of string
      | Double  of real
      | Float   of real
      | Int     of int
      | Long    of int
      | Short   of int
      | String  of string
      | Vol     of 'a
*/
typedef enum {
    union_Char = 1,
    union_Double,
    union_Float,
    union_Int,
    union_Long,
    union_Short,
    union_String,
    union_Vol,
    union_Uint /* 9 */
} UnionTypes;


static Handle UNION_MAKE(UnionTypes tag, Handle contents)
{
    Handle res = SAVE(alloc(2));
    UNHANDLE(res)->Set(1, TAGGED(tag));
    DEREFHANDLE(res)->Set(0, UNHANDLE(contents));
    return res;
}

/*
    datatype 'ctype unionChoice =
        chooseChar
      | chooseDouble
      | chooseFloat
      | chooseInt
      | chooseLong
      | chooseShort
      | chooseString;
      | chooseVol of 'ctype
*/
typedef enum {
    choice_chooseChar = 1,
    choice_chooseDouble,
    choice_chooseFloat,
    choice_chooseInt,
    choice_chooseLong,
    choice_chooseShort,
    choice_chooseString,
    choice_chooseVol, /* not used, since boxed value is untagged */
    choice_chooseUint /* 9 */
} ChoiceTypes;

#define TAG(x)          (UNHANDLE(x)->Get(1))
#define CONTENTS(x)     (SAVE(UNHANDLE(x)->Get(0)))


#define LIST_ISNULL(x)  (ML_Cons_Cell::IsNull(UNHANDLE(x)))
#define LIST_HEAD(x)    (SAVE(Head(DEREFWORD(x))))
#define LIST_TAIL(x)    (SAVE(Tail(DEREFWORD(x))))
#define LIST_NULL       (SAVE(ListNull))

static Handle LIST_CONS (Handle x,Handle xs)
{
    Handle res = SAVE(alloc(sizeof(ML_Cons_Cell)));
    Head(DEREFWORD(res)) = UNHANDLE(x);
    Tail(DEREFWORD(res)) = DEREFWORD(xs);
    return res;
}

#define TUPLE_GET1(x)  (SAVE(DEREFHANDLE(x)->Get(0)))
#define TUPLE_GET2(x)  (SAVE(DEREFHANDLE(x)->Get(1)))
#define TUPLE_GET3(x)  (SAVE(DEREFHANDLE(x)->Get(2)))

static Handle TUPLE_MAKE2 (Handle x,Handle y)
{
    Handle res = SAVE(alloc(2));
    DEREFHANDLE(res)->Set(0, DEREFWORD(x));
    DEREFHANDLE(res)->Set(1, DEREFWORD(y));
    return res;
}

/* For testing...
static Handle print_ctype_and_vol (Handle pair)
{

  word* ctype = UNHANDLE(TUPLE_GET1(pair));
  Handle vol  = TUPLE_GET2(pair);
  void* thing = DEREFVOL(UNHANDLE(vol));

  switch ((Ctype)UNTAGGED(ctype)) {
  case Cchar    : printf("Cchar <%c>\n",         *(char*)thing);     break;
  case Cdouble  : printf("Cdouble <%f>\n",       *(double*)thing);   break;
  case Cfloat   : printf("Cfloat <%f>\n",        *(float*)thing);    break;
  case Cint     : printf("Cint <%d>\n",          *(int*)thing);      break;
  case Clong    : printf("Clong <%ld>\n",        *(long*)thing);     break;
  case Cpointer : printf("Cpointer <%.3s>...\n", *(char**)thing);    break;
  case Cshort   : printf("Cshort <%d>\n",        *(short*)thing);    break;
  default       : {
    show(("Must be a Cstruct <%d>\n", get_C_long(ctype)));
  }}
  return pair;
}
...*/


static Handle union2vol_and_ctype (Handle u)
{
    Handle contents = CONTENTS(u);
    switch (UNTAGGED(TAG(u))) {
    case union_Char:      return TUPLE_MAKE2(SAVE(TAGGED(Cchar)),    toCchar(contents));
    case union_Double:    return TUPLE_MAKE2(SAVE(TAGGED(Cdouble)),  toCdouble(contents));
    case union_Float:     return TUPLE_MAKE2(SAVE(TAGGED(Cfloat)),   toCfloat(contents));
    case union_Int:       return TUPLE_MAKE2(SAVE(TAGGED(Cint)),     toCint(contents));
    case union_Long:      return TUPLE_MAKE2(SAVE(TAGGED(Clong)),    toClong(contents));
    case union_Short:     return TUPLE_MAKE2(SAVE(TAGGED(Cshort)),   toCshort(contents));
    case union_String:    return TUPLE_MAKE2(SAVE(TAGGED(Cpointer)), toCstring(contents));
    case union_Uint:      return TUPLE_MAKE2(SAVE(TAGGED(Cuint)),    toCuint(contents));
    case union_Vol:       return contents;
        
    default:              RAISE_EXN ("Unknown union tag");
    }
    /*NOTREACHED*/
	return 0;
}


static Handle choice2ctype (Handle choice)
{
    PolyWord either_tag_or_pointer = DEREFWORDHANDLE(choice);

    if (IS_INT(either_tag_or_pointer)) {
        switch (UNTAGGED(either_tag_or_pointer)) {
        case choice_chooseChar:       return SAVE(TAGGED(Cchar));
        case choice_chooseDouble:     return SAVE(TAGGED(Cdouble));
        case choice_chooseFloat:      return SAVE(TAGGED(Cfloat));
        case choice_chooseInt:        return SAVE(TAGGED(Cint));
        case choice_chooseLong:       return SAVE(TAGGED(Clong));
        case choice_chooseShort:      return SAVE(TAGGED(Cshort));
        case choice_chooseString:     return SAVE(TAGGED(Cpointer));
        case choice_chooseUint:       return SAVE(TAGGED(Cuint));
        default:                      RAISE_EXN ("Unknown choice type"); return 0;
        }
    }
    else /* It's a pointer */ return SAVE(either_tag_or_pointer.AsObjPtr()->Get(0));
}

  
static  Handle choice_and_vol2union (Handle pair)
{
    Handle choice = TUPLE_GET1(pair);
    Handle vol = TUPLE_GET2(pair);
    PolyWord maybe_tag = DEREFWORDHANDLE(choice);
    if (IS_INT(maybe_tag)) {
        switch (UNTAGGED(maybe_tag)) {
        case choice_chooseChar:       return UNION_MAKE(union_Char,   fromCchar(vol));
        case choice_chooseDouble:     return UNION_MAKE(union_Double, fromCdouble(vol));
        case choice_chooseFloat:      return UNION_MAKE(union_Float,  fromCfloat(vol));
        case choice_chooseInt:        return UNION_MAKE(union_Int,    fromCint(vol));
        case choice_chooseLong:       return UNION_MAKE(union_Long,   fromClong(vol));
        case choice_chooseShort:      return UNION_MAKE(union_Short,  fromCshort(vol));
        case choice_chooseString:     return UNION_MAKE(union_String, fromCstring(vol));
        case choice_chooseUint:       return UNION_MAKE(union_Uint,   fromCuint(vol));
        default:                      RAISE_EXN ("Unknown choice type"); return 0;
        }
    }
    else  /* It's a pointer */ return UNION_MAKE(union_Vol,    vol);
}

  
/*
DCJM 7/4/04.  Based on reading the code, it seems as though the arguments to a function
can be either "in" or "out" parameters.  "in" parameters are passed as expected from ML
to C whereas "out" parameters have to be constructed from the type information supplied
and their values returned along with the result.
*/
static void mkArgs (Handle xs, Handle &args, Handle &rets)
{
    TRACE;
    if (LIST_ISNULL(xs))
    {
        args = LIST_NULL;
        rets = LIST_NULL;
    }
    else
    {
        Handle x    = LIST_HEAD(xs);
        mkArgs(LIST_TAIL(xs), args, rets);
        
        switch (UNTAGGED(TAG(x))) {
        case directedArg_In:
            {
                // IN parameter - We have the type information and the value to pass.
                // Convert the value into a "vol".
                Handle vol_and_ctype = union2vol_and_ctype(CONTENTS(x));
                args = LIST_CONS(vol_and_ctype,args);
                return;
            }
        case directedArg_Out:
            {
                // OUT parameter - We have just the type information and have to construct
                // a "vol" to pass as the actual argument.
                Handle choice = CONTENTS(x);
                Handle ctype = choice2ctype(choice);
                Handle space = allocate(c_sizeof(ctype));
                Handle arg = TUPLE_MAKE2(SAVE(TAGGED(Cpointer)), address(space));
                Handle ret = TUPLE_MAKE2(choice,space);
            
                args = LIST_CONS(arg,args);
                rets = LIST_CONS(ret,rets);
                return;
            }
            
        default: RAISE_EXN ("Unknown directedArg tag\n");
        }
    }
    /*NOTREACHED*/
}

typedef Handle (*Handle2Handle)(Handle);

static Handle map (Handle2Handle f, Handle xs)
{
  return
    LIST_ISNULL(xs) ? LIST_NULL
                    : LIST_CONS( f(LIST_HEAD(xs)), map(f,LIST_TAIL(xs)) );
}
/*
DCJM 7/4/04.  For reading the code it seems as though call_sym_and_convert takes three
arguments: sym, unionArgs and retChoice and returns a pair (x, y) as the result.
sym is simply the symbol to call.
unionArgs is a list of arguments to the function.  An argument can either be an "in" parameter
consisting of type info and a value or an "out" parameter in which case only the type info
is supplied.
retChoice is the type of the result.
The result of this function is a pair consisting of the value and a list of the "out" parameters.

The reason for using call_sym_and_convert rather than call_sym is that call_sym_and_convert takes
the function arguments as a list of a union of the ML values and converts them to the "vols" with
the C values as part of the call.  This avoids the multiple calls between ML and the RTS that
would be needed if each argument was converted separately, something that is needed for call_sym.
*/
static Handle call_sym_and_convert (Handle triple)
{
    TRACE;

    Handle sym       = TUPLE_GET1(triple);
    Handle unionArgs = TUPLE_GET2(triple);
    Handle retChoice = TUPLE_GET3(triple);
    
    Handle args, rets;
    mkArgs(unionArgs, args, rets);
    Handle retCtype  = choice2ctype(retChoice);
    
    // If call_sym results in a callback the save vector will be reset and
    // all these handles will be overwritten.  We have to save them on a
    // separate stack.  In addition it's essential to keep a reference to
    // the "rets" otherwise they could be garbage collected away.
    if (recursiveCallStackPtr+2 >= RECURSIVECALLSTACKSIZE)
        RAISE_EXN ("Too many recursive calls to callback functions\n");

    recursiveCallStack[recursiveCallStackPtr++] = retChoice->WordP();
    recursiveCallStack[recursiveCallStackPtr++] = rets->WordP();
   
    /*map(print_ctype_and_vol,args);*/
    Handle vol = call_sym(sym,args,retCtype);

    // Pop the old values and put them back on the save vector
    rets = SAVE(recursiveCallStack[--recursiveCallStackPtr]);
    retChoice = SAVE(recursiveCallStack[--recursiveCallStackPtr]);
    
    return TUPLE_MAKE2 (choice_and_vol2union(TUPLE_MAKE2(retChoice,vol)),
              map(choice_and_vol2union,rets));
}

/*
DCJM 7/4/04.  This function creates a vol containing a pointer to a function that can
be used as a callback.  
*/

/* When calling using the Pascal calling convention the called code has to pop the arguments. */
// TODO: This is actually i386-specific.  We should move this into x86_dep.cpp
static int computeArgSpace(Handle argTypeList)
{
    PolyWord argPtr = DEREFWORD(argTypeList);
    int sum = 0;
    while (! ML_Cons_Cell::IsNull(argPtr)) {
        ML_Cons_Cell *argP = (ML_Cons_Cell *)argPtr.AsObjPtr();
        PolyWord argType = argP->h;
        if (! IS_INT(argType)) {
            /* It's a structure.  Don't allow it at least at the moment. */
            RAISE_EXN ("Structure arguments to callbacks are not supported\n");
        }
        else {
            Ctype ctype = (Ctype)UNTAGGED(argType);
            int nSize = 0;
            /* char and short args are always converted to ints. */
            switch (ctype) {
            case Cchar: case Cshort: case Cint: nSize = sizeof(int); break;
            case Cdouble: nSize = sizeof(double); break;
            case Cfloat: nSize = sizeof(float); break;
            case Clong: nSize = sizeof(long); break;
            case Cpointer: nSize = sizeof(void*); break;
            case Cuint: nSize = sizeof(unsigned); break;
            case Cstruct: break; /* To avoid a warning */
            }
            sum += nSize;
            argPtr = argP->t;
        }
    }
    return sum;
}

static Handle createCallbackFunction(Handle triple, bool isPascal, Handle argTypeList)
{
    TRACE;
    int nArgSpace;
    if (isPascal) nArgSpace = computeArgSpace(argTypeList);
    else nArgSpace = 0;

    Handle cArgTypeList = TUPLE_GET1(triple);
    Handle cResultType = TUPLE_GET2(triple);
    Handle mlFunction = TUPLE_GET3(triple);
    // Make a new entry in the callback table.
    struct _cbStructEntry *newTable =
        (struct _cbStructEntry*)realloc(callbackTable, (callBackEntries+1)*sizeof(struct _cbStructEntry));
    if (newTable == 0)
        RAISE_EXN("Unable to allocate memory for callback table");
    callbackTable = newTable;
    callbackTable[callBackEntries].argType = UNHANDLE(cArgTypeList);
    callbackTable[callBackEntries].mlFunction = UNHANDLE(mlFunction);
    callbackTable[callBackEntries].cFunction = machineDependent->BuildCallback(callBackEntries, cResultType, nArgSpace);
    if (callbackTable[callBackEntries].cFunction == 0)
            RAISE_EXN("Callback functions are currently only implemented for the i386");
    /* Construct a "vol" containing the pointer to the C function. */
    Handle res = vol_alloc_with_c_space(sizeof(void*));
    *(unsigned char **)C_POINTER(UNVOLHANDLE(res)) = callbackTable[callBackEntries].cFunction;
    callBackEntries++;
    return res;
}

/* Create a callback using C calling conventions.  The calling function removes the
   arguments from the stack. */
static Handle toCfunction (Handle triple)
{
    return createCallbackFunction(triple, false, TUPLE_GET1(triple));
}

/* Create a callback using Pascal/WINAPI/CALLBACK/__stdcall calling conventions.
   The CALLED function must remove the arguments from the stack before returning. */
static Handle toPascalfunction (Handle triple)
{
    return createCallbackFunction(triple, true, TUPLE_GET1(triple));
}

/* Create the ML argument list from the C arguments. */
static Handle buildArgList(Handle argTypeList, void ** argPtr)
{
    TRACE; {
        if (LIST_ISNULL(argTypeList)) return argTypeList; /* Handle pointing to NULL. */
        else {
            Handle argType      = LIST_HEAD(argTypeList);
            Handle argValue;
            if (! IS_INT(DEREFWORD(argType))) {
                /* It's a structure.  This is a mess.  GCC apparently passes the address of the
                   structure here whereas MS C passes the structure by value. */
                /* I don't think an exception will work here. */
                RAISE_EXN ("Structure arguments to callbacks are not supported\n");
            }
            else {
                Ctype ctype = (Ctype)(UNTAGGED(UNHANDLE(argType)));
                int nSize = 0;
                trace(("<%s>\n", stringOfCtype(ctype)));
                /* Extract the appropriately typed argument and create a vol with the value.
                   This means that the process of extracting the arguments actually involves two
                   steps: this step where we create the vol list and a second step which uses the
                   fromXX to extract the actual ML arguments. */
                switch (ctype) {
                case Cchar: nSize = sizeof(char); break;
                case Cdouble: nSize = sizeof(double); break;
                case Cfloat: nSize = sizeof(float); break;
                case Cint: nSize = sizeof(int); break;
                case Clong: nSize = sizeof(long); break;
                case Cpointer: nSize = sizeof(void*); break;
                case Cshort: nSize = sizeof(short); break;
                case Cuint: nSize = sizeof(unsigned); break;
                case Cstruct: break; /* To avoid a warning */
                }
                argValue = vol_alloc_with_c_space(nSize);
                machineDependent->GetCallbackArg(argPtr, DEREFVOL(UNHANDLE(argValue)), nSize);
            }
            return LIST_CONS(argValue, buildArgList(LIST_TAIL(argTypeList), argPtr));
        }
    }
}

void *CCallbackFunction(unsigned cbNo, void **args)
{
    ASSERT(cbNo >= 0 && cbNo < callBackEntries);
    if (callbackTable[cbNo].mlFunction == NULL) {
        /* The entry has never been set or more likely it's been GCed away. */
        Crash("Attempt to a callback to an ML function that no longer exists.");
    }
    Handle h = SAVE(callbackTable[cbNo].mlFunction);

    /* Set up the ML arguments from the C arguments. */
    /* We use the argType list to process the function arguments and build up
       a list of the actual arguments. */
    Handle mlArgs = buildArgList(SAVE(callbackTable[cbNo].argType), args);
    // Callbacks previously involved forking a new ML process.  They are
    // now handled on the caller's stack.
    machineDependent->SetCallbackFunction(h, mlArgs);

    Handle resultHandle = EnterPolyCode();

    /* Return the address of the vol.  The stub function then has to extract the
       appropriate result depending on how it was compiled. */
    return DEREFVOL(UNHANDLE(resultHandle));
}

/**********************************************************************
 *
 *  Foreign Dispatch
 *   
 **********************************************************************/

typedef Handle(* type_hh_fun)(Handle);

static type_hh_fun handlers[] =
{
  get_foreign_debug,
  set_foreign_debug,

  load_lib,
  load_sym,
  call_sym_and_convert,

  allocate,
  address,
  deref,
  offset,
  assign,
  c_sizeof,
  alignment,
  
  toCchar,
  fromCchar,

  toCdouble,
  fromCdouble,

  toCfloat,
  fromCfloat,

  toCint,
  fromCint,

  toClong,
  fromClong,

  toCshort,
  fromCshort,

  fillCstring,
  toCstring,
  fromCstring,

  toCuint,      /* Added DCJM 17/5/01. */
  fromCuint,        /* Added DCJM 17/5/01. */

  toCbytes,     /* Added DCJM 29/6/01. */
  fromCbytes,

  toCfunction,      /* Added DCJM 7/4/04. */
  toPascalfunction /* Added DCJM 7/4/04. */
};
    
#define NUM_HANDLERS ((int)(sizeof(handlers)/sizeof(type_hh_fun)))


Handle foreign_dispatch_c (Handle args, Handle fcode_h)
{
    int fcode = get_C_long(DEREFWORD(fcode_h));
    
    if (fcode < 0 || fcode >= NUM_HANDLERS) {
        char buf[100];
        sprintf(buf, "Unknown foreign dispatch code <%d>", fcode);
        RAISE_EXN(buf);
    }
    
    mes(("dispatch code = %d\n", fcode));
    /* dispatch to desired function */
    return (handlers[fcode])(args);
}

#else
// The foreign function interface isn't available.
#include "foreign.h"
#include "run_time.h"
#include "sys.h"

int in_foreign_code = 0;

Handle foreign_dispatch_c (Handle args, Handle fcode_h)
{
    raise_exception_string(EXC_foreign, "The foreign function interface is not available on this platform");
}

#endif
