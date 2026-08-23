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
#include "md_snoop.h"
#include "md_romid.h"

// Worker. Laeuft komplett off-UI-Thread (blockierende Serial-Calls).
// Liest den Work-RAM ueber den Custom-Mapper (md_snoop) statt aus dem
// Stub-Puffer. Spielerkennung und Anti-Cheat laufen ueber den ROM-Header
// im Cartridge-PSRAM (md_romid).
class MonitorWorker : public QObject {
    Q_OBJECT
public:
    MonitorWorker(QString port, Game game, QString user, QString token,
                  std::shared_ptr<RaClient> client, long long unused = 0, bool hardcore = false);

public slots:
    void start();
    void stop();
    void updateGame(const Game& g) {
        QMutexLocker lk(&gameMtx_);
        game_ = g;
        havePrevRam_ = false;
        mapDirty_ = true;
        writeSessionMarker(g);
    }

signals:
    void log(const QString& msg);
    void gameDetected(int gameId);
    void romIdentified(const QString& fingerprint, const QString& title);
    void palState(bool isPal);
    void bramDump(const QByteArray& rawBytes);
    void unlocked(const QString& title, int points, qlonglong achId);
    void connectionLost(const QString& reason);
    void coreMissing();
    // Hardcore auf Softcore zurueckgestuft (In-Game-Menu/Cheats am
    // EverDrive aktiv, oder Status nicht lesbar). liste = Klartext der
    // Gruende, leer wenn nicht lesbar.
    void hardcoreDowngraded(const QString& liste);
    void finished();

private:
    void writeSessionMarker(const Game& g);

    int consolePal_ = -1;        // -1 unbekannt, 0 NTSC, 1 PAL
    int coreVerified_ = -1;
    bool hardcore_ = false;
    bool mapDirty_ = true;       // Blockzuordnung muss neu geschrieben werden
    bool mapFailed_ = false;     // Set passt nicht in die verfuegbaren Slots

    QString port_;
    Game game_;
    QString user_;
    QString token_;
    std::shared_ptr<RaClient> client_;
    std::atomic<bool> running_{false};
    QMutex gameMtx_;

    RamMap prevRam_;
    bool havePrevRam_ = false;
};
