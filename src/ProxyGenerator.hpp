#pragma once

#include "PeExports.hpp"

#include <QString>
#include <QVector>

struct GeneratedProxy {
    QString sourcePath;
    QString defPath;
    QString originalDllName;
    QVector<PeExportEntry> exports;
};

class ProxyGenerator {
public:
    GeneratedProxy generateProbeSource(const QString &targetDllPath, const QString &outputDirectory) const;

private:
    static QString escapeCppString(const QString &value);
    static QString escapeLinkerExport(const QString &value);
    static QString safeStem(const QString &value);
};
