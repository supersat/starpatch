/********************************************************
 * StarPatch - StarCraft Windowed-Mode Patcher          
 * Copyright 2000-2004 Karl Koscher.                    
 * All rights reserved. Starcraft is a                  
 * trademark of Blizzard Entertainment.                 
 *                                                      
 * Redistribution and use in source and binary forms,   
 * with or without modification, are permitted provided 
 * that the following conditions are met:               
 *
 *  * Redistributions of source code must retain the 
 *    above copyright notice, this list of conditions 
 *    and the following disclaimer.
 *  * Redistributions in binary form must reproduce the 
 *    above copyright notice, this list of conditions 
 *    and the following disclaimer in the documentation
 *    and/or other materials provided with the 
 *    distribution.
 *  * The name of the authors may not be used to endorse 
 *    or promote products derived from this software 
 *    without specific prior written permission.
 *                                                        
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS     
 * AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED    
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL 
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY 
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY 
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *********************************************************/

#include <windows.h>
#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <ddraw.h>

// The following values are for StarCraft v1.10
#define SCHWND               0x006509a0
#define SCLPDIRECTDRAW       0x006b6e8c
#define SCLPDDPRIMARYSURFACE 0x006b6e84
#define SCLPDDPALETTE        0x006b6e90

#define SCCOOPLEVEL          0x004d795b
#define SCCREATEWINDOW       0x00468299
#define SCRESCHANGE          0x004d7989
#define SCADJPALETTE         0x004d7a89
#define SCCREATECLIPPER      0x004d7a84

#define SCLOCKSURF           0x15013ba9
#define SCRESTORESURF        0x15013bc7
#define SCUNLOCKSURF         0x150142ac

#define SCWNDCLASSNAME       0xc0, 0x51, 0x4f, 0x00 // little endian

PROCESS_INFORMATION		scProcessInfo;

unsigned char nopBuf[] = { 
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
	0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

unsigned char patchCreateWindow[] = {
	0x51, 0x56, 0x57, 0x68,
	SCWNDCLASSNAME,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0x90, 0x90,
	0x90, 0x90, 0xE8, 0x00,
	0xE6, 0xB9, 0x44, 0x90
};

unsigned char patchLockCode[] = {
    0x8D, 0x54, 0x24, 0x0c, 0x52, 0x57, 0x50, 0x8b, 
	0x08, 0xE8, 0xDB, 0x32, 0xFF, 0x2F
};

unsigned char patchUnlockCode[] = {
	0xE8, 0xFF, 0x2E, 0xFF, 0x2F, 0x90
};

unsigned char patchCreateClipper[] = {
	0xE8, 0xD9, 0xA9, 0xB2, 0x44
};

unsigned char normalCooperativeMode[] = { 0x08 };

typedef int (__stdcall *GSMF)(int);
typedef HWND (__stdcall *CWF)(int, LPCTSTR, LPCTSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
typedef BOOL (__stdcall *GWR)(HWND, LPRECT);
typedef HWND (__stdcall *GFW)(VOID);
typedef UINT_PTR (__stdcall *ST)(HWND, UINT_PTR, UINT, TIMERPROC);
typedef void (__stdcall *DAPS)(DWORD);

typedef struct {
	CWF CreateWindowEx;
	GWR GetWindowRect;
	GFW GetForegroundWindow;
	ST SetTimer;
} WinSysCalls;

int __stdcall FakeLock(LPDIRECTDRAWSURFACE thisSurface, LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc);
int __stdcall FakeUnlock(LPDIRECTDRAWSURFACE thisSurface, void *lockedRect);
int __stdcall CreateClipper();
HWND __stdcall PatchedCreateWindow(char *className, char *windowName, GSMF GetSystemMetrics, HINSTANCE hInstance);
void CALLBACK RefreshSurface(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

int SpawnStarcraft()
{
	STARTUPINFO si;

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	
	return CreateProcess("starcraft.exe", "", NULL, NULL, 0, 
		NORMAL_PRIORITY_CLASS | DEBUG_PROCESS,
		NULL, NULL, &si, &scProcessInfo); 
}

void AllocateProcessMemory()
{
	// Allocate the new code section
	VirtualAllocEx(scProcessInfo.hProcess, (void *)0x45000000, 65536,
		MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	// Allocate the WinSysCalls block
	VirtualAllocEx(scProcessInfo.hProcess, (void *)0x45010000, 65536,
		MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

	// Allocate the virtual frame buffer
	VirtualAllocEx(scProcessInfo.hProcess, (void *)0x4a000000, 307200, 
		MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void ReadFrameBuffer()
{
	unsigned char buf[300];

	ReadProcessMemory(scProcessInfo.hProcess, (void *)0x4a000000, buf, 300, NULL);
}

void AdjustPatchCodeOffsets()
{
	*(unsigned int *)(patchCreateClipper + 1) = 0x45001000 - SCCREATECLIPPER - 5;
	*(unsigned int *)(patchCreateWindow + 0x23) = 0x45005000 - SCCREATEWINDOW - 0x27;
	*(unsigned int *)(patchLockCode + 0xa) = 0x45000000 - SCLOCKSURF - 0xe;
	*(unsigned int *)(patchUnlockCode + 1) = 0x45000100 - SCUNLOCKSURF - 5;
}

void TerminateIfPossible()
{
	DAPS daps = (DAPS)GetProcAddress(GetModuleHandle("kernel32.dll"), "DebugActiveProcessStop");
	if (daps) {
		DebugActiveProcessStop(scProcessInfo.dwProcessId);
		ExitProcess(0);
	}
}

void CopyCodeToProcess()
{
	WinSysCalls wsc;

	AdjustPatchCodeOffsets();

	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCCOOPLEVEL, normalCooperativeMode, 1, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCRESCHANGE, nopBuf, 0x17, NULL); // Stop screen resolutions changes
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCADJPALETTE, nopBuf, 0x0D, NULL); // Stop palette manipulation
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCCREATECLIPPER, patchCreateClipper, 5, NULL); // Create a clipper

	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCCREATEWINDOW, patchCreateWindow, 0x28, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCLOCKSURF, patchLockCode, 0xe, NULL); // Inject our custom "Lock" code
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCRESTORESURF, nopBuf, 10, NULL); // Inject our custom "Lock" code
	WriteProcessMemory(scProcessInfo.hProcess, (void *)SCUNLOCKSURF, patchUnlockCode, 0x6, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45000000, FakeLock, 0x100, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45000100, FakeUnlock, 0x300, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45001000, CreateClipper, 0x100, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45002000, RefreshSurface, 0x500, NULL);
	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45005000, PatchedCreateWindow, 0x100, NULL);

	// Set up the WinSysCalls struct and copy it
	// WARNING: This only works if the various Windows DLLs load at their preferred addressses
	// However, this is virtually always the case
	wsc.CreateWindowEx = CreateWindowEx;
	wsc.GetWindowRect = GetWindowRect;
	wsc.GetForegroundWindow = GetForegroundWindow;
	wsc.SetTimer = SetTimer;

	WriteProcessMemory(scProcessInfo.hProcess, (void *)0x45010000, &wsc, sizeof(wsc), NULL);
}

void DumpThreadContext(void *addr, unsigned int exceptionCode, CONTEXT *context)
{
	char dbgMsg[512];

	sprintf(dbgMsg, "Exception %08x @ %08x\n",
		exceptionCode, addr);
	OutputDebugString(dbgMsg);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nShowCmd)
{
	DEBUG_EVENT de;
	CONTEXT thContext;
	char *dbgStr;

	if (!SpawnStarcraft())
		return -1;

	for (;;) {
		WaitForDebugEvent(&de, INFINITE);
		switch(de.dwDebugEventCode) {
		case CREATE_PROCESS_DEBUG_EVENT:
			AllocateProcessMemory();
			break;
		case LOAD_DLL_DEBUG_EVENT:
			if (de.u.LoadDll.lpBaseOfDll == (void *)0x15000000) // check to see if STORM has been loaded
				CopyCodeToProcess(); // patch the code now that STORM has been loaded
			break;
		case EXCEPTION_DEBUG_EVENT:
			if (de.u.Exception.ExceptionRecord.ExceptionCode == 0xc0000005) {
				GetThreadContext(scProcessInfo.hThread, &thContext);
				DumpThreadContext(de.u.Exception.ExceptionRecord.ExceptionAddress, 
					de.u.Exception.ExceptionRecord.ExceptionCode, &thContext);
				return -1;
			}
			ReadFrameBuffer();
			break;
		case OUTPUT_DEBUG_STRING_EVENT:
			dbgStr = malloc(de.u.DebugString.nDebugStringLength);
			ReadProcessMemory(scProcessInfo.hProcess, de.u.DebugString.lpDebugStringData, dbgStr, de.u.DebugString.nDebugStringLength, NULL);
			OutputDebugString(dbgStr);
			free(dbgStr);
			break;
		case EXIT_PROCESS_DEBUG_EVENT:
			return 0;
		}
		ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
	}
	return -1;
}

// copied over to 0x45000000 in Starcraft.exe
int __stdcall FakeLock(LPDIRECTDRAWSURFACE thisSurface, LPRECT lpDestRect, LPDDSURFACEDESC lpDDSurfaceDesc)
{
//	HRESULT lockResult = 0;

//	lockResult = thisSurface->lpVtbl->Lock(thisSurface, lpDestRect, lpDDSurfaceDesc, flags, 0);

	lpDDSurfaceDesc->dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH |
		DDSD_PITCH | DDSD_LPSURFACE | DDSD_PIXELFORMAT;
	lpDDSurfaceDesc->dwHeight = 480;
	lpDDSurfaceDesc->dwWidth = 640;
	lpDDSurfaceDesc->lpSurface = (void *)0x4a000000;
	lpDDSurfaceDesc->lPitch = 640;
	lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
	lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_PALETTEINDEXEDTO8;

	return 0;

}

int __stdcall FakeUnlock(LPDIRECTDRAWSURFACE thisSurface, void *lockedRect)
{
	return 0;
}

// Creates a clipper for the window, plus a timer to redraw the screen at a constant interval
int __stdcall CreateClipper()
{
	WinSysCalls *wsc = (WinSysCalls *)(0x45010000);
	HWND hwnd = *(HWND *)(SCHWND);
	LPDIRECTDRAW lpDD = *(LPDIRECTDRAW *)(SCLPDIRECTDRAW);
	LPDIRECTDRAWSURFACE lpDDSurface = *(LPDIRECTDRAWSURFACE *)(SCLPDDPRIMARYSURFACE);
	LPDIRECTDRAWCLIPPER lpDDClipper;

	wsc->SetTimer(NULL, 0, 20, (TIMERPROC)(0x45002000));

	lpDD->lpVtbl->CreateClipper(lpDD, 0, &lpDDClipper, NULL);
	lpDDClipper->lpVtbl->SetHWnd(lpDDClipper, 0, hwnd);
	lpDDSurface->lpVtbl->SetClipper(lpDDSurface, lpDDClipper);

	return 0;
}

HWND __stdcall PatchedCreateWindow(char *className, char *windowName, GSMF GetSystemMetrics, HINSTANCE hInstance)
{
	WinSysCalls *wsc = (WinSysCalls *)(0x45010000);
	int height, width;
	int *xOffset = (int *)(0x45003000), *yOffset = (int *)(0x45003004);

	*xOffset = GetSystemMetrics(SM_CXFIXEDFRAME);
	*yOffset = GetSystemMetrics(SM_CYFIXEDFRAME) + GetSystemMetrics(SM_CYCAPTION);

	width = 640 + (*xOffset * 2);
	height = 480 + *yOffset + GetSystemMetrics(SM_CYFIXEDFRAME);

	return wsc->CreateWindowEx(0, className, windowName, WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX,
		0, 0, width, height, NULL, NULL, hInstance, NULL);
}

// Called 50 times a second
void CALLBACK RefreshSurface(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	LPDIRECTDRAWPALETTE lpDDPalette = *(LPDIRECTDRAWPALETTE *)(SCLPDDPALETTE);
	LPDIRECTDRAWSURFACE thisSurface = *(LPDIRECTDRAWSURFACE *)(SCLPDDPRIMARYSURFACE);
	DDSURFACEDESC surfDesc;
	unsigned char *frameBuf, *fakeBufPtr;
	unsigned int *bufPtr, palette[256];
	WinSysCalls *wsc = (WinSysCalls *)(0x45010000);
	int xOffset, yOffset;
	int i, y, pitch;
	RECT windowRect;

	if (!lpDDPalette || !thisSurface) // make sure we have our objects set up 
		return;

	if (wsc->GetForegroundWindow() != *(HWND *)(SCHWND)) // Check to see if our window is in the foreground
		return;

	wsc->GetWindowRect(*(HWND *)(SCHWND), &windowRect);
	xOffset = windowRect.left + *(int *)(0x45003000);
	yOffset = windowRect.top + *(int *)(0x45003004);

	lpDDPalette->lpVtbl->GetEntries(lpDDPalette, 0, 0, 256, (void *)&palette);

	for (i = 0; i < 256; i++) { // palette color swaping
		palette[i] = 
			((palette[i] & 0xFF) << 16) | 
			((palette[i] & 0xFF0000) >> 16) | 
			(palette[i] & 0xFF00);
	}


	for (i = 0; i < sizeof(DDSURFACEDESC); i++) // our memset hack
		((char *)&surfDesc)[i] = 0;
	surfDesc.dwSize = sizeof(DDSURFACEDESC);

	if (thisSurface->lpVtbl->Lock(thisSurface, NULL, &surfDesc, 1, 0))
		return;
	
	frameBuf = surfDesc.lpSurface;
	pitch = surfDesc.lPitch;

	fakeBufPtr = (unsigned char *)0x4a000000;

	for (y = yOffset; y < 480 + yOffset; y++) {
		bufPtr = (unsigned int *)(frameBuf + (y * pitch)) + xOffset;
		for (i = 0; i < 640; i++) {
			 *bufPtr++ = palette[*fakeBufPtr++];
		}
	}

	thisSurface->lpVtbl->Unlock(thisSurface, NULL);
}

