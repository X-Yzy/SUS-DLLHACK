#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

struct ZeroEyeOptions {
    QString inputFile;
    QString scanDirectory;
    QString moduleFile;
    QString importFile;
    QString exportFile;
    QString arch;
    QString scanType = "all";
    QString excludeList;
    bool signatureOnly = false;
    bool excludeSystemOnly = false;
};

class ZeroEyeTask : public QObject {
    Q_OBJECT

public:
    explicit ZeroEyeTask(QObject *parent = nullptr);
    void run(const ZeroEyeOptions &options, const QString &workingDirectory);
    void stop();
    bool isRunning() const;

signals:
    void logLine(const QString &line);
    void finished(int exitCode, const QString &workingDirectory);
    void failed(const QString &reason);

private slots:
    void flushOutput();

private:
    QStringList buildArguments(const ZeroEyeOptions &options) const;

    QProcess m_process;
    QString m_workingDirectory;
};
