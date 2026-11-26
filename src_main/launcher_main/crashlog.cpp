#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void LogLine(HANDLE file, const char* format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    int length = _vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
    va_end(args);
    if (length < 0) length = (int)strlen(text);
    DWORD written;
    WriteFile(file, text, length, &written, NULL);
}

LONG WINAPI LogUnhandledException(EXCEPTION_POINTERS* exception)
{
    char path[MAX_PATH];
    DWORD pathLength = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!pathLength || pathLength >= sizeof(path)) return EXCEPTION_CONTINUE_SEARCH;
    char* slash = strrchr(path, '\\');
    if (!slash) return EXCEPTION_CONTINUE_SEARCH;
    strcpy_s(slash + 1, sizeof(path) - (slash + 1 - path), "hl2_errors.log");
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME now; GetLocalTime(&now);
    EXCEPTION_RECORD* record = exception->ExceptionRecord;
    LogLine(file, "\r\n[%04u-%02u-%02u %02u:%02u:%02u] Unhandled exception 0x%08lX at %p; pid=%lu thread=%lu\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        record->ExceptionCode, record->ExceptionAddress, GetCurrentProcessId(), GetCurrentThreadId());
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        LogLine(file, "Access: %s at %p\r\n", record->ExceptionInformation[0] == 1 ? "write" : record->ExceptionInformation[0] == 8 ? "execute" : "read", (void*)record->ExceptionInformation[1]);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    if (SymInitialize(process, NULL, TRUE))
    {
        CONTEXT context = *exception->ContextRecord;
        STACKFRAME64 frame = {};
        frame.AddrPC.Offset = context.Eip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Ebp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Esp; frame.AddrStack.Mode = AddrModeFlat;
        for (int index = 0; index < 64 && frame.AddrPC.Offset; ++index)
        {
            DWORD64 address = frame.AddrPC.Offset, displacement = 0;
            char storage[sizeof(SYMBOL_INFO) + 1024] = {};
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)storage;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = 1024;
            IMAGEHLP_MODULE64 module = {}; module.SizeOfStruct = sizeof(module);
            IMAGEHLP_LINE64 line = {}; line.SizeOfStruct = sizeof(line); DWORD lineOffset = 0;
            BOOL hasModule = SymGetModuleInfo64(process, address, &module);
            BOOL hasSymbol = SymFromAddr(process, address, &displacement, symbol);
            BOOL hasLine = SymGetLineFromAddr64(process, address, &lineOffset, &line);
            LogLine(file, "#%02d %08llX %s!%s +0x%llX", index, address,
                hasModule ? module.ModuleName : "?", hasSymbol ? symbol->Name : "?", displacement);
            if (hasLine) LogLine(file, " %s:%lu", line.FileName, line.LineNumber);
            LogLine(file, "\r\n");
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, GetCurrentThread(), &frame, &context,
                NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
        }
        SymCleanup(process);
    }
    else LogLine(file, "Symbol initialization failed: %lu\r\n", GetLastError());
    CloseHandle(file);
    return EXCEPTION_EXECUTE_HANDLER;
}
