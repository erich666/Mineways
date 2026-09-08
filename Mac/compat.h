// macOS compatibility shims for Mineways Win32 source
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

// errno_t: C11 type, not always exposed on macOS headers
#ifndef errno_t
typedef int errno_t;
#endif

// ── Basic Windows types ───────────────────────────────────────────────────────
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   DWORD;
typedef long           BOOL;
typedef void*          LPVOID;
typedef unsigned int   UINT;
typedef long long      LONGLONG;
typedef unsigned long long ULONGLONG;
typedef int            INT;
typedef long           LONG;
typedef unsigned long  ULONG;

// boolean (used by ObjFileManip.cpp — Java-ish type, not standard C++)
typedef bool boolean;

// Win32 opaque handle types (not used functionally in the Mac build)
typedef void* HKEY;
typedef void* HINSTANCE;
typedef void* HWND;
typedef void* HMODULE;

// HANDLE is FILE* on macOS (PortaCreate/PortaWrite/PortaClose all use FILE*)
typedef FILE* HANDLE;

#define TRUE  1
#define FALSE 0

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// INVALID_HANDLE_VALUE: PortaOpen returns FILE* (NULL on failure)
#define INVALID_HANDLE_VALUE ((FILE*)NULL)

// ── qsort_s: Windows has (base, num, size, compare, ctx); macOS qsort_r is (base, num, size, ctx, compare) ──
// ponytail: wraps argument reorder — same semantics, different call-site order
static inline void qsort_s(void* base, size_t num, size_t size,
                            int (*compare)(void*, const void*, const void*),
                            void* ctx)
{
    qsort_r(base, num, size, ctx, compare);
}

// ── strcpy_s 2-arg template (Windows allows strcpy_s(dst,src) for arrays) ────
#ifdef __cplusplus
template<size_t N>
inline errno_t strcpy_s(char (&dst)[N], const char* src) {
    strncpy(dst, src, N - 1); dst[N - 1] = '\0'; return 0;
}
template<size_t N>
inline errno_t strcpy_s(wchar_t (&dst)[N], const wchar_t* src) {
    wcsncpy(dst, src, N - 1); dst[N - 1] = L'\0'; return 0;
}
// 3-arg versions for pointer targets (fall-through to macro below if not array)
inline errno_t strcpy_s(char* dst, size_t n, const char* src) {
    strncpy(dst, src, n - 1); dst[n - 1] = '\0'; return 0;
}
#endif

// ── Calling conventions (no-ops on ARM64/macOS) ───────────────────────────────
#define __stdcall
#define __cdecl
#define __forceinline __attribute__((always_inline)) inline
#ifndef __declspec
#define __declspec(x)
#endif
#define WINAPI
#define WINAPIV
#define CALLBACK

// ── TCHAR — keep as wchar_t to match existing L"..." literals in core files ───
#ifndef TCHAR
typedef wchar_t TCHAR;
#endif
#ifndef _T
#define _T(x) L##x
#endif
#define TEXT(x) L##x

// ── String aliases ────────────────────────────────────────────────────────────
#define _strdup strdup
#ifndef strcat_s
static inline errno_t _mwStrcat_s(char* dst, size_t n, const char* src) {
    strncat(dst, src, n - strlen(dst) - 1);
    return 0;
}
#define strcat_s(dst, n, src) _mwStrcat_s(dst, n, src)
#endif

// Wide-string aliases
#define _wcsicmp(a,b)       wcscasecmp(a,b)
#define _wcsnicmp(a,b,n)    wcsncasecmp(a,b,n)
// Narrow-string case-insensitive
#define _stricmp(a,b)       strcasecmp(a,b)
#define _strnicmp(a,b,n)    strncasecmp(a,b,n)

static inline errno_t _strlwr_s(char* s, size_t /*n*/) {
    for (; *s; ++s) *s = (char)tolower((unsigned char)*s);
    return 0;
}
static inline errno_t _wcslwr_s(wchar_t* s, size_t /*n*/) {
    for (; *s; ++s) *s = (wchar_t)towlower(*s);
    return 0;
}

// Time shims (__time32_t / _time32 / _localtime32_s / asctime_s)
#include <time.h>
#define __time32_t  time_t
#define _time32(p)  time(p)
static inline errno_t _localtime32_s(struct tm* t, const time_t* clock) {
    return localtime_r(clock, t) ? 0 : 1;
}
static inline errno_t asctime_s(char* buf, size_t sz, const struct tm* t) {
    char* s = asctime(t);
    if (!s) return 1;
    strncpy(buf, s, sz - 1);
    buf[sz - 1] = '\0';
    return 0;
}

static inline errno_t _mwWcscpy_s(wchar_t* dst, size_t n, const wchar_t* src) {
    wcsncpy(dst, src, n - 1);
    dst[n - 1] = L'\0';
    return 0;
}
#define wcscpy_s(dst, n, src) _mwWcscpy_s(dst, n, src)

static inline errno_t _mwWcscat_s(wchar_t* dst, size_t n, const wchar_t* src) {
    size_t dstLen = wcslen(dst);
    wcsncat(dst, src, n - dstLen - 1);
    return 0;
}
#define wcscat_s(dst, n, src) _mwWcscat_s(dst, n, src)
#ifndef wcsncat_s
#define wcsncat_s(dst, n, src, cnt) (wcsncat(dst, src, cnt), 0)
#endif

static inline errno_t wcsncpy_s(wchar_t* dst, size_t dstSz, const wchar_t* src, size_t cnt) {
    size_t n = (cnt < dstSz - 1) ? cnt : dstSz - 1;
    wcsncpy(dst, src, n);
    dst[n] = L'\0';
    return 0;
}

// ── Missing CRT helpers ───────────────────────────────────────────────────────
// _fileno: macOS has fileno() (POSIX); Windows spells it _fileno
#ifndef _fileno
#define _fileno fileno
#endif

// swprintf_s: macOS has swprintf with identical args
#ifndef swprintf_s
#define swprintf_s(buf, count, ...) swprintf(buf, count, __VA_ARGS__)
#endif

// wcscpy_s / wcscat_s (safe variants not in POSIX)
#ifndef wcscpy_s
#define wcscpy_s(dst, n, src) wcsncpy(dst, src, n)
#endif
#ifndef wcscat_s
#define wcscat_s(dst, n, src) wcsncat(dst, n - wcslen(dst) - 1, src)  // ponytail: approximate
#endif
#ifndef wcsncat_s
#define wcsncat_s(dst, n, src, cnt) wcsncat(dst, src, cnt)
#endif

// Checked conversions used by all wide-path wrappers below. The application sets a
// UTF-8 locale at startup, so the platform multibyte encoding is UTF-8. Both helpers
// fail instead of returning a truncated, unterminated path.
static inline char* _mwWideToUtf8Alloc(const wchar_t* src)
{
    if (!src) { errno = EINVAL; return NULL; }
    mbstate_t state = {};
    const wchar_t* scan = src;
    size_t len = wcsrtombs(NULL, &scan, 0, &state);
    if (len == (size_t)-1) return NULL;
    char* dst = (char*)malloc(len + 1);
    if (!dst) return NULL;
    state = {};
    scan = src;
    if (wcsrtombs(dst, &scan, len + 1, &state) != len) {
        free(dst);
        return NULL;
    }
    return dst;
}

static inline bool _mwWideToUtf8Buffer(const wchar_t* src, char* dst, size_t dstSize)
{
    if (!dst || dstSize == 0) { errno = EINVAL; return false; }
    dst[0] = '\0';
    char* converted = _mwWideToUtf8Alloc(src);
    if (!converted) return false;
    size_t len = strlen(converted);
    if (len >= dstSize) {
        free(converted);
        errno = ERANGE;
        return false;
    }
    memcpy(dst, converted, len + 1);
    free(converted);
    return true;
}

static inline bool _mwUtf8ToWideBuffer(const char* src, wchar_t* dst, size_t dstSize)
{
    if (!src || !dst || dstSize == 0) { errno = EINVAL; return false; }
    dst[0] = L'\0';
    mbstate_t state = {};
    const char* scan = src;
    size_t len = mbsrtowcs(NULL, &scan, 0, &state);
    if (len == (size_t)-1) return false;
    if (len >= dstSize) { errno = ERANGE; return false; }
    state = {};
    scan = src;
    return mbsrtowcs(dst, &scan, dstSize, &state) == len;
}

static inline char* _mwNormalizeWidePath(const wchar_t* wpath)
{
    char* path = _mwWideToUtf8Alloc(wpath);
    if (!path) return NULL;
    if (path[0] != '~' || (path[1] != '/' && path[1] != '\0')) return path;

    const char* home = getenv("HOME");
    if (!home) return path;
    size_t homeLen = strlen(home);
    size_t suffixLen = strlen(path + 1);
    if (homeLen > SIZE_MAX - suffixLen - 1) {
        free(path);
        errno = EOVERFLOW;
        return NULL;
    }
    char* expanded = (char*)malloc(homeLen + suffixLen + 1);
    if (!expanded) { free(path); return NULL; }
    memcpy(expanded, home, homeLen);
    memcpy(expanded + homeLen, path + 1, suffixLen + 1);
    free(path);
    return expanded;
}

// _wfopen_s: Windows wide-path fopen; macOS needs checked conversion
static inline int _wfopen_s(FILE** pFile, const wchar_t* path, const wchar_t* mode)
{
    if (!pFile) return EINVAL;
    *pFile = NULL;
    char* p = _mwNormalizeWidePath(path);
    char* m = _mwWideToUtf8Alloc(mode);
    if (!p || !m) { free(p); free(m); return errno ? errno : EINVAL; }
    *pFile = fopen(p, m);
    free(p);
    free(m);
    return (*pFile == NULL) ? 1 : 0;
}

// ── Win32 text encoding stubs (MultiByteToWideChar / WideCharToMultiByte) ────
// ponytail: only handles UTF-8 ↔ wchar_t; sufficient for Mineways path/name conversions
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 0x00000008
static inline int MultiByteToWideChar(unsigned /*cp*/, unsigned /*flags*/,
                                       const char* mbstr, int mblen,
                                       wchar_t* wcstr, int wclen)
{
    if (!mbstr || mblen < -1 || wclen < 0) { errno = EINVAL; return 0; }
    size_t inputLen = mblen == -1 ? strlen(mbstr) : (size_t)mblen;
    bool includeNull = mblen == -1 || (inputLen > 0 && mbstr[inputLen - 1] == '\0');
    if (mblen != -1 && includeNull) inputLen--;
    char* input = (char*)malloc(inputLen + 1);
    if (!input) return 0;
    memcpy(input, mbstr, inputLen);
    input[inputLen] = '\0';
    mbstate_t state = {};
    const char* scan = input;
    size_t converted = mbsrtowcs(NULL, &scan, 0, &state);
    if (converted == (size_t)-1 || converted > (size_t)INT_MAX - (includeNull ? 1 : 0)) {
        free(input);
        return 0;
    }
    size_t required = converted + (includeNull ? 1 : 0);
    if (!wcstr || wclen == 0) { free(input); return (int)required; }
    if ((size_t)wclen < required) { free(input); errno = ERANGE; return 0; }
    state = {};
    scan = input;
    size_t result = mbsrtowcs(wcstr, &scan, required, &state);
    free(input);
    if (result != converted) return 0;
    return (int)required;
}
static inline int WideCharToMultiByte(unsigned /*cp*/, unsigned /*flags*/,
                                       const wchar_t* wcstr, int wclen,
                                       char* mbstr, int mblen,
                                       const char* /*dflt*/, int* /*used*/)
{
    if (!wcstr || wclen < -1 || mblen < 0) { errno = EINVAL; return 0; }
    size_t inputLen = wclen == -1 ? wcslen(wcstr) : (size_t)wclen;
    bool includeNull = wclen == -1 || (inputLen > 0 && wcstr[inputLen - 1] == L'\0');
    if (wclen != -1 && includeNull) inputLen--;
    wchar_t* input = (wchar_t*)malloc((inputLen + 1) * sizeof(wchar_t));
    if (!input) return 0;
    memcpy(input, wcstr, inputLen * sizeof(wchar_t));
    input[inputLen] = L'\0';
    mbstate_t state = {};
    const wchar_t* scan = input;
    size_t converted = wcsrtombs(NULL, &scan, 0, &state);
    if (converted == (size_t)-1 || converted > (size_t)INT_MAX - (includeNull ? 1 : 0)) {
        free(input);
        return 0;
    }
    size_t required = converted + (includeNull ? 1 : 0);
    if (!mbstr || mblen == 0) { free(input); return (int)required; }
    if ((size_t)mblen < required) { free(input); errno = ERANGE; return 0; }
    state = {};
    scan = input;
    size_t result = wcsrtombs(mbstr, &scan, required, &state);
    free(input);
    if (result != converted) return 0;
    return (int)required;
}

// ── Win32 filesystem stubs ────────────────────────────────────────────────────
#define ERROR_ALREADY_EXISTS EEXIST
static inline DWORD GetLastError() { return (DWORD)errno; }

static inline BOOL CreateDirectoryW(const wchar_t* wpath, void* /*sa*/)
{
    char* path = _mwNormalizeWidePath(wpath);
    if (!path) return FALSE;
    BOOL result = (mkdir(path, 0755) == 0 || errno == EEXIST) ? TRUE : FALSE;
    free(path);
    return result;
}

// ── Win32 file I/O stubs (maps to fwrite/fread via HANDLE=FILE*) ─────────────
static inline BOOL WriteFile(HANDLE h, const void* buf, DWORD len,
                              DWORD* written, void* /*ov*/)
{
    if (!h || (!buf && len != 0)) { errno = EINVAL; return FALSE; }
    size_t n = fwrite(buf, 1, len, h);
    if (written) *written = (DWORD)n;
    return (n == (size_t)len && !ferror(h)) ? TRUE : FALSE;
}

static inline BOOL ReadFile(HANDLE h, void* buf, DWORD len,
                             DWORD* nread, void* /*ov*/)
{
    if (!h || (!buf && len != 0)) { errno = EINVAL; return FALSE; }
    size_t n = fread(buf, 1, len, h);
    if (nread) *nread = (DWORD)n;
    return ferror(h) ? FALSE : TRUE;
}

static inline DWORD GetFileSize(HANDLE h, DWORD* high)
{
    if (!h) { errno = EINVAL; return 0xFFFFFFFFu; }
    off_t cur = ftello(h);
    if (cur < 0 || fseeko(h, 0, SEEK_END) != 0) return 0xFFFFFFFFu;
    off_t size = ftello(h);
    if (fseeko(h, cur, SEEK_SET) != 0 || size < 0) return 0xFFFFFFFFu;
    uint64_t usize = (uint64_t)size;
    if (high) {
        *high = (DWORD)(usize >> 32);
    } else if (usize > UINT32_MAX) {
        // Callers using the low-word-only form cannot represent this file.
        errno = EOVERFLOW;
        return 0xFFFFFFFFu;
    }
    return (DWORD)(usize & UINT32_MAX);
}

// ── Wide-path PortaOpen/Create/Append (converts wchar_t* → UTF-8) ─────────────
// These are referenced by macros in stdafx.h below
static inline FILE* _mwPortaOpenW(const wchar_t* wpath)
{
    char* path = _mwNormalizeWidePath(wpath);
    if (!path) return NULL;
    FILE* file = fopen(path, "rb");
    free(path);
    return file;
}
static inline FILE* _mwPortaAppendW(const wchar_t* wpath)
{
    char* path = _mwNormalizeWidePath(wpath);
    if (!path) return NULL;
    FILE* file = fopen(path, "a");
    free(path);
    return file;
}
static inline FILE* _mwPortaCreateW(const wchar_t* wpath)
{
    char* path = _mwNormalizeWidePath(wpath);
    if (!path) return NULL;
    FILE* file = fopen(path, "wb");
    free(path);
    return file;
}

// ── Win32 directory enumeration (POSIX opendir/readdir/fnmatch backend) ───────
#include <dirent.h>
#include <fnmatch.h>

#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define _TRUNCATE                ((size_t)-1)

static inline DWORD GetFileAttributesW(const wchar_t* wpath)
{
    char* path = _mwNormalizeWidePath(wpath);
    if (!path) return INVALID_FILE_ATTRIBUTES;
    struct stat st;
    int result = stat(path, &st);
    free(path);
    if (result != 0) return INVALID_FILE_ATTRIBUTES;
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

typedef struct _WIN32_FIND_DATAW {
    DWORD   dwFileAttributes;
    wchar_t cFileName[MAX_PATH];
} WIN32_FIND_DATA;

struct _mwFindHandle {
    DIR*  dir;
    char* pattern;
    char* dirPath;
};

static inline bool _mwFindMatch(struct dirent* de, struct _mwFindHandle* fh,
                                WIN32_FIND_DATA* ffd)
{
    if (fnmatch(fh->pattern, de->d_name, FNM_CASEFOLD) != 0) return false;
    if (!_mwUtf8ToWideBuffer(de->d_name, ffd->cFileName, MAX_PATH)) return false;
    size_t dirLen = strlen(fh->dirPath);
    size_t nameLen = strlen(de->d_name);
    if (dirLen > SIZE_MAX - nameLen - 2) return false;
    char* full = (char*)malloc(dirLen + nameLen + 2);
    if (!full) return false;
    memcpy(full, fh->dirPath, dirLen);
    full[dirLen] = '/';
    memcpy(full + dirLen + 1, de->d_name, nameLen + 1);
    struct stat st;
    int statResult = stat(full, &st);
    free(full);
    ffd->dwFileAttributes = (statResult == 0 && S_ISDIR(st.st_mode))
                            ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    return true;
}

static inline HANDLE FindFirstFileW(const wchar_t* wpattern, WIN32_FIND_DATA* ffd)
{
    char* pat = _mwNormalizeWidePath(wpattern);
    if (!pat) return INVALID_HANDLE_VALUE;
    char* slash = strrchr(pat, '/');
    if (!slash) slash = strrchr(pat, '\\');
    char* dirPath;
    char* filePat;
    if (slash) {
        size_t n = (size_t)(slash - pat);
        dirPath = n == 0 ? strdup("/") : (char*)malloc(n + 1);
        filePat = strdup(slash + 1);
        if (dirPath && n != 0) { memcpy(dirPath, pat, n); dirPath[n] = '\0'; }
    } else {
        dirPath = strdup(".");
        filePat = strdup(pat);
    }
    free(pat);
    if (!dirPath || !filePat) { free(dirPath); free(filePat); return INVALID_HANDLE_VALUE; }
    DIR* d = opendir(dirPath);
    if (!d) { free(dirPath); free(filePat); return INVALID_HANDLE_VALUE; }
    struct _mwFindHandle* fh = (struct _mwFindHandle*)malloc(sizeof(struct _mwFindHandle));
    if (!fh) { closedir(d); free(dirPath); free(filePat); return INVALID_HANDLE_VALUE; }
    fh->dir = d;
    fh->pattern = filePat;
    fh->dirPath = dirPath;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (_mwFindMatch(de, fh, ffd)) return (HANDLE)fh;
    }
    closedir(d); free(fh->pattern); free(fh->dirPath); free(fh);
    return INVALID_HANDLE_VALUE;
}

static inline BOOL FindNextFileW(HANDLE hFind, WIN32_FIND_DATA* ffd)
{
    struct _mwFindHandle* fh = (struct _mwFindHandle*)hFind;
    struct dirent* de;
    while ((de = readdir(fh->dir)) != NULL) {
        if (_mwFindMatch(de, fh, ffd)) return TRUE;
    }
    return FALSE;
}

static inline BOOL FindClose(HANDLE hFind)
{
    struct _mwFindHandle* fh = (struct _mwFindHandle*)hFind;
    closedir(fh->dir);
    free(fh->pattern);
    free(fh->dirPath);
    free(fh);
    return TRUE;
}

#define FindFirstFile  FindFirstFileW
#define FindNextFile   FindNextFileW

// ── SKETCHFAB HTTP upload stubs (curl/openssl not needed for core export) ────
// If SKETCHFAB is defined the source pulls in PublishSkfb.h which needs curl.
// We undef it here; Mineways.cpp (the GUI) isn't compiled anyway.
#ifdef SKETCHFAB
#undef SKETCHFAB
#endif
