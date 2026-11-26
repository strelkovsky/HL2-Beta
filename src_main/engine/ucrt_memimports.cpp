// Modern UCRT headers generate indirect imports for allocation calls. Route
// those imports through the same Valve allocator as memoverride.cpp.
#include "tier0/memalloc.h"
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#if defined(_MSC_VER) && _MSC_VER >= 1900
static void * __cdecl EngineMalloc(size_t n) { return g_pMemAlloc->Alloc(n); }
static void __cdecl EngineFree(void *p) { g_pMemAlloc->Free(p); }
static void * __cdecl EngineRealloc(void *p, size_t n) { return g_pMemAlloc->Realloc(p, n); }
static void * __cdecl EngineCalloc(size_t count, size_t size)
{
    if (size && count > static_cast<size_t>(-1) / size) return NULL;
    size_t bytes = count * size;
    void *p = g_pMemAlloc->Alloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}
static size_t __cdecl EngineMsize(void *p) { return g_pMemAlloc->GetSize(p); }
static void * __cdecl EngineMallocDbg(size_t n, int, const char *file, int line) { return g_pMemAlloc->Alloc(n, file, line); }
static void __cdecl EngineFreeDbg(void *p, int) { g_pMemAlloc->Free(p); }
static void * __cdecl EngineReallocDbg(void *p, size_t n, int, const char *file, int line) { return g_pMemAlloc->Realloc(p, n, file, line); }
static void * __cdecl EngineCallocDbg(size_t count, size_t size, int, const char *file, int line)
{
    if (size && count > static_cast<size_t>(-1) / size) return NULL;
    size_t bytes = count * size;
    void *p = g_pMemAlloc->Alloc(bytes, file, line);
    if (p) memset(p, 0, bytes);
    return p;
}
static size_t __cdecl EngineMsizeDbg(void *p, int) { return g_pMemAlloc->GetSize(p); }
static int __cdecl EngineHeapCheck() { return g_pMemAlloc->heapchk(); }
static void __cdecl EngineMemCheckpoint(_CrtMemState *p) { g_pMemAlloc->CrtMemCheckpoint(p); }

// Route imported CRT diagnostics to the same allocator callback as memoverride.
static int __cdecl EngineCrtDbgReport(int type, const char *file, int line, const char *module, const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format ? format : "", args);
    va_end(args);
    return g_pMemAlloc->CrtDbgReport(type, file, line, module, message);
}

// On x86 the compiler prepends an underscore to these symbols.
extern "C" {
int (__cdecl *_imp___CrtDbgReport)(int, const char *, int, const char *, const char *, ...) = EngineCrtDbgReport;
void * (__cdecl *_imp__malloc)(size_t) = EngineMalloc;
void (__cdecl *_imp__free)(void *) = EngineFree;
void * (__cdecl *_imp__realloc)(void *, size_t) = EngineRealloc;
void * (__cdecl *_imp__calloc)(size_t, size_t) = EngineCalloc;
size_t (__cdecl *_imp___msize)(void *) = EngineMsize;
void * (__cdecl *_imp___malloc_dbg)(size_t, int, const char *, int) = EngineMallocDbg;
void (__cdecl *_imp___free_dbg)(void *, int) = EngineFreeDbg;
void * (__cdecl *_imp___realloc_dbg)(void *, size_t, int, const char *, int) = EngineReallocDbg;
void * (__cdecl *_imp___calloc_dbg)(size_t, size_t, int, const char *, int) = EngineCallocDbg;
size_t (__cdecl *_imp___msize_dbg)(void *, int) = EngineMsizeDbg;
int (__cdecl *_imp___heapchk)() = EngineHeapCheck;
void (__cdecl *_imp___CrtMemCheckpoint)(_CrtMemState *) = EngineMemCheckpoint;
}
#endif
