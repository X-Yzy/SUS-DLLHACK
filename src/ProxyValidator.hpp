#pragma once

#include "ProxyGenerator.hpp"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class ProxyValidator : public QObject {
    Q_OBJECT

public:
    explicit ProxyValidator(QObject *parent = nullptr);
    void start(const QString &hostExePath, const QString &targetDllPath);
    bool isRunning() const;

signals:
    void logLine(const QString &line);
    void exportsLoaded(const QVector<PeExportEntry> &exports);
    void validationFinished(const QStringList &calledExports, const QString &logPath, const QString &sourcePath);
    void failed(const QString &reason);

private:
    void prepareSandbox();
    void startCompile();
    void startHost();
    QStringList readCalledExports() const;

    QProcess m_compileProcess;
    QProcess m_hostProcess;
    QTimer m_compileTimer;
    QString m_hostExePath;
    QString m_targetDllPath;
    QString m_sandboxDir;
    QString m_localHostExe;
    QString m_localTargetDll;
    QString m_logPath;
    QString m_compileLogPath;
    QString m_compileOutput;
    GeneratedProxy m_proxy;
    bool m_compileTimedOut = false;
};
