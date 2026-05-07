#include "ZeroEyeTask.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

ZeroEyeTask::ZeroEyeTask(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &ZeroEyeTask::flushOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, &ZeroEyeTask::flushOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit failed(m_process.errorString());
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        flushOutput();
        emit finished(exitCode, m_workingDirectory);
    });
}

void ZeroEyeTask::run(const ZeroEyeOptions &options, const QString &workingDirectory)
{
    if (m_process.state() != QProcess::NotRunning) {
        emit failed(QString::fromUtf8("扫描任务正在运行。"));
        return;
    }

    m_workingDirectory = workingDirectory;
    QDir().mkpath(m_workingDirectory);
    const QString exe = QDir(QCoreApplication::applicationDirPath()).filePath("zeroeye_cli.exe");
    if (!QFileInfo::exists(exe)) {
        emit failed(QString::fromUtf8("未在程序目录找到 zeroeye_cli.exe。"));
        return;
    }

    const QStringList args = buildArguments(options);
    emit logLine(QString::fromUtf8("调用 ZeroEye 扫描核心：%1").arg(args.join(' ')));

    m_process.setProgram(exe);
    m_process.setArguments(args);
    m_process.setWorkingDirectory(workingDirectory);
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_process.start();
}

bool ZeroEyeTask::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

void ZeroEyeTask::stop()
{
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    m_process.terminate();
    if (!m_process.waitForFinished(1500)) {
        m_process.kill();
        m_process.waitForFinished(1500);
    }
}

void ZeroEyeTask::flushOutput()
{
    const QByteArray data = m_process.readAllStandardOutput() + m_process.readAllStandardError();
    const QString text = QString::fromUtf8(data);
    for (const QString &line : text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)) {
        emit logLine(line);
    }
}

QStringList ZeroEyeTask::buildArguments(const ZeroEyeOptions &options) const
{
    QStringList args;
    auto addPair = [&args](const QString &key, const QString &value) {
        if (!value.trimmed().isEmpty()) {
            args << key << value;
        }
    };

    addPair("-i", options.inputFile);
    addPair("-p", options.scanDirectory);
    addPair("-d", options.moduleFile);
    addPair("-IM", options.importFile);
    addPair("-EX", options.exportFile);
    if (options.signatureOnly) {
        args << "-s";
    }
    if (options.excludeSystemOnly) {
        args << "-e";
    }
    addPair("-x", options.arch);
    addPair("-g", options.excludeList);
    addPair("-t", options.scanType);
    return args;
}
