#pragma once

#include "ExtractionManager.hpp"
#include "PeExports.hpp"
#include "ProxyValidator.hpp"
#include "ZeroEyeTask.hpp"

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QQueue>
#include <QTableWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    struct ValidationJob {
        int row = -1;
        QString hostExe;
        QString dllPath;
        QString label;
    };

    struct VerifiedCandidate {
        QString hostExe;
        QString dllPath;
        QString label;
        QString sourcePath;
        QStringList calledExports;
    };

    void buildUi();
    void connectSignals();
    void appendConsole(const QString &line);
    void appendScanLog(const QString &line);
    void appendEvent(const QString &level, const QString &message);
    ZeroEyeOptions collectOptions() const;
    void normalizeScanOutput(const QString &workingDirectory);
    void refreshResults(const QString &workingDirectory);
    void startBatchValidation();
    void enqueueBundleJobs(int row, const ScanResult &result);
    void runNextValidation();
    void updateValidationStatus(int row, const QString &status, const QColor &color);
    void showCalledExports(const QStringList &called, const ValidationJob &job, const QString &savedCppPath);
    QString saveAnnotatedProxySource(const QString &sourcePath, const QString &dllPath, const QStringList &called);
    void addVerifiedCandidate(const ValidationJob &job, const QStringList &called, const QString &sourcePath);
    void generateProxyDll();
    void saveTemplateSource();
    void clearCache();
    void loadVerifiedCandidatesFromLog();
    void setExports(const QVector<PeExportEntry> &exports);
    void markCalledExports(const QStringList &called);

    QLineEdit *m_scanPath = nullptr;
    QLineEdit *m_inputFile = nullptr;
    QLineEdit *m_moduleFile = nullptr;
    QLineEdit *m_importFile = nullptr;
    QLineEdit *m_exportFile = nullptr;
    QLineEdit *m_excludeList = nullptr;
    QLineEdit *m_hostExe = nullptr;
    QLineEdit *m_proxyDll = nullptr;
    QLineEdit *m_templatePath = nullptr;
    QLineEdit *m_binPath = nullptr;
    QPushButton *m_runScanButton = nullptr;
    QPushButton *m_stopScanButton = nullptr;
    QComboBox *m_arch = nullptr;
    QComboBox *m_scanType = nullptr;
    QComboBox *m_generatorTarget = nullptr;
    QComboBox *m_templatePreset = nullptr;
    QCheckBox *m_signatureOnly = nullptr;
    QCheckBox *m_excludeSystem = nullptr;
    QCheckBox *m_autoValidate = nullptr;
    QCheckBox *m_sgnObfuscate = nullptr;
    QCheckBox *m_xorEncrypt = nullptr;
    QLineEdit *m_xorKey = nullptr;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTableWidget *m_results = nullptr;
    QListWidget *m_exports = nullptr;
    QPlainTextEdit *m_console = nullptr;
    QPlainTextEdit *m_templateEditor = nullptr;

    ZeroEyeTask m_zeroEye;
    ProxyValidator m_validator;
    QList<ScanResult> m_resultItems;
    QVector<PeExportEntry> m_exportItems;
    QQueue<ValidationJob> m_validationQueue;
    QList<VerifiedCandidate> m_verifiedCandidates;
    QString m_templateSource;
    ValidationJob m_activeJob;
    bool m_batchActive = false;
    bool m_scanStopRequested = false;
};
