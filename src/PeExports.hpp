#pragma once

#include <QString>
#include <QVector>

struct PeExportEntry {
    QString decoratedName;
    QString undecoratedName;
    quint32 ordinal = 0;
    quint32 rva = 0;
};

class PeExports {
public:
    static QVector<PeExportEntry> read(const QString &dllPath);
};
