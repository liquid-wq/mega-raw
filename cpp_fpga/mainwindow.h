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
    void onDetectGame();
    void onSetupMapper();
    void onRestoreMappers();
    void onLogin();
    void playUnlockSound();
    void updateMonitorState();
    void onOptions();
    void onToggleMonitor();

    void onWorkerLog(const QString& msg);
    void onWorkerRam(const QByteArray& rawBytes);
    void onWorkerRomIdentified(const QString& fingerprint, const QString& title);
    void onWorkerUnlocked(const QString& title, int points, qlonglong achId);
    void onWorkerConnectionLost(const QString& reason);
    void onWorkerCoreMissing();
    void onWorkerFinished();

private:
    QLineEdit* userEdit_;
    QLineEdit* passEdit_;
    QPushButton* loginBtn_;
    QLabel* loginStatus_;

    QLineEdit* romPathEdit_;
    QPushButton* chooseBtn_;
    QPushButton* detectBtn_ = nullptr;
    QPushButton* setupMapperBtn_ = nullptr;
    class QLabel* edStatusLabel_ = nullptr;
    class QLabel* monitorHint_ = nullptr;
    bool setHasSet_ = false;
    bool diagCompare_ = false;
    QLabel* statusLabel_;
    QPlainTextEdit* log_;

    QPushButton* monitorBtn_;
    class QCheckBox* hardcoreChk_ = nullptr;
    QLabel* ramLabel_;
    QByteArray lastRamBytes_;
    QVector<qint64> lastByteChangeMs_;
    class QListWidget* acList_;

    QString romPath_;
    BadgeLoader badges_;
    class AchievementPopup* achPopup_ = nullptr;
    int consolePal_ = -1;
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
    void loadRom(QString path);
    bool detectGame();
    void diagnoseRead(const QByteArray& rom);
    void tryLoadGame(const std::string& md5);
    void stopMonitorIfRunning();
    void rebuildAcList();
    void checkForUpdate();
};
