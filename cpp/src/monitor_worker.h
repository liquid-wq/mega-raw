#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <memory>
#include "ed_serial_qt.h"
#include "ra_engine.h"
#include "ra_client.h"

constexpr int BRAM_LEN = 208;

// Worker. Laeuft komplett off-UI-Thread (blockierende Serial-Calls).
// Bewusst NICHT portiert (fehlende GUI-Ruecksignale wuerden den Rahmen
// sprengen): Auto-Spielerkennung ueber Stub-GameID, PAL/NTSC-Live-Anzeige,
// recording.csv. Kernstueck (BRAM lesen -> Achievements pruefen -> awarden)
// ist vollstaendig vorhanden.
class MonitorWorker : public QObject {
    Q_OBJECT
public:
    MonitorWorker(QString port, Game game, QString user, QString token,
                  std::shared_ptr<RaClient> client, long long stubAddr = 0, bool tvFlash = false);

public slots:
    void start();
    void stop();
    void requestTvFlash() { tvFlashRequested_ = true; }
    void updateGame(const Game& g) {
        QMutexLocker lk(&gameMtx_);
        game_ = g;
        havePrevRam_ = false; // Reset
    }

signals:
    void log(const QString& msg);
    void gameDetected(int gameId);
    void palState(bool isPal);
    void bramDump(const QByteArray& rawBytes);
    void unlocked(const QString& title, int points, qlonglong achId);
    void connectionLost(const QString& reason);
    void finished();

private:
    int autoGidTried_ = 0;
    int consolePal_ = -1;  // -1 unbekannt, 0 NTSC, 1 PAL
    long long stubAddr_ = 0;
    bool tvFlash_ = false;
    int stubVerified_ = -1;  // -1 nicht geprueft, 0 fehlgeschlagen, 1 ok
    QString port_;
    Game game_;
    QString user_;
    QString token_;
    std::shared_ptr<RaClient> client_;
    std::atomic<bool> running_{false};
    std::atomic<bool> tvFlashRequested_{false};
    QMutex gameMtx_;

    RamMap prevRam_;
    bool havePrevRam_ = false;
};
