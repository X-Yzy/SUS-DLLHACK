#pragma once

#include <QList>
#include <QString>

struct ScanResult {
    QString name;
    QString type;
    int dllCount = 0;
    QString sizeText;
    QString bundlePath;
    QString infoPath;
};

class ExtractionManager {
public:
    static QList<ScanResult> discover(const QString &workingDirectory);
    static QString extractPreservingTree(const QList<ScanResult> &results,
                                         const QString &scanRoot,
                                         const QString &workingDirectory);

private:
    static ScanResult parseBundle(const QString &path);
};
