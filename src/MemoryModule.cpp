/*
 * Memory DLL loading code
 * Version 0.0.4
 *
 * Copyright (c) 2004-2015 by Joachim Bauch / mail@joachim-bauch.de
 * http://www.joachim-bauch.de
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 2.0 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 *  Changes have been made for:
 *
/*  -== ExeLoader ==-
 *
 *  Load .exe / .dll from memory and remap functions
 *  Run your binaries on any x86 hardware
 *
 *  @autors
 *   - Maeiky
 *   - Sebastien FAVIER
 *  
 * Copyright (c) 2020 - V·Liance / SPinti-Software. All rights reserved.
 *
 * The contents of this file are subject to the Apache License Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * If a copy of the Apache License Version 2.0 was not distributed with this file,
 * You can obtain one at https://www.apache.org/licenses/LICENSE-2.0.html
 */
 
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "MemoryModule.h"

//#define Use_HeapAlloc
// #include "malloc.h" 
 
#ifdef CpcDos
#define CustomLoader
#else
//#define OnWin //TODO
#endif
// Temp
#define CustomLoader
 #define OnWin //temp
  #define  DEBUG_OUTPUT
 
#include <iostream>

#include "FuncTable.h"


#if _MSC_VER
// Disable warning about data -> function pointer conversion
#pragma warning(disable:4055)
#endif

#ifndef IMAGE_SIZEOF_BASE_RELOCATION
// Vista SDKs no longer define IMAGE_SIZEOF_BASE_RELOCATION!?
#define IMAGE_SIZEOF_BASE_RELOCATION (sizeof(IMAGE_BASE_RELOCATION))
#endif

// #include "ManagedAlloc.h"


char aOrdinalFunc[MAX_ORDINAL_FUNC][4] = {0}; //SpecialCharOrdinal"%" Ordinal"FFFF" "\n" := char
int	  aOrdinalFunc_size = 0;



typedef BOOL (WINAPI *DllEntryProc)(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved);
typedef int (WINAPI *ExeEntryProc)(void);
 
typedef struct {
	LPVOID address;
	LPVOID alignedAddress;
	DWORD size;
	DWORD characteristics;
	BOOL last;
} SECTIONFINALIZEDATA, *PSECTIONFINALIZEDATA;

#define GET_HEADER_DICTIONARY(module, idx)  &(module)->headers->OptionalHeader.DataDirectory[idx]
#define ALIGN_DOWN(address, alignment)      (LPVOID)((uintptr_t)(address) & ~((alignment) - 1))
#define ALIGN_VALUE_UP(value, alignment)    (((value) + (alignment) - 1) & ~((alignment) - 1))

#ifdef DEBUG_OUTPUT
static void
OutputLastError(const char *msg) {
	LPVOID tmp;
	char *tmpmsg;
	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&tmp, 0, NULL);
	tmpmsg = (char *)LocalAlloc(LPTR, strlen(msg) + strlen((char*)tmp) + 3);
	sprintf(tmpmsg, "%s: %s", msg, tmp);
	printf("\n%s: ", tmpmsg);
	LocalFree(tmpmsg);
	LocalFree(tmp);
}
#endif

static BOOL
CheckSize(size_t size, size_t expected) {
	if (size < expected) {
		#ifdef ImWin
		SetLastError(ERROR_INVALID_DATA);
		#endif
		return FALSE;
	}
	return TRUE;
}

static BOOL CopySections(const unsigned char *data, size_t size, PIMAGE_NT_HEADERS old_headers, PMEMORYMODULE module, ManagedAlloc& AllocManager) 
{
	int i, section_size;
	unsigned char *codeBase = module->codeBase;
	unsigned char *dest;
	module->section_text = 0;
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(module->headers);
	for (i=0; i < module->headers->FileHeader.NumberOfSections; i++, section++) {
		if (section->SizeOfRawData == 0) {
			// section doesn't contain data in the dll itself, but may define
			// uninitialized data
			section_size = old_headers->OptionalHeader.SectionAlignment;
			if (section_size > 0) {
				dest = (unsigned char *)module->alloc(codeBase + section->VirtualAddress,
					section_size,
					MEM_COMMIT,
					PAGE_READWRITE,
					module->userdata, (ManagedAlloc&) AllocManager);
				if (dest == NULL) {
					return FALSE;
				}

				// Always use position from file to support alignments smaller
				// than page size.
				dest = codeBase + section->VirtualAddress;
				section->Misc.PhysicalAddress = (DWORD) (uintptr_t) dest;
				memset(dest, 0, section_size);
			}

			// section is empty
			continue;
		}

		if (!CheckSize(size, section->PointerToRawData + section->SizeOfRawData)) {
			return FALSE;
		}


printf("\n codeBase: %p VirtualAddress: %p, both: %p ",codeBase, section->VirtualAddress, codeBase + section->VirtualAddress);
		// commit memory block and copy data from dll
		
		unsigned char *test = 0;
		dest = (unsigned char *)module->alloc(codeBase + section->VirtualAddress,
							section->SizeOfRawData,
							MEM_COMMIT,
							PAGE_READWRITE,
							module->userdata, (ManagedAlloc&) AllocManager);
		if (dest == NULL) {
			return FALSE;
		}

		// Always use position from file to support alignments smaller
		// than page size.
		dest = codeBase + section->VirtualAddress;
		memcpy(dest, data + section->PointerToRawData, section->SizeOfRawData);
		section->Misc.PhysicalAddress = (DWORD) (uintptr_t) dest;
		
		if( strcmp( (char*)section->Name, ".text") == 0){
			module->section_text = dest;
		}
		printf("\n-----COPY section [%s]: dest[0x%p], src[0x%p], size[%d]", section->Name , dest,  data + section->PointerToRawData, section->SizeOfRawData );
	}
	return TRUE;
}

// Protection flags for memory pages (Executable, Readable, Writeable)
static int ProtectionFlags[2][2][2] = {
	{
		// not executable
		{PAGE_NOACCESS, PAGE_WRITECOPY},
		{PAGE_READONLY, PAGE_READWRITE},
	}, {
		// executable
		{PAGE_EXECUTE, PAGE_EXECUTE_WRITECOPY},
		{PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE},
	},
};
 
static DWORD
GetRealSectionSize(PMEMORYMODULE module, PIMAGE_SECTION_HEADER section) {
	DWORD size = section->SizeOfRawData;
	if (size == 0) {
		if (section->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) {
			size = module->headers->OptionalHeader.SizeOfInitializedData;
		} else if (section->Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) {
			size = module->headers->OptionalHeader.SizeOfUninitializedData;
		}
	}
	return size;
}

static BOOL
FinalizeSection(PMEMORYMODULE module, PSECTIONFINALIZEDATA sectionData, ManagedAlloc& AllocManager) {
	DWORD protect, oldProtect;
	BOOL executable;
	BOOL readable;
	BOOL writeable;

	if (sectionData->size == 0) {
		return TRUE;
	}

	if (sectionData->characteristics & IMAGE_SCN_MEM_DISCARDABLE) {
		// section is not needed any more and can safely be freed
		if (sectionData->address == sectionData->alignedAddress &&
			(sectionData->last ||
			 module->headers->OptionalHeader.SectionAlignment == module->pageSize ||
			 (sectionData->size % module->pageSize) == 0)
		   ) {
			// Only allowed to decommit whole pages
			module->free_(sectionData->address, sectionData->size, MEM_DECOMMIT, module->userdata, (ManagedAlloc&) AllocManager);
		}
		return TRUE;
	}

	// determine protection flags based on characteristics
	executable = (sectionData->characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
	readable =   (sectionData->characteristics & IMAGE_SCN_MEM_READ) != 0;
	writeable =  (sectionData->characteristics & IMAGE_SCN_MEM_WRITE) != 0;
	protect = ProtectionFlags[executable][readable][writeable];
	if (sectionData->characteristics & IMAGE_SCN_MEM_NOT_CACHED) {
		protect |= PAGE_NOCACHE;
	}

	#ifdef OnWin
			// change memory access flags
			if (VirtualProtect(sectionData->address, sectionData->size, protect, &oldProtect) == 0) {
		#ifdef DEBUG_OUTPUT
				OutputLastError("Error protecting memory page");
		#endif
				return FALSE;
			}
	#endif

	return TRUE;
}

static BOOL
FinalizeSections(PMEMORYMODULE module, ManagedAlloc &AllocManager) {
	int i;
	PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(module->headers);
#ifdef _WIN64
	uintptr_t imageOffset = (module->headers->OptionalHeader.ImageBase & 0xffffffff00000000);
#else
	#define imageOffset 0
#endif
	SECTIONFINALIZEDATA sectionData;
	sectionData.address = (LPVOID)((uintptr_t)section->Misc.PhysicalAddress | imageOffset);
	sectionData.alignedAddress = ALIGN_DOWN(sectionData.address, module->pageSize);
	sectionData.size = GetRealSectionSize(module, section);
	sectionData.characteristics = section->Characteristics;
	sectionData.last = FALSE;
	section++;

	// loop through all sections and change access flags
	for (i=1; i < module->headers->FileHeader.NumberOfSections; i++, section++) {
		LPVOID sectionAddress = (LPVOID)((uintptr_t)section->Misc.PhysicalAddress | imageOffset);
		LPVOID alignedAddress = ALIGN_DOWN(sectionAddress, module->pageSize);
		DWORD sectionSize = GetRealSectionSize(module, section);
		// Combine access flags of all sections that share a page
		// TODO(fancycode): We currently share flags of a trailing large section
		//   with the page of a first small section. This should be optimized.
		if (sectionData.alignedAddress == alignedAddress || (uintptr_t) sectionData.address + sectionData.size > (uintptr_t) alignedAddress) {
			// Section shares page with previous
			if ((section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0 || (sectionData.characteristics & IMAGE_SCN_MEM_DISCARDABLE) == 0) {
				sectionData.characteristics = (sectionData.characteristics | section->Characteristics) & ~IMAGE_SCN_MEM_DISCARDABLE;
			} else {
				sectionData.characteristics |= section->Characteristics;
			}
			sectionData.size = (((uintptr_t)sectionAddress) + sectionSize) - (uintptr_t) sectionData.address;
			continue;
		}

		if (!FinalizeSection(module, &sectionData, (ManagedAlloc&) AllocManager)) {
			return FALSE;
		}
		sectionData.address = sectionAddress;
		sectionData.alignedAddress = alignedAddress;
		sectionData.size = sectionSize;
		sectionData.characteristics = section->Characteristics;
	}
	sectionData.last = TRUE;
	if (!FinalizeSection(module, &sectionData, (ManagedAlloc&) AllocManager)) {
		return FALSE;
	}
#ifndef _WIN64
#undef imageOffset
#endif
	return TRUE;
}

static BOOL ExecuteTLS(PMEMORYMODULE module) 
{
	unsigned char *codeBase = module->codeBase;
	PIMAGE_TLS_DIRECTORY tls;
	PIMAGE_TLS_CALLBACK* callback;

	PIMAGE_DATA_DIRECTORY directory = GET_HEADER_DICTIONARY(module, IMAGE_DIRECTORY_ENTRY_TLS);
	if (directory->VirtualAddress == 0) {
		return TRUE;
	}

	tls = (PIMAGE_TLS_DIRECTORY) (codeBase + directory->VirtualAddress);
	callback = (PIMAGE_TLS_CALLBACK *) tls->AddressOfCallBacks;
	if (callback) {
		while (*callback) {
			(*callback)((LPVOID) codeBase, DLL_PROCESS_ATTACH, NULL);
			callback++;
		}
	}
	return TRUE;
}
  
static BOOL PerformBaseRelocation(PMEMORYMODULE module, ptrdiff_t delta) 
{

	unsigned char *codeBase = module->codeBase;
	PIMAGE_BASE_RELOCATION relocation;

	PIMAGE_DATA_DIRECTORY directory = GET_HEADER_DICTIONARY(module, IMAGE_DIRECTORY_ENTRY_BASERELOC);
	if (directory->Size == 0) {
		return (delta == 0);
	}

	relocation = (PIMAGE_BASE_RELOCATION) (codeBase + directory->VirtualAddress);
	printf("\nPerform Relocation of %d VirtualAddress", relocation->VirtualAddress );
//return false; //Temp

	for (; relocation->VirtualAddress > 0; ) {
		DWORD i;
		unsigned char *dest = codeBase + relocation->VirtualAddress;
		unsigned short *relInfo = (unsigned short *)((unsigned char *)relocation + IMAGE_SIZEOF_BASE_RELOCATION);
		for (i=0; i < ((relocation->SizeOfBlock - IMAGE_SIZEOF_BASE_RELOCATION) / 2); i++, relInfo++) {
			DWORD *patchAddrHL;
#ifdef _WIN64
			ULONGLONG *patchAddr64;
#endif
			int type, offset;

			// the upper 4 bits define the type of relocation
			type = *relInfo >> 12;
			// the lower 12 bits define the offset
			offset = *relInfo & 0xfff;

			switch (type) {
			case IMAGE_REL_BASED_ABSOLUTE:
				// skip relocation
				break;

			case IMAGE_REL_BASED_HIGHLOW:
				// change complete 32 bit address
				patchAddrHL = (DWORD *) (dest + offset);
				*patchAddrHL += (DWORD) delta;
				break;

#ifdef _WIN64
			case IMAGE_REL_BASED_DIR64:
				patchAddr64 = (ULONGLONG *) (dest + offset);
				*patchAddr64 += (ULONGLONG) delta;
				break;
#endif

			default:
				// printf("Unknown relocation: %d\n", type);
				break;
			}
		}

		// advance to next relocation block
		relocation = (PIMAGE_BASE_RELOCATION) (((char *) relocation) + relocation->SizeOfBlock);
	}
	return TRUE;
}

static BOOL BuildImportTable(PMEMORYMODULE module, ManagedAlloc &AllocManager /* Recuperer l'instance */) 
{
	unsigned char *codeBase = module->codeBase;
	PIMAGE_IMPORT_DESCRIPTOR importDesc;
	BOOL result = TRUE;

	PIMAGE_DATA_DIRECTORY directory = GET_HEADER_DICTIONARY(module, IMAGE_DIRECTORY_ENTRY_IMPORT);
	if (directory->Size == 0) {
		return TRUE;
	}

	importDesc = (PIMAGE_IMPORT_DESCRIPTOR) (codeBase + directory->VirtualAddress);


#ifdef OnWin
	for (; !IsBadReadPtr(importDesc, sizeof(IMAGE_IMPORT_DESCRIPTOR)) && importDesc->Name; importDesc++) {
#else
	for (; importDesc->Name; importDesc++) {
#endif

		uintptr_t *thunkRef;
		FARPROC *funcRef;
		HCUSTOMMODULE *tmp;


		HCUSTOMMODULE handle = module->loadLibrary((LPCSTR) (codeBase + importDesc->Name), module->userdata, (ManagedAlloc&) AllocManager);
		if (handle == NULL) {
			#ifdef ImWin
			SetLastError(ERROR_MOD_NOT_FOUND);
			#endif
			result = FALSE;
			break;
		}
  
printf("\n New LIB[%p]: %s", handle, (LPCSTR) (codeBase + importDesc->Name));
  
  
  
	   // tmp = (HCUSTOMMODULE *) realloc(module->modules, (module->numModules+1)*(sizeof(HCUSTOMMODULE)));
	   _EXE_LOADER_DEBUG(1, "\n Module No.%d de %d octets", "\n Module Nb.%d of %d bytes", (module->numModules+1), (module->numModules+1)*(sizeof(HCUSTOMMODULE)));
	   
	   /* Corrige via FreeLibrary */
		tmp = (HCUSTOMMODULE *) AllocManager.ManagedCalloc((module->numModules+1),(sizeof(HCUSTOMMODULE)));
		// tmp = (HCUSTOMMODULE *) malloc((module->numModules+1)*(sizeof(HCUSTOMMODULE)));
		if (tmp == 0) {
			module->freeLibrary(handle, module->userdata);
			#ifdef ImWin
			SetLastError(ERROR_OUTOFMEMORY);
			#endif
			result = FALSE;
			break;
		}
		module->modules = tmp;

		module->modules[module->numModules++] = handle;
		if (importDesc->OriginalFirstThunk) {
			thunkRef = (uintptr_t *) (codeBase + importDesc->OriginalFirstThunk);
			funcRef = (FARPROC *) (codeBase + importDesc->FirstThunk);
		} else {
			// no hint table
			thunkRef = (uintptr_t *) (codeBase + importDesc->FirstThunk);
			funcRef = (FARPROC *) (codeBase + importDesc->FirstThunk);
		}
		for (; *thunkRef; thunkRef++, funcRef++) {
			if (IMAGE_SNAP_BY_ORDINAL(*thunkRef)) {
				//exetern FARPROC MyMemoryDefaultGetProcAddress(HCUSTOMMODULE module, LPCSTR name, void *userdata);
				//IMAGE_ORDINAL is DWORD
				
				//https://stackoverflow.com/questions/41792848/possible-to-hook-iat-function-by-ordinal
				//showinf("TODO Ordinal func %d ", (DWORD)IMAGE_ORDINAL(*thunkRef));
				char* _sOrdinal = aOrdinalFunc[aOrdinalFunc_size];aOrdinalFunc_size++;
				_sOrdinal[0] = '#';
				short _ordinal = (short)IMAGE_ORDINAL(*thunkRef);
				if(_ordinal > 9999){
					showinf("Error, No support for Ordinal > 9999","");
					*funcRef = (FARPROC)aDummyFunc[0].dFunc;
				}else{
					sprintf(&_sOrdinal[1], "%d", _ordinal);
					*funcRef = module->getProcAddress(handle, (LPCSTR)_sOrdinal, module->userdata);
				}

			} else {
				PIMAGE_IMPORT_BY_NAME thunkData = (PIMAGE_IMPORT_BY_NAME) (codeBase + (*thunkRef));
				//exetern FARPROC MyMemoryDefaultGetProcAddress(HCUSTOMMODULE module, LPCSTR name, void *userdata);
				*funcRef = module->getProcAddress(handle, (LPCSTR)&thunkData->Name, module->userdata);
			}
			if (*funcRef == 0) {
				/* Correction memory LEAKCHECKER 19 MARS 2020*/
				// if(tmp != NULL) free(tmp);
				
				module->freeLibrary(handle, module->userdata);
				
				result = FALSE;

				break;
			}
		}
		
		/* Correction memory LEAKCHECKER 19 MARS 2020*/
		// if(tmp != NULL) free(tmp);
		if (!result) {
			
			module->freeLibrary(handle, module->userdata);
			#ifdef ImWin
			SetLastError(ERROR_PROC_NOT_FOUND);
			#endif
			break;
		}
		module->freeLibrary(handle, module->userdata);
	}
	
	return result;
}

////////////////////////////////////////////////////////////////////////


	LPVOID MyMemoryDefaultAlloc(LPVOID address, SIZE_T size, DWORD allocationType, DWORD protect, void* userdata, ManagedAlloc &AllocManager) 
	{
		UNREFERENCED_PARAMETER(userdata);
		
		#ifdef VirtualLoadPE
			//https://github.com/biesigrr/pe-loader/blob/master/pe-loader/pe-loader.cpp
			printf("\nVirtual Allocation!: 0x%p  size: %d, allocationType: 0x%p, protect: 0x%p", address, size, allocationType, protect);
			
			if(allocationType == MEM_RESERVE | MEM_COMMIT){
				//(Process in 2 Step) => if it's already reserved we can commit without error
				LPVOID _ret =  VirtualAlloc(address, size, MEM_RESERVE, protect);
				allocationType = MEM_COMMIT;
			}
			
			LPVOID _ret =  VirtualAlloc(address, size, allocationType, protect);
			if(_ret == 0){
				OutputLastError("\nVirtual Allocation ERROR");
			}
			return _ret;
		#else
		// return VirtualAlloc(address, size, allocationType, protect);
		// return calloc(1, size);
		return AllocManager.ManagedCalloc(1, size); //Must initialised to zero
		#endif
	}

	BOOL MyMemoryDefaultFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType, void* userdata, ManagedAlloc& AllocManager)  
	{
		UNREFERENCED_PARAMETER(userdata);
		
		#ifdef VirtualLoadPE
			return VirtualFree(lpAddress, dwSize, dwFreeType);
		#else
		 //   return VirtualFree(lpAddress, dwSize, dwFreeType);
			// free(lpAddress);
			AllocManager.ManagedFree(lpAddress);
			return true;
		#endif
	}

	bool fMainExeLoader(const char* _sPath);

	HCUSTOMMODULE MyMemoryDefaultLoadLibrary(LPCSTR filename, void *userdata, ManagedAlloc &AllocManager) 
	{
		//result = LoadLibraryA(filename);
		//return (HCUSTOMMODULE) result;
	
		HMODULE result;
		UNREFERENCED_PARAMETER(userdata);

		_EXE_LOADER_DEBUG(1, "\n------ [DLL] Import:%s \n", "\n------ [DLL] Import:%s \n",   filename);

		MEMORYMODULE* _oModule =  (MEMORYMODULE*)AllocManager.ManagedCalloc(1, sizeof(MEMORYMODULE));
		_oModule->isDLL = true;
		_oModule->codeBase = (unsigned char*)filename;

		return (HCUSTOMMODULE*)_oModule;
	}

	FARPROC MyMemoryDefaultGetProcAddress(HCUSTOMMODULE module, LPCSTR name, void *userdata) {
		//return (FARPROC) GetProcAddress((HMODULE) module, name);
		UNREFERENCED_PARAMETER(userdata);

		MEMORYMODULE* _oDll = (MEMORYMODULE*)module;
		char* sDllName = (char*)"";

		if (_oDll != 0) {
		   sDllName =  (char*)_oDll->codeBase;
		}

		unsigned int _nSize = sizeof(aTableFunc) /  sizeof(sFunc);
		for (unsigned int i=0; i < _nSize; i++) {
			if (strcmp(name, aTableFunc[i].sFuncName) == 0) {
				if(aTableFunc[i].sLib[0] == 0){ 
					//Any Lib
					{
						_EXE_LOADER_DEBUG(5, "Trouve %s: --> %s [CHARGE]", "Found %s: --> %s [LOADED]",  sDllName, name);
						return (FARPROC)aTableFunc[i].dFunc;
					}
				}else{ 
					//Lib is specified
					if (strcmp(sDllName, aTableFunc[i].sLib) == 0) {
						_EXE_LOADER_DEBUG(5, "Trouve %s: --> %s [CHARGE]", "Found %s: --> %s [LOADED]",  sDllName, name);
						return (FARPROC)aTableFunc[i].dFunc;
					}
				}
			}
		}

		static unsigned int current = 0;
		current++;

		_EXE_LOADER_DEBUG(3, "\nAvertissement, %s:  ---------   %s  ", "\nWarning, %s:  ---------   %s ",  sDllName, name);
		
		aDummyFunc[current].Who = name;
		aDummyFunc[current].DLL = sDllName;

		if (current >=  sizeof(aDummyFunc) / sizeof( aDummyFunc[0] )) {current = 0;}

	   return (FARPROC)aDummyFunc[current].dFunc;
	}

	void MyMemoryDefaultFreeLibrary(HCUSTOMMODULE module, void *userdata)
	{
		UNREFERENCED_PARAMETER(userdata);
		// AllocManager.  //TODO
		// FreeLibrary((HMODULE) module);
	}

///////////////////////////////////////////////////////////////////////////


HMEMORYMODULE MemoryModule::MemoryLoadLibrary(const void *data, size_t size) {
		
	return MemoryLoadLibraryEx(data, size, MyMemoryDefaultAlloc, MyMemoryDefaultFree, MyMemoryDefaultLoadLibrary, MyMemoryDefaultGetProcAddress, MyMemoryDefaultFreeLibrary, NULL);
}
 
void MemoryModule::Fin_instance()
{
	fprintf(stdout, "Fin_instance() BEGIN \n");
	instance_AllocManager.ManagedAlloc_clean();
	fprintf(stdout, "Fin_instance() END \n");
}


void memVirtualQueryInfo(LPCVOID* _adr, int _size){

	MEMORY_BASIC_INFORMATION meminf;
	LPCVOID* _end =	_adr + (_size>>2);
	
	printf("\n--========= VirtualQuery(adr: %p, size: [%.2fM, 0x%p], end: 0x%p):", _adr, (_size/1024./1024.), _size,  _end);
	
	while (VirtualQuery(_adr, &meminf,  sizeof(MEMORY_BASIC_INFORMATION)) != 0){
		
		//static const char* eState = {"MEM_COMMIT", "MEM_RESERVE", , "MEM_FREE"};>>3
		
		printf("\n----------");
		printf("\nmemeinf->BaseAddress: %p",	 		meminf.BaseAddress);	
		printf("\nmemeinf->AllocationBase: %p",	 		meminf.AllocationBase);
		
		//// PAGE PROTECT ////
		char* eProt = (char*)"UNKNOW";
		switch(meminf.AllocationProtect){
			case PAGE_EXECUTE:
				eProt = (char*)"PAGE_EXECUTE";break;
			case PAGE_EXECUTE_READ:
				eProt = (char*)"PAGE_EXECUTE_READ";break;
			case PAGE_EXECUTE_READWRITE:
				eProt = (char*)"PAGE_EXECUTE_READWRITE";break;
			case PAGE_EXECUTE_WRITECOPY:
				eProt = (char*)"PAGE_EXECUTE_WRITECOPY";break;
			case PAGE_NOACCESS:
				eProt = (char*)"PAGE_NOACCESS";break;
			case PAGE_READONLY:
				eProt = (char*)"PAGE_READONLY";break;
			case PAGE_READWRITE:
				eProt = (char*)"PAGE_READWRITE";break;
			case PAGE_WRITECOPY:
				eProt = (char*)"PAGE_WRITECOPY";break;	
		}
		printf("\nmemeinf->AllocationProtect: %s [%p]",	 		eProt,	meminf.AllocationProtect);
		printf("\nmemeinf->RegionSize: [%.2fM, 0x%p]",	 			meminf.RegionSize/1024./1024., meminf.RegionSize);
		
		//// State ////
		char* eState = (char*)"UNKNOW";
		switch(meminf.State){
			case MEM_COMMIT:
				eState = (char*)"MEM_COMMIT";break;
			case MEM_RESERVE:
				eState = (char*)"MEM_RESERVE";break;
			case MEM_COMMIT | MEM_RESERVE:
				eState = (char*)"MEM_COMMIT | MEM_RESERVE";break;
			case MEM_FREE:
				eState = (char*)"MEM_FREE";break;
		}
		printf("\nmemeinf->State: %s [%p]",	 			eState,	meminf.State);
		
		//// PAGE PROTECT ////
		char* ePProt = (char*)"UNKNOW";
		switch(meminf.Protect){
			case PAGE_NOACCESS:
				ePProt = (char*)"PAGE_NOACCESS";break;
			case PAGE_READONLY:
				ePProt = (char*)"PAGE_READONLY";break;
			case PAGE_READWRITE:
				ePProt = (char*)"PAGE_READWRITE";break;
			case PAGE_WRITECOPY:
				ePProt = (char*)"PAGE_WRITECOPY";break;
		}
		printf("\nmemeinf->Protect: %s [%p]",	 		ePProt,	meminf.Protect);
		
		//// TYPE ////
		char* eType = (char*)"UNKNOW";
		switch(meminf.Type){
			case MEM_IMAGE:
				eType = (char*)"MEM_IMAGE";break;
			case MEM_MAPPED:
				eType = (char*)"MEM_MAPPED";break;
			case MEM_PRIVATE:
				eType = (char*)"MEM_PRIVATE";break;
		}
		printf("\nmemeinf->Type: %s [%p]",	 			eType,	meminf.Type);
		//////////////
		printf("\n----------");
		
		/*
		//if(VirtualFree(meminf.BaseAddress,0 , MEM_DECOMMIT)){
		if(VirtualFree(meminf.BaseAddress,0 , MEM_RELEASE)){
			printf("\nFreed Successfully");
		}else{
			OutputLastError("VirtualFree Errror: ");
		}*/
		
		_adr += (meminf.RegionSize>>2);
		if(_adr >= _end){
			break;
		}
	}
}




HMEMORYMODULE MemoryModule::MemoryLoadLibraryEx(const void *data, size_t size,
	CustomAllocFunc allocMemory,
	CustomFreeFunc freeMemory,
	CustomLoadLibraryFunc loadLibrary,
	CustomGetProcAddressFunc getProcAddress,
	CustomFreeLibraryFunc freeLibrary,
	void *userdata) {
	PMEMORYMODULE result = NULL;
	PIMAGE_DOS_HEADER dos_header;
	PIMAGE_NT_HEADERS old_header;
	unsigned char *code, *headers;
	ptrdiff_t locationDelta;
	SYSTEM_INFO sysInfo;
	PIMAGE_SECTION_HEADER section;
	DWORD i;
	size_t optionalSectionSize;
	size_t lastSectionEnd = 0;
	size_t alignedImageSize;

	if (!CheckSize(size, sizeof(IMAGE_DOS_HEADER))) {
		printf("\nWarning, no IMAGE_DOS_HEADER");
		return NULL;
	}
	
	dos_header = (PIMAGE_DOS_HEADER)data;
	if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
		#ifdef ImWin
		SetLastError(ERROR_BAD_EXE_FORMAT);
		#endif
		printf("\nWarning, no IMAGE_DOS_SIGNATURE");
		return NULL;
	}


	if (!CheckSize(size, dos_header->e_lfanew + sizeof(IMAGE_NT_HEADERS))) {
		return NULL;
	}
	old_header = (PIMAGE_NT_HEADERS)&((const unsigned char *)(data))[dos_header->e_lfanew];
	if (old_header->Signature != IMAGE_NT_SIGNATURE) {
		SetLastError(ERROR_BAD_EXE_FORMAT);
		printf("\nWarning, no IMAGE_NT_SIGNATURE");
		return NULL;
	}

#ifdef _WIN64
	if (old_header->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
#else
	if (old_header->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
#endif
		if(old_header->FileHeader.Machine == IMAGE_FILE_MACHINE_I386){
			printf("\nWarning, executable is 32 bit: IMAGE_FILE_MACHINE_I386");
			return NULL;
		}else if (old_header->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64){
			printf("\nWarning, executable is 64 bit: IMAGE_FILE_MACHINE_AMD64");
			return NULL;
		}else{
			SetLastError(ERROR_BAD_EXE_FORMAT);
			printf("\nWarning, no IMAGE_FILE_MACHINE_I386: %p", old_header->FileHeader.Machine);
			return NULL;
		}
	}

	if (old_header->OptionalHeader.SectionAlignment & 1) {
		// Only support section alignments that are a multiple of 2
		SetLastError(ERROR_BAD_EXE_FORMAT);
		printf("\nWarning, Only support section alignments that are a multiple of 2");
		return NULL;
	}

	section = IMAGE_FIRST_SECTION(old_header);
	optionalSectionSize = old_header->OptionalHeader.SectionAlignment;
	for (i=0; i < old_header->FileHeader.NumberOfSections; i++, section++) {
		size_t endOfSection;
		if (section->SizeOfRawData == 0) {
			// Section without data in the DLL
			endOfSection = section->VirtualAddress + optionalSectionSize;
		} else {
			endOfSection = section->VirtualAddress + section->SizeOfRawData;
		}

		if (endOfSection > lastSectionEnd) {
			lastSectionEnd = endOfSection;
		}
	}

	int dwPageSize;

	#ifdef OnWin
		GetNativeSystemInfo(&sysInfo);
		dwPageSize = sysInfo.dwPageSize;
	#else
		dwPageSize = 4096;  // TODO
	#endif

	alignedImageSize = ALIGN_VALUE_UP(old_header->OptionalHeader.SizeOfImage, dwPageSize);
	printf("\nold_header->OptionalHeader.SizeOfImage: %d", old_header->OptionalHeader.SizeOfImage);
	printf("\nalignedImageSize: %d", alignedImageSize);
	printf("\nlastSectionEnd: %d", lastSectionEnd);
	printf("\ndwPageSize: %d", dwPageSize);
	printf("\nALIGN_VALUE_UP_bef: %d", ALIGN_VALUE_UP(old_header->OptionalHeader.SizeOfImage, dwPageSize));
	printf("\nALIGN_VALUE_UP: %d", ALIGN_VALUE_UP(lastSectionEnd, dwPageSize));
	if (alignedImageSize != ALIGN_VALUE_UP(lastSectionEnd, dwPageSize)) {
		SetLastError(ERROR_BAD_EXE_FORMAT);
		printf("\nWarning, alignedImageSize");
	}
	// alignedImageSize=0;

	// reserve memory for image of library
	// XXX: is it correct to commit the complete memory region at once?
	//      calling DllEntry raises an exception if we don't...
	code = (unsigned char *)allocMemory((LPVOID)(old_header->OptionalHeader.ImageBase),
		alignedImageSize,
		MEM_RESERVE | MEM_COMMIT, 
		PAGE_READWRITE,
		userdata, instance_AllocManager);

	if (code == NULL) {

		memVirtualQueryInfo((LPCVOID*)old_header->OptionalHeader.ImageBase, alignedImageSize);
			
		// try to allocate memory at arbitrary position
		code = (unsigned char *)allocMemory(NULL,
			alignedImageSize,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE,
			userdata, instance_AllocManager);
			
		if (code == NULL) {
			SetLastError(ERROR_OUTOFMEMORY);
			printf("\nWarning, OUTOFMEMORY");
			return NULL;
		}else{
			printf("\nWarning, try to allocate memory at arbitrary position: %p", code);
		}
	}

	#ifdef Use_HeapAlloc
		result = (PMEMORYMODULE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MEMORYMODULE));
	#else
		result = (PMEMORYMODULE)instance_AllocManager.ManagedCalloc(1, sizeof(MEMORYMODULE));
		// result = (PMEMORYMODULE)calloc(1, sizeof(MEMORYMODULE));
	#endif

	if (result == NULL) {
		freeMemory(code, 0, MEM_RELEASE, userdata, instance_AllocManager);
		SetLastError(ERROR_OUTOFMEMORY);
		printf("\nWarning, OUTOFMEMORY!");
		return NULL;
	}

printf("\n-+-------------- New codeBase: %p ", (code ) );

	result->codeBase = code;
	result->isDLL = (old_header->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
	result->alloc = allocMemory;
	result->free_ = freeMemory;
	result->loadLibrary = loadLibrary;
	result->getProcAddress = getProcAddress;
	result->freeLibrary = freeLibrary;
	result->userdata = userdata;
	result->pageSize = dwPageSize;

	if (!CheckSize(size, old_header->OptionalHeader.SizeOfHeaders)) {
		printf("\nWarning, !CheckSize");
		goto error;
	}

	// commit memory for headers
	headers = (unsigned char *)allocMemory(code,
		old_header->OptionalHeader.SizeOfHeaders,
		MEM_COMMIT,
		PAGE_READWRITE,
		userdata, instance_AllocManager);

	// copy PE header to code
	memcpy(headers, dos_header, old_header->OptionalHeader.SizeOfHeaders);
	
	result->headers = (PIMAGE_NT_HEADERS)&((const unsigned char *)(headers))[dos_header->e_lfanew];

	// update position
	result->headers->OptionalHeader.ImageBase = (uintptr_t)code;
	

	// copy sections from DLL file block to new memory location
	if (!CopySections((const unsigned char *) data, size, old_header, result, instance_AllocManager)) {
		printf("\nWarning, !CopySections");
		goto error;
	}

	// adjust base address of imported data
	result->isRelocated = FALSE;
	locationDelta = (ptrdiff_t)(result->headers->OptionalHeader.ImageBase - old_header->OptionalHeader.ImageBase);
	if (locationDelta != 0) {
		result->isRelocated = PerformBaseRelocation(result, locationDelta);
	}else{
		result->isRelocated = TRUE; //Not reloacated but already good
	}
	if(!result->isRelocated) {
		printf("\nWarning, ! Not relocated (Probably no .reloc section)");
	}

	// load required dlls and adjust function table of imports
	if (!BuildImportTable(result, instance_AllocManager)) {
		printf("\nWarning, !BuildImportTable");
		goto error;
	}

	// mark memory pages depending on section headers and release
	// sections that are marked as "discardable"
	if (!FinalizeSections(result, instance_AllocManager)) {
		printf("\nWarning, !FinalizeSections");
		goto error;
	}


#ifndef CustomLoader
	// Todo According to Microsoft, Thread Local Storage (TLS) is a mechanism that allows Microsoft Windows to define data objects that are not automatic (stack) variables,
	// yet are "local to each individual thread that runs the code. Thus, each thread can maintain a different value for a variable declared by using TLS."
	// TLS callbacks are executed BEFORE the main loading
	if (!ExecuteTLS(result))
		printf("\nWarning, !ExecuteTLS");
		goto error;
#endif

	// get entry point of loaded library
	if (result->headers->OptionalHeader.AddressOfEntryPoint != 0) {
		if (result->isDLL) {
				// 
			_EXE_LOADER_DEBUG(0, "Format de fichier : Librairie", "File format : Library");
			DllEntryProc DllEntry = (DllEntryProc)(LPVOID)(code + result->headers->OptionalHeader.AddressOfEntryPoint);
			result->dllEntry = DllEntry;
			//if (DllEntry == 0) {return NULL;}
			
			/* TODO: notify library about attaching to process
			BOOL successfull = (*DllEntry)((HINSTANCE)code, DLL_PROCESS_ATTACH, 0);
			if (!successfull) {
				SetLastError(ERROR_DLL_INIT_FAILED);
				goto error;
			}
			*/
			result->initialized = TRUE;
		} else {
			_EXE_LOADER_DEBUG(0, "Format de fichier : Executable Windows", "File format : Windows execuable");
			result->exeEntry = (ExeEntryProc)(LPVOID)(code + result->headers->OptionalHeader.AddressOfEntryPoint);
			// fprintf(stdout, "Address 0x%08x\n", result->exeEntry);
		}
	} else {
		result->exeEntry = NULL;
	}
	
	return (HMEMORYMODULE)result;

error:
	// cleanup
	MemoryFreeLibrary(result);
	printf("\nError!");
	return NULL;
}

FARPROC MemoryModule::MemoryGetProcAddress(HMEMORYMODULE module, LPCSTR name) {
	unsigned char *codeBase = ((PMEMORYMODULE)module)->codeBase;
	DWORD idx = 0;
	PIMAGE_EXPORT_DIRECTORY exports;
	PIMAGE_DATA_DIRECTORY directory = GET_HEADER_DICTIONARY((PMEMORYMODULE)module, IMAGE_DIRECTORY_ENTRY_EXPORT);
	if (directory->Size == 0) {
		// no export table found
		printf("\nWarning, Non export table found");
		SetLastError(ERROR_PROC_NOT_FOUND);
		return NULL;
	}

	exports = (PIMAGE_EXPORT_DIRECTORY) (codeBase + directory->VirtualAddress);
	if (exports->NumberOfNames == 0 || exports->NumberOfFunctions == 0) {
		// DLL doesn't export anything
		SetLastError(ERROR_PROC_NOT_FOUND);
		printf("\nWarning, Nothing exported");
		return NULL;
	}

		/*
		printf("\nTotal export: %d", exports->NumberOfNames);
	if (HIWORD(name) == 0) {//WTF
	
		printf("\nload function by ordinal value");
			
		// load function by ordinal value
		if (LOWORD(name) < exports->Base) {
			#ifdef ImWin
			SetLastError(ERROR_PROC_NOT_FOUND);
			#endif
			printf("\nWarning, Nothing exported?");
			return NULL;
		}

		idx = LOWORD(name) - exports->Base;
	} else {*/
		// search function name in list of exported names
		DWORD i;
		DWORD *nameRef = (DWORD *) (codeBase + exports->AddressOfNames);
		WORD *ordinal = (WORD *) (codeBase + exports->AddressOfNameOrdinals);
		BOOL found = FALSE;
		for (i=0; i < exports->NumberOfNames; i++, nameRef++, ordinal++) {
		
			//printf("\nSearch[%s]: %s", name, (const char *) (codeBase + (*nameRef)));
			if (strcmp(name, (const char *) (codeBase + (*nameRef))) == 0) {
				idx = *ordinal;
				found = TRUE;
				break;
			}
		}

		if (!found) {
			// exported symbol not found
			printf("\nWarning, Not found");
			SetLastError(ERROR_PROC_NOT_FOUND);
			return NULL;
		}
	//}

	if (idx > exports->NumberOfFunctions) {
		// name <-> ordinal number don't match
		SetLastError(ERROR_PROC_NOT_FOUND);
		printf("\nWarning, Not found!");
		return NULL;
	}

	// AddressOfFunctions contains the RVAs to the "real" functions
	return (FARPROC)(LPVOID)(codeBase + (*(DWORD *) (codeBase + exports->AddressOfFunctions + (idx*4))));
}

void MemoryModule::MemoryFreeLibrary(HMEMORYMODULE mod) {
	PMEMORYMODULE module = (PMEMORYMODULE)mod;

	if (module == NULL) {
		return;
	}
	if (module->initialized) {
		// notify library about detaching from process
		DllEntryProc DllEntry = (DllEntryProc)(LPVOID)(module->codeBase + module->headers->OptionalHeader.AddressOfEntryPoint);
		(*DllEntry)((HINSTANCE)module->codeBase, DLL_PROCESS_DETACH, 0);
	}

	if (module->modules != NULL) {
		// free previously opened libraries
		int i;
		for (i=0; i < module->numModules; i++) {
			if (module->modules[i] != NULL) {
				module->freeLibrary(module->modules[i], module->userdata);
			}
		}

		instance_AllocManager.ManagedFree(module->modules);
		// free(module->modules);
	}

	if (module->codeBase != NULL) {
		// release memory of library
		module->free_(module->codeBase, 0, MEM_RELEASE, module->userdata, instance_AllocManager);
	}
	
	if (module->headers != NULL) {
		// release memory of library
		 module->free_(module->headers, 0, MEM_RELEASE, module->userdata, instance_AllocManager);
	}


	#ifdef Use_HeapAlloc
		HeapFree(GetProcessHeap(), 0, module);
	#else
		module->free_(module, 0, MEM_RELEASE, module->userdata, instance_AllocManager);
	//	instance_AllocManager.ManagedFree(module);
		// free(module);
	#endif
}

int MemoryModule::MemoryCallEntryPoint(HMEMORYMODULE mod) {
	PMEMORYMODULE module = (PMEMORYMODULE)mod;
	
	fprintf(stdout, "Adresse %p \n", module->exeEntry);

	//if (module == NULL || module->isDLL || module->exeEntry == NULL || !module->isRelocated) {
	if (module == NULL || module->isDLL || module->exeEntry == NULL ) {
		fprintf(stdout, "ARF.....\n");
		return -1;
	}

	fprintf(stdout, "EXECUTION..... %p \n", module->exeEntry);
	return module->exeEntry();
}

#define DEFAULT_LANGUAGE        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)

HMEMORYRSRC MemoryModule::MemoryFindResource(HMEMORYMODULE module, LPCTSTR name, LPCTSTR type) {
	return MemoryFindResourceEx(module, name, type, DEFAULT_LANGUAGE);
}


static PIMAGE_RESOURCE_DIRECTORY_ENTRY _MemorySearchResourceEntry(void *root, PIMAGE_RESOURCE_DIRECTORY resources,
		LPCTSTR key) {
	/* TODO: MemorySearchResourceEntry

	PIMAGE_RESOURCE_DIRECTORY_ENTRY entries = (PIMAGE_RESOURCE_DIRECTORY_ENTRY) (resources + 1);
	PIMAGE_RESOURCE_DIRECTORY_ENTRY result = NULL;
	DWORD start;
	DWORD end;
	DWORD middle;

	if (!IS_INTRESOURCE(key) && key[0] == TEXT('#')) {
		// special case: resource id given as string
		TCHAR *endpos = NULL;
		long int tmpkey = (WORD) strtol((TCHAR *) &key[1], &endpos, 10);
		if (tmpkey <= 0xffff && strlen(endpos) == 0) {
			key = MAKEINTRESOURCE(tmpkey);
		}
	}

	// entries are stored as ordered list of named entries,
	// followed by an ordered list of id entries - we can do
	// a binary search to find faster...
	if (IS_INTRESOURCE(key)) {
		WORD check = (WORD) (uintptr_t) key;
		start = resources->NumberOfNamedEntries;
		end = start + resources->NumberOfIdEntries;

		while (end > start) {
			WORD entryName;
			middle = (start + end) >> 1;
			entryName = (WORD) entries[middle].Name;
			if (check < entryName) {
				end = (end != middle ? middle : middle-1);
			} else if (check > entryName) {
				start = (start != middle ? middle : middle+1);
			} else {
				result = &entries[middle];
				break;
			}
		}
	} else {
		CONST CHAR* searchKey;
		size_t searchKeyLen = strlen(key);
#if defined(UNICODE)
		searchKey = key;
#else
		// Resource names are always stored using 16bit characters, need to
		// convert string we search for.
#define _LOCAL_KEY_LENGTH 2048
		// In most cases resource names are short, so optimize for that by
		// using a pre-allocated array.
		wchar_t _searchKeySpace[MAX_LOCAL_KEY_LENGTH+1];
		LPWSTR _searchKey;
		if (searchKeyLen > MAX_LOCAL_KEY_LENGTH) {
			size_t _searchKeySize = (searchKeyLen + 1) * sizeof(wchar_t);
			_searchKey = (LPWSTR) malloc(_searchKeySize);
			if (_searchKey == NULL) {
				SetLastError(ERROR_OUTOFMEMORY);
				return NULL;
			}
		} else {
			_searchKey = &_searchKeySpace[0];
		}

		mbstowcs(_searchKey, key, searchKeyLen);
		_searchKey[searchKeyLen] = 0;
		searchKey = _searchKey;
#endif
		start = 0;
		end = resources->NumberOfNamedEntries;
		while (end > start) {
			int cmp;
			PIMAGE_RESOURCE_DIR_STRING_U resourceString;
			middle = (start + end) >> 1;
			resourceString = (PIMAGE_RESOURCE_DIR_STRING_U) (((char *) root) + (entries[middle].Name & 0x7FFFFFFF));
			cmp = strncmp(searchKey, resourceString->NameString, resourceString->Length);
			if (cmp == 0) {
				// Handle partial match
				cmp = searchKeyLen - resourceString->Length;
			}
			if (cmp < 0) {
				end = (middle != end ? middle : middle-1);
			} else if (cmp > 0) {
				start = (middle != start ? middle : middle+1);
			} else {
				result = &entries[middle];
				break;
			}
		}
#if !defined(UNICODE)
		if (searchKeyLen > MAX_LOCAL_KEY_LENGTH) {
			free(_searchKey);
		}
#undef MAX_LOCAL_KEY_LENGTH
#endif
	}

	return result;
	*/
	return 0;
}

HMEMORYRSRC MemoryModule::MemoryFindResourceEx(HMEMORYMODULE module, LPCTSTR name, LPCTSTR type, WORD language) {
	/* TODO: MemoryFindResourceEx
	unsigned char *codeBase = ((PMEMORYMODULE) module)->codeBase;
	PIMAGE_DATA_DIRECTORY directory = GET_HEADER_DICTIONARY((PMEMORYMODULE) module, IMAGE_DIRECTORY_ENTRY_RESOURCE);
	PIMAGE_RESOURCE_DIRECTORY rootResources;
	PIMAGE_RESOURCE_DIRECTORY nameResources;
	PIMAGE_RESOURCE_DIRECTORY typeResources;
	PIMAGE_RESOURCE_DIRECTORY_ENTRY foundType;
	PIMAGE_RESOURCE_DIRECTORY_ENTRY foundName;
	PIMAGE_RESOURCE_DIRECTORY_ENTRY foundLanguage;
	if (directory->Size == 0) {
		// no resource table found
		SetLastError(ERROR_RESOURCE_DATA_NOT_FOUND);
		return NULL;
	}

	if (language == DEFAULT_LANGUAGE) {
		// use language from current thread
		language = LANGIDFROMLCID(GetThreadLocale());
	}

	// resources are stored as three-level tree
	// - first node is the type
	// - second node is the name
	// - third node is the language
	rootResources = (PIMAGE_RESOURCE_DIRECTORY) (codeBase + directory->VirtualAddress);
	foundType = _MemorySearchResourceEntry(rootResources, rootResources, type);
	if (foundType == NULL) {
		SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
		return NULL;
	}

	typeResources = (PIMAGE_RESOURCE_DIRECTORY) (codeBase + directory->VirtualAddress + (foundType->OffsetToData & 0x7fffffff));
	foundName = _MemorySearchResourceEntry(rootResources, typeResources, name);
	if (foundName == NULL) {
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
		return NULL;
	}

	nameResources = (PIMAGE_RESOURCE_DIRECTORY) (codeBase + directory->VirtualAddress + (foundName->OffsetToData & 0x7fffffff));
	foundLanguage = _MemorySearchResourceEntry(rootResources, nameResources, (LPCTSTR) (uintptr_t) language);
	if (foundLanguage == NULL) {
		// requested language not found, use first available
		if (nameResources->NumberOfIdEntries == 0) {
			SetLastError(ERROR_RESOURCE_LANG_NOT_FOUND);
			return NULL;
		}

		foundLanguage = (PIMAGE_RESOURCE_DIRECTORY_ENTRY) (nameResources + 1);
	}

	return (codeBase + directory->VirtualAddress + (foundLanguage->OffsetToData & 0x7fffffff));
	*/
	return 0;
}

DWORD MemoryModule::MemorySizeofResource(HMEMORYMODULE module, HMEMORYRSRC resource) {
	PIMAGE_RESOURCE_DATA_ENTRY entry;
	UNREFERENCED_PARAMETER(module);
	entry = (PIMAGE_RESOURCE_DATA_ENTRY) resource;
	if (entry == NULL) {
		return 0;
	}

	return entry->Size;
}

LPVOID MemoryModule::MemoryLoadResource(HMEMORYMODULE module, HMEMORYRSRC resource) {
	unsigned char *codeBase = ((PMEMORYMODULE) module)->codeBase;
	PIMAGE_RESOURCE_DATA_ENTRY entry = (PIMAGE_RESOURCE_DATA_ENTRY) resource;
	if (entry == NULL) {
		return NULL;
	}

	return codeBase + entry->OffsetToData;
}

int MemoryModule::MemoryLoadString(HMEMORYMODULE module, UINT id, LPTSTR buffer, int maxsize) {
	return MemoryLoadStringEx(module, id, buffer, maxsize, DEFAULT_LANGUAGE);
}

int MemoryModule::MemoryLoadStringEx(HMEMORYMODULE module, UINT id, LPTSTR buffer, int maxsize, WORD language) {
	/* TODO: MemoryLoadStringEx
	HMEMORYRSRC resource;
	PIMAGE_RESOURCE_DIR_STRING_U data;
	DWORD size;
	if (maxsize == 0) {
		return 0;
	}

	resource = MemoryFindResourceEx(module, MAKEINTRESOURCE((id >> 4) + 1), RT_STRING, language);
	if (resource == NULL) {
		buffer[0] = 0;
		return 0;
	}

	data = (PIMAGE_RESOURCE_DIR_STRING_U) MemoryLoadResource(module, resource);
	id = id & 0x0f;
	while (id--) {
		data = (PIMAGE_RESOURCE_DIR_STRING_U) (((char *) data) + (data->Length + 1) * sizeof(WCHAR));
	}
	if (data->Length == 0) {
		SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
		buffer[0] = 0;
		return 0;
	}

	size = data->Length;
	if (size >= (DWORD) maxsize) {
		size = maxsize;
	} else {
		buffer[size] = 0;
	}
#if defined(UNICODE)
	wcsncpy(buffer, data->NameString, size);
#else
	wcstombs(buffer, data->NameString, size);
#endif
	return size;
	*/
	return 0;
}
