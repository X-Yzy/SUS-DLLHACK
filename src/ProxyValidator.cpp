#include "ProxyValidator.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {
bool copyFileOverwrite(const QString &from, const QString &to)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo::exists(to)) {
        QFile::remove(to);
    }
    return QFile::copy(from, to);
}

QString gccPath()
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
    return "gcc.exe";
}

QString summarizeCompilerOutput(const QString &text)
{
    QStringList picked;
    const auto lines = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString clean = line.simplified();
        if (clean.contains("error:", Qt::CaseInsensitive)
            || clean.contains("undefined symbol", Qt::CaseInsensitive)
            || clean.contains("could not", Qt::CaseInsensitive)
            || clean.contains("no such file", Qt::CaseInsensitive)
            || clean.contains("ld.exe:", Qt::CaseInsensitive)
            || clean.contains("gcc.exe:", Qt::CaseInsensitive)) {
            picked << clean;
        }
        if (picked.size() >= 3) {
            break;
        }
    }
    return picked.join(" | ");
}
}

ProxyValidator::ProxyValidator(QObject *parent)
    : QObject(parent)
{
    m_compileTimer.setSingleShot(true);
    m_compileTimer.setInterval(25000);
    connect(&m_compileTimer, &QTimer::timeout, this, [this]() {
        if (m_compileProcess.state() == QProcess::NotRunning) {
            return;
        }
        m_compileTimedOut = true;
        m_compileProcess.kill();
        emit failed(QString::fromUtf8("代理 DLL 编译超时，已跳过当前候选。请确认 tools/mingw64 是否完整。"));
    });

    connect(&m_compileProcess, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString text = QString::fromUtf8(m_compileProcess.readAllStandardOutput());
        m_compileOutput += text;
        for (const QString &line : text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
            emit logLine(line);
        }
    });
    connect(&m_compileProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QString text = QString::fromUtf8(m_compileProcess.readAllStandardError());
        m_compileOutput += text;
        for (const QString &line : text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
            emit logLine(line);
        }
    });
    connect(&m_compileProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_compileTimedOut) {
            return;
        }
        m_compileTimer.stop();
        if (error == QProcess::FailedToStart) {
            emit failed(QString::fromUtf8("无法启动代理编译器：%1").arg(m_compileProcess.errorString()));
        } else {
            emit failed(QString::fromUtf8("代理编译进程异常：%1").arg(m_compileProcess.errorString()));
        }
    });
    connect(&m_compileProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        m_compileTimer.stop();
        if (m_compileTimedOut) {
            m_compileTimedOut = false;
            return;
        }
        if (exitCode != 0) {
            QFile log(m_compileLogPath);
            if (log.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                QTextStream out(&log);
                out.setEncoding(QStringConverter::Utf8);
                out << m_compileOutput;
            }
            const QString detail = summarizeCompilerOutput(m_compileOutput);
            emit failed(detail.isEmpty()
                ? QString::fromUtf8("代理 DLL 编译失败，退出码：%1。详细信息：%2").arg(exitCode).arg(m_compileLogPath)
                : QString::fromUtf8("代理 DLL 编译失败：%1。详细信息：%2").arg(detail, m_compileLogPath));
            return;
        }
        emit logLine(QString::fromUtf8("代理 DLL 编译完成，开始在沙箱中启动宿主程序。"));
        startHost();
    });
}

void ProxyValidator::start(const QString &hostExePath, const QString &targetDllPath)
{
    if (isRunning()) {
        emit failed(QString::fromUtf8("劫持验证正在运行。"));
        return;
    }
    m_hostExePath = hostExePath;
    m_targetDllPath = targetDllPath;
    prepareSandbox();
    startCompile();
}

bool ProxyValidator::isRunning() const
{
    return m_compileProcess.state() != QProcess::NotRunning
        || m_hostProcess.state() != QProcess::NotRunning;
}

void ProxyValidator::prepareSandbox()
{
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    m_sandboxDir = QDir(QCoreApplication::applicationDirPath()).filePath("log/Work/proxy_" + stamp);
    QDir().mkpath(m_sandboxDir);

    const QFileInfo hostInfo(m_hostExePath);
    const QFileInfo dllInfo(m_targetDllPath);
    const QDir hostDir(hostInfo.absolutePath());

    QDirIterator files(hostDir.absolutePath(), QDir::Files);
    while (files.hasNext()) {
        const QString source = files.next();
        const QString target = QDir(m_sandboxDir).filePath(QFileInfo(source).fileName());
        copyFileOverwrite(source, target);
    }

    m_localHostExe = QDir(m_sandboxDir).filePath(hostInfo.fileName());
    m_localTargetDll = QDir(m_sandboxDir).filePath(dllInfo.fileName());
    if (!QFileInfo::exists(m_localTargetDll)) {
        copyFileOverwrite(m_targetDllPath, m_localTargetDll);
    }

    const QString originalName = dllInfo.completeBaseName() + "_orig.dll";
    const QString localOriginal = QDir(m_sandboxDir).filePath(originalName);
    copyFileOverwrite(m_localTargetDll, localOriginal);
    QFile::remove(m_localTargetDll);

    ProxyGenerator generator;
    m_proxy = generator.generateProbeSource(localOriginal, m_sandboxDir);
    m_logPath = QDir(m_sandboxDir).filePath("proxy.log");
    m_compileLogPath = QDir(m_sandboxDir).filePath("compile.log");
    emit exportsLoaded(m_proxy.exports);
    emit logLine(QString::fromUtf8("已生成代理源码：%1").arg(m_proxy.sourcePath));
}

void ProxyValidator::startCompile()
{
    QStringList args;
    args << "-fno-use-linker-plugin"
         << "-shared"
         << "-nostdlib"
         << "-Os"
         << "-s"
         << "-fno-stack-protector"
         << "-fno-builtin"
         << "-Wl,-e,DllMain"
         << "-o" << m_localTargetDll
         << m_proxy.sourcePath
         << m_proxy.defPath
         << "-lkernel32"
         << "-luser32";

    const QString compiler = gccPath();
    emit logLine(QString::fromUtf8("代理编译器：%1").arg(compiler));
    m_compileTimedOut = false;
    m_compileOutput.clear();
    m_compileProcess.setProgram(compiler);
    m_compileProcess.setArguments(args);
    m_compileProcess.setWorkingDirectory(m_sandboxDir);
    m_compileProcess.start();
    m_compileTimer.start();
}

void ProxyValidator::startHost()
{
    if (!QFileInfo::exists(m_localHostExe)) {
        emit failed(QString::fromUtf8("沙箱中的宿主 EXE 不存在。"));
        return;
    }

    m_hostProcess.setProgram(m_localHostExe);
    m_hostProcess.setWorkingDirectory(m_sandboxDir);
    m_hostProcess.start();
    if (!m_hostProcess.waitForStarted(3000)) {
        emit failed(QString::fromUtf8("宿主程序无法启动。"));
        return;
    }

    QTimer::singleShot(6000, this, [this]() {
        if (m_hostProcess.state() != QProcess::NotRunning) {
            m_hostProcess.terminate();
            if (!m_hostProcess.waitForFinished(1500)) {
                m_hostProcess.kill();
                m_hostProcess.waitForFinished(1500);
            }
        }
        const QStringList called = readCalledExports();
        emit validationFinished(called, m_logPath, m_proxy.sourcePath);
    });
}

QStringList ProxyValidator::readCalledExports() const
{
    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QStringList called;
    const QString text = QString::fromUtf8(file.readAll());
    const auto lines = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith("CALL ")) {
            called << line.mid(5).trimmed();
        }
    }
    called.removeDuplicates();
    return called;
}
