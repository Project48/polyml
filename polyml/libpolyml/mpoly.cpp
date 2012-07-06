/*
    Title:      Main program

    Copyright (c) 2000
        Cambridge University Technical Services Limited

    Further development copyright David C.J. Matthews 2001-12

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

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) 0
#endif

#ifdef HAVE_TCHAR_H
#include <tchar.h>
#endif

#include "globals.h"
#include "sys.h"
#include "gc.h"
#include "heapsizing.h"
#include "run_time.h"
#include "machine_dep.h"
#include "version.h"
#include "diagnostics.h"
#include "processes.h"
#include "mpoly.h"
#include "scanaddrs.h"
#include "save_vec.h"
#include "../polyexports.h"
#include "memmgr.h"
#include "pexport.h"

#if (defined(_WIN32) && ! defined(__CYGWIN__))
#include "Console.h"
#endif

static void  InitHeaderFromExport(exportDescription *exports);
NORETURNFN(static void Usage(const char *message, ...));

// Return the entry in the io vector corresponding to the Poly system call.
PolyWord *IoEntry(unsigned sysOp)
{
    MemSpace *space = gMem.IoSpace();
    return space->bottom + sysOp * IO_SPACING;
}

struct _userOptions userOptions;

time_t exportTimeStamp;

enum {
    OPT_HEAPMIN,
    OPT_HEAPMAX,
    OPT_GCPERCENT,
    OPT_RESERVE,
    OPT_GCTHREADS,
    OPT_DEBUGOPTS,
    OPT_DEBUGFILE
};

static struct __argtab {
    const char *argName, *argHelp;
    unsigned argKey;
} argTable[] =
{
    { "-H",             "Minimum heap size (MB)",                               OPT_HEAPMIN },
    { "--minheap",      "Minimum heap size (MB)",                               OPT_HEAPMIN },
    { "--maxheap",      "Maximum heap size (MB)",                               OPT_HEAPMAX },
    { "--gcpercent",    "Target percentage time in GC (1-99)",                  OPT_GCPERCENT },
    { "--stackspace",   "Space to reserve for thread stacks and C++ heap(MB)",  OPT_RESERVE },
    { "--gcthreads",    "Number of threads to use for garbage collection",      OPT_GCTHREADS },
    { "--debug",        "Debug options: checkmem, gc, x",                       OPT_DEBUGOPTS },
    { "--logfile",      "Logging file (default is to log to stdout)",           OPT_DEBUGFILE }
};

static struct __debugOpts {
    const char *optName, *optHelp;
    unsigned optKey;
} debugOptTable[] =
{
    { "checkmem",           "Perform additional debugging checks on memory",    DEBUG_CHECK_OBJECTS },
    { "gc",                 "Log summary garbage-collector information",        DEBUG_GC },
    { "gcdetail",           "Log detailed garbage-collector information",       DEBUG_GC_DETAIL },
    { "memmgr",             "Memory manager information",                       DEBUG_MEMMGR },
    { "threads",            "Thread related information",                       DEBUG_THREADS },
    { "gctasks",            "Log multi-thread GC information",                  DEBUG_GCTASKS },
    { "heapsize",           "Log heap resizing data",                           DEBUG_HEAPSIZE },
    { "x",                  "Log X-windows information",                        DEBUG_X},
    { "sharing",            "Information from PolyML.shareCommonData",          DEBUG_SHARING},
    { "locks",              "Information about contended locks",                DEBUG_CONTENTION},
    { "rts",                "General run-time system calls",                    DEBUG_RTSCALLS}
};

// Parse a parameter that is meant to be a size.  Returns the value as a number
// of kilobytes.
unsigned parseSize(const char *p, const char *arg)
{
    unsigned result = 0;
    if (*p < '0' || *p > '9')
        // There must be at least one digit
        Usage("Incomplete %s option\n", arg);
    while (true)
    {
        result = result*10 + *p++ - '0';
        if (*p == 0)
        {
            // The default is megabytes
            result *= 1024;
            break;
        }
        if (*p == 'G' || *p == 'g')
        {
            result *= 1024 * 1024;
            p++;
            break;
        }
        if (*p == 'M' || *p == 'm')
        {
            result *= 1024;
            p++;
            break;
        }
        if (*p == 'K' || *p == 'k')
        {
            p++;
            break;
        }
        if (*p < '0' || *p > '9')
            break;
    }
    if (*p != 0)
        Usage("Malformed %s option\n", arg);
    return result;
}

/* In the Windows version this is called from WinMain in Console.c */
int polymain(int argc, char **argv, exportDescription *exports)
{
    unsigned minsize=0, maxsize=0, gcpercent=0;
    /* Get arguments. */
    memset(&userOptions, 0, sizeof(userOptions)); /* Reset it */
    userOptions.gcthreads = 0; // Default multi-threaded

    // Get the program name for CommandLine.name.  This is allowed to be a full path or
    // just the last component so we return whatever the system provides.
    if (argc > 0) 
        userOptions.programName = argv[0];
    else
        userOptions.programName = ""; // Set it to a valid empty string
    
    char *importFileName = 0;
    debugOptions       = 0;

    userOptions.user_arg_count   = 0;
    userOptions.user_arg_strings = (char**)malloc(argc * sizeof(char*)); // Enough room for all of them

    // Process the argument list removing those recognised by the RTS and adding the
    // remainder to the user argument list.
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-')
        {
            bool argUsed = false;
            for (unsigned j = 0; j < sizeof(argTable)/sizeof(argTable[0]); j++)
            {
                size_t argl = strlen(argTable[j].argName);
                if (strncmp(argv[i], argTable[j].argName, argl) == 0)
                {
                    char *p, *endp;
                    if (strlen(argv[i]) == argl)
                    { // If it has used all the argument pick the next
                        i++;
                        p = argv[i];
                    }
                    else
                    {
                        p = argv[i]+argl;
                        if (*p == '=') p++; // Skip an equals sign
                    }
                    if (i >= argc)
                        Usage("Incomplete %s option\n", argTable[j].argName);
                    else switch (argTable[j].argKey)
                    {
                    case OPT_HEAPMIN:
                        minsize = parseSize(p, argTable[j].argName);
                        break;
                    case OPT_HEAPMAX:
                        maxsize = parseSize(p, argTable[j].argName);
                        break;
                    case OPT_GCPERCENT:
                        gcpercent = strtol(p, &endp, 10);
                        if (*endp != '\0') 
                            Usage("Malformed %s option\n", argTable[j].argName);
                        if (gcpercent < 1 || gcpercent > 99)
                        {
                            Usage("%s argument must be between 1 and 99\n", argTable[j].argName);
                            gcpercent = 0;
                        }
                        break;
                    case OPT_RESERVE:
                        {
                            unsigned reserve = parseSize(p, argTable[j].argName);
                            if (reserve != 0)
                                gHeapSizeParameters.SetReservation(strtol(p, &endp, 10) * 1024);
                            break;
                        }
                    case OPT_GCTHREADS:
                        userOptions.gcthreads = strtol(p, &endp, 10);
                        if (*endp != '\0') 
                            Usage("Incomplete %s option\n", argTable[j].argName);
                        break;
                    case OPT_DEBUGOPTS:
                        while (*p != '\0')
                        {
                            // Debug options are separated by commas
                            bool optFound = false;
                            char *q = strchr(p, ',');
                            if (q == NULL) q = p+strlen(p);
                            for (unsigned k = 0; k < sizeof(debugOptTable)/sizeof(debugOptTable[0]); k++)
                            {
                                if (strlen(debugOptTable[k].optName) == (size_t)(q-p) &&
                                        strncmp(p, debugOptTable[k].optName, q-p) == 0)
                                {
                                    debugOptions |= debugOptTable[k].optKey;
                                    optFound = true;
                                }
                            }
                            if (! optFound)
                                Usage("Unknown argument to --debug\n");
                            if (*q == ',') p = q+1; else p = q;
                        }
                        break;
                    case OPT_DEBUGFILE:
                        SetLogFile(p);
                        break;
                    }
                    argUsed = true;
                    break;
                }
            }
            if (! argUsed) // Add it to the user args.
                userOptions.user_arg_strings[userOptions.user_arg_count++] = argv[i];
        }
        else if (exports == 0 && importFileName == 0)
            importFileName = argv[i];
        else
            userOptions.user_arg_strings[userOptions.user_arg_count++] = argv[i];
    }

    if (exports == 0 && importFileName == 0)
        Usage("Missing import file name\n");

    if (userOptions.gcthreads == 0)
        // For the moment, at any rate, indicate that we should use as many
        // threads as there are processors by a thread count of zero.
        userOptions.gcthreads = NumberOfProcessors();

    // Set the heap size if it has been provided otherwise use the default.
    gHeapSizeParameters.SetHeapParameters(minsize, maxsize, gcpercent);
   
    // Initialise the run-time system before creating the heap.
    InitModules();
    CreateHeap();
    
    PolyObject *rootFunction = 0;

    if (exports != 0)
    {
        InitHeaderFromExport(exports);
        rootFunction = (PolyObject *)exports->rootFunction;
    }
    else
    {
        if (importFileName != 0)
            rootFunction = ImportPortable(importFileName);
        if (rootFunction == 0)
            exit(1);
    }
   
    /* Initialise the interface vector. */
    machineDependent->InitInterfaceVector(); /* machine dependent entries. */
    
    // This word has a zero value and is used for null strings.
    add_word_to_io_area(POLY_SYS_emptystring, PolyWord::FromUnsigned(0));
    
    // This is used to represent zero-sized vectors.
    // N.B. This assumes that the word before is zero because it's
    // actually the length word we want to be zero here.
    add_word_to_io_area(POLY_SYS_nullvector, PolyWord::FromUnsigned(0));
    
    /* The standard input and output streams are persistent i.e. references
       to the the standard input in one session will refer to the standard
       input in the next. */
    add_word_to_io_area(POLY_SYS_stdin,  PolyWord::FromUnsigned(0));
    add_word_to_io_area(POLY_SYS_stdout, PolyWord::FromUnsigned(1));
    add_word_to_io_area(POLY_SYS_stderr, PolyWord::FromUnsigned(2));
    
    StartModules();
    
    // Set up the initial process to run the root function.
    processes->BeginRootThread(rootFunction);
    
    finish(0);
    
    /*NOTREACHED*/
    return 0; /* just to keep lint happy */
}

void Uninitialise(void)
// Close down everything and free all resources.  Stop any threads or timers.
{
    StopModules();
}

void finish (int n)
{
    // Make sure we don't get any interrupts once the destructors are
    // applied to globals or statics.
    Uninitialise();
#if (defined(_WIN32) && ! defined(__CYGWIN__))
    ExitThread(n);
#else
    exit (n);
#endif
}

// Print a message and exit if an argument is malformed.
void Usage(const char *message, ...)
{
    va_list vl;
    printf("\n");
    va_start(vl, message);
    vprintf(message, vl);
    va_end(vl);

    for (unsigned j = 0; j < sizeof(argTable)/sizeof(argTable[0]); j++)
    {
        printf("%s <%s>\n", argTable[j].argName, argTable[j].argHelp);
    }
    printf("Debug options:\n");
    for (unsigned k = 0; k < sizeof(debugOptTable)/sizeof(debugOptTable[0]); k++)
    {
        printf("%s <%s>\n", debugOptTable[k].optName, debugOptTable[k].optHelp);
    }
    fflush(stdout);
    
#if (defined(_WIN32) && ! defined(__CYGWIN__))
    if (useConsole)
    {
        MessageBox(hMainWindow, "Poly/ML has exited", "Poly/ML", MB_OK);
    }
#endif
    exit (1);
}

// Return a string containing the argument names.  Can be printed out in response
// to a --help argument.  It is up to the ML application to do that since it may well
// want to produce information about any arguments it chooses to process.
char *RTSArgHelp(void)
{
    static char buff[2000];
    char *p = buff;
    for (unsigned j = 0; j < sizeof(argTable)/sizeof(argTable[0]); j++)
    {
        int spaces = sprintf(p, "%s <%s>\n", argTable[j].argName, argTable[j].argHelp);
        p += spaces;
    }
    {
        int spaces = sprintf(p, "Debug options:\n");
        p += spaces;
    }
    for (unsigned k = 0; k < sizeof(debugOptTable)/sizeof(debugOptTable[0]); k++)
    {
        int spaces = sprintf(p, "%s <%s>\n", debugOptTable[k].optName, debugOptTable[k].optHelp);
        p += spaces;
    }
    ASSERT((unsigned)(p - buff) < (unsigned)sizeof(buff));
    return buff;
}

void InitHeaderFromExport(exportDescription *exports)
{
    // Check the structure sizes stored in the export structure match the versions
    // used in this library.
    if (exports->structLength != sizeof(exportDescription) ||
        exports->memTableSize != sizeof(memoryTableEntry) ||
        exports->rtsVersion < FIRST_supported_version ||
        exports->rtsVersion > LAST_supported_version)
    {
#if (FIRST_supported_version == LAST_supported_version)
        Exit("The exported object file has version %0.2f but this library supports %0.2f",
            ((float)exports->rtsVersion) / 100.0,
            ((float)FIRST_supported_version) / 100.0);
#else
        Exit("The exported object file has version %0.2f but this library supports %0.2f-%0.2f",
            ((float)exports->rtsVersion) / 100.0,
            ((float)FIRST_supported_version) / 100.0,
            ((float)LAST_supported_version) / 100.0);
#endif
    }
    // We could also check the RTS version and the architecture.
    exportTimeStamp = exports->timeStamp; // Needed for load and save.

    memoryTableEntry *memTable = exports->memTable;
    for (unsigned i = 0; i < exports->memTableEntries; i++)
    {
        // Construct a new space for each of the entries.
        if (i == exports->ioIndex)
        {
            if (gMem.InitIOSpace((PolyWord*)memTable[i].mtAddr, memTable[i].mtLength/sizeof(PolyWord)) == 0)
                Exit("Unable to initialise the memory space");
        }
        else
        {
            if (gMem.NewPermanentSpace(
                    (PolyWord*)memTable[i].mtAddr,
                    memTable[i].mtLength/sizeof(PolyWord), (unsigned)memTable[i].mtFlags,
                    (unsigned)memTable[i].mtIndex) == 0)
                Exit("Unable to initialise a permanent memory space");
        }
    }
}
