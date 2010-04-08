;#
;#  Title:  Assembly code routines for the poly system.
;#  Author:    David Matthews
;#  Copyright (c) Cambridge University Technical Services Limited 2000
;#  This library is free software; you can redistribute it and/or
;#  modify it under the terms of the GNU Lesser General Public
;#  License as published by the Free Software Foundation; either
;#  version 2.1 of the License, or (at your option) any later version.
;#  
;#  This library is distributed in the hope that it will be useful,
;#  but WITHOUT ANY WARRANTY; without even the implied warranty of
;#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;#  Lesser General Public License for more details.
;#  
;#  You should have received a copy of the GNU Lesser General Public
;#  License along with this library; if not, write to the Free Software
;#  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
;#
;#
;#
;#
;# *********************************************************************
;# * IMPORTANT                                                         *
;# * This file is used directly by MASM but is also converted          *
;# * for use by gas on Unix.  For reasons best known to the respective *
;# * developers the assembly language accepted by gas is different     *
;# * from that used by MASM.  This file uses a sort of half-way house  *
;# * between the two versions in that the gas instruction ordering is  *
;# * used i.e. the first argument is the source and the second the     *
;# * destination, but the MASM format of addresses is used.            * 
;# * After making any changes to this file ensure that it can be       *
;# * successfully converted and compiled under both MASM and gas.      *
;# * DCJM January 2000                                                 *
;# *********************************************************************

;#
;# Registers used :-
;#
;#  %Reax: First argument to function.  Result of function call.
;#  %Rebx: Second argument to function.
;#  %Recx: General register
;#  %Redx: Closure pointer in call.
;#  %Rebp: Points to memory used for extra registers
;#  %Resi: General register.
;#  %Redi: General register.
;#  %Resp: Stack pointer.
;#  X86_64 Additional registers
;#  %R8:   Third argument to function
;#  %R9:   Fourth argument to function
;#  %R10:  Fifth argument to function
;#  %R11:  General register
;#  %R12:  General register
;#  %R13:  General register
;#  %R14:  General register
;#  %R15:  Memory allocation pointer

IFDEF WINDOWS
.486

    .model  flat,c

;# No name munging needed in MASM
EXTNAME     TEXTEQU <>

;# CALLMACRO is used to indicate to the converter that we have a macro
;# since macros have to be converted into C preprocessor macros.
CALLMACRO       TEXTEQU <>

IFNDEF HOSTARCHITECTURE_X86_64
Reax        TEXTEQU <eax>
Rebx        TEXTEQU <ebx>
Recx        TEXTEQU <ecx>
Redx        TEXTEQU <edx>
Resi        TEXTEQU <esi>
Redi        TEXTEQU <edi>
Resp        TEXTEQU <esp>
Rebp        TEXTEQU <ebp>
ELSE
Reax        TEXTEQU <rax>
Rebx        TEXTEQU <rbx>
Recx        TEXTEQU <rcx>
Redx        TEXTEQU <rdx>
Resi        TEXTEQU <rsi>
Redi        TEXTEQU <rdi>
Resp        TEXTEQU <rsp>
Rebp        TEXTEQU <rbp>
ENDIF
R_cl        TEXTEQU <cl>
R_bl        TEXTEQU <bl>
R_al        TEXTEQU <al>
R_ax        TEXTEQU <ax>

CONST       TEXTEQU <>

;#  gas-style instructions
;#  These are the reverse order from MASM.
MOVL        MACRO   f,t
            mov     t,f
            ENDM

MOVB        MACRO   f,t
            mov     t,f
            ENDM

ADDL        MACRO   f,t
            add     t,f
            ENDM

SUBL        MACRO   f,t
            sub     t,f
            ENDM

XORL        MACRO   f,t
            xor     t,f
            ENDM

ORL         MACRO   f,t
            or      t,f
            ENDM

ANDL        MACRO   f,t
            and     t,f
            ENDM

CMPL        MACRO   f,t
            cmp     t,f
            ENDM

CMPB        MACRO   f,t
            cmp     t,f
            ENDM

LEAL        MACRO   f,t
            lea     t,f
            ENDM

SHRL        MACRO   f,t
            shr     t,f
            ENDM

SARL        MACRO   f,t
            sar     t,f
            ENDM

SHLL        MACRO   f,t
            shl     t,f
            ENDM

TESTL       MACRO   f,t
            test    t,f
            ENDM

IMULL       MACRO   f,t
            imul    t,f
            ENDM

LOCKXADDL   MACRO   f,t
            lock xadd t,f
            ENDM

MULL         TEXTEQU <mul>

NEGL         TEXTEQU <neg>
PUSHL        TEXTEQU <push>
POPL         TEXTEQU <pop>
POPFL        TEXTEQU <popfd>
PUSHFL       TEXTEQU <pushfd>
PUSHAL       TEXTEQU <pushad>
POPAL        TEXTEQU <popad>
INCL         TEXTEQU <inc>

ELSE
#include "config.h"
;# External names in older versions of FreeBSD have a leading underscore.
#if ! defined(__ELF__)
#define EXTNAME(x)  _##x
#else
#define EXTNAME(x)  x
#endif

IFNDEF HOSTARCHITECTURE_X86_64
#define Reax        %eax
#define Rebx        %ebx
#define Recx        %ecx
#define Redx        %edx
#define Resi        %esi
#define Redi        %edi
#define Resp        %esp
#define Rebp        %ebp
ELSE
#define Reax        %rax
#define Rebx        %rbx
#define Recx        %rcx
#define Redx        %rdx
#define Resi        %rsi
#define Redi        %rdi
#define Resp        %rsp
#define Rebp        %rbp
ENDIF
#define R_al        %al
#define R_cl        %cl
#define R_bl        %bl
#define R_ax        %ax
IFDEF HOSTARCHITECTURE_X86_64
#define R8          %r8
#define R9          %r9
#define R10         %r10
#define R11         %r11
#define R12         %r12
#define R13         %r13
#define R14         %r14
#define R15         %r15
ENDIF

#define CONST       $

#define END

IFDEF HOSTARCHITECTURE_X86_64
#define MOVL         movq
#define MOVB         movb
#define ADDL         addq
#define SUBL         subq
#define XORL         xorq
#define ORL          orq
#define ANDL         andq
#define CMPL         cmpq
#define CMPB         cmpb
#define LEAL         leaq
#define SHRL         shrq
#define SARL         sarq
#define SHLL         shlq
#define TESTL        testq
#define IMULL        imulq
#define MULL         mulq
#define NEGL         negq
#define PUSHL        pushq
#define POPL         popq
#define POPFL        popfq
#define PUSHFL       pushfq
#define PUSHAL       pushaq
#define POPAL        popaq
#define LOCKXADDL    lock xaddq

ELSE
#define MOVL         movl
#define MOVB         movb
#define ADDL         addl
#define SUBL         subl
#define XORL         xorl
#define ORL          orl
#define ANDL         andl
#define CMPL         cmpl
#define CMPB         cmpb
#define LEAL         leal
#define SHRL         shrl
#define SARL         sarl
#define SHLL         shll
#define TESTL        testl
#define IMULL        imull
#define MULL         mull
#define NEGL         negl
#define PUSHL        pushl
#define POPL         popl
#define POPFL        popfl
#define PUSHFL       pushfl
#define PUSHAL       pushal
#define POPAL        popal
;# Older versions of GCC require a semicolon here.
#define LOCKXADDL    lock; xaddl

ENDIF

ENDIF


;# Register mask entries - must match coding used in I386CODECONS.ML
IFDEF WINDOWS
M_Reax      EQU     000001H
M_Recx      EQU     000002H
M_Redx      EQU     000004H
M_Rebx      EQU     000008H
M_Resi      EQU     000010H
M_Redi      EQU     000020H
M_FP0       EQU     002000H
M_FP1       EQU     004000H
M_FP2       EQU     008000H
M_FP3       EQU     010000H
M_FP4       EQU     020000H
M_FP5       EQU     040000H
M_FP6       EQU     080000H
M_FP7       EQU     100000H

Mask_all    EQU     1FFFFFH

;# Set the register mask entry
RegMask     MACRO   name,mask
Mname       TEXTEQU <Mask_&name&>
%Mname      EQU     mask
            ENDM

ELSE
;# Register mask entries - must match coding used in I386CODECONS.ML
#define     M_Reax  0x000001
#define     M_Recx  0x000002
#define     M_Redx  0x000004
#define     M_Rebx  0x000008
#define     M_Resi  0x000010
#define     M_Redi  0x000020

IFDEF HOSTARCHITECTURE_X86_64
#define     M_R8         64
#define     M_R9        128
#define     M_R10       256
#define     M_R11       512
#define     M_R12      1024
#define     M_R13      2048
#define     M_R14      4096
ENDIF
            ;# Floating point registers.
#define     M_FP0   0x002000
#define     M_FP1   0x004000
#define     M_FP2   0x008000
#define     M_FP3   0x010000
#define     M_FP4   0x020000
#define     M_FP5   0x040000
#define     M_FP6   0x080000
#define     M_FP7   0x100000

#define     Mask_all 0x1FFFFF

#define     RegMask(name,mask) \
.set        Mask_##name,    mask

#define     OR  |

ENDIF

;#
;# Macro to begin the hand-coded functions
;#
IFDEF WINDOWS
INLINE_ROUTINE  MACRO   id
PUBLIC  id
id:
ENDM

ELSE

IFDEF MACOSX
#define GLOBAL .globl
ELSE
#define GLOBAL .global
ENDIF

#define INLINE_ROUTINE(id) \
GLOBAL EXTNAME(id); \
EXTNAME(id):

ENDIF


IFDEF WINDOWS

;#
;# Tagged values.   A few operations, such as shift assume that the tag bit
;# is the bottom bit.
;#


TAG         EQU 1
TAGSHIFT    EQU 1
TAGMULT     EQU 2

TAGGED      MACRO   i
            LOCAL   t
            t   TEXTEQU <i*2+1>
            EXITM   %t
ENDM

MAKETAGGED  MACRO   f,t
            lea     t,1[f*2]
ENDM

ELSE

.set    TAG,        1
.set    TAGSHIFT,   1
.set    TAGMULT,    (1 << TAGSHIFT)

#define TAGGED(i) ((i << TAGSHIFT) | TAG)
#define MAKETAGGED(from,to)     LEAL    TAG(,from,2),to

ENDIF


IFDEF WINDOWS

NIL         TEXTEQU     TAGGED(0)
UNIT        TEXTEQU     TAGGED(0)
ZERO        TEXTEQU     TAGGED(0)
FALSE       TEXTEQU     TAGGED(0)
TRUE        TEXTEQU     TAGGED(1)
MINUS1      TEXTEQU     TAGGED(0-1)
B_bytes     EQU         01h
B_mutablebytes  EQU     41h
B_mutable   EQU         40h
IFNDEF HOSTARCHITECTURE_X86_64
Max_Length  EQU         00ffffffh
ELSE
Max_Length  EQU         00ffffffffffffffh
ENDIF

ELSE

.set    NIL,        TAGGED(0)
.set    UNIT,       TAGGED(0)
.set    ZERO,       TAGGED(0)
.set    FALSE,      TAGGED(0)
.set    TRUE,       TAGGED(1)
.set    MINUS1,     TAGGED(0-1)
.set    B_bytes,    0x01
.set    B_mutable,  0x40
.set    B_mutablebytes, 0x41
IFNDEF HOSTARCHITECTURE_X86_64
.set    Max_Length, 0x00ffffff
ELSE
.set    Max_Length, 0x00ffffffffffffff
ENDIF

ENDIF

;# The "memory registers" are pointed to by Rebp within the ML code
;# The first few offsets are built into the compiled code.
;# All the offsets are built into x86_dep.c .

IFDEF WINDOWS
LocalMpointer       EQU     0

IFNDEF HOSTARCHITECTURE_X86_64
HandlerRegister     EQU     4
LocalMbottom        EQU     8
StackTop            EQU     16  ;# Upper limit of stack
RequestCode         EQU     20  ;# Byte: Io function to call.
InRTS               EQU     21  ;# Byte: Set when in the RTS
PolyStack           EQU     24  ;# Current stack base
SavedSp             EQU     28  ;# Saved stack pointer
IOEntryPoint        EQU     48  ;# IO call
RaiseDiv            EQU     52  ;# Call to raise the Div exception
ArbEmulation		EQU		56  ;# Arbitrary precision emulation
ThreadId			EQU		60	;# My thread id
RealTemp            EQU     64 ;# Space for int-real conversions

ELSE
HandlerRegister     EQU     8
LocalMbottom        EQU     16
StackLimit          EQU     24  ;# Lower limit of stack
StackTop            EQU     32  ;# Upper limit of stack
RequestCode         EQU     40  ;# Byte: Io function to call.
InRTS               EQU     41  ;# Byte: Set when in the RTS
ReturnReason        EQU     42  ;# Byte: Reason for returning from ML. 
PolyStack           EQU     48  ;# Current stack base
SavedSp             EQU     56  ;# Saved stack pointer
HeapOverflow        EQU     64  ;# Heap overflow code
StackOverflow       EQU     72  ;# Stack overflow code
StackOverflowEx     EQU     80  ;# Stack overflow code (for EDI)
RaiseExEntry        EQU     88  ;# Raise exception
IOEntryPoint        EQU     96  ;# IO call
RaiseDiv            EQU     104  ;# Exception trace
ArbEmulation        EQU     112  ;# Arbitrary precision emulation
ThreadId			EQU		120	;# My thread id
RealTemp            EQU     128 ;# Space for int-real conversions
ENDIF

ELSE
.set    LocalMpointer,0
IFNDEF HOSTARCHITECTURE_X86_64
.set    HandlerRegister,4
.set    LocalMbottom,8
.set    StackTop,16
.set    RequestCode,20
.set    InRTS,21
.set    PolyStack,24
.set    SavedSp,28
.set    IOEntryPoint,48
.set    RaiseDiv,52
.set    ArbEmulation,56
.set    ThreadId,60
.set    RealTemp,64
ELSE
.set    HandlerRegister,8
.set    LocalMbottom,16
.set    StackLimit,24
.set    StackTop,32
.set    RequestCode,40
.set    InRTS,41
.set    PolyStack,48
.set    SavedSp,56
.set    HeapOverflow,64
.set    StackOverflow,72
.set    StackOverflowEx,80
.set    RaiseExEntry,88
.set    IOEntryPoint,96
.set    RaiseDiv,104
.set    ArbEmulation,112
.set    ThreadId,120
.set    RealTemp,128
ENDIF

ENDIF

;# IO function numbers.  These are functions that are called
;# to handle special cases in this code

IFDEF WINDOWS
POLY_SYS_alloc_store        EQU 11
POLY_SYS_give_ex_trace      EQU 31
POLY_SYS_quotrem            EQU 104
POLY_SYS_aplus              EQU 106
POLY_SYS_aminus             EQU 107
POLY_SYS_amul               EQU 108
POLY_SYS_adiv               EQU 109
POLY_SYS_amod               EQU 110
POLY_SYS_aneg               EQU 111
POLY_SYS_xora               EQU 112
POLY_SYS_equala             EQU 113
POLY_SYS_ora                EQU 114
POLY_SYS_anda               EQU 115
POLY_SYS_int_geq            EQU 231
POLY_SYS_int_leq            EQU 232
POLY_SYS_int_gtr            EQU 233
POLY_SYS_int_lss            EQU 234
POLY_SYS_Add_real           EQU 125
POLY_SYS_Sub_real           EQU 126
POLY_SYS_Mul_real           EQU 127
POLY_SYS_Div_real           EQU 128
POLY_SYS_Neg_real           EQU 130
POLY_SYS_real_to_int        EQU 134
POLY_SYS_int_to_real        EQU 135
POLY_SYS_sqrt_real          EQU 136
POLY_SYS_sin_real           EQU 137
POLY_SYS_cos_real           EQU 138
POLY_SYS_arctan_real        EQU 139
POLY_SYS_exp_real           EQU 140
POLY_SYS_ln_real            EQU 141
ELSE
#include "sys.h"
ENDIF

;#
;# Stack format from objects.h is:
;#  typedef struct
;#  {                byte offset of start
;#    word  p_space ;            0
;#    byte *p_pc ;               4
;#    word *p_sp ;               8
;#    word *p_hr ;              12
;#    word  p_nreg ;            16 = no of checked registers (always CHECKED_REGS)
;#    word  p_reg[1] ;          20
;#  } StackObject ;
;#
 
;#
;# Starting offsets

IFDEF WINDOWS

SPACE_OFF   EQU     0
PC_OFF      EQU     4
SP_OFF      EQU     8
HR_OFF      EQU     12
EAX_OFF     EQU     20
EBX_OFF     EQU     24
ECX_OFF     EQU     28
EDX_OFF     EQU     32
ESI_OFF     EQU     36
EDI_OFF     EQU     40
FLAGS_OFF   EQU     48
FPREGS_OFF  EQU     52

ELSE

.set    SPACE_OFF,  0
IFNDEF HOSTARCHITECTURE_X86_64
.set    PC_OFF,     4
.set    SP_OFF,     8
.set    HR_OFF,     12
.set    EAX_OFF,    20
.set    EBX_OFF,    24
.set    ECX_OFF,    28
.set    EDX_OFF,    32
.set    ESI_OFF,    36
.set    EDI_OFF,    40
.set    FLAGS_OFF,  48
.set    FPREGS_OFF, 52
ELSE
.set    PC_OFF,     8
.set    SP_OFF,     16
.set    HR_OFF,     24
;# 32 is the count of the number of checked registers
.set    EAX_OFF,    40
.set    EBX_OFF,    48
.set    ECX_OFF,    56
.set    EDX_OFF,    64
.set    ESI_OFF,    72
.set    EDI_OFF,    80
.set    R8_OFF,     88
.set    R9_OFF,     96
.set    R10_OFF,    104
.set    R11_OFF,    112
.set    R12_OFF,    120
.set    R13_OFF,    128
.set    R14_OFF,    136
;# 144 is the count of the number of unchecked registers
.set    FLAGS_OFF,  152
.set    FPREGS_OFF, 160
ENDIF

ENDIF


;#
;# CODE STARTS HERE
;#
IFDEF WINDOWS
    .CODE
ELSE
    .text
ENDIF

;# Define standard call macro. CALL_IO ioCallNo  where ioCallNo is the io function to call.
;# We need to include M_Redx in the register sets.  MD_set_for_retry may modify it
;# if the function was called directly and not via the closure register.

IFDEF WINDOWS

CALL_IO    MACRO   index
        mov     byte ptr [RequestCode+Rebp],index
IFNDEF HOSTARCHITECTURE_X86_64
        jmp     dword ptr [IOEntryPoint+Rebp]
ELSE
        jmp     qword ptr [IOEntryPoint+Rebp]
ENDIF
ENDM

ELSE

#define CALL_IO(index) \
        MOVB  $index,RequestCode[Rebp]; \
        jmp   *IOEntryPoint[Rebp];
ENDIF

;# Load the registers from the ML stack and jump to the code.
;# This is used to start ML code.
;# The argument is the address of the MemRegisters struct and goes into %rbp.
IFDEF WINDOWS
    PUBLIC  X86AsmSwitchToPoly
X86AsmSwitchToPoly:
ELSE
GLOBAL EXTNAME(X86AsmSwitchToPoly)
EXTNAME(X86AsmSwitchToPoly):               ;# Entry point from C
ENDIF
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Recx                    ;# Argument - address of MemRegisters - goes into Rebp
    PUSHAL                                  ;# Save all the registers just to be safe
    MOVL    Resp,SavedSp[Recx]              ;# savedSp:=%Resp - Save the system stack pointer.

    MOVL    Recx,Rebp                       ;# Put address of MemRegisters where it belongs
ELSE
;# The argument to the function is passed in Redi
    PUSHL   Rebp                             ;# Save callee--save registers
    PUSHL   Rebx
    PUSHL   %r12
    PUSHL   %r13
    PUSHL   %r14
    PUSHL   %r15
    MOVL    Resp,SavedSp[Redi]              ;# savedSp:=%Resp - Save the system stack pointer.
    MOVL    Redi,Rebp                       ;# Put address of MemRegisters where it belongs
ENDIF
    MOVL    PolyStack[Rebp],Reax
IFDEF HOSTARCHITECTURE_X86_64
    MOVL    LocalMpointer[Rebp],R15         ;# Set the heap pointer register
ENDIF
    MOVL    SP_OFF[Reax],Resp               ;# Set the new stack ptr
    PUSHL   PC_OFF[Reax]                    ;# Push the code address
    PUSHL   FLAGS_OFF[Reax]                 ;# Push the flags
    FRSTOR  FPREGS_OFF[Reax]
    MOVL    EBX_OFF[Reax],Rebx              ;# Load the registers
    MOVL    ECX_OFF[Reax],Recx
    MOVL    EDX_OFF[Reax],Redx
    MOVL    ESI_OFF[Reax],Resi
    MOVL    EDI_OFF[Reax],Redi
IFDEF HOSTARCHITECTURE_X86_64
    MOVL    R8_OFF[Reax],R8
    MOVL    R9_OFF[Reax],R9
    MOVL    R10_OFF[Reax],R10
    MOVL    R11_OFF[Reax],R11
    MOVL    R12_OFF[Reax],R12
    MOVL    R13_OFF[Reax],R13
    MOVL    R14_OFF[Reax],R14
ENDIF
	cld										;# Clear this just in case
    MOVL    EAX_OFF[Reax],Reax
    POPFL                                   ;# reset flags
IFDEF WINDOWS
    mov     byte ptr [InRTS+Rebp],0
ELSE
    MOVB    CONST 0,InRTS[Rebp]             ;# inRTS:=0 (stack now kosher)
ENDIF
    ret                                     ;# Jump to code address

;# Code to save the state and switch to C
IFDEF WINDOWS
    PUBLIC  X86AsmSaveStateAndReturn
X86AsmSaveStateAndReturn  PROC
ELSE
GLOBAL EXTNAME(X86AsmSaveStateAndReturn)
EXTNAME(X86AsmSaveStateAndReturn):
ENDIF
    PUSHFL                      ;# Save flags
    PUSHL   Reax                ;# Save eax
    MOVL    PolyStack[Rebp],Reax
    MOVL    Rebx,EBX_OFF[Reax]
    MOVL    Recx,ECX_OFF[Reax]
    MOVL    Redx,EDX_OFF[Reax]
    MOVL    Resi,ESI_OFF[Reax]
    MOVL    Redi,EDI_OFF[Reax]
    FNSAVE  FPREGS_OFF[Reax]
IFDEF HOSTARCHITECTURE_X86_64
    MOVL    R8,R8_OFF[Reax]
    MOVL    R9,R9_OFF[Reax]
    MOVL    R10,R10_OFF[Reax]
    MOVL    R11,R11_OFF[Reax]
    MOVL    R12,R12_OFF[Reax]
    MOVL    R13,R13_OFF[Reax]
    MOVL    R14,R14_OFF[Reax]
    MOVL    R15,LocalMpointer[Rebp]  ;# Save the heap pointer
ENDIF
    POPL    Rebx                ;# Get old eax value
    MOVL    Rebx,EAX_OFF[Reax]
    POPL    Rebx
    MOVL    Rebx,FLAGS_OFF[Reax]
    MOVL    Resp,SP_OFF[Reax]
IFDEF WINDOWS
    mov     byte ptr [InRTS+Rebp],1
ELSE
    MOVB    CONST 1,InRTS[Rebp]             ;# inRTS:=0 (stack now kosher)
ENDIF
    MOVL    SavedSp[Rebp],Resp
IFNDEF HOSTARCHITECTURE_X86_64
    POPAL
ELSE
    POPL    %r15                            ;# Restore callee-save registers
    POPL    %r14
    POPL    %r13
    POPL    %r12
    POPL    Rebx
    POPL    Rebp
ENDIF
    ret

IFDEF WINDOWS
X86AsmSaveStateAndReturn  ENDP
ENDIF

;#
;# A number of functions implemented in Assembly for efficiency reasons
;#

CALLMACRO   INLINE_ROUTINE  int_to_word
 ;# Extract the low order bits from a word.
    TESTL   CONST TAG,Reax
    jz      get_first_long_word_a1
    ret                 ;# Return the argument
CALLMACRO   RegMask int_to_word,(M_Reax)

 ;# This is now used in conjunction with isShort in Word.fromInt.
get_first_long_word_a1:
CALLMACRO   INLINE_ROUTINE  get_first_long_word_a
IFDEF WINDOWS
    test    byte ptr [Reax-1],CONST 16  ;# 16 is the "negative" bit
ELSE
    testb   CONST 16,(-1)[Reax]     ;# 16 is the "negative" bit
ENDIF
    MOVL    [Reax],Reax     ;# Extract the word which is already little-endian
    jz      gfw1
    NEGL    Reax            ;# We can ignore overflow
gfw1:
CALLMACRO   MAKETAGGED  Reax,Reax
    ret
CALLMACRO   RegMask get_first_long_word,(M_Reax)



CALLMACRO    INLINE_ROUTINE move_bytes
 ;# Move a segment of memory from one location to another.
 ;# Must deal with the case of overlapping segments correctly.
 ;# (source, sourc_offset, destination, dest_offset, length)

 ;# Assume that the offsets and length are all short integers.
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    12[Resp],Redi               ;# Destination address
    MOVL    8[Resp],Recx                ;# Destination offset, untagged
ELSE
    MOVL    R8,Redi               ;# Destination address
    MOVL    R9,Recx                ;# Destination offset, untagged
ENDIF
    SHRL    CONST TAGSHIFT,Recx
    ADDL    Recx,Redi
    MOVL    Reax,Resi                   ;# Source address
    SHRL    CONST TAGSHIFT,Rebx
    ADDL    Rebx,Resi
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Recx                ;# Get the length to move
ELSE
    MOVL    R10,Recx                ;# Get the length to move
ENDIF
    SHRL    CONST TAGSHIFT,Recx
    cld                             ;# Default to increment Redi,Resi
    CMPL    Redi,Resi                   ;# Check for potential overlap
 ;# If dest > src then use decrementing moves else
 ;# use incrementing moves.
    ja      mvb1
    std                             ;# Decrement Redi,Resi
    LEAL    (-1)[Resi+Recx],Resi
    LEAL    (-1)[Redi+Recx],Redi
mvb1:
IFDEF WINDOWS
    rep movsb                       ;# Copy the bytes
ELSE
    rep
    movsb                           ;# Copy the bytes
ENDIF
    MOVL    CONST UNIT,Reax             ;# The function returns unit
    MOVL    Reax,Rebx               ;# Clobber bad value in %rbx
    MOVL    Reax,Recx               ;# and %Recx
    MOVL    Reax,Redi
    MOVL    Reax,Resi
 ;# Visual Studio 5 C++ seems to assume that the direction flag
 ;# is cleared.  I think that`s a bug but we have to go along with it.
    cld
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 12
ELSE
    ret
ENDIF

CALLMACRO   RegMask move_bytes,Mask_all


CALLMACRO    INLINE_ROUTINE move_words
 ;# Move a segment of memory from one location to another.
 ;# Must deal with the case of overlapping segments correctly.
 ;# (source, source_offset, destination, dest_offset, length)
 ;# Assume that the offsets and length are all short integers.
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    12[Resp],Redi               ;# Destination address
    MOVL    8[Resp],Recx                ;# Destination offset
    LEAL    (-2)[Redi+Recx*2],Redi      ;# Destination address plus offset
    LEAL    (-2)[Reax+Rebx*2],Resi      ;# Source address plus offset
    MOVL    4[Resp],Recx                ;# Get the length to move (words)
ELSE
    MOVL    R8,Redi               ;# Destination address
    MOVL    R9,Recx                ;# Destination offset
    LEAL    (-4)[Redi+Recx*4],Redi      ;# Destination address plus offset
    LEAL    (-4)[Reax+Rebx*4],Resi      ;# Source address plus offset
    MOVL    R10,Recx                ;# Get the length to move (words)
ENDIF
    SHRL    CONST TAGSHIFT,Recx
    cld                             ;# Default to increment Redi,Resi
    CMPL    Redi,Resi                   ;# Check for potential overlap
 ;# If dest > src then use decrementing moves else
 ;# use incrementing moves.
    ja      mvw1
    std                             ;# Decrement Redi,Resi
IFNDEF HOSTARCHITECTURE_X86_64
    LEAL    (-4)[Resi+Recx*4],Resi
    LEAL    (-4)[Redi+Recx*4],Redi
ELSE
    LEAL    (-8)[Resi+Recx*8],Resi
    LEAL    (-8)[Redi+Recx*8],Redi
ENDIF
mvw1:
IFDEF WINDOWS
IFNDEF HOSTARCHITECTURE_X86_64
    rep movsd                       ;# Copy the words
ELSE
    rep movsq                       ;# Copy the words
ENDIF
ELSE
    rep
IFNDEF HOSTARCHITECTURE_X86_64
    movsl                           ;# Copy the words
ELSE
    movsq                           ;# Copy the words
ENDIF
ENDIF
    MOVL    CONST UNIT,Reax             ;# The function returns unit
    MOVL    Reax,Recx               ;# Clobber bad values
    MOVL    Reax,Redi
    MOVL    Reax,Resi
 ;# Visual Studio 5 C++ seems to assume that the direction flag
 ;# is cleared.  I think that`s a bug but we have to go along with it.
    cld
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 12
ELSE
    ret
ENDIF

CALLMACRO   RegMask move_words,Mask_all
;#

RetFalse:
    MOVL    CONST FALSE,Reax
    ret
CALLMACRO   RegMask int_eq,(M_Reax)

RetTrue:
    MOVL    CONST TRUE,Reax
    ret
CALLMACRO   RegMask int_neq,(M_Reax)

CALLMACRO   INLINE_ROUTINE  not_bool
    XORL    CONST (TRUE-TAG),Reax   ;# Change the value but leave the tag
    ret
CALLMACRO   RegMask not_bool,(M_Reax)

 ;# or, and, xor shift etc. assume the values are tagged integers
CALLMACRO   INLINE_ROUTINE  or_word
    ORL     Rebx,Reax
    ret
CALLMACRO   RegMask or_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  and_word
    ANDL    Rebx,Reax
    ret
CALLMACRO   RegMask and_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  xor_word
    XORL    Rebx,Reax
    ORL     CONST TAG,Reax  ;# restore the tag
    ret
CALLMACRO   RegMask xor_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  shift_left_word
 ;# Assume that both args are tagged integers
 ;# Word.<<(a,b) is defined to return 0 if b > Word.wordSize
IFNDEF HOSTARCHITECTURE_X86_64
    CMPL    CONST TAGGED(31),Rebx
ELSE
    CMPL    CONST TAGGED(63),Rebx
ENDIF
    jb      slw1
    MOVL    CONST ZERO,Reax
    ret
slw1:
    MOVL    Rebx,Recx
    SHRL    CONST TAGSHIFT,Recx ;# remove tag
    SUBL    CONST TAG,Reax
    SHLL    R_cl,Reax
    ORL     CONST TAG,Reax  ;# restore the tag
    MOVL    Reax,Recx       ;# clobber %Recx
    ret
CALLMACRO   RegMask shift_left_word,(M_Reax OR M_Recx)

CALLMACRO   INLINE_ROUTINE  shift_right_word
 ;# Word.>>(a,b) is defined to return 0 if b > Word.wordSize
IFNDEF HOSTARCHITECTURE_X86_64
    CMPL    CONST TAGGED(31),Rebx
ELSE
    CMPL    CONST TAGGED(63),Rebx
ENDIF
    jb      srw1
    MOVL    CONST ZERO,Reax
    ret
srw1:
    MOVL    Rebx,Recx
    SHRL    CONST TAGSHIFT,Recx ;# remove tag
    SHRL    R_cl,Reax
    ORL     CONST TAG,Reax  ;# restore the tag
    MOVL    Reax,Recx       ;# clobber %Recx
    ret
CALLMACRO   RegMask shift_right_word,(M_Reax OR M_Recx)

CALLMACRO   INLINE_ROUTINE  shift_right_arith_word
 ;# Word.~>>(a,b) is defined to return 0 or ~1 if b > Word.wordSize
 ;# The easiest way to do that is to set the shift to 31.
IFNDEF HOSTARCHITECTURE_X86_64
    CMPL    CONST TAGGED(31),Rebx
ELSE
    CMPL    CONST TAGGED(63),Rebx
ENDIF
    jb      sra1
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    CONST TAGGED(31),Rebx
ELSE
    MOVL    CONST TAGGED(63),Rebx
ENDIF
sra1:
    MOVL    Rebx,Recx
    SHRL    CONST TAGSHIFT,Recx ;# remove tag
    SARL    R_cl,Reax
    ORL     CONST TAG,Reax  ;# restore the tag
    MOVL    Reax,Recx       ;# clobber %Recx
    ret
CALLMACRO   RegMask shift_right_arith_word,(M_Reax OR M_Recx)

CALLMACRO   INLINE_ROUTINE  locksega
 ;# Clears the "mutable" bit on a segment

IFDEF WINDOWS
    and     byte ptr    [Reax-1],CONST(0ffh-B_mutable)
ELSE
    andb    CONST(0xff-B_mutable),-1[Reax]
ENDIF
    MOVL     CONST TAGGED(0),Reax   ;# Return Unit,
    ret
CALLMACRO   RegMask lockseg,M_Reax

;#INLINE_ROUTINE(get_flags)
;#  CMPL    data0,%Reax
;#  jb  vf1     ; skip if < data0
;#  movzbl  (%Reax-1),%Reax ; if > data0 return flag
;#  SHLL    $TAGSHIFT,%Reax ; Tag it
;#  ORL $TAG,%Reax
;#  ret
;#
;#vf1:  mov $TAGGED(256),%Reax ; if < data0 must be in io area, return 256
;#  ret

;# For backwards compatibility this needs to call the RTS.
;# In due course it should be possible to have it simply return
;# the top byte of the length word as a tagged integer. 
;# CALLMACRO   CALL_IO1    get_flags_

;# CALLMACRO    INLINE_ROUTINE  get_flags_a
;#  movzx   Reax, byte ptr [Reax-1]
;#  LEAL    1[Reax*2],Reax
;#  ret
;# CALLMACRO    RegMask get_flags_,(M_Reax)


CALLMACRO   INLINE_ROUTINE  get_length_a
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    (-4)[Reax],Reax
ELSE
    MOVL    (-8)[Reax],Reax
ENDIF
    SHLL    CONST 8,Reax            ;# Clear top byte
    SHRL    CONST(8-TAGSHIFT),Reax  ;# Make it a tagged integer
    ORL CONST TAG,Reax
    ret
CALLMACRO   RegMask get_length,(M_Reax)


CALLMACRO   INLINE_ROUTINE  is_shorta
;# Returns true if the argument is tagged
    ANDL    CONST TAG,Reax
    jz      RetFalse
    jmp     RetTrue
CALLMACRO   RegMask is_short,(M_Reax)

CALLMACRO   INLINE_ROUTINE  string_length
    TESTL   CONST TAG,Reax  ;# Single char strings are represented by the
    jnz     RetOne      ;# character.
    MOVL    [Reax],Reax ;# Get length field
CALLMACRO   MAKETAGGED  Reax,Reax
    ret
RetOne: MOVL    CONST TAGGED(1),Reax
    ret
CALLMACRO   RegMask string_length,(M_Reax)

 ;# Store the length of a string in the first word.
CALLMACRO   INLINE_ROUTINE  set_string_length_a
    SHRL    CONST TAGSHIFT,Rebx ;# Untag the length
    MOVL    Rebx,[Reax]
    MOVL    CONST UNIT,Reax     ;# Return unit
    MOVL    Reax,Rebx           ;# Clobber untagged value
    ret
CALLMACRO   RegMask set_string_length,(M_Reax OR M_Rebx)

;# raisex (formerly raisexn) is used by compiled code.
CALLMACRO   INLINE_ROUTINE  raisex

    MOVL    HandlerRegister[Rebp],Recx    ;# Get next handler into %rcx

 ;# Much of the rest of this is legacy code needed for backwards
 ;# compatibility.  From 5.4 the compiler generates handlers that expect to
 ;# restore the old handler themselves and deal with exception discrimination
 ;# so raising an exception just involves jumping to the innermost handler.
 ;# We need to check whether the handler is a new-format or old format.

 ;# Is this an old-format handler?  The first word will be either a tagged value
 ;# or the address of a handler as a word-aligned value.  New format handlers
 ;# have a code-address here which will be word + 2 byte aligned.

IFDEF WINDOWS
    test    dword ptr [Recx],1
    jne     rsx1          ;# Old format
	test    dword ptr [Recx],3
	je      rsx1          ;# Old format
    jmp     dword ptr [Recx]
ELSE
    TESTL   CONST 1,[Recx]
    jne     rsx1          ;# Old format
    TESTL   CONST 3,[Recx]
    je      rsx1          ;# Old format
    jmp     *[Recx]
ENDIF


 ;# Loop to find the handler for this exception. Handlers consist of one or more
 ;# pairs of identifier and code address, followed by the address of the next
 ;# handler.
rsx1:
IFDEF WINDOWS
    cmp     dword ptr [Recx],TAGGED(0)
ELSE
    CMPL    CONST TAGGED(0),[Recx]
ENDIF
    je      rsx7        ;# default handler if it is TAGGED(0)
    MOVL    [Recx],Rebx ;# Arg1 - the identifier for this handler
    CMPL    [Reax],Rebx ;# Compare with exception tag - Have we got the right handler?
    je      rsx7        ;# Skip if we found a match.
IFNDEF HOSTARCHITECTURE_X86_64
    ADDL    CONST 8,Recx        ;# Look at the next handler.
ELSE
    ADDL    CONST 16,Recx        ;# Look at the next handler.
ENDIF
    MOVL    [Recx],Rebx
    CMPL    Recx,Rebx       ;# Is it a pointer to the next handler i.e.
    jb      rsx1        ;# does it point further up the stack or at itself.
                        ;# (The last handler on the stack points at itself).
    CMPL    StackTop[Rebp],Rebx
    ja      rsx1        ;# If not it must be a new pair, so look at that.
    MOVL    Rebx,Recx       ;# It is a pointer to a new handler.
    jmp     rsx1

rsx7:   ;# We have found the right handler - %Recx points to the data
IFNDEF HOSTARCHITECTURE_X86_64
    ADDL    CONST 4,Recx        ;# point it at the code
ELSE
    ADDL    CONST 8,Recx        ;# point it at the code
ENDIF

    MOVL    [Recx],Redx     ;# Get the handler entry point

 ;# There may be some other identifier/entry point pairs in this group.
 ;# We have to remove them and find the pointer to the next handler in the
 ;# chain.  This becomes the new handler pointer.
rsx6:
IFNDEF HOSTARCHITECTURE_X86_64
    ADDL    CONST 4,Recx
ELSE
    ADDL    CONST 8,Recx
ENDIF
    MOVL    [Recx],Rebx
    CMPL    Recx,Rebx   ;# Is it a pointer to the next handler i.e.
    jb      rsx6        ;# does it point further up the stack or at itself?
    CMPL    StackTop[Rebp],Rebx
    ja      rsx6

;# We`re now pointing to the pointer to the next handler.
    CMPL    CONST TAGGED(0),Redx    ;# See if it was set up by exception_trace
    je      rsx9
 ;# Ordinary exception
    MOVL    Recx,Resp       ;# Move stack pointer to handler frame
    POPL    HandlerRegister[Rebp] ;# Load previous handler
    MOVL    CONST UNIT,Rebx ;# The values in some regs are illegal.
    MOVL    CONST UNIT,Recx
IFDEF WINDOWS
    jmp     Redx      ;# Now enter the handler
ELSE
    jmp     *Redx      ;# Now enter the handler
ENDIF

rsx9:
 ;# Must give an exception trace - ex_tracec unwinds to the next handler.
    MOVL    Reax,Rebx
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_give_ex_trace

CALLMACRO   INLINE_ROUTINE  load_byte
    MOVL    Rebx,Redi
    SHRL    CONST TAGSHIFT,Redi
IFDEF WINDOWS
    movzx   Redi, byte ptr [Reax][Redi]
ELSE
IFNDEF HOSTARCHITECTURE_X86_64
    movzbl  (Reax,Redi,1),Redi
ELSE
    movzbq  (Reax,Redi,1),Redi
ENDIF
ENDIF
CALLMACRO   MAKETAGGED  Redi,Reax
    MOVL    Reax,Redi       ;# Clobber bad value in %Redi
    ret
CALLMACRO   RegMask load_byte,(M_Reax OR M_Redi)

CALLMACRO   INLINE_ROUTINE  load_word
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    (-2)[Reax+Rebx*2],Reax
ELSE
    MOVL    (-4)[Reax+Rebx*4],Reax
ENDIF
    MOVL    Reax,Rebx
    ret
CALLMACRO   RegMask load_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  assign_byte

;# We can assume that the data value will not overflow 30 bits (it is only 1 byte!)
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Recx
ELSE
    MOVL    R8,Recx
ENDIF
    SHRL    CONST TAGSHIFT,Recx       ;# Remove tags from data value

;# We can assume that the index will not overflow 30 bits i.e. it is a tagged short
    SHRL    CONST TAGSHIFT,Rebx     ;# Remove tags from offset
    MOVB    R_cl,[Reax+Rebx]

    MOVL    CONST UNIT,Reax             ;# The function returns unit
    MOVL    Reax,Rebx                   ;# Clobber bad value in %Rebx
    MOVL    Reax,Recx                   ;# and %Recx
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 4
ELSE
    ret
ENDIF
CALLMACRO   RegMask assign_byte,(M_Reax OR M_Rebx OR M_Recx)


CALLMACRO   INLINE_ROUTINE  assign_word
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Recx
    MOVL    Recx,(-2)[Reax+Rebx*2]
ELSE
    MOVL    R8,(-4)[Reax+Rebx*4]      ;# The offset is tagged already
ENDIF
    MOVL    CONST UNIT,Reax           ;# The function returns unit
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 4
CALLMACRO   RegMask assign_word,(M_Reax OR M_Recx)
ELSE
    ret
CALLMACRO   RegMask assign_word,(M_Reax)
ENDIF

;# Allocate a piece of memory that does not need to be initialised.
;# We can't actually risk leaving word objects uninitialised so for the
;# moment we always initialise.
CALLMACRO   INLINE_ROUTINE  alloc_uninit
IFDEF HOSTARCHITECTURE_X86_64
    MOVL    CONST ZERO,R8
ELSE
    POP     Recx         ;# Get the return address
    PUSHL   CONST ZERO   ;# Push the initial value - zero
    PUSHL   Recx         ;# Restore the return address
ENDIF
;# Drop through into alloc_store

IFNDEF HOSTARCHITECTURE_X86_64
CALLMACRO   RegMask alloc_uninit,(M_Reax OR M_Rebx OR M_Recx OR M_Redx OR M_Redi)
ELSE
CALLMACRO   RegMask alloc_uninit,(M_Reax OR M_Rebx OR M_Recx OR M_Redx OR M_Redi OR M_R8)
ENDIF

;# alloc(size, flags, initial).  Allocates a segment of a given size and
;# initialises it.
;#
;# This is primarily used for arrays and for strings.  Refs are
;# allocated using inline code.
CALLMACRO   INLINE_ROUTINE  alloc_store
allsts:
 ;# alloc(size, flags, initial).  Allocates a segment of a given size and
 ;# initialises it.
 ;# First check that the length is acceptable
    TESTL   CONST TAG,Reax
    jz      alloc_in_rts            ;# Get the RTS to raise an exception
    MOVL    Reax,Redi
    SHRL    CONST TAGSHIFT,Redi     ;# Remove tag
    jnz     allst0                  ;# (test for 0) Make zero sized objects 1
    MOVL    CONST 1,Redi            ;# because they mess up the g.c.
    jmp     alloc_in_rts
allst0:
IFNDEF HOSTARCHITECTURE_X86_64
    CMPL    CONST Max_Length,Redi   ;# Length field must fit in 24 bits
ELSE
    MOVL    CONST Max_Length,Redx   ;# Length field must fit in 56 bits
    CMPL    Redx,Redi
ENDIF
    ja      alloc_in_rts            ;# Get the RTS to raise an exception
IFNDEF HOSTARCHITECTURE_X86_64
    INCL    Redi                    ;# Add 1 word
    SHLL    CONST 2,Redi            ;# Get length in bytes
    MOVL    LocalMpointer[Rebp],Redx
ELSE
    ADDL    CONST 1,Redi            ;# Add 1 word
    SHLL    CONST 3,Redi            ;# Get length in bytes
    MOVL    R15,Redx
ENDIF
    SUBL    Redi,Redx               ;# Allocate the space
    MOVL    Reax,Redi               ;# Clobber bad value in Redi
    CMPL    LocalMbottom[Rebp],Redx            ;# Check for free space
IFNDEF HOSTARCHITECTURE_X86_64
    jb      alloc_in_rts            ;# TODO: Is this exactly the right test?

    MOVL    Redx,LocalMpointer[Rebp]             ;# Put back in the heap ptr
ELSE
    jb      alloc_in_rts
    MOVL    Redx,R15                 ;# Put back in the heap ptr
ENDIF
    SHRL    CONST TAGSHIFT,Reax
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    Reax,(-4)[Redx]         ;# Put in length
ELSE
    MOVL    Reax,(-8)[Redx]         ;# Put in length
ENDIF
    SHRL    CONST TAGSHIFT,Rebx     ;# remove tag from flag
    ORL     CONST B_mutable,Rebx    ;# set mutable bit
    MOVB    R_bl,(-1)[Redx]         ;# and put it in.
 ;# Initialise the store.
    MOVL    Reax,Recx               ;# Get back the no. of words.
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Reax            ;# Get initial value.
ELSE
    MOVL    R8,Reax                 ;# Get initial value.
ENDIF
    CMPL    CONST B_mutablebytes,Rebx
    jne     allst2

 ;# If this is a byte seg
    SHRL    CONST TAGSHIFT,Reax ;# untag the initialiser
IFNDEF HOSTARCHITECTURE_X86_64
    SHLL    CONST 2,Recx        ;# Convert to bytes
ELSE
    SHLL    CONST 3,Recx        ;# Convert to bytes
ENDIF
    MOVL    Redx,Redi
IFDEF WINDOWS
    rep stosb
ELSE
    rep
    stosb
ENDIF
    jmp     allst3

 ;# If this is a word segment
allst2:
    MOVL    Redx,Redi
IFDEF WINDOWS
    rep stosd
ELSE
    rep
IFNDEF HOSTARCHITECTURE_X86_64
    stosl
ELSE
    stosq
ENDIF
ENDIF

allst3:
    MOVL    Redx,Reax

    MOVL    Reax,Recx       ;# Clobber these
    MOVL    Reax,Redx
    MOVL    Reax,Rebx
    MOVL    Reax,Redi
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 4
ELSE
    ret
ENDIF
CALLMACRO   RegMask alloc_store,(M_Reax OR M_Rebx OR M_Recx OR M_Redx OR M_Redi)

;# This is used if we have reached the store limit and need to garbage-collect.
alloc_in_rts:
    MOVL    Reax,Redx       ;# Clobber these first
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_alloc_store


CALLMACRO   INLINE_ROUTINE  add_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      add_really_long
    LEAL    (-TAG)[Reax],Redi
    ADDL    Rebx,Redi
    jo      add_really_long
    MOVL    Redi,Reax
    ret
add_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_aplus
CALLMACRO   RegMask aplus,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  sub_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      sub_really_long
    MOVL    Reax,Redi
    SUBL    Rebx,Redi
    jo      sub_really_long
    LEAL    TAG[Redi],Reax      ;# Put back the tag
    MOVL    Reax,Redi
    ret
sub_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_aminus
CALLMACRO   RegMask aminus,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  mult_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      mul_really_long
    MOVL    Rebx,Redi
    SARL    CONST TAGSHIFT,Redi ;# Shift multiplicand
    MOVL    Reax,Resi
    SUBL    CONST TAG,Resi          ;# Just subtract off the tag off multiplier
    IMULL   Redi,Resi
    jo      mul_really_long
    ADDL    CONST TAG,Resi
    MOVL    Resi,Reax
    MOVL    Reax,Redi
    ret
mul_really_long:
    MOVL    Reax,Resi       ;# Clobber this
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_amul
CALLMACRO   RegMask amul,(M_Reax OR M_Redi OR M_Resi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  div_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi          ;# %Redi now contains $0 or $1 (both legal!)
    jz      div_really_long
    CMPL    CONST TAGGED(0),Rebx    ;# Check that it's non-zero
    jz      div_really_long         ;# We don't want a trap.
 ;# The only case of overflow is dividing the smallest negative number by -1
    CMPL    CONST TAGGED((-1)),Rebx
    jz      div_really_long
    SARL    CONST TAGSHIFT,Reax
    MOVL    Rebx,Redi
    SARL    CONST TAGSHIFT,Redi
IFNDEF HOSTARCHITECTURE_X86_64
    cdq
ELSE
    cqo
ENDIF
    idiv    Redi
CALLMACRO   MAKETAGGED  Reax,Reax
    MOVL    Reax,Redx
    MOVL    Reax,Redi
    ret
div_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_adiv
CALLMACRO   RegMask adiv,(M_Reax OR M_Redi OR M_Redx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  rem_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi      ;# %Redi now contains $0 or $1 (both legal!
    jz      rem_really_long
    CMPL    CONST TAGGED(0),Rebx    ;# Check that it's non-zero
    jz      rem_really_long         ;# We don't want a trap.
 ;# The only case of overflow is dividing the smallest negative number by -1
    CMPL    CONST TAGGED((-1)),Rebx
    jz      rem_really_long
    SARL    CONST TAGSHIFT,Reax
    MOVL    Rebx,Redi
    SARL    CONST TAGSHIFT,Redi
IFNDEF HOSTARCHITECTURE_X86_64
    cdq
ELSE
    cqo
ENDIF
    idiv    Redi
CALLMACRO   MAKETAGGED  Redx,Reax
    MOVL    Reax,Redx
    MOVL    Reax,Redi
    ret
rem_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_amod
CALLMACRO   RegMask amod,(M_Reax OR M_Redi OR M_Redx OR Mask_all)

 ;# Combined quotient and remainder.  We have to use the long form
 ;# if the arguments are long or there's an overflow.  The first two
 ;# arguments are the values to be divided.  The third argument is the
 ;# address where the results should be placed. 
CALLMACRO   INLINE_ROUTINE quotrem_long
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      quotrem_really_long
    CMPL    CONST TAGGED(0),Rebx
    jz      quotrem_really_long
 ;# The only case of overflow is dividing the smallest negative number by -1
    CMPL    CONST TAGGED((-1)),Rebx
    jz      quotrem_really_long

 ;# Get the address for the result.
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    4[Resp],Recx
ELSE
    MOVL    R8,Recx
ENDIF
;# Do the division
    SARL    CONST TAGSHIFT,Reax
    MOVL    Rebx,Redi
    SARL    CONST TAGSHIFT,Redi
IFNDEF HOSTARCHITECTURE_X86_64
    cdq
ELSE
    cqo
ENDIF
    idiv    Redi
CALLMACRO   MAKETAGGED  Reax,Reax
CALLMACRO   MAKETAGGED  Redx,Redx
    MOVL    Reax,Redi
    MOVL    Reax,[Recx]
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    Redx,4[Recx]
ELSE
    MOVL    Redx,8[Recx]
ENDIF
    MOVL    Recx,Reax
IFNDEF HOSTARCHITECTURE_X86_64
    ret     CONST 4
ELSE
    ret
ENDIF

mem_for_remquot1:  ;# Not enough store: clobber bad value in ecx.
    MOVL   CONST 1,Recx

quotrem_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_quotrem
CALLMACRO   RegMask quotrem,(M_Reax OR M_Redi OR M_Redx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  equal_long
    CMPL    Reax,Rebx
    je      RetTrue
    MOVL    Reax,Recx   ;# If either is short
    ORL     Rebx,Reax   ;# the result is false
    ANDL    CONST TAG,Reax
    jnz     RetFalse
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_equala
CALLMACRO   RegMask equala,(M_Reax OR M_Recx OR Mask_all)


CALLMACRO   INLINE_ROUTINE  or_long
IFDEF NOTATTHEMOMENT
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      or_really_long
    ORL     Rebx,Reax
    MOVL    Reax,Redi
    ret
or_really_long:
ENDIF
CALLMACRO   CALL_IO    POLY_SYS_ora
CALLMACRO   RegMask ora,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  xor_long
IFDEF NOTATTHEMOMENT
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      xor_really_long
    XORL    Rebx,Reax
    ORL     CONST TAG,Reax  ;# restore the tag
    MOVL    Reax,Redi
    ret
xor_really_long:
ENDIF
CALLMACRO   CALL_IO    POLY_SYS_xora
CALLMACRO   RegMask xora,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  and_long
IFDEF NOTATTHEMOMENT
    MOVL    Reax,Redi
    ANDL    Rebx,Redi
    ANDL    CONST TAG,Redi
    jz      and_really_long
    ANDL    Rebx,Reax
    MOVL    Reax,Redi
    ret
and_really_long:
ENDIF
CALLMACRO   CALL_IO    POLY_SYS_anda
CALLMACRO   RegMask anda,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  neg_long
    TESTL   CONST TAG,Reax
    jz      neg_really_long
    MOVL    CONST (TAGGED(0)+TAG),Redi
    SUBL    Reax,Redi
    jo      neg_really_long
    MOVL    Redi,Reax
    ret
neg_really_long:
    MOVL    Reax,Redi
CALLMACRO   CALL_IO    POLY_SYS_aneg
CALLMACRO   RegMask aneg,(M_Reax OR M_Redi OR Mask_all)

CALLMACRO   INLINE_ROUTINE  int_geq
    MOVL    Reax,Recx   ;# Use long test if either is long
    ANDL    Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      igeq1
    CMPL    Rebx,Recx
    jge     RetTrue
    jmp     RetFalse
igeq1:
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_int_geq
CALLMACRO   RegMask int_geq,(M_Reax OR M_Recx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  int_leq
    MOVL    Reax,Recx
    ANDL    Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      ileq1
    CMPL    Rebx,Recx
    jle     RetTrue
    jmp     RetFalse
ileq1:
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_int_leq
CALLMACRO   RegMask int_leq,(M_Reax OR M_Recx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  int_gtr
    MOVL    Reax,Recx
    ANDL    Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      igtr1
    CMPL    Rebx,Recx
    jg      RetTrue
    jmp     RetFalse
igtr1:
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_int_gtr
CALLMACRO   RegMask int_gtr,(M_Reax OR M_Recx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  int_lss
    MOVL    Reax,Recx
    ANDL    Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      ilss1
    CMPL    Rebx,Recx
    jl      RetTrue
    jmp     RetFalse
ilss1:
    MOVL    Recx,Reax
CALLMACRO   CALL_IO    POLY_SYS_int_lss
CALLMACRO   RegMask int_lss,(M_Reax OR M_Recx OR Mask_all)

CALLMACRO   INLINE_ROUTINE  offset_address
 ;# This is needed in the code generator, but is a very risky thing to do.
    SHRL    CONST TAGSHIFT,Rebx     ;# Untag
    ADDL    Rebx,Reax       ;# and add in
    MOVL    Reax,Rebx
    ret
CALLMACRO   RegMask offset_address,(M_Reax OR M_Rebx)

CALLMACRO   INLINE_ROUTINE  teststreq
    MOVL    Reax,Recx       ;# Are either just single chars?
    ORL     Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      teststreq2
    CMPL    Rebx,Recx       ;# Must be identical
    jz      RetTrue
    jmp     RetFalse
teststreq2:
    MOVL    Recx,Reax
    MOVL    Rebx,Redi       ;# Move ready for cmpsb.
    MOVL    [Reax],Recx     ;# Get length
IFNDEF HOSTARCHITECTURE_X86_64
    ADDL    CONST 4,Recx    ;# add 4 for the length field.
ELSE
    ADDL    CONST 8,Recx    ;# add 8 for the length field.
ENDIF
    MOVL    Reax,Resi       ;# Move to correct reg for cmpsb
    cld                     ;# Make sure we increment
    CMPL    Reax,Reax       ;# Set the Zero bit
;# Compare the strings.  Because the length field is at the beginning
;# it does not matter if the value we loaded into %Recx is wrong
IFDEF WINDOWS
    repe    cmpsb
ELSE
    repe    
    cmpsb
ENDIF
    MOVL    Reax,Resi       ;# Make these valid
    MOVL    Reax,Recx
    MOVL    Reax,Redi
    jz      RetTrue
    jmp     RetFalse
CALLMACRO   RegMask teststreq,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

CALLMACRO   INLINE_ROUTINE  teststrneq
    MOVL    Reax,Recx       ;# Are either just single chars?
    ORL     Rebx,Reax
    ANDL    CONST TAG,Reax
    jz      teststrneq2
    CMPL    Rebx,Recx       ;# Must be identical
    jz      RetFalse
    jmp     RetTrue
teststrneq2:
    MOVL    Recx,Reax
    MOVL    Rebx,Redi       ;# Move ready for cmpsb.
    MOVL    [Reax],Recx     ;# Get length
IFNDEF HOSTARCHITECTURE_X86_64
    ADDL    CONST 4,Recx    ;# add 4 for the length field.
ELSE
    ADDL    CONST 8,Recx    ;# add 8 for the length field.
ENDIF
    MOVL    Reax,Resi       ;# Move to correct reg for cmpsb
    cld                     ;# Make sure we increment
    CMPL    Reax,Reax       ;# Set the Zero bit
;# Compare the strings.  Because the length field is at the beginning
;# it does not matter if the value we loaded into %Recx is wrong
IFDEF WINDOWS
    repe    cmpsb
ELSE
    repe
    cmpsb
ENDIF
    MOVL    Reax,Resi       ;# Make these valid
    MOVL    Reax,Recx
    MOVL    Reax,Redi
    jz      RetFalse
    jmp     RetTrue
CALLMACRO   RegMask teststrneq,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

;# General test routine.  Returns with the condition codes set
;# appropriately.

teststr:
    TESTL   CONST TAG,Reax     ;# Is arg1 short
    jz      tststr1
    TESTL   CONST TAG,Rebx     ;# Yes: is arg2 also short?
    jz      tststr0a
    ;# Both are short - just compare the characters
    CMPL    Rebx,Reax
    ret

tststr0a:
    MOVL    CONST 1,Redi        ;# Is arg2 the null string ?
    CMPL    [Rebx],Redi
    jg      tststr4            ;# Return with "gtr" set if it is
    SHRL    CONST TAGSHIFT,Reax
IFNDEF HOSTARCHITECTURE_X86_64
    CMPB    4[Rebx],R_al
ELSE
    CMPB    8[Rebx],R_al
ENDIF
    jne     tststr4            ;# If they're not equal that's the result
    CMPL    CONST 256,Reax     ;# But if they're equal set "less" because A is less than B
    jmp     tststr4

tststr1: ;# arg2 is not short.  Is arg1 ?
    TESTL   CONST TAG,Rebx
    jz      tststr2
    MOVL    [Reax],Redi        ;# Is arg1 the null string
    CMPL    CONST 1,Redi
    jl      tststr4            ;# Return with "less" set if it is
    SHRL    CONST TAGSHIFT,Rebx
IFNDEF HOSTARCHITECTURE_X86_64
    MOVB    4[Reax],R_cl
ELSE
    MOVB    8[Reax],R_cl
ENDIF
    CMPB    R_bl,R_cl
    jne     tststr4            ;# If they're not equal that's the result
    CMPL    CONST 0,Redi      ;# But if they're equal set "greater" because A is greater than B
    jmp     tststr4

tststr2:
    MOVL    [Reax],Redi     ;# Get length.
    MOVL    [Rebx],Recx     ;# 
    CMPL    Recx,Redi       ;# Find shorter length
    jge     tststr3
    MOVL    Redi,Recx
tststr3:
IFNDEF HOSTARCHITECTURE_X86_64
    LEAL    4[Reax],Resi    ;# Load ptrs for cmpsb
    LEAL    4[Rebx],Redi
ELSE
    LEAL    8[Reax],Resi    ;# Load ptrs for cmpsb
    LEAL    8[Rebx],Redi
ENDIF
    cld                 ;# Make sure we increment
    CMPL    Reax,Reax       ;# Set the Zero bit
IFDEF WINDOWS
    repe cmpsb          ;# Compare while equal and Recx > 0
ELSE
    repe    
IFNDEF HOSTARCHITECTURE_X86_64
    cmpsb           ;# Compare while equal and %ecx > 0
ELSE
    cmpsb           ;# Compare while equal and %rcx > 0
ENDIF
ENDIF
    jnz     tststr4
 ;# Strings are equal as far as the shorter of the two.  Have to compare
 ;# the lengths.
    MOVL    [Reax],Redi
    CMPL    [Rebx],Redi
tststr4:
    MOVL    CONST 1,Reax      ;# Clobber these
    MOVL    Reax,Rebx       
    MOVL    Reax,Recx       
    MOVL    Reax,Resi
    MOVL    Reax,Redi
    ret

 ;# These functions compare strings for lexical ordering.  This version, at
 ;# any rate, assumes that they are UNSIGNED bytes.

CALLMACRO   INLINE_ROUTINE  str_compare
    call    teststr
    ja      RetTrue         ;# Return TAGGED(1) if it's greater
    je      RetFalse        ;# Return TAGGED(0) if it's equal
    MOVL    CONST MINUS1,Reax   ;# Return TAGGED(-1) if it's less.
    ret
CALLMACRO   RegMask str_compare,(M_Reax OR M_Recx OR M_Redi OR M_Resi)


CALLMACRO   INLINE_ROUTINE  teststrgeq
    call    teststr
    jnb     RetTrue
    jmp     RetFalse
CALLMACRO   RegMask teststrgeq,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

CALLMACRO   INLINE_ROUTINE  teststrleq
    call    teststr
    jna     RetTrue
    jmp     RetFalse
CALLMACRO   RegMask teststrleq,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

CALLMACRO   INLINE_ROUTINE  teststrlss
    call    teststr
    jb      RetTrue
    jmp     RetFalse
CALLMACRO   RegMask teststrlss,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

CALLMACRO   INLINE_ROUTINE  teststrgtr
    call    teststr
    ja      RetTrue
    jmp     RetFalse
CALLMACRO   RegMask teststrgtr,(M_Reax OR M_Recx OR M_Redi OR M_Resi)

CALLMACRO   INLINE_ROUTINE  is_big_endian
    jmp     RetFalse    ;# I386/486 is little-endian
CALLMACRO   RegMask is_big_endian,(M_Reax)

CALLMACRO   INLINE_ROUTINE  bytes_per_word
IFNDEF HOSTARCHITECTURE_X86_64
    MOVL    CONST TAGGED(4),Reax  ;# 4 bytes per word
ELSE
    MOVL    CONST TAGGED(8),Reax  ;# 8 bytes per word
ENDIF
    ret
CALLMACRO   RegMask bytes_per_word,(M_Reax)

 ;# Word functions.  These are all unsigned and do not raise Overflow
 
CALLMACRO   INLINE_ROUTINE  mul_word
    SHRL    CONST TAGSHIFT,Rebx ;# Untag the multiplier
    SUBL    CONST TAG,Reax      ;# Remove the tag from the multiplicand
    MULL    Rebx                ;# unsigned multiplication
    ADDL    CONST TAG,Reax      ;# Add back the tag, but don`t shift
    MOVL    Reax,Redx           ;# clobber this which has the high-end result
    MOVL    Reax,Rebx           ;# and the other bad result.
    ret
CALLMACRO   RegMask mul_word,(M_Reax OR M_Rebx OR M_Redx)

CALLMACRO   INLINE_ROUTINE  plus_word
    LEAL    (-TAG)[Reax+Rebx],Reax  ;# Add the values and subtract a tag
    ret
CALLMACRO   RegMask plus_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  minus_word
    SUBL    Rebx,Reax
    ADDL    CONST TAG,Reax          ;# Put back the tag
    ret
CALLMACRO   RegMask minus_word,(M_Reax)

CALLMACRO   INLINE_ROUTINE  div_word
    SHRL    CONST TAGSHIFT,Rebx
    jz      raise_div_ex
    SHRL    CONST TAGSHIFT,Reax
    MOVL    CONST 0,Redx
    div     Rebx
CALLMACRO   MAKETAGGED  Reax,Reax
    MOVL    Reax,Redx
    MOVL    Reax,Rebx
    ret
CALLMACRO   RegMask div_word,(M_Reax OR M_Rebx OR M_Redx)

CALLMACRO   INLINE_ROUTINE  mod_word
    SHRL    CONST TAGSHIFT,Rebx
    jz      raise_div_ex
    SHRL    CONST TAGSHIFT,Reax
    MOVL    CONST 0,Redx
    div     Rebx
CALLMACRO   MAKETAGGED  Redx,Reax
    MOVL    Reax,Redx
    MOVL    Reax,Rebx
    ret
CALLMACRO   RegMask mod_word,(M_Reax OR M_Rebx OR M_Redx)

raise_div_ex:
IFDEF WINDOWS
    jmp     dword ptr [RaiseDiv+Rebp]
ELSE
    jmp     *RaiseDiv[Rebp]
ENDIF

CALLMACRO   INLINE_ROUTINE  word_eq
    CMPL    Rebx,Reax
    jz      RetTrue         ;# True if they are equal.
    jmp     RetFalse
CALLMACRO   RegMask word_eq,(M_Reax)

CALLMACRO   INLINE_ROUTINE  word_neq
    CMPL    Rebx,Reax
    jz      RetFalse
    jmp     RetTrue
CALLMACRO   RegMask word_neq,(M_Reax)

CALLMACRO   INLINE_ROUTINE  word_geq
    CMPL    Rebx,Reax
    jnb     RetTrue
    jmp     RetFalse
CALLMACRO   RegMask word_geq,(M_Reax)

CALLMACRO   INLINE_ROUTINE  word_leq
    CMPL    Rebx,Reax
    jna     RetTrue
    jmp     RetFalse
CALLMACRO   RegMask word_leq,(M_Reax)

CALLMACRO   INLINE_ROUTINE  word_gtr
    CMPL    Rebx,Reax
    ja      RetTrue
    jmp     RetFalse
CALLMACRO   RegMask word_gtr,(M_Reax)

CALLMACRO   INLINE_ROUTINE  word_lss
    CMPL    Rebx,Reax
    jb      RetTrue
    jmp     RetFalse
CALLMACRO   RegMask word_lss,(M_Reax)

;# Atomically increment the value at the address of the arg and return the
;# updated value.  Since the xadd instruction returns the original value
;# we have to increment it.
CALLMACRO   INLINE_ROUTINE  atomic_increment
    MOVL	CONST 2,Rebx
	LOCKXADDL Rebx,[Reax]
	ADDL	CONST 2,Rebx
	MOVL	Rebx,Reax
	ret

CALLMACRO   RegMask atomic_incr,(M_Reax OR M_Rebx)

;# Atomically decrement the value at the address of the arg and return the
;# updated value.  Since the xadd instruction returns the original value
;# we have to decrement it.
CALLMACRO   INLINE_ROUTINE  atomic_decrement
    MOVL	CONST -2,Rebx
	LOCKXADDL Rebx,[Reax]
	MOVL	Rebx,Reax
	SUBL	CONST 2,Reax
	ret

CALLMACRO   RegMask atomic_decr,(M_Reax OR M_Rebx)

;# Return the thread id object for the current thread
CALLMACRO   INLINE_ROUTINE  thread_self
	MOVL    ThreadId[Rebp],Reax
	ret
CALLMACRO   RegMask thread_self,(M_Reax)



;# FLOATING POINT
;# If we have insufficient space for the result we call in to
;# main RTS to do the work.


mem_for_real:
;# Allocate memory for the result.
IFNDEF HOSTARCHITECTURE_X86_64
        MOVL    LocalMpointer[Rebp],Recx
	    SUBL    CONST 12,Recx        ;# Length word (4 bytes) + 8 bytes
ELSE
        MOVL    R15,Recx
	    SUBL    CONST 16,Recx        ;# Length word (8 bytes) + 8 bytes
ENDIF
IFDEF TEST_ALLOC
;# Test case - this will always force a call into RTS.
        CMPL    LocalMpointer[Rebp],Recx
ELSE
        CMPL    LocalMbottom[Rebp],Recx
ENDIF
        jb      mem_for_real1
IFNDEF HOSTARCHITECTURE_X86_64
        MOVL    Recx,LocalMpointer[Rebp] ;# Updated allocation pointer
IFDEF WINDOWS
        mov     dword ptr (-4)[Recx],01000002h  ;# Length word:
ELSE
        MOVL    CONST 0x01000002,(-4)[Recx]		;# Two words plus tag
ENDIF
ELSE
        MOVL    Recx,R15                        ;# Updated allocation pointer
	MOVL    CONST 1,(-8)[Recx]		;# One word
	MOVB    CONST B_bytes,(-1)[Recx]	;# Set the byte flag.
ENDIF
        ret
mem_for_real1:  ;# Not enough store: clobber bad value in ecx.
        MOVL   CONST 1,Recx
	ret


CALLMACRO INLINE_ROUTINE real_add
        call    mem_for_real
	jb      real_add_1     ;# Not enough space - call RTS.
;# Do the operation and put the result in the allocated
;# space.
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FADD    qword ptr [Rebx]
	FSTP    qword ptr [Recx]
ELSE
	FLDL    [Reax]
	FADDL   [Rebx]
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_add_1:
	CALLMACRO   CALL_IO    POLY_SYS_Add_real
;# The mask includes FP7 rather than FP0 because this pushes a value which
;# overwrites the bottom of the stack.
CALLMACRO   RegMask real_add,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)



CALLMACRO INLINE_ROUTINE real_sub
        call    mem_for_real
	jb      real_sub_1     ;# Not enough space - call RTS.
;# Do the operation and put the result in the allocated
;# space.
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FSUB    qword ptr [Rebx]
	FSTP    qword ptr [Recx]
ELSE
	FLDL    [Reax]
	FSUBL   [Rebx]
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_sub_1:
	CALLMACRO   CALL_IO    POLY_SYS_Sub_real

CALLMACRO   RegMask real_sub,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)


CALLMACRO INLINE_ROUTINE real_mul
        call    mem_for_real
	jb      real_mul_1     ;# Not enough space - call RTS.
;# Do the operation and put the result in the allocated
;# space.
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FMUL    qword ptr [Rebx]
	FSTP    qword ptr [Recx]
ELSE
	FLDL    [Reax]
	FMULL   [Rebx]
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_mul_1:
	CALLMACRO   CALL_IO    POLY_SYS_Mul_real

CALLMACRO   RegMask real_mul,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)


CALLMACRO INLINE_ROUTINE real_div
        call    mem_for_real
	jb      real_div_1     ;# Not enough space - call RTS.
;# Do the operation and put the result in the allocated
;# space.
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FDIV    qword ptr [Rebx]
	FSTP    qword ptr [Recx]
ELSE
	FLDL    [Reax]
	FDIVL   [Rebx]
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_div_1:
	CALLMACRO   CALL_IO    POLY_SYS_Div_real

CALLMACRO   RegMask real_div,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)


CALLMACRO INLINE_ROUTINE real_neg
        call    mem_for_real
	jb      real_neg_1     ;# Not enough space - call RTS.
;# Do the operation and put the result in the allocated
;# space.
;# N.B. Real.~ X is not the same as 0.0 - X.  Real.~ 0.0 is ~0.0;
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FCHS
	FSTP    qword ptr [Recx]
ELSE
	FLDL    [Reax]
	FCHS
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_neg_1:
	CALLMACRO   CALL_IO    POLY_SYS_Neg_real

CALLMACRO   RegMask real_neg,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)



CALLMACRO INLINE_ROUTINE real_eq
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FCOMP   qword ptr [Rebx]
ELSE
	FLDL    [Reax]
	FCOMPL  [Rebx]
ENDIF
        FNSTSW  R_ax
;# Not all 64-bit processors support SAHF.
;# The result is true if the zero flag is set and parity flag clear.  
        ANDL    CONST 17408,Reax ;# 0x4400
        CMPL    CONST 16384,Reax ;# 0x4000
	je      RetTrue
	jmp     RetFalse
CALLMACRO   RegMask real_eq,(M_Reax OR M_FP7)


CALLMACRO INLINE_ROUTINE real_neq
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FCOMP   qword ptr [Rebx]
ELSE
	FLDL    [Reax]
	FCOMPL  [Rebx]
ENDIF
        FNSTSW  R_ax
        ANDL    CONST 17408,Reax ;# 0x4400
        CMPL    CONST 16384,Reax ;# 0x4000
	jne     RetTrue
	jmp     RetFalse

CALLMACRO   RegMask real_neq,(M_Reax OR M_FP7)


CALLMACRO INLINE_ROUTINE real_lss
;# Compare Rebx > Reax
IFDEF WINDOWS
	FLD     qword ptr [Rebx]
	FCOMP   qword ptr [Reax]
ELSE
	FLDL    [Rebx]
	FCOMPL  [Reax]
ENDIF
        FNSTSW  R_ax

;# True if the carry flag (C0), zero flag (C3) and parity (C2) are all clear
        ANDL    CONST 17664,Reax ;# 0x4500

	je      RetTrue
	jmp     RetFalse

CALLMACRO   RegMask real_lss,(M_Reax OR M_FP7)


CALLMACRO INLINE_ROUTINE real_gtr
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FCOMP   qword ptr [Rebx]
ELSE
	FLDL    [Reax]
	FCOMPL  [Rebx]
ENDIF
        FNSTSW  R_ax

;# True if the carry flag (C0), zero flag (C3) and parity (C2) are all clear
        ANDL    CONST 17664,Reax ;# 0x4500

	je      RetTrue
	jmp     RetFalse

CALLMACRO   RegMask real_gtr,(M_Reax OR M_FP7)


CALLMACRO INLINE_ROUTINE real_leq
;# Compare Rebx > Reax
IFDEF WINDOWS
	FLD     qword ptr [Rebx]
	FCOMP   qword ptr [Reax]
ELSE
	FLDL    [Rebx]
	FCOMPL  [Reax]
ENDIF
        FNSTSW  R_ax
;# True if the carry flag (C0) and parity (C2) are both clear
        ANDL    CONST 1280,Reax ;# 0x500

	je      RetTrue
	jmp     RetFalse

CALLMACRO   RegMask real_leq,(M_Reax OR M_FP7)


CALLMACRO INLINE_ROUTINE real_geq
IFDEF WINDOWS
	FLD     qword ptr [Reax]
	FCOMP   qword ptr [Rebx]
ELSE
	FLDL    [Reax]
	FCOMPL  [Rebx]
ENDIF
        FNSTSW  R_ax
;# True if the carry flag (C0) and parity (C2) are both clear
        ANDL    CONST 1280,Reax ;# 0x500

	je      RetTrue
	jmp     RetFalse

CALLMACRO   RegMask real_geq,(M_Reax OR M_FP7)

CALLMACRO INLINE_ROUTINE real_from_int
    TESTL   CONST TAG,Reax   ;# Is it long ?
    jz      real_float_1
    call    mem_for_real
	jb      real_float_1     ;# Not enough space - call RTS.
    SARL    CONST TAGSHIFT,Reax ;# Untag the value
	MOVL    Reax,RealTemp[Rebp]	;# Save it in a temporary (N.B. It's now untagged)
IFDEF WINDOWS
	FILD    dword ptr RealTemp[Rebp]
	FSTP    qword ptr [Recx]
ELSE
IFDEF HOSTARCHITECTURE_X86_64
    FILDQ   RealTemp[Rebp]
ELSE
	FILDL   RealTemp[Rebp]
ENDIF
	FSTPL   [Recx]
ENDIF
	MOVL    Recx,Reax
	ret

real_float_1:
	CALLMACRO   CALL_IO    POLY_SYS_int_to_real

CALLMACRO   RegMask real_from_int,(M_Reax OR M_Recx OR M_Redx OR M_FP7 OR Mask_all)


;# Register mask vector. - extern int registerMaskVector[];
;# Each entry in this vector is a set of the registers modified
;# by the function.  It is an untagged bitmap with the registers
;# encoded in the same way as the 
IFDEF WINDOWS
    align   4
    PUBLIC  registerMaskVector
registerMaskVector  dd  Mask_all                ;# 0 is unused
ELSE
        GLOBAL EXTNAME(registerMaskVector)
EXTNAME(registerMaskVector):
#define dd  .long
    dd  Mask_all                ;# 0 is unused
ENDIF
    dd  Mask_all                 ;# 1
    dd  Mask_all                 ;# 2
    dd  Mask_all                 ;# 3 is unused
    dd  Mask_all                 ;# 4 is unused
    dd  Mask_all                 ;# 5 is unused
    dd  Mask_all                 ;# 6
    dd  Mask_all                 ;# 7 is unused
    dd  Mask_all                 ;# 8 is unused
    dd  Mask_all                 ;# 9
    dd  Mask_all                 ;# 10 is unused
    dd  Mask_alloc_store         ;# 11
    dd  Mask_alloc_uninit        ;# 12
    dd  Mask_all                 ;# return = 13
    dd  Mask_all                 ;# raisex = 14
    dd  Mask_get_length          ;# 15
    dd  Mask_all                 ;# 16 is unused
    dd  Mask_all                 ;# 17
    dd  Mask_all                 ;# 18 is no longer used
    dd  Mask_all                 ;# 19 is no longer used
    dd  Mask_all                 ;# 20 is no longer used
    dd  Mask_all                 ;# 21 is unused
    dd  Mask_all                 ;# 22 is unused
    dd  Mask_str_compare         ;# 23
    dd  Mask_teststreq           ;# 24
    dd  Mask_teststrneq          ;# 25
    dd  Mask_teststrgtr          ;# 26
    dd  Mask_teststrlss          ;# 27
    dd  Mask_teststrgeq          ;# 28
    dd  Mask_teststrleq          ;# 29
    dd  Mask_all                 ;# 30
    dd  Mask_all                 ;# 31 is no longer used
    dd  Mask_all                 ;# 32 is no longer used
    dd  Mask_all                 ;# 33 is no longer used
    dd  Mask_all                 ;# 34 is no longer used
    dd  Mask_all                 ;# 35 is no longer used
    dd  Mask_all                 ;# 36 is no longer used
    dd  Mask_all                 ;# 37 is unused
    dd  Mask_all                 ;# 38 is unused
    dd  Mask_all                 ;# 39 is unused
    dd  Mask_all                 ;# 40
    dd  Mask_all                 ;# 41 is unused
    dd  Mask_all                 ;# 42
    dd  Mask_all                 ;# 43
    dd  Mask_all                 ;# 44 is no longer used
    dd  Mask_all                 ;# 45 is no longer used
    dd  Mask_all                 ;# 46
    dd  Mask_lockseg             ;# 47
    dd  Mask_all                 ;# nullorzero = 48
    dd  Mask_all                 ;# 49 is no longer used
    dd  Mask_all                 ;# 50 is no longer used
    dd  Mask_all                 ;# 51
    dd  Mask_all                 ;# 52
    dd  Mask_all                 ;# 53 is unused
    dd  Mask_all                 ;# 54 is unused
    dd  Mask_all                 ;# version_number = 55
    dd  Mask_all                 ;# 56 is unused
    dd  Mask_all                 ;# 57 is unused
    dd  Mask_all                 ;# 58 is unused
    dd  Mask_all                 ;# 59 is unused
    dd  Mask_all                 ;# 60 is unused
    dd  Mask_all                 ;# 61
    dd  Mask_all                 ;# 62
    dd  Mask_all                 ;# 63 is unused
    dd  Mask_all                 ;# 64 is unused
    dd  Mask_all                 ;# 65 is unused
    dd  Mask_all                 ;# 66 is unused
    dd  Mask_all                 ;# 67 is unused
    dd  Mask_all                 ;# 68 is unused
    dd  Mask_all                 ;# 69 is unused
    dd  Mask_atomic_incr         ;# 70
    dd  Mask_atomic_decr         ;# 71
    dd  Mask_thread_self         ;# 72
    dd  Mask_all                 ;# 73
    dd  Mask_all                 ;# 74 is unused
    dd  Mask_all                 ;# 75 is unused
    dd  Mask_all                 ;# 76 is unused
    dd  Mask_all                 ;# 77 is unused
    dd  Mask_all                 ;# 78 is unused
    dd  Mask_all                 ;# 79 is unused
    dd  Mask_all                 ;# Mask_version_number_1 = 80
    dd  Mask_all                 ;# 81 is now unused
    dd  Mask_all                 ;# 82 is now unused
    dd  Mask_all                 ;# 83 is now unused
    dd  Mask_all                 ;# 84
    dd  Mask_all                 ;# 85 is now unused
    dd  Mask_all                 ;# 86 is now unused
    dd  Mask_all                 ;# 87 is now unused
    dd  Mask_all                 ;# 88
    dd  Mask_all                 ;# 89 is unused
    dd  Mask_all                 ;# 90 is unused
    dd  Mask_all                 ;# 91 is unused
    dd  Mask_all                 ;# 92
    dd  Mask_all                 ;# 93
    dd  Mask_all                 ;# 94
    dd  Mask_all                 ;# 95 is unused
    dd  Mask_all                 ;# 96 is unused
    dd  Mask_all                 ;# 97 is unused
    dd  Mask_all                 ;# 98
    dd  Mask_all                 ;# 99
    dd  Mask_all                 ;# 100
    dd  Mask_all                 ;# 101 is unused
    dd  Mask_all                 ;# 102 is unused
    dd  Mask_all                 ;# 103
    dd  Mask_quotrem             ;# 104
    dd  Mask_is_short            ;# 105
    dd  Mask_aplus               ;# 106
    dd  Mask_aminus              ;# 107
    dd  Mask_amul                ;# 108
    dd  Mask_adiv                ;# 109
    dd  Mask_amod                ;# 110
    dd  Mask_aneg                ;# 111
    dd  Mask_xora                ;# 112
    dd  Mask_equala              ;# 113
    dd  Mask_ora                 ;# 114
    dd  Mask_anda                ;# 115
    dd  Mask_all                 ;# version_number_3 = 116
    dd  Mask_all                 ;# 117
    dd  Mask_real_geq            ;# 118
    dd  Mask_real_leq            ;# 119
    dd  Mask_real_gtr            ;# 120
    dd  Mask_real_lss            ;# 121
    dd  Mask_real_eq             ;# 122
    dd  Mask_real_neq            ;# 123
    dd  Mask_all                 ;# 124
    dd  Mask_real_add            ;# 125
    dd  Mask_real_sub            ;# 126
    dd  Mask_real_mul            ;# 127
    dd  Mask_real_div            ;# 128
    dd  Mask_all                 ;# 129 is unused
    dd  Mask_real_neg            ;# 130
    dd  Mask_all                 ;# 131 is unused
    dd  Mask_all                 ;# 132
    dd  Mask_all                 ;# 133
    dd  Mask_all                 ;# 134
    dd  Mask_real_from_int       ;# 135
    dd  Mask_all                 ;# 136
    dd  Mask_all                 ;# 137
    dd  Mask_all                 ;# 138
    dd  Mask_all                 ;# 139
    dd  Mask_all                 ;# 140
    dd  Mask_all                 ;# 141
    dd  Mask_all                 ;# 142 is no longer used
    dd  Mask_all                 ;# 143 is unused
    dd  Mask_all                 ;# 144 is unused
    dd  Mask_all                 ;# 145 is unused
    dd  Mask_all                 ;# 146 is unused
    dd  Mask_all                 ;# 147 is unused
    dd  Mask_all                 ;# stdin = 148
    dd  Mask_all                 ;# stdout= 149
    dd  Mask_all                 ;# 150
    dd  Mask_set_string_length   ;# 151
    dd  Mask_get_first_long_word ;# 152
    dd  Mask_all                 ;# 153 is unused
    dd  Mask_all                 ;# 154 is unused
    dd  Mask_all                 ;# 155 is unused
    dd  Mask_all                 ;# 156 is unused
    dd  Mask_all                 ;# 157 is unused
    dd  Mask_all                 ;# 158 is unused
    dd  Mask_all                 ;# 159 is unused
    dd  Mask_all                 ;# 160 is unused
    dd  Mask_all                 ;# 161 is unused
    dd  Mask_all                 ;# 162 is unused
    dd  Mask_all                 ;# 163 is unused
    dd  Mask_all                 ;# 164 is unused
    dd  Mask_all                 ;# 165 is unused
    dd  Mask_all                 ;# 166 is unused
    dd  Mask_all                 ;# 167 is unused
    dd  Mask_all                 ;# 168 is unused
    dd  Mask_all                 ;# 169 is unused
    dd  Mask_all                 ;# 170 is unused
    dd  Mask_all                 ;# 171 is unused
    dd  Mask_all                 ;# 172 is unused
    dd  Mask_all                 ;# 173 is unused
    dd  Mask_all                 ;# 174 is unused
    dd  Mask_all                 ;# 175 is unused
    dd  Mask_all                 ;# 176 is unused
    dd  Mask_all                 ;# 177 is unused
    dd  Mask_all                 ;# 178 is unused
    dd  Mask_all                 ;# 179 is unused
    dd  Mask_all                 ;# 180 is unused
    dd  Mask_all                 ;# 181 is unused
    dd  Mask_all                 ;# 182 is unused
    dd  Mask_all                 ;# 183 is unused
    dd  Mask_all                 ;# 184 is unused
    dd  Mask_all                 ;# 185 is unused
    dd  Mask_all                 ;# 186 is unused
    dd  Mask_all                 ;# 187 is unused
    dd  Mask_all                 ;# 188 is unused
    dd  Mask_all                 ;# 189
    dd  Mask_all                 ;# 190 is unused
    dd  Mask_all                 ;# 191 is no longer used
    dd  Mask_all                 ;# 192 is unused
    dd  Mask_all                 ;# 193 is unused
    dd  Mask_all                 ;# 194
    dd  Mask_move_words          ;# 195
    dd  Mask_shift_right_arith_word  ;# 196
    dd  Mask_int_to_word         ;# 197
    dd  Mask_move_bytes          ;# 198
    dd  Mask_all                 ;# 199 now unused
    dd  Mask_all                 ;# 200
    dd  Mask_all                 ;# 201
    dd  Mask_all                 ;# stderr = 202
    dd  Mask_all                 ;# 203 now unused
    dd  Mask_all                 ;# 204
    dd  Mask_all                 ;# 205
    dd  Mask_all                 ;# 206
    dd  Mask_all                 ;# 207 is unused
    dd  Mask_all                 ;# 208 now unused
    dd  Mask_all                 ;# 209
    dd  Mask_all                 ;# 210 is unused
    dd  Mask_all                 ;# 211 is unused
    dd  Mask_all                 ;# 212 is unused
    dd  Mask_is_big_endian       ;# 213
    dd  Mask_bytes_per_word      ;# 214
    dd  Mask_offset_address      ;# 215
    dd  Mask_shift_right_word    ;# 216
    dd  Mask_word_neq            ;# 217
    dd  Mask_not_bool            ;# 218
    dd  Mask_all                 ;# 219 is unused
    dd  Mask_all                 ;# 220 is unused
    dd  Mask_all                 ;# 221 is unused
    dd  Mask_all                 ;# 222 is unused
    dd  Mask_string_length       ;# 223
    dd  Mask_all                 ;# 224 is unused
    dd  Mask_all                 ;# 225 is unused
    dd  Mask_all                 ;# 226 is unused
    dd  Mask_all                 ;# 227 is unused
    dd  Mask_all                 ;# 228 is unused
    dd  Mask_int_eq              ;# 229
    dd  Mask_int_neq             ;# 230
    dd  Mask_int_geq             ;# 231
    dd  Mask_int_leq             ;# 232
    dd  Mask_int_gtr             ;# 233
    dd  Mask_int_lss             ;# 234
    dd  Mask_all                 ;# 235
    dd  Mask_all                 ;# 236 is unused
    dd  Mask_all                 ;# 237 is unused
    dd  Mask_mul_word            ;# 238
    dd  Mask_plus_word           ;# 239
    dd  Mask_minus_word          ;# 240
    dd  Mask_div_word            ;# 241
    dd  Mask_or_word             ;# 242
    dd  Mask_and_word            ;# 243
    dd  Mask_xor_word            ;# 244
    dd  Mask_shift_left_word     ;# 245
    dd  Mask_mod_word            ;# 246
    dd  Mask_word_geq            ;# 247
    dd  Mask_word_leq            ;# 248
    dd  Mask_word_gtr            ;# 249
    dd  Mask_word_lss            ;# 250
    dd  Mask_word_eq             ;# 251
    dd  Mask_load_byte           ;# 252
    dd  Mask_load_word           ;# 253
    dd  Mask_assign_byte         ;# 254
    dd  Mask_assign_word         ;# 255

IFDEF HOSTARCHITECTURE_X86_64


ENDIF
END
