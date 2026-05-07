#include "PeExports.hpp"

#include "PEFile.hpp"

#include <string>

QVector<PeExportEntry> PeExports::read(const QString &dllPath)
{
    QVector<PeExportEntry> entries;
    PEFile pe(dllPath.toLocal8Bit().constData());
    if (!pe.isValid()) {
        return entries;
    }

    for (const auto &entry : pe.readExports()) {
        PeExportEntry out;
        out.decoratedName = QString::fromUtf8(entry.decoratedName.c_str());
        out.undecoratedName = QString::fromUtf8(entry.undecoratedName.c_str());
        out.ordinal = entry.ordinal;
        out.rva = entry.rva;
        entries.push_back(out);
    }
    return entries;
}
