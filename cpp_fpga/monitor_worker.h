#pragma once
#include <QObject>
#include <QString>
#include <QMutex>
#include <QMutexLocker>
#include <QFile>
#include <QBuffer>
#include <QElapsedTimer>
#include <atomic>
#include <map>
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
    // Unser Core laeuft nicht mehr (Spiel verlassen). Der Worker beendet
    // sich danach selbst.
    void gameLeft();
    // Hardcore auf Softcore zurueckgestuft (In-Game-Menu/Cheats am
    // EverDrive aktiv, oder Status nicht lesbar). liste = Klartext der
    // Gruende, leer wenn nicht lesbar.
    void hardcoreDowngraded(const QString& liste);
    void finished();

private:
    void writeSessionMarker(const Game& g);
    // Ab Mapper-Build 128: Kopierprogramm laden, pruefen, einschalten.
    // Bleibt kopierWeg_ 0, laeuft der alte Mitschnitt weiter.
    void richteKopieEin(mdsnoop::MdSnoop& snoop, const std::vector<uint32_t>& addrs, bool zuordnungOk);
    // Der alte Mitschnitt liefert nur auf dem CORE verlaessliche Werte. Ab
    // Core-Build 132 meldet der Core sein Geraet (B003); vorher gilt das
    // bisherige Verhalten.
    bool altWegErlaubt() const { return coreBuild_ < 132 || plattform_ == 1; }
    int plattform_ = -1;         // 1 CORE, 2 PRO, 0 unbekannt, -1 nicht gelesen
    // datei = false: nur in die Diagnose (Einrichtung erfolgreich).
    QString schreibeBericht(const QStringList& zeilen, bool datei = true);

    int coreBuild_ = 0;
    int kopierWeg_ = 0;          // 0 alter Mitschnitt, 1 Weg W, 2 Weg R

    // Bildpuffer (Mapper ab Build 130): jedes Bild einzeln auswerten
    bool ring_ = false;
    std::map<uint32_t, uint32_t> ringLage_;   // Work-RAM-Offset -> Position
    uint32_t ringGroesse_ = 0;
    uint32_t ringPruef_ = 0;
    uint32_t ringSp_ = 0;        // Position des Stapelzeigers im Bild, 0 = keiner
    int ringStapel_ = 0;         // Byte, die das Programm unter den Stapelzeiger schreibt
    int diagRingBad_ = 0;        // Bilder mit falschem Pruefwert
    int diagRingStapel_ = 0;     // Bilder, in denen unser Sichern ueberwachte Adressen traf
    uint32_t diagRingSpMin_ = 0xFFFFFFFF;
    bool ringPost_ = false;      // Nachher-Modus (nach dem VBlank-Teil des Spiels)
    bool ringHint_ = false;      // HBlank-Betrieb (Spiel ohne VBlank-Interrupt)
    int ringBadAlt_ = 0;         // letzter Stand von B004
    bool ringSperre_ = false;    // Bilder bis ringSperreBis_ verwerfen
    int ringSperreBis_ = 0;
    int diagRingNest_ = 0;       // deshalb verworfene Bilder
    std::map<uint32_t, int> spHisto_;   // Stapelzeiger beim Bildwechsel -> Haeufigkeit
    uint32_t spRuhe_ = 0;        // Ruhewert (Spiel wartet aufs naechste Bild)
    int diagBusy_ = 0;           // Bilder, in denen das Spiel nicht in Ruhe war (Lag)
    int postZwang_ = -1;         // Test: Nachher-Modus erzwingen (1) / verbieten (0)
    QElapsedTimer wechselUhr_;   // Test: Moduswechsel
    int ringAbschnitt_ = 1024;   // Bytes je Bild im Spiegel
    int ringTiefe_ = 8;          // Bilder im Ring
    int ringLetzte_ = -1;        // zuletzt ausgewerteter Lauf (7 Bit), -1 = neu
    QElapsedTimer ringUhr_;      // Abstand der Abfragen (Ueberlauf des Zaehlers)
    bool ringLuecke_ = false;    // seit dem letzten ausgewerteten Bild fehlen Bilder
    int diagRingResets_ = 0;     // Ruecksetzungen wegen einer Luecke
    int diagRingBilder_ = 0;     // ausgewertete Bilder seit der letzten Diagnosezeile
    int diagRingVerloren_ = 0;   // nicht rechtzeitig gelesene Bilder
    uint16_t diagRingWert_ = 0;  // Pruefwert im zuletzt gelesenen Bild

    // Diagnosestation: eine Zeile je Sekunde, dazu jede Logzeile mit
    // Zeitstempel. Wird im Speicher gesammelt und nur bei einem Fehler als
    // diagnose_live.txt neben die EXE geschrieben (diagSichern).
    void diagOeffnen();
    void diagZeile(const QString& z);
    void diagSichern();
    void diagSekunde(mdsnoop::MdSnoop& snoop, const RamMap& ram);
    std::vector<uint32_t> diagExtra() const;
    QBuffer diagDatei_;
    QByteArray diagKopf_;
    bool diagFehler_ = false;    // Verbindung verloren - beim Ende sichern
    QElapsedTimer diagUhr_;
    QElapsedTimer diagTakt_;
    int diagPolls_ = 0;
    qint64 diagPollSumMs_ = 0;
    qint64 diagPollMaxMs_ = 0;
    int diagAenderungen_ = 0;
    int diagHokAlt_ = -1;
    long long diagVbAlt_ = -1;
    int diagFcAlt_ = -1;
    uint32_t diagPruef_ = 0;     // Pruefstelle des aktiven Kopierprogramms

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
