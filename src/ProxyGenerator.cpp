#include "ProxyGenerator.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {
QString displayExportName(const PeExportEntry &entry)
{
    return entry.undecoratedName.isEmpty() ? entry.decoratedName : entry.undecoratedName;
}

QString defExportName(const QString &name)
{
    QString escaped = name;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    return "\"" + escaped + "\"";
}
}

GeneratedProxy ProxyGenerator::generateProbeSource(const QString &targetDllPath,
                                                   const QString &outputDirectory) const
{
    QDir().mkpath(outputDirectory);

    const QFileInfo dllInfo(targetDllPath);
    const QString dllStem = dllInfo.completeBaseName();
    const QString originalDllName = dllInfo.fileName();
    const QVector<PeExportEntry> exports = PeExports::read(targetDllPath);
    const QString sourcePath = QDir(outputDirectory).filePath(dllStem + "_logging_proxy.c");
    const QString defPath = QDir(outputDirectory).filePath(dllStem + "_logging_proxy.def");

    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }

    QTextStream out(&source);
    out.setEncoding(QStringConverter::Utf8);
    out << "typedef unsigned long long uintptr_t;\n";
    out << "typedef long long intptr_t;\n";
    out << "typedef void* HANDLE;\n";
    out << "typedef void* HMODULE;\n";
    out << "typedef void* FARPROC;\n";
    out << "typedef void* LPVOID;\n";
    out << "typedef const void* LPCVOID;\n";
    out << "typedef const char* LPCSTR;\n";
    out << "typedef unsigned long DWORD;\n";
    out << "typedef int BOOL;\n";
    out << "#define TRUE 1\n";
    out << "#define MAX_PATH 260\n";
    out << "#define DLL_PROCESS_ATTACH 1\n";
    out << "#define DLL_PROCESS_DETACH 0\n";
    out << "#define FILE_APPEND_DATA 0x0004\n";
    out << "#define FILE_SHARE_READ 0x00000001\n";
    out << "#define FILE_SHARE_WRITE 0x00000002\n";
    out << "#define OPEN_ALWAYS 4\n";
    out << "#define FILE_ATTRIBUTE_NORMAL 0x00000080\n";
    out << "#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)\n";
    out << "#define MAKEINTRESOURCEA(i) ((LPCSTR)((uintptr_t)((unsigned short)(i))))\n\n";
    out << "__declspec(dllimport) DWORD __stdcall GetModuleFileNameA(HMODULE, char*, DWORD);\n";
    out << "__declspec(dllimport) int __stdcall lstrlenA(LPCSTR);\n";
    out << "__declspec(dllimport) char* __stdcall lstrcatA(char*, LPCSTR);\n";
    out << "__declspec(dllimport) HANDLE __stdcall CreateFileA(LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);\n";
    out << "__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, LPCVOID, DWORD, DWORD*, LPVOID);\n";
    out << "__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);\n";
    out << "__declspec(dllimport) HMODULE __stdcall LoadLibraryA(LPCSTR);\n";
    out << "__declspec(dllimport) FARPROC __stdcall GetProcAddress(HMODULE, LPCSTR);\n";
    out << "__declspec(dllimport) BOOL __stdcall FreeLibrary(HMODULE);\n";
    out << "__declspec(dllimport) BOOL __stdcall DisableThreadLibraryCalls(HMODULE);\n";
    out << "__declspec(dllimport) void __stdcall OutputDebugStringA(LPCSTR);\n";
    out << "__declspec(dllimport) int __stdcall wsprintfA(char*, LPCSTR, ...);\n\n";
    out << "static HMODULE gSelfModule = 0;\n";
    out << "static HMODULE gOriginalDll = 0;\n";
    out << "static const char kOriginalDllName[] = \"" << escapeCppString(originalDllName) << "\";\n\n";
    out << "static void DllHackAppendLine(const char* text) {\n";
    out << "    char modulePath[MAX_PATH];\n";
    out << "    int i;\n";
    out << "    for (i = 0; i < MAX_PATH; ++i) modulePath[i] = 0;\n";
    out << "    GetModuleFileNameA(gSelfModule, modulePath, MAX_PATH);\n";
    out << "    for (i = lstrlenA(modulePath) - 1; i >= 0; --i) {\n";
    out << "        if (modulePath[i] == '\\\\' || modulePath[i] == '/') { modulePath[i + 1] = 0; break; }\n";
    out << "    }\n";
    out << "    lstrcatA(modulePath, \"proxy.log\");\n";
    out << "    HANDLE h = CreateFileA(modulePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);\n";
    out << "    if (h != INVALID_HANDLE_VALUE) {\n";
    out << "        DWORD written = 0;\n";
    out << "        WriteFile(h, text, (DWORD)lstrlenA(text), &written, 0);\n";
    out << "        WriteFile(h, \"\\r\\n\", 2, &written, 0);\n";
    out << "        CloseHandle(h);\n";
    out << "    }\n";
    out << "}\n\n";
    out << "__declspec(noinline) void DllHackWriteLog(const char* name) {\n";
    out << "    char line[512];\n";
    out << "    int i;\n";
    out << "    for (i = 0; i < 512; ++i) line[i] = 0;\n";
    out << "    wsprintfA(line, \"CALL %s\", name);\n";
    out << "    OutputDebugStringA(line);\n";
    out << "    DllHackAppendLine(line);\n";
    out << "}\n\n";
    out << "void DllHackMissingExport(void) { DllHackWriteLog(\"<missing export>\"); }\n\n";

    for (int i = 0; i < exports.size(); ++i) {
        const auto &entry = exports[i];
        const QString stub = QString("DllHackProxyStub_%1").arg(i);
        const QString proc = QString("gProc_%1").arg(i);
        const QString name = QString("DllHackExportName_%1").arg(i);

        out << "FARPROC " << proc << " = 0;\n";
        out << "const char " << name << "[] = \"" << escapeCppString(displayExportName(entry)) << "\";\n";
        out << "void " << stub << "(void);\n";
        out << "__asm__(\n";
        out << "    \".intel_syntax noprefix\\n\"\n";
        out << "    \".text\\n\"\n";
        out << "    \".globl " << stub << "\\n\"\n";
        out << "    \"" << stub << ":\\n\"\n";
        out << "    \"pushfq\\n\"\n";
        out << "    \"push rax\\npush rcx\\npush rdx\\npush rbx\\npush rbp\\npush rsi\\npush rdi\\n\"\n";
        out << "    \"push r8\\npush r9\\npush r10\\npush r11\\npush r12\\npush r13\\npush r14\\npush r15\\n\"\n";
        out << "    \"sub rsp, 136\\n\"\n";
        out << "    \"movdqu [rsp + 32], xmm0\\nmovdqu [rsp + 48], xmm1\\nmovdqu [rsp + 64], xmm2\\n\"\n";
        out << "    \"movdqu [rsp + 80], xmm3\\nmovdqu [rsp + 96], xmm4\\nmovdqu [rsp + 112], xmm5\\n\"\n";
        out << "    \"lea rcx, [rip + " << name << "]\\n\"\n";
        out << "    \"call DllHackWriteLog\\n\"\n";
        out << "    \"movdqu xmm0, [rsp + 32]\\nmovdqu xmm1, [rsp + 48]\\nmovdqu xmm2, [rsp + 64]\\n\"\n";
        out << "    \"movdqu xmm3, [rsp + 80]\\nmovdqu xmm4, [rsp + 96]\\nmovdqu xmm5, [rsp + 112]\\n\"\n";
        out << "    \"add rsp, 136\\n\"\n";
        out << "    \"pop r15\\npop r14\\npop r13\\npop r12\\npop r11\\npop r10\\npop r9\\npop r8\\n\"\n";
        out << "    \"pop rdi\\npop rsi\\npop rbp\\npop rbx\\npop rdx\\npop rcx\\npop rax\\n\"\n";
        out << "    \"popfq\\n\"\n";
        out << "    \"jmp qword ptr [rip + " << proc << "]\\n\"\n";
        out << "    \".att_syntax prefix\\n\"\n";
        out << ");\n\n";
    }

    out << "static void DllHackResolveExports(void) {\n";
    out << "    char modulePath[MAX_PATH];\n";
    out << "    int i;\n";
    out << "    for (i = 0; i < MAX_PATH; ++i) modulePath[i] = 0;\n";
    out << "    GetModuleFileNameA(gSelfModule, modulePath, MAX_PATH);\n";
    out << "    for (i = lstrlenA(modulePath) - 1; i >= 0; --i) {\n";
    out << "        if (modulePath[i] == '\\\\' || modulePath[i] == '/') { modulePath[i + 1] = 0; break; }\n";
    out << "    }\n";
    out << "    lstrcatA(modulePath, kOriginalDllName);\n";
    out << "    gOriginalDll = LoadLibraryA(modulePath);\n";
    out << "    if (!gOriginalDll) return;\n";
    for (int i = 0; i < exports.size(); ++i) {
        const auto &entry = exports[i];
        out << "    gProc_" << i << " = GetProcAddress(gOriginalDll, \""
            << escapeCppString(entry.decoratedName) << "\");\n";
        out << "    if (!gProc_" << i << ") gProc_" << i
            << " = GetProcAddress(gOriginalDll, MAKEINTRESOURCEA(" << entry.ordinal << "));\n";
        out << "    if (!gProc_" << i << ") gProc_" << i << " = (FARPROC)&DllHackMissingExport;\n";
    }
    out << "}\n\n";
    out << "BOOL __stdcall DllMain(HMODULE module, DWORD reason, LPVOID reserved) {\n";
    out << "    (void)reserved;\n";
    out << "    if (reason == DLL_PROCESS_ATTACH) {\n";
    out << "        gSelfModule = module;\n";
    out << "        DisableThreadLibraryCalls(module);\n";
    out << "        DllHackAppendLine(\"=== proxy loaded ===\");\n";
    out << "        DllHackResolveExports();\n";
    out << "    } else if (reason == DLL_PROCESS_DETACH) {\n";
    out << "        if (gOriginalDll) FreeLibrary(gOriginalDll);\n";
    out << "    }\n";
    out << "    return TRUE;\n";
    out << "}\n";
    source.close();

    QFile def(defPath);
    if (!def.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return {};
    }
    QTextStream defOut(&def);
    defOut.setEncoding(QStringConverter::Utf8);
    defOut << "LIBRARY \"" << escapeCppString(dllInfo.fileName()) << "\"\n";
    defOut << "EXPORTS\n";
    for (int i = 0; i < exports.size(); ++i) {
        const auto &entry = exports[i];
        defOut << "    " << defExportName(entry.decoratedName)
               << "=DllHackProxyStub_" << i
               << " @" << entry.ordinal << "\n";
    }

    GeneratedProxy generated;
    generated.sourcePath = sourcePath;
    generated.defPath = defPath;
    generated.originalDllName = originalDllName;
    generated.exports = exports;
    return generated;
}

QString ProxyGenerator::escapeCppString(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch == '\\') out += "\\\\";
        else if (ch == '"') out += "\\\"";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else out += ch;
    }
    return out;
}

QString ProxyGenerator::escapeLinkerExport(const QString &value)
{
    QString out = value;
    out.replace("\\", "\\\\");
    out.replace("\"", "\\\"");
    return out;
}

QString ProxyGenerator::safeStem(const QString &value)
{
    QString out = value;
    out.replace(QRegularExpression("[^A-Za-z0-9_]"), "_");
    if (out.isEmpty() || out.front().isDigit()) {
        out.prepend('_');
    }
    return out;
}
