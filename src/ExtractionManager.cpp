#include "ExtractionManager.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {
QString uniqueDirectory(const QString &parent, const QString &baseName)
{
    QDir dir(parent);
    QString candidate = baseName;
    int index = 2;
    while (dir.exists(candidate)) {
        candidate = QString("%1(%2)").arg(baseName).arg(index++);
    }
    dir.mkpath(candidate);
    return dir.filePath(candidate);
}

QString safeRelativePath(const QString &root, const QString &filePath)
{
    QDir rootDir(root);
    QString relative = rootDir.relativeFilePath(filePath);
    if (relative.startsWith("..") || QFileInfo(relative).isAbsolute()) {
        const QFileInfo info(filePath);
        relative = info.fileName();
    }
    return QDir::fromNativeSeparators(relative);
}

void copyFilePreservingRoot(const QString &sourcePath, const QString &scanRoot, const QString &targetRoot)
{
    if (!QFileInfo::exists(sourcePath)) {
        return;
    }
    const QString relative = safeRelativePath(scanRoot, sourcePath);
    const QString target = QDir(targetRoot).filePath(relative);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::copy(sourcePath, target);
}

QStringList originalPathsFromInfo(const QString &infoPath)
{
    QFile file(infoPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QStringList paths;
    const QString text = QString::fromUtf8(file.readAll());
    const auto lines = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
    static const QRegularExpression drivePath(R"(([A-Za-z]:[\\/][^)\r\n]+))");
    for (const QString &line : lines) {
        const auto match = drivePath.match(line);
        if (match.hasMatch()) {
            paths << QDir::fromNativeSeparators(match.captured(1).trimmed());
        }
    }
    paths.removeDuplicates();
    return paths;
}
}

QList<ScanResult> ExtractionManager::discover(const QString &workingDirectory)
{
    QList<ScanResult> results;
    QString eyebin = QDir(workingDirectory).filePath("Whitebin");
    if (!QDir(eyebin).exists()) {
        eyebin = QDir(workingDirectory).filePath("Eyebin");
    }
    if (!QDir(eyebin).exists()) {
        return results;
    }

    QDirIterator it(eyebin, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QFileInfo::exists(QDir(path).filePath("infos/Info.txt"))) {
            const ScanResult result = parseBundle(path);
            if (!result.name.isEmpty()) {
                results << result;
            }
        }
    }
    return results;
}

QString ExtractionManager::extractPreservingTree(const QList<ScanResult> &results,
                                                 const QString &scanRoot,
                                                 const QString &workingDirectory)
{
    const QString outputRoot = QDir(workingDirectory).filePath("Extracted");
    QDir().mkpath(outputRoot);

    for (const ScanResult &result : results) {
        const QFileInfo bundleInfo(result.bundlePath);
        const QString targetRoot = uniqueDirectory(outputRoot, bundleInfo.fileName());

        const QString originalTree = QDir(targetRoot).filePath("original_tree");
        QDir().mkpath(originalTree);
        for (const QString &sourcePath : originalPathsFromInfo(result.infoPath)) {
            copyFilePreservingRoot(sourcePath, scanRoot, originalTree);
        }

        const QString bundleCopy = QDir(targetRoot).filePath("zeroeye_bundle");
        QDir().mkpath(bundleCopy);
        QDirIterator files(result.bundlePath, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString source = files.next();
            const QString relative = QDir(result.bundlePath).relativeFilePath(source);
            const QString target = QDir(bundleCopy).filePath(relative);
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::copy(source, target);
        }
    }

    return outputRoot;
}

ScanResult ExtractionManager::parseBundle(const QString &path)
{
    ScanResult result;
    const QFileInfo info(path);
    const QString dirName = info.fileName();

    static const QRegularExpression fullPattern(R"(^(.+)\[(gui|cmd|dotnet-core|dotnet)-(\d+)-([^\]]+)\](?:\(\d+\))?$)");
    static const QRegularExpression sysPattern(R"(^(.+)\[(sys)-([^\]]+)\](?:\(\d+\))?$)");

    auto match = fullPattern.match(dirName);
    if (match.hasMatch()) {
        result.name = match.captured(1);
        result.type = match.captured(2);
        result.dllCount = match.captured(3).toInt();
        result.sizeText = match.captured(4);
    } else {
        match = sysPattern.match(dirName);
        if (!match.hasMatch()) {
            return {};
        }
        result.name = match.captured(1);
        result.type = match.captured(2);
        result.dllCount = 1;
        result.sizeText = match.captured(3);
    }

    result.bundlePath = path;
    result.infoPath = QDir(path).filePath("infos/Info.txt");
    return result;
}
