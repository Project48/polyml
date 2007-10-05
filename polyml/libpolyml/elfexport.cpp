/*
    Title:     Write out a database as an ELF object file
    Author:    David Matthews.

    Copyright (c) 2006-7 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR H PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x)
#endif

// If we haven't got elf.h we shouldn't be building this.
#include <elf.h>

// Solaris seems to put processor-specific constants in separate files
#ifdef HAVE_SYS_ELF_SPARC_H
#include <sys/elf_SPARC.h>
#endif
#ifdef HAVE_SYS_ELF_386_H
#include <sys/elf_386.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#include "globals.h"

#include "diagnostics.h"
#include "sys.h"
#include "machine_dep.h"
#include "gc.h"
#include "mpoly.h"
#include "scanaddrs.h"
#include "elfexport.h"
#include "run_time.h"
#include "version.h"
#include "polystring.h"

#define sym_last_local_sym sym_data_section

// The first two symbols are special:
// Zero is always special in ELF
// 1 is used for the data section
#define EXTRA_SYMBOLS   2
static unsigned AreaToSym(unsigned area) { return area+EXTRA_SYMBOLS; }

// Section table entries
enum {
    sect_initial = 0,
    sect_sectionnametable,
    sect_stringtable,
    sect_data,
    sect_relocation,
    sect_symtab,
    sect_Num_Sections
};

// Generate the address relative to the start of the segment.
void ELFExport::setRelocationAddress(void *p, ElfXX_Addr *reloc)
{
    unsigned area = findArea(p);
    POLYUNSIGNED offset = (char*)p - (char*)memTable[area].mtAddr;
    // We have to add in the sizes of the areas preceding this one in the file.
    for (unsigned i = 0; i < area; i++)
        offset += memTable[i].mtLength;
    *reloc = offset;
}

/* Get the index corresponding to an address. */
PolyWord ELFExport::createRelocation(PolyWord p, void *relocAddr)
{
    void *addr = p.AsAddress();
    unsigned addrArea = findArea(addr);
    POLYUNSIGNED offset = (char*)addr - (char*)memTable[addrArea].mtAddr;

    if (useRela)
    {
        ElfXX_Rela reloc;
        // Set the offset within the section we're scanning.
        setRelocationAddress(relocAddr, &reloc.r_offset);
        reloc.r_info = ELFXX_R_INFO(AreaToSym(addrArea), directReloc);
        reloc.r_addend = offset;
        fwrite(&reloc, sizeof(reloc), 1, exportFile);
        relocationCount++;
        return PolyWord::FromUnsigned(0);
    }
    else {
        ElfXX_Rel reloc;
        setRelocationAddress(relocAddr, &reloc.r_offset);
        reloc.r_info = ELFXX_R_INFO(AreaToSym(addrArea), directReloc);
        fwrite(&reloc, sizeof(reloc), 1, exportFile);
        relocationCount++;
        return PolyWord::FromUnsigned(offset);
    }
}

/* This is called for each constant within the code. 
   Print a relocation entry for the word and return a value that means
   that the offset is saved in original word. */
void ELFExport::ScanConstant(byte *addr, ScanRelocationKind code)
{
    PolyWord p = GetConstantValue(addr, code);

    if (IS_INT(p) || p == PolyWord::FromUnsigned(0))
        return;

    void *a = p.AsAddress();
    unsigned aArea = findArea(a);

    // We don't need a relocation if this is relative to the current segment
    // since the relative address will already be right.
    if (code == PROCESS_RELOC_I386RELATIVE && aArea == findArea(addr))
        return;

    // Set the value at the address to the offset relative to the symbol.
    POLYUNSIGNED offset = (char*)a - (char*)memTable[aArea].mtAddr;

    switch (code)
    {
    case PROCESS_RELOC_DIRECT: // 32 or 64 bit address of target
        {
            ElfXX_Rel reloc;
            setRelocationAddress(addr, &reloc.r_offset);
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), directReloc);
            for (unsigned i = 0; i < sizeof(PolyWord); i++)
            {
                addr[i] = (byte)(offset & 0xff);
                offset >>= 8;
            }
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;
        }
        break;
#if(defined(HOSTARCHITECTURE_X86) || defined(HOSTARCHITECTURE_X86_64))
     case PROCESS_RELOC_I386RELATIVE:         // 32 or 64 bit relative address
        {
            ElfXX_Rel reloc;
            setRelocationAddress(addr, &reloc.r_offset);
             // We seem to need to subtract 4 bytes to get the correct offset in ELF
            offset -= 4;
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), R_386_PC32);
            for (unsigned i = 0; i < sizeof(PolyWord); i++)
            {
                addr[i] = (byte)(offset & 0xff);
                offset >>= 8;
            }
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;
        }
        break;
#endif
#ifdef HOSTARCHITECTURE_PPC
    case PROCESS_RELOC_PPCDUAL16SIGNED:       // Power PC - two consecutive words
    case PROCESS_RELOC_PPCDUAL16UNSIGNED:
        {
            ElfXX_Rela reloc;
            setRelocationAddress(addr+2 /* actual bytes to be updated */, &reloc.r_offset);
            // We need two relocations here.
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea),
                    code == PROCESS_RELOC_PPCDUAL16SIGNED ? R_PPC_ADDR16_HA : R_PPC_ADDR16_HI);
            reloc.r_addend = offset;
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;

            setRelocationAddress(addr+sizeof(PolyWord)+2, &reloc.r_offset);
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), R_PPC_ADDR16_LO);
            reloc.r_addend = offset;
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;
            // Set the constant values to zero.  This doesn't seem to be necessary
            // on the PPC but can't do any harm.
            POLYUNSIGNED *caddr = (POLYUNSIGNED *)addr;
            caddr[0] = caddr[0] & 0xffff0000;
            caddr[1] = caddr[1] & 0xffff0000;
        }
        break;
#endif
#ifdef HOSTARCHITECTURE_SPARC
    case PROCESS_RELOC_SPARCDUAL: // Sparc - two consecutive words
        {
            ElfXX_Rela reloc;
            setRelocationAddress(addr, &reloc.r_offset);
            // We need two relocations here.
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), R_SPARC_HI22);
            reloc.r_addend = offset;
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;

            setRelocationAddress(addr+sizeof(PolyWord), &reloc.r_offset);
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), R_SPARC_LO10);
            reloc.r_addend = offset;
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;
            POLYUNSIGNED *caddr = (POLYUNSIGNED *)addr;
            caddr[0] = caddr[0] & 0xffc00000;
            caddr[1] = caddr[1] & 0xfffff000;
        }
        break;
    case PROCESS_RELOC_SPARCRELATIVE: // Sparc 30-bit relative address
        {
            ElfXX_Rela reloc;
            setRelocationAddress(addr, &reloc.r_offset);
            reloc.r_info = ELFXX_R_INFO(AreaToSym(aArea), R_SPARC_WDISP30);
            reloc.r_addend = offset;
            fwrite(&reloc, sizeof(reloc), 1, exportFile);
            relocationCount++;
            POLYUNSIGNED *caddr = (POLYUNSIGNED *)addr;
            caddr[0] = caddr[0] & 0xc0000000;
        }
        break;
#endif
        default:
            ASSERT(0); // Wrong type of relocation for this architecture.
    }
}

unsigned long ELFExport::makeStringTableEntry(const char *str, ExportStringTable *stab)
{
    if (str == NULL || str[0] == 0)
        return 0; // First entry is the null string.
    else
        return stab->makeEntry(str);
}

void ELFExport::writeSymbol(const char *symbolName, long value, long size, int binding, int sttype, int section)
{
    ElfXX_Sym symbol;
    memset(&symbol, 0, sizeof(symbol)); // Zero unused fields
    symbol.st_name = makeStringTableEntry(symbolName, &symStrings);
    symbol.st_value = value;
    symbol.st_size = size;
    symbol.st_info = ELFXX_ST_INFO(binding, sttype);
    symbol.st_other = 0;
    symbol.st_shndx = section;
    fwrite(&symbol, sizeof(symbol), 1, exportFile);
    symbolCount++;
}

// Set the file alignment.
void ELFExport::alignFile(int align)
{
    char pad[32]; // Maximum alignment
    int offset = ftell(exportFile);
    memset(pad, 0, sizeof(pad));
    if ((offset % align) == 0) return;
    fwrite(&pad, align - (offset % align), 1, exportFile);
}

void ELFExport::createStructsRelocation(unsigned sym, POLYUNSIGNED offset, POLYSIGNED addend)
{
    if (useRela)
    {
        ElfXX_Rela reloc;
        reloc.r_info = ELFXX_R_INFO(sym, directReloc);
        reloc.r_offset = offset;
        reloc.r_addend = addend;
        fwrite(&reloc, sizeof(reloc), 1, exportFile);
        relocationCount++;
    }
    else
    {
        ElfXX_Rel reloc;
        reloc.r_info = ELFXX_R_INFO(sym, directReloc);
        reloc.r_offset = offset;
        fwrite(&reloc, sizeof(reloc), 1, exportFile);
        relocationCount++;
    }
}

void ELFExport::exportStore(void)
{
    PolyWord    *p;
    ElfXX_Ehdr fhdr;
    ElfXX_Shdr sections[sect_Num_Sections];
    unsigned i;

    // Both the string tables have an initial null entry.
    symStrings.makeEntry("");
    sectionStrings.makeEntry("");

    // Write out initial values for the headers.  These are overwritten at the end.
    // File header
    memset(&fhdr, 0, sizeof(fhdr));
    fhdr.e_ident[EI_MAG0] = 0x7f;
    fhdr.e_ident[EI_MAG1] = 'E';
    fhdr.e_ident[EI_MAG2] = 'L';
    fhdr.e_ident[EI_MAG3] = 'F';
    fhdr.e_ident[EI_CLASS] = ELFCLASSXX; // ELFCLASS32 or ELFCLASS64
    fhdr.e_ident[EI_VERSION] = EV_CURRENT;
    {
        union { unsigned long wrd; char chrs[sizeof(unsigned long)]; } endian;
        endian.wrd = 1;
        if (endian.chrs[0] == 0)
            fhdr.e_ident[EI_DATA] = ELFDATA2MSB; // Big endian
        else
            fhdr.e_ident[EI_DATA] = ELFDATA2LSB; // Little endian
    }
    fhdr.e_type = ET_REL;
    // The machine needs to match the machine we're compiling for
    // even if this is actually portable code.
#if defined(HOSTARCHITECTURE_X86)
    fhdr.e_machine = EM_386;
    directReloc = R_386_32;
    useRela = false;
#elif defined(HOSTARCHITECTURE_PPC)
    fhdr.e_machine = EM_PPC;
    directReloc = R_PPC_ADDR32;
    useRela = true;
#elif defined(HOSTARCHITECTURE_SPARC)
    fhdr.e_machine = EM_SPARC;
    directReloc = R_SPARC_32;
    useRela = true;
    /* Sparc/Solaris, at least 2.8, requires ELF32_Rela relocations.  For some reason,
       though, it adds the value in the location being relocated (as with ELF32_Rel
       relocations) as well as the addend. To be safe, whenever we use an ELF32_Rela
       relocation we always zero the location to be relocated. */
#elif defined(HOSTARCHITECTURE_X86_64)
    fhdr.e_machine = EM_X86_64;
    directReloc = R_X86_64_64;
    useRela = false;
#elif defined(HOSTARCHITECTURE_ARM)
#ifndef EF_ARM_EABI_VER4 
#define EF_ARM_EABI_VER4     0x04000000
#endif
    // When linking ARM binaries the linker checks the ABI version.  We
    // need to set the version to the same as the libraries. 
    // GCC currently uses version 4.
    fhdr.e_machine = EM_ARM;
    directReloc = R_ARM_ABS32;
    useRela = false;
    fhdr.e_flags = EF_ARM_EABI_VER4;
#else
#error "No support for exporting on this architecture"
#endif
    fhdr.e_version = EV_CURRENT;
    fhdr.e_shoff = sizeof(fhdr); // Offset to section header - immediately follows
    fhdr.e_ehsize = sizeof(fhdr);
    fhdr.e_shentsize = sizeof(ElfXX_Shdr);
    fhdr.e_shnum = sect_Num_Sections;
    fhdr.e_shstrndx = sect_sectionnametable; // Section name table section index;
    fwrite(&fhdr, sizeof(fhdr), 1, exportFile); // Write it for the moment.

    // Set up the section header but don't write it yet.
    memset(sections, 0, sizeof(sections));
    // Section 0 - all zeros
    sections[sect_initial].sh_type = SHT_NULL;
    sections[sect_initial].sh_link = SHN_UNDEF;

    // Section name table.
    sections[sect_sectionnametable].sh_name = makeStringTableEntry(".shstrtab", &sectionStrings);
    sections[sect_sectionnametable].sh_type = SHT_STRTAB;
    sections[sect_sectionnametable].sh_addralign = sizeof(char);
    // sections[sect_sectionnametable].sh_offset is set later
    // sections[sect_sectionnametable].sh_size is set later

    // Symbol name table.
    sections[sect_stringtable].sh_name = makeStringTableEntry(".strtab", &sectionStrings);
    sections[sect_stringtable].sh_type = SHT_STRTAB;
    sections[sect_stringtable].sh_addralign = sizeof(char);
    // sections[sect_stringtable].sh_offset is set later
    // sections[sect_stringtable].sh_size is set later

    // Main data section
    // TODO: Replace this with separate sections for each of the memory areas.
    // At the very least we want to distinguish mutable and immutable areas.
    sections[sect_data].sh_name = makeStringTableEntry(".poly", &sectionStrings);
    sections[sect_data].sh_type = SHT_PROGBITS;
    sections[sect_data].sh_flags = SHF_WRITE | SHF_ALLOC | SHF_EXECINSTR;
    sections[sect_data].sh_addralign = 8; // 8-byte alignment
    // sections[sect_data].sh_size is set later
    // sections[sect_data].sh_offset is set later.
    // sections[sect_data].sh_size is set later.

    // Relocation section
    sections[sect_relocation].sh_name = makeStringTableEntry(useRela ? ".rela.poly" : ".rel.poly", &sectionStrings);
    sections[sect_relocation].sh_type = useRela ? SHT_RELA : SHT_REL; // Contains relocation with/out explicit addends (ElfXX_Rel)
    sections[sect_relocation].sh_link = sect_symtab; // Index to symbol table
    sections[sect_relocation].sh_info = sect_data; // Applies to data section
    sections[sect_relocation].sh_addralign = sizeof(long); // Align to a word
    sections[sect_relocation].sh_entsize = useRela ? sizeof(ElfXX_Rela) : sizeof(ElfXX_Rel);
    // sections[sect_relocation].sh_offset is set later.
    // sections[sect_relocation].sh_size is set later.

    // Symbol table.
    sections[sect_symtab].sh_name = makeStringTableEntry(".symtab", &sectionStrings);
    sections[sect_symtab].sh_type = SHT_SYMTAB;
    sections[sect_symtab].sh_link = sect_stringtable; // String table to use
    sections[sect_symtab].sh_addralign = sizeof(long); // Align to a word
    sections[sect_symtab].sh_entsize = sizeof(ElfXX_Sym);
    // sections[sect_symtab].sh_info is set later
    // sections[sect_symtab].sh_size is set later
    // sections[sect_symtab].sh_offset is set later

    // First the symbol table.
    alignFile(sections[sect_symtab].sh_addralign);
    sections[sect_symtab].sh_offset = ftell(exportFile);
    writeSymbol("", 0, 0, 0, 0, 0); // Initial symbol
    // Write the local symbols first.
    writeSymbol("", 0, 0, STB_LOCAL, STT_SECTION, sect_data); // .data section
    POLYUNSIGNED areaSpace = 0;

    // Create symbols for the address areas.  AreaToSym assumes these come first.
    for (i = 0; i < memTableEntries; i++)
    {
        if (i == ioMemEntry)
            writeSymbol("ioarea", areaSpace, 0, STB_LOCAL, STT_OBJECT, sect_data);
        else {
            char buff[50];
            sprintf(buff, "area%1u", i);
            writeSymbol(buff, areaSpace, 0, STB_LOCAL, STT_OBJECT, sect_data);
        }
        areaSpace += memTable[i].mtLength;
    }

    // Extra symbols to help debugging.
    areaSpace = 0;
    for (i = 0; i < memTableEntries; i++)
    {
        if (i != ioMemEntry)
        {
            char buff[50];
            // Write the names of the functions as local symbols.  This isn't necessary
            // but it makes debugging easier since the function names appear in gdb.
            char *start = (char*)memTable[i].mtAddr;
            char *end = start + memTable[i].mtLength;
            for (p = (PolyWord*)start; p < (PolyWord*)end; )
            {
                p++;
                PolyObject *obj = (PolyObject*)p;
                POLYUNSIGNED length = obj->Length();
                if (length != 0 && obj->IsCodeObject())
                {
                    PolyWord *name = obj->ConstPtrForCode();
                    // Copy as much of the name as will fit and ignore any extra.
                    // Do we need to worry about duplicates?
                    (void)Poly_string_to_C(*name, buff, sizeof(buff));
                    writeSymbol(buff, areaSpace + ((char*)p - start), 0, STB_LOCAL, STT_OBJECT, sect_data);
                }
                p += length;
            }
        }
        areaSpace += memTable[i].mtLength;
    }

    // Global symbols - Just one: poly_exports
    writeSymbol("poly_exports", areaSpace, 
        sizeof(exportDescription)+sizeof(memoryTableEntry)*memTableEntries,
        STB_GLOBAL, STT_OBJECT, sect_data);

    sections[sect_symtab].sh_info = symbolCount-1; // One more than last local sym
    sections[sect_symtab].sh_size = sizeof(ElfXX_Sym) * symbolCount;

    // Write the relocations.
    alignFile(sections[sect_relocation].sh_addralign);
    sections[sect_relocation].sh_offset = ftell(exportFile);
    relocationCount = 0;

    for (i = 0; i < memTableEntries; i++)
    {
        if (i != ioMemEntry) // Don't relocate the IO area
        {
            // Create the relocation table and turn all addresses into offsets.
            char *start = (char*)memTable[i].mtAddr;
            char *end = start + memTable[i].mtLength;
            for (p = (PolyWord*)start; p < (PolyWord*)end; )
            {
                p++;
                PolyObject *obj = (PolyObject*)p;
                POLYUNSIGNED length = obj->Length();
                relocateObject(obj);
                if (length != 0 && obj->IsCodeObject())
                    machineDependent->ScanConstantsWithinCode(obj, this);
                p += length;
            }
        }
    }

    // Relocations for "exports" and "memTable";
    // TODO: This won't be needed if we put these in a separate section.
    areaSpace = 0;
    for (i = 0; i < memTableEntries; i++)
        areaSpace += memTable[i].mtLength;

    // Address of "memTable" within "exports". We can't use createRelocation because
    // the position of the relocation is not in either the mutable or the immutable area.
    POLYSIGNED memTableOffset = (POLYSIGNED)sizeof(exportDescription); // It follows immediately after this.
    createStructsRelocation(symbolCount-1 /* Last symbol */, areaSpace+offsetof(exportDescription, memTable), memTableOffset);

    // Address of "rootFunction" within "exports"
    unsigned rootAddrArea = findArea(rootFunction);
    POLYSIGNED rootOffset = (char*)rootFunction - (char*)memTable[rootAddrArea].mtAddr;
    createStructsRelocation(AreaToSym(rootAddrArea), areaSpace+offsetof(exportDescription, rootFunction), rootOffset);

    // Addresses of the areas within memtable.
    for (i = 0; i < memTableEntries; i++)
    {
        createStructsRelocation(AreaToSym(i),
            areaSpace + sizeof(exportDescription) + i * sizeof(memoryTableEntry) + offsetof(memoryTableEntry, mtAddr),
            0 /* No offset relative to base symbol*/);
    }

    if (useRela)
        sections[sect_relocation].sh_size = relocationCount * sizeof(ElfXX_Rela);
    else
        sections[sect_relocation].sh_size = relocationCount * sizeof(ElfXX_Rel);

    // Now the binary data.
    alignFile(sections[sect_data].sh_addralign);
    sections[sect_data].sh_offset = ftell(exportFile);
    sections[sect_data].sh_size =
        areaSpace + sizeof(exportDescription) + memTableEntries*sizeof(memoryTableEntry);

    // Now the binary data.
    for (i = 0; i < memTableEntries; i++)
    {
        fwrite(memTable[i].mtAddr, 1, memTable[i].mtLength, exportFile);
    }

    exportDescription exports;
    memset(&exports, 0, sizeof(exports));
    memset(memTable, 0, sizeof(memTable));
    exports.structLength = sizeof(exportDescription);
    exports.memTableSize = sizeof(memoryTableEntry);
    exports.memTableEntries = memTableEntries;
    exports.ioIndex = 0; // The io entry is the first in the memory table
    exports.memTable = useRela ? 0 : (memoryTableEntry *)memTableOffset;
    // Set the value to be the offset relative to the base of the area.  We have set a relocation
    // already which will add the base of the area.
    exports.rootFunction = useRela ? 0 : (void*)rootOffset;
    exports.timeStamp = time(NULL);
    exports.ioSpacing = ioSpacing;
    exports.architecture = machineDependent->MachineArchitecture();
    exports.rtsVersion = POLY_version_number;

    // Set the address values to zero before we write.  They will always
    // be relative to their base symbol.
    for (i = 0; i < memTableEntries; i++)
        memTable[i].mtAddr = 0;

    fwrite(&exports, sizeof(exports), 1, exportFile);
    fwrite(memTable, sizeof(memoryTableEntry), memTableEntries, exportFile);

    // The section name table
    sections[sect_sectionnametable].sh_offset = ftell(exportFile);
    fwrite(sectionStrings.strings, sectionStrings.stringSize, 1, exportFile);
    sections[sect_sectionnametable].sh_size = sectionStrings.stringSize;

    // The symbol name table
    sections[sect_stringtable].sh_offset = ftell(exportFile);
    fwrite(symStrings.strings, symStrings.stringSize, 1, exportFile);
    sections[sect_stringtable].sh_size = symStrings.stringSize;

    // Finally the section headers.
    alignFile(4);
    fhdr.e_shoff = ftell(exportFile);
    fwrite(sections, sizeof(sections), 1, exportFile);

    // Rewind to rewrite the file header with the offset of the section headers.
    rewind(exportFile);
    fwrite(&fhdr, sizeof(fhdr), 1, exportFile);
    fclose(exportFile); exportFile = NULL;
}
