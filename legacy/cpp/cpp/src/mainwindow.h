#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QVector>
#include "badge_loader.h"
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QThread>
#include <optional>
#include <memory>
#include "ra_engine.h"
#include "ra_client.h"
#include "ra_cache.h"
#include "settings.h"
#include "monitor_worker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onChooseRom();
    void onLogin();
    void onPatch();
    void playUnlockSound();
    void onSdExport();
    void onBatchPatch();
    void updateMonitorState();
    void onTvTest();
    void onOptions();
    void onToggleMonitor();
    void onUpdateHooks();

    void onWorkerLog(const QString& msg);
    void onWorkerBram(const QByteArray& rawBytes);
    void onWorkerUnlocked(const QString& title, int points, qlonglong achId);
    void onWorkerConnectionLost(const QString& reason);
    void onWorkerFinished();

private:
    QLineEdit* userEdit_;
    QLineEdit* passEdit_;
    QPushButton* loginBtn_;
    QLabel* loginStatus_;
    QPushButton* updateHooksBtn_ = nullptr;

    QLineEdit* romPathEdit_;
    QPushButton* chooseBtn_;
    QPushButton* patchBtn_;
    class QCheckBox* lightStubCheck_;
    class QPushButton* sdExportBtn_;
    class QPushButton* sdCancelBtn_ = nullptr;
    class QProgressBar* busyIndicator_ = nullptr;
    class QPushButton* batchPatchBtn_ = nullptr;
    class QLabel* edStatusLabel_ = nullptr;
    class QLabel* monitorHint_ = nullptr;
    bool setHasSet_ = false;
    QLabel* statusLabel_;
    QPlainTextEdit* log_;

    QPushButton* monitorBtn_;
    QLabel* bramLabel_;
    QByteArray lastBramBytes_;
    QVector<qint64> lastByteChangeMs_;
    class QListWidget* acList_;

    QString romPath_;
    BadgeLoader badges_;
    int consolePal_ = -1;
    long long stubAddrForVerify_ = 0;
    std::optional<Game> game_;
    QString token_;

    std::shared_ptr<RaCache> cache_;
    std::shared_ptr<RaClient> client_;
    Settings settings_;
    QString settingsPath_;

    QThread* monThread_ = nullptr;
    MonitorWorker* monWorker_ = nullptr;
    bool monitoring_ = false;

    void appendLog(const QString& line);
    void tryLoadGame(const std::string& md5);
    void stopMonitorIfRunning();
    void rebuildAcList();
    void checkForUpdate();
    QString hooksCachePath() const;
};
