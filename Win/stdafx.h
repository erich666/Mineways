/*
Copyright (c) 2010, Sean Kasun
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

// define #include <crtdbg.h> below and this line to test memory leaks, see https://msdn.microsoft.com/en-us/library/x98tx3cf.aspx
//#define TEST_FOR_MEMORY_LEAKS
#ifdef TEST_FOR_MEMORY_LEAKS
#define _CRTDBG_MAP_ALLOC
#endif

#define SKETCHFAB

// For internet update, sadly does not link under x64:
// 1>uafxcw.lib(appcore.obj) : error LNK2001: unresolved external symbol __wargv
// 1>uafxcw.lib(appcore.obj) : error LNK2001: unresolved external symbol __argc
// also needs an "#ifndef IDC_STATIC" bit in resource.h,
// also needs to link the wininet.lib library.
//#include <afxwin.h>
//#include <afxinet.h>
#include "targetver.h"
#include "cache.h"
#include "MinewaysMap.h"
#include "ObjFileManip.h"
#include "nbt.h"
#include "region.h"
#include "terrainExtData.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <commctrl.h>

// C RunTime Header Files
#include <stdlib.h>
// define _CRTDBG_MAP_ALLOC and this next line to test memory leaks, see https://msdn.microsoft.com/en-us/library/x98tx3cf.aspx
#ifdef TEST_FOR_MEMORY_LEAKS
#include <crtdbg.h>
#endif
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>

#define MINEWAYS_MAJOR_VERSION 10
#define MINEWAYS_MINOR_VERSION 16

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#ifndef clamp
#define clamp(a,min,max)	((a)<(min)?(min):((a)>(max)?(max):(a)))
#endif

#ifndef swapint
#define swapint(a,b)	{int tempint = (a); (a)=(b); (b)=tempint;}
#endif

#define MAX_PATH_AND_FILE (2*MAX_PATH)

#ifdef WIN32
#define PORTAFILE HANDLE
#define PortaOpen(fn) CreateFileW(fn,GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL)
#define PortaAppend(fn) CreateFileW(fn,GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL,CREATE_NEW,0,NULL)
#define PortaCreate(fn) CreateFileW(fn,GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL,CREATE_ALWAYS,0,NULL)
#define PortaSeek(h,ofs) SetFilePointer(h,ofs,NULL,FILE_BEGIN)==INVALID_SET_FILE_POINTER
#define PortaRead(h,buf,len) !ReadFile(h,buf,len,&br,NULL)
#define PortaWrite(h,buf,len) !WriteFile(h,buf,(DWORD)len,&br,NULL)
#define PortaClose(h) CloseHandle(h)
#endif

#ifndef WIN32
#define strncpy_s(f,n,w,m) strncpy(f,w,m)
#define strncat_s(f,n,w,m) strncat(f,w,m)
#define sprintf_s snprintf
#define PORTAFILE FILE*
#define PortaOpen(fn) fopen(fn,"rb")
#define PortaAppend(fn) fopen(fn,"a")
#define PortaCreate(fn) fopen(fn,"w")
#define PortaSeek(h,ofs) fseek(h,ofs,SEEK_SET)
#define PortaRead(h,buf,len) fread(buf,len,1,h)!=1
#define PortaWrite(h,buf,len) fwrite(buf,len,1,h)!=1
#define PortaClose(h) fclose(h)
#endif

#if __STDC_VERSION__ >= 199901L
#define C99
#endif
