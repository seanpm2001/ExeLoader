/*  -== ExeLoader ==-
 *
 *  Load .exe / .dll from memory and remap functions
 *  Run your binaries on any x86 hardware
 *
 *  @autors
 *   - Maeiky
 *  
 * Copyright (c) 2020 - V·Liance / SPinti-Software. All rights reserved.
 *
 * The contents of this file are subject to the Apache License Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * If a copy of the Apache License Version 2.0 was not distributed with this file,
 * You can obtain one at https://www.apache.org/licenses/LICENSE-2.0.html

* Description:
* 
* FuncTable_Sys is an attempt to remake system functions, or redirect them, 
* this is a mix between FuncTable_Pipe & FuncTable_Imp
* 
* Warning: Windows WINAPI function are __stdcall instead of __cdecl. 
* __stdcall remapped function must have the EXACT same paramters and must be specified as __stdcall
*  If not, your app will likely crash when the function return.
*
*/

#include "FuncPrototype\CPC_WPR.h"

//!VOID WINAPI SetLastError (DWORD dwErrCode)
DWORD last_error = 0;
VOID WINAPI sys_SetLastError (DWORD dwErrCode){
	if(dwErrCode != 0){
		showfunc("SetLastError( dwErrCode: %d)", dwErrCode); 
	}
	#if defined(Func_Win) || defined(USE_Window_LastError) 
		SetLastError(dwErrCode);
	#else
	last_error = dwErrCode;
	#endif
}

//!DWORD WINAPI GetLastError (VOID)
DWORD WINAPI sys_GetLastError(VOID){
	showfunc_opt("GetLastError( )", ""); 
	#if defined(Func_Win) || defined(USE_Window_LastError) 
	DWORD error = GetLastError();
	if (error){
		LPVOID lpMsgBuf;
		DWORD bufLen = FormatMessage(	FORMAT_MESSAGE_ALLOCATE_BUFFER |
										FORMAT_MESSAGE_FROM_SYSTEM |
										FORMAT_MESSAGE_IGNORE_INSERTS,
										NULL,error,MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),(LPTSTR) &lpMsgBuf,0, NULL );
		if (bufLen){
		  LPCSTR lpMsgStr = (LPCSTR)lpMsgBuf;
		  std::string result(lpMsgStr, lpMsgStr+bufLen);
		  LocalFree(lpMsgBuf);
		  showinf("GetLastError:%s", result.c_str());
		}

	}
	return error;
	#else
	 return last_error;
	#endif
}


//!WINBOOL WINAPI GetVersionExA (LPOSVERSIONINFOA lpVersionInformation)
//!WINBOOL WINAPI GetVersionExW (LPOSVERSIONINFOW lpVersionInformation)
WINBOOL WINAPI sys_GetVersionExW (LPOSVERSIONINFOW lpVersionInformation){
	showfunc("GetVersionExW( lpVersionInformation: %p)", lpVersionInformation); 
	
	//DWORD dwOSVersionInfoSize;
	//DWORD dwMajorVersion;
	//DWORD dwMinorVersion;
	//DWORD dwBuildNumber;
	//DWORD dwPlatformId;
	//WCHAR szCSDVersion[128];	
	#ifdef Func_Win
		return GetVersionExW(lpVersionInformation);
	#else
		lpVersionInformation->dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		lpVersionInformation->dwMajorVersion = 10;
		lpVersionInformation->dwMinorVersion = 0;
		lpVersionInformation->dwBuildNumber = 0;
		lpVersionInformation->dwPlatformId = 2;
		lpVersionInformation->szCSDVersion[128] = 0;

	return 1;
	#endif
}


//!WINBOOL WINAPI TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack)
WINBOOL WINAPI sys_TrackMouseEvent(LPTRACKMOUSEEVENT lpEventTrack){
	showfunc("TrackMouseEvent( lpEventTrack: %p)", lpEventTrack); 
	#ifdef Func_Win
		return TrackMouseEvent(lpEventTrack);
	#else
		return true;
	#endif
}


//!HDC GetDC(HWND hWnd)
inline HDC WINAPI sys_GetDC(HWND hWnd){
	showfunc("GetDC( lpModuleName: %p)", hWnd); 
	#ifdef Func_Win
		return GetDC(hWnd);
	#else
		return (HDC)hWnd; //HDC is same as HWND (not necessary to dissociate them)
	#endif
}

//!HWND WindowFromDC(HDC hDC)
inline WINAPI HWND pipe_WindowFromDC(HDC hDC){
	showfunc_opt("WindowFromDC( hDC:%p )",hDC);
	#ifdef Func_Win
	return WindowFromDC(hDC);
	#else
	return (HWND)hDC; //HDC is same as HWND (not necessary to dissociate them)
	#endif
}


extern CpcdosOSx_CPintiCore* oCpc; //TODO free

//!HWND WINAPI CreateWindowExW(DWORD dwExStyle,LPCWSTR lpClassName,LPCWSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam)
HWND WINAPI pipe_CreateWindowExW(DWORD dwExStyle,LPCWSTR lpClassName,LPCWSTR lpWindowName,DWORD dwStyle,int X,int Y,int nWidth,int nHeight,HWND hWndParent,HMENU hMenu,HINSTANCE hInstance,LPVOID lpParam){
	showfunc("CreateWindowExW( dwExStyle: %d, lpClassName: %p, lpWindowName :%d, dwStyle: %d, X: %d, Y: %d, nWidth: %d, nHeight: %d, hWndParent: %p, hMenu: %p, hInstance: %d, lpParam: %d )",
								dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
	#ifdef Func_Win
		return CreateWindowExW( dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam );
	#else
		//Create a new Window Context
		aContext_count++; //Important: Skip the zero index (NULL)
		int idx = aContext_count;
		
		aContext[idx].idx = idx;
		aContext[idx].width = nWidth;
		aContext[idx].height = nHeight;
		#ifdef ShowPixView
		aContext[idx].hwnd_View = pixView_createWindow(hExeloader, &aContext[idx]);
		#endif

		#ifdef CpcDos
		if(nWidth > 10){
			// Get ID context from cpcdos
			aContext[idx].id_context = oCpc->Create_Context(nWidth, nHeight);
			showinf("Create_Context()= idx: %d, height: %d, width: %d", idx,  aContext[idx].height,  aContext[idx].width);
		}
		#endif
		
		showinf("PixView= idx: %d, height: %d, width: %d", idx,  aContext[idx].height,  aContext[idx].width);
		showinf("create hwnd_View( hwnd_View: %d, idx: %d, height: %d, width: %d )", aContext[idx].hwnd_View,  idx,  aContext[idx].height,  aContext[idx].width );
	
		return (HWND)idx;
		
		
	#endif
}


//!int StretchDIBits(HDC hdc,int xDest,int yDest,int DestWidth,int DestHeight,int xSrc,int ySrc, int SrcWidth, int SrcHeight, const VOID *lpBits, const BITMAPINFO *lpbmi, UINT iUsage, DWORD rop)
int WINAPI pipe_StretchDIBits(HDC hdc,int xDest,int yDest,int DestWidth,int DestHeight,int xSrc,int ySrc, int SrcWidth, int SrcHeight, const VOID *lpBits, const BITMAPINFO *lpbmi, UINT iUsage, DWORD rop){
	showfunc("StretchDIBits( hdc: %p )", hdc);
	#ifdef Func_Win
		return StretchDIBits(hdc, xDest, yDest, DestWidth, DestHeight, xSrc, ySrc, SrcWidth, SrcHeight, lpBits, lpbmi, iUsage, rop);
	#else
		/*
		showinf("lpbmi.bmiHeader.biWidth: %d", lpbmi->bmiHeader.biWidth);
		showinf("lpbmi.bmiHeader.biHeight: %d", lpbmi->bmiHeader.biHeight);
		showinf("lpbmi.bmiHeader.biPlanes: %d", lpbmi->bmiHeader.biPlanes);
		showinf("lpbmi.bmiHeader.biBitCount: %d", lpbmi->bmiHeader.biBitCount);
		showinf("lpbmi.bmiHeader.biCompression: %d", lpbmi->bmiHeader.biCompression);
		showinf("lpbmi.bmiHeader.biSizeImage: %d", lpbmi->bmiHeader.biSizeImage);
		showinf("lpbmi.bmiHeader.biXPelsPerMeter: %d", lpbmi->bmiHeader.biXPelsPerMeter);
		showinf("lpbmi.bmiHeader.biYPelsPerMeter: %d", lpbmi->bmiHeader.biYPelsPerMeter);
		showinf("lpbmi.bmiHeader.biClrUsed: %d", lpbmi->bmiHeader.biClrUsed);
		showinf("lpbmi.bmiHeader.biClrImportant: %d", lpbmi->bmiHeader.biClrImportant);
		showinf("lpbmi.bmiColors[0].rgbBlue: %d", lpbmi->bmiColors[0].rgbBlue );
		showinf("lpbmi.bmiColors[0].rgbGreen: %d", lpbmi->bmiColors[0].rgbGreen );
		showinf("lpbmi.bmiColors[0].rgbRed: %d", lpbmi->bmiColors[0].rgbRed );
		showinf("lpbmi.bmiColors[0].rgbReserved: %d", lpbmi->bmiColors[0].rgbReserved );
		*/
	
		int idx = (int)(long long)hdc; //HDC is same as HWND (not necessary to dissociate them)
		#ifdef ShowPixView
			// aContext[idx].width & SrcWidth may differ (+32pix for depth buffer?)
			pixView_MakeSurface(&aContext[idx]);
			uint32_t* pix_src = (uint32_t*)lpBits;
			uint32_t* pix_dest = (uint32_t*)aContext[idx].pixels;
			
			for(int y = 0; y <  aContext[idx].height; y++){
				memcpy(pix_dest + (y * aContext[idx].width), pix_src + (y * SrcWidth), aContext[idx].width *4);
			}
			
			//( aContext[idx].width & SrcWidth may differ )
			//memcpy(aContext[idx].pixels, lpBits, aContext[idx].height * aContext[idx].width *4);
			
			pixView_update(&aContext[idx]);
			//showinf("PixView= idx: %d, height: %d, width: %d", idx,  aContext[idx].height,  aContext[idx].width);
		#endif
		
		#ifdef CpcDos
		if(aContext[idx].width > 10){
			aContext[idx].pixels = oCpc->Init_Get_Context_PTR(aContext[1].id_context);

			uint32_t* pix_src = (uint32_t*)lpBits;
			uint32_t* pix_dest = (uint32_t*)aContext[idx].pixels;
			
			for(int y = 0; y <  aContext[idx].height; y++){
				memcpy(pix_dest + (y * aContext[idx].width), pix_src + (y * SrcWidth), aContext[idx].width *4);
			}

			oCpc->Blitting(aContext[1].id_context);
		}
		#endif
		
		//showinf("use hwnd_View( hwnd_View: %d )", aContext[idx].hwnd_View);
		
		return aContext[idx].height; //number of scan lines copied
	#endif
}


//!WINBOOL WINAPI GetClientRect(HWND hWnd,LPRECT lpRect)
//struct RECT {LONG left; LONG top;LONG right;LONG bottom;}
WINBOOL WINAPI sys_GetClientRect(HWND hWnd,LPRECT lpRect){
 	showfunc_opt("GetClientRect( hWnd: %p, lpRect: %p )", hWnd, lpRect);
	#ifdef Func_Win
		return GetClientRect(hWnd, lpRect);
	#else
		lpRect->left = 0;
		lpRect->top  = 0;
		lpRect->right  = aContext[(int)(long long)hWnd].width;
		lpRect->bottom = aContext[(int)(long long)hWnd].height;
		return true;
	#endif
}

//!WINBOOL WINAPI GetWindowRect(HWND hWnd,LPRECT lpRect)
WINBOOL WINAPI sys_GetWindowRect(HWND hWnd,LPRECT lpRect){
	showfunc_opt("GetWindowRect( hWnd: %p, lpRect: %p )", hWnd, lpRect);
	#ifdef Func_Win
		return GetWindowRect(hWnd, lpRect);
	#else
		lpRect->left = 0;
		lpRect->top  = 0;
		lpRect->right  = aContext[(int)(long long)hWnd].width;
		lpRect->bottom = aContext[(int)(long long)hWnd].height;
		return true;
	#endif
}

//!WINBOOL WINAPI TranslateMessage(CONST MSG *lpMsg)
WINBOOL WINAPI sys_TranslateMessage(CONST MSG *lpMsg){
 	showfunc_opt("TranslateMessage( lpMsg: %p )", lpMsg);
	#ifdef Func_Win
		return TranslateMessage(lpMsg);
	#else
		return true;
	#endif
}

//!LRESULT WINAPI DispatchMessageA(CONST MSG *lpMsg)
//!LRESULT WINAPI DispatchMessageW(CONST MSG *lpMsg)
WINBOOL WINAPI sys_DispatchMessageA(CONST MSG *lpMsg){
 	showfunc_opt("DispatchMessageA( lpMsg: %p )", lpMsg);
	#ifdef Func_Win
		return DispatchMessageA(lpMsg);
	#else
		return true;
	#endif
}
WINBOOL WINAPI sys_DispatchMessageW(CONST MSG *lpMsg){
 	showfunc_opt("DispatchMessageW( lpMsg: %p )", lpMsg);
	#ifdef Func_Win
		return DispatchMessageW(lpMsg);
	#else
		return 0;
	#endif
}


//!UINT WINAPI SetErrosrMode (UINT uMode)
UINT WINAPI sys_SetErrorMode(UINT uMode){
 	showfunc("SetErrorMode( uMode: %p )", uMode);
	#ifdef Func_Win
		return SetErrorMode(uMode);
	#else
		return 0;
	#endif
}



///////////////////// HERE OK
///////////////////// HERE OK
///////////////////// HERE OK
  


//!DWORD GetFileType(HANDLE hFile)
DWORD sys_GetFileType(HANDLE hFile){
	showfunc("GetFileType( hFile: %p )", hFile);
	#ifdef Func_Win
		return GetFileType(hFile);
	#else
	return 0;
	#endif
}

//!ULONGLONG NTAPI VerSetConditionMask (ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition);
ULONGLONG NTAPI sys_VerSetConditionMask (ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition){
	showfunc_opt("VerSetConditionMask( ConditionMask: %p, TypeMask: %d, Condition: %d )", ConditionMask, TypeMask, Condition);
	#ifdef Func_Win
		return VerSetConditionMask(ConditionMask, TypeMask, Condition);
	#else
		return 0;
	#endif

}

//!WINBOOL WINAPI VerifyVersionInfoW (LPOSVERSIONINFOEXW lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask)
WINBOOL WINAPI sys_VerifyVersionInfoW (LPOSVERSIONINFOEXW lpVersionInformation, DWORD dwTypeMask, DWORDLONG dwlConditionMask){
	showfunc_opt("VerifyVersionInfoW( lpVersionInformation: %p, dwTypeMask: %d, dwlConditionMask: %d )", lpVersionInformation, dwTypeMask, dwlConditionMask);
	#ifdef Func_Win
		return VerifyVersionInfoW(lpVersionInformation, dwTypeMask, dwlConditionMask);
	#else
		//If the currently running operating system satisfies the specified requirements, the return value is a nonzero value.
		return 1;
	#endif
}

//!BOOL IMAGEAPI EnumerateLoadedModules64(__in HANDLE hProcess,__in PENUMLOADED_MODULES_CALLBACK64 EnumLoadedModulesCallback,__in PVOID UserContext)
typedef BOOL (CALLBACK* PENUMLOADED_MODULES_CALLBACK64)( PCSTR ModuleName, ULONG ModuleBase, ULONG ModuleSize, PVOID UserContext);
BOOL WINAPI sys_EnumerateLoadedModules64( HANDLE hProcess, PENUMLOADED_MODULES_CALLBACK64 EnumLoadedModulesCallback, PVOID UserContext){
	showfunc_opt("EnumerateLoadedModules64( hProcess: %p, EnumLoadedModulesCallback: %p, UserContext: %p )", hProcess, EnumLoadedModulesCallback, UserContext);
	// static BOOL CALLBACK ELM_Callback(WIN32_ELMCB_PCSTR ModuleName, DWORD64 ModuleBase,ULONG ModuleSize, PVOID UserContext);
	//Just send a fake Module 
	EnumLoadedModulesCallback(0,0,0,0);
	return true;
}

//!DWORD WINAPI GetCurrentDirectoryA (DWORD nBufferLength, LPSTR lpBuffer)
//!DWORD WINAPI GetCurrentDirectoryW (DWORD nBufferLength, LPWSTR lpBuffer)
DWORD WINAPI sys_GetCurrentDirectoryA (DWORD nBufferLength, LPSTR lpBuffer){
	showfunc_opt("GetCurrentDirectoryA( nBufferLength: %d, lpBuffer: %p )", nBufferLength, lpBuffer);
	#ifdef Func_Win
		return GetCurrentDirectoryA( nBufferLength, lpBuffer);
	#else
		//If the currently running operating system satisfies the specified requirements, the return value is a nonzero value.
		return 1;
	#endif
}
DWORD WINAPI sys_GetCurrentDirectoryW (DWORD nBufferLength, LPWSTR lpBuffer){
	showfunc_opt("GetCurrentDirectoryW( nBufferLength: %d, lpBuffer: %p )", nBufferLength, lpBuffer);
	#ifdef Func_Win
		return GetCurrentDirectoryW( nBufferLength, lpBuffer);
	#else
		//return GetCurrentDirectoryW( nBufferLength, lpBuffer);//TODO
		//If the currently running operating system satisfies the specified requirements, the return value is a nonzero value.
		return 1;
	#endif
}

//!HRESULT GetDpiForMonitor(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType,UINT *dpiX,UINT *dpiY)
HRESULT sys_GetDpiForMonitor(HMONITOR hmonitor,int dpiType,UINT* dpiX,UINT* dpiY){
	showfunc("GetDpiForMonitor( hmonitor: %p, dpiType: %d, dpiX: %p,  dpiY: %p )", hmonitor, dpiType, dpiX, dpiY);
	*dpiX = 0;
	*dpiY = 0;
	return 0;
}

//!BOOL SetProcessDPIAware()
inline BOOL sys_SetProcessDPIAware(){
	showfunc("SetProcessDPIAware( )","");
	return true;
}

//!HRESULT SetProcessDpiAwareness(PROCESS_DPI_AWARENESS value)
inline HRESULT sys_SetProcessDpiAwareness(int value){
	showfunc("SetProcessDpiAwareness( value: %d )",value);
	return 0;
}

//!WINBOOL WINAPI QueryPerformanceCounter (LARGE_INTEGER *lpPerformanceCount)

WINBOOL WINAPI sys_QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount){
   	showfunc_opt("QueryPerformanceCounter(lpPerformanceCount)", lpPerformanceCount);
	#ifdef Func_Win
		return QueryPerformanceCounter( lpPerformanceCount);
	#else
		static int i = 0; i++;
		if(lpPerformanceCount != 0){
			LARGE_INTEGER lpPerformanceCount_ = {(DWORD)521891041 + i};//Dummy value
			*lpPerformanceCount = lpPerformanceCount_;
		}
		return true;
	#endif
}

//!WINBOOL WINAPI QueryPerformanceFrequency (LARGE_INTEGER *lpFrequency)
WINBOOL WINAPI sys_QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency){
   	showfunc("QueryPerformanceFrequency( lpFrequency: %p )", lpFrequency);
	#ifdef Func_Win
		return QueryPerformanceFrequency( lpFrequency);
	#else
		static const LARGE_INTEGER lpFrequency_ = {8221038}; //Dummy value
		if(lpFrequency != 0){*lpFrequency = lpFrequency_;}
		return false;
	#endif
}

//!DWORD WINAPI GetTickCount (VOID)
DWORD WINAPI sys_GetTickCount(VOID){
 	showfunc("GetTickCount( )", "");
	#ifdef Func_Win
		return GetTickCount();
	#else
		return 1;//Fake
	#endif
}

//!DWORD WINAPI GetCurrentThreadId (VOID)
DWORD WINAPI sys_GetCurrentThreadId(VOID){
 	showfunc("GetCurrentThreadId( )", "");
	#ifdef Func_Win
		return GetCurrentThreadId();
	#else
		return 1;//Fake
	#endif
}

//!DWORD WINAPI GetCurrentThreadId (VOID)
DWORD WINAPI sys_GetCurrentProcessId(VOID){
 	showfunc("GetCurrentProcessId( )", "");
	#ifdef Func_Win
		return GetCurrentProcessId();
	#else
		return 1;//TODO
	#endif
}

 //!VOID WINAPI GetSystemTimeAsFileTime (LPFILETIME lpSystemTimeAsFileTime)
 VOID WINAPI sys_GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime){
	 showfunc("GetSystemTimeAsFileTime( lpSystemTimeAsFileTime: %p )", lpSystemTimeAsFileTime);
	#ifdef Func_Win
		GetSystemTimeAsFileTime(lpSystemTimeAsFileTime);
	#else
		//typedef struct _FILETIME {DWORD dwLowDateTime;DWORD dwHighDateTime;} FILETIME,*PFILETIME,*LPFILETIME;
		lpSystemTimeAsFileTime->dwLowDateTime = 1; //Fake time
		lpSystemTimeAsFileTime->dwHighDateTime = 1; //Fake time
	#endif
 }

//!LPTOP_LEVEL_EXCEPTION_FILTER WINAPI SetUnhandledExceptionFilter (LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter)
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI sys_SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter){
 	showfunc("SetUnhandledExceptionFilter( lpTopLevelExceptionFilter: %p )", lpTopLevelExceptionFilter);
	#ifdef Func_Win
		return SetUnhandledExceptionFilter(lpTopLevelExceptionFilter);
	#else
		return 0;
	#endif
}


//!HANDLE WINAPI CreateEventA (LPSECURITY_ATTRIBUTES lpEventAttributes, WINBOOL bManualReset, WINBOOL bInitialState, LPCSTR lpName)
//!HANDLE WINAPI CreateEventW (LPSECURITY_ATTRIBUTES lpEventAttributes, WINBOOL bManualReset, WINBOOL bInitialState, LPCWSTR lpName)
HANDLE WINAPI sys_CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttributes, WINBOOL bManualReset, WINBOOL bInitialState, LPCSTR lpName){
	showfunc("CreateEventA( lpEventAttributes: %p,  bManualReset: %d, bInitialState: %d, lpName: %s )", lpEventAttributes, bManualReset, bInitialState, lpName);
	#ifdef Func_Win
		return CreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);
	#else
		return 0;
	#endif
}
HANDLE WINAPI sys_CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, WINBOOL bManualReset, WINBOOL bInitialState, LPCWSTR lpName){
	showfunc("CreateEventW( lpEventAttributes: %p,  bManualReset: %d, bInitialState: %d, lpName: %p )", lpEventAttributes, bManualReset, bInitialState, lpName);
	#ifdef Func_Win
		return CreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);
	#else
		return 0;
	#endif
}

//!SHORT WINAPI GetKeyState(int nVirtKey)
SHORT WINAPI sys_GetKeyState(int nVirtKey){
	showfunc_opt("GetKeyState( nVirtKey: %d )", nVirtKey);
	#ifdef Func_Win
		return GetKeyState(nVirtKey);
	#else
		return 0;
	#endif
}


//!VOID WINAPI InitializeSListHead (PSLIST_HEADER ListHead)
VOID WINAPI sys_InitializeSListHead(PSLIST_HEADER ListHead){
	showfunc("InitializeSListHead( ListHead: %d )", ListHead);
	#ifdef Func_Win
		 InitializeSListHead(ListHead);
	#else
	//	 0;
	//TODO
	#endif
}
/*
  WINBASEAPI VOID WINAPI InitializeSListHead (PSLIST_HEADER ListHead);
  WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPopEntrySList (PSLIST_HEADER ListHead);
  WINBASEAPI PSLIST_ENTRY WINAPI InterlockedPushEntrySList (PSLIST_HEADER ListHead, PSLIST_ENTRY ListEntry);
  WINBASEAPI PSLIST_ENTRY WINAPI InterlockedFlushSList (PSLIST_HEADER ListHead);
  WINBASEAPI USHORT WINAPI QueryDepthSList (PSLIST_HEADER ListHead);
*/

//!LPCH WINAPI GetEnvironmentStrings (VOID)
//!LPWCH WINAPI GetEnvironmentStringsW (VOID)
LPCH WINAPI sys_GetEnvironmentStrings (VOID){
	showfunc("GetEnvironmentStrings( )", "");
	#ifdef Func_Win
		return GetEnvironmentStrings();
	#else
		return 0;	//TODO (Not work!?)
	#endif
}
LPWCH WINAPI sys_GetEnvironmentStringsW (VOID){
	showfunc("GetEnvironmentStringsW( )", "");

	#ifdef Func_Win
		return GetEnvironmentStringsW();
	#else
		return 0;	//TODO (Not work!?)
	#endif
}
 //!WINBOOL WINAPI FreeEnvironmentStringsA (LPCH penv)
 //!WINBOOL WINAPI FreeEnvironmentStringsW (LPWCH penv)
 WINBOOL WINAPI sys_FreeEnvironmentStringsA (LPCH penv){
 	showfunc("FreeEnvironmentStringsA( penv: %p )", penv);
	#ifdef Func_Win
		return FreeEnvironmentStringsA(penv);
	#else
		return 0;	//TODO (Not work!?)
	#endif
 }
 WINBOOL WINAPI sys_FreeEnvironmentStringsW (LPWCH penv){
  	showfunc("FreeEnvironmentStringsW( penv: %p )", penv);
	#ifdef Func_Win
		return FreeEnvironmentStringsW(penv);
	#else
		return 0;	//TODO (Not work!?)
	#endif
 }
 
//!DWORD WINAPI GetModuleFileNameA (HMODULE hModule, LPSTR lpFilename, DWORD nSize)
//!DWORD WINAPI GetModuleFileNameW (HMODULE hModule, LPWSTR lpFilename, DWORD nSize)
DWORD WINAPI sys_GetModuleFileNameA (HMODULE hModule, LPSTR lpFilename, DWORD nSize){
	showfunc("GetModuleFileNameA( hModule: %p, lpFilename: %s, nSize: %d )", hModule, lpFilename, nSize);
	#ifdef Func_Win
		return sys_GetModuleFileNameA(hModule, lpFilename, nSize);
	#else
		return 0;
	#endif
}
DWORD WINAPI sys_GetModuleFileNameW (HMODULE hModule, LPWSTR lpFilename, DWORD nSize){
	showfunc("GetModuleFileNameW( hModule: %p, lpFilename: %s, nSize: %d )", hModule, lpFilename, nSize);
	#ifdef Func_Win
		return GetModuleFileNameW(hModule, lpFilename, nSize);
	#else
		return 0;
	#endif
}

