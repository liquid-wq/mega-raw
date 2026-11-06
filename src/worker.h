// Monitor-Worker. Logik 1:1 aus der Qt-Fassung (monitor_worker.h/.cpp).
//
// Geaendert ist nur die Verdrahtung: statt QThread ein std::thread, statt
// Qt-Signalen Rueckrufe (std::function). Die Rueckrufe laufen im
// Worker-Thread; die Oberflaeche reicht sie in ihre eigene Warteschlange
// weiter (App::Post).
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ed_serial.h"
#include "i18n.h"
#include "md_snoop.h"
#include "ra_client.h"
#include "ra_engine.h"

// Ersatz fuer QElapsedTimer.
class Uhr {
public:
    void start() { t_ = std::chrono::steady_clock::now(); gueltig_ = true; }
    bool isValid() const { return gueltig_; }
    long long elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_).count();
    }
    long long restart() { const long long e = elapsed(); start(); return e; }
private:
    std::chrono::steady_clock::time_point t_{};
    bool gueltig_ = false;
};

class MonitorWorker {
public:
    MonitorWorker(std::string port, Game game, std::string user, std::string token,
                  std::shared_ptr<RaClient> client, bool hardcore = false);
    ~MonitorWorker();

    // Startet den eigenen Thread (QThread::started -> start()).
    void startThread();
    void stop();
    // Laeuft der Thread noch? (fuer das Aufraeumen)
    bool laeuft() const { return threadLaeuft_; }
    // Wartet hoechstens ms auf das Ende; false, wenn er noch laeuft.
    bool warte(int ms);

    void updateGame(const Game& g) {
        std::lock_guard<std::mutex> lk(gameMtx_);
        game_ = g;
        havePrevRam_ = false;
        mapDirty_ = true;
        writeSessionMarker(g);
    }

    // --- Rueckrufe (im Worker-Thread) ---
    // Logzeile in beiden Sprachen (Anzeige folgt dem Sprachwechsel).
    std::function<void(const Zweisprachig&)> log;
    std::function<void(bool)> palState;
    std::function<void(const Bytes&)> bramDump;
    std::function<void(const std::string&, int, long long)> unlocked;
    std::function<void(const std::string&)> connectionLost;
    std::function<void()> coreMissing;
    std::function<void()> gameLeft;
    std::function<void(const std::string&)> hardcoreDowngraded;
    std::function<void()> finished;

private:
    void start();
    void emitLog(const Zweisprachig& m);

    void writeSessionMarker(const Game& g);
    void richteKopieEin(mdsnoop::MdSnoop& snoop, const std::vector<uint32_t>& addrs, bool zuordnungOk);
    bool altWegErlaubt() const { return coreBuild_ < 132 || plattform_ == 1; }
    int plattform_ = -1;         // 1 CORE, 2 PRO, 0 unbekannt, -1 nicht gelesen
    // datei = false: nur in die Diagnose (Einrichtung erfolgreich).
    std::string schreibeBericht(const std::vector<std::string>& zeilen, bool datei = true);

    int coreBuild_ = 0;
    int kopierWeg_ = 0;          // 0 alter Mitschnitt, 1 Weg W, 2 Weg R

    // Bildpuffer (Mapper ab Build 130): jedes Bild einzeln auswerten
    bool ring_ = false;
    std::map<uint32_t, uint32_t> ringLage_;
    uint32_t ringGroesse_ = 0;
    bool altUnplausibel_ = false;   // alter Weg: zuletzt alle FF/00 verworfen
    uint32_t ringPruef_ = 0;
    uint32_t ringSp_ = 0;
    int ringStapel_ = 0;
    int diagRingBad_ = 0;
    int diagRingStapel_ = 0;
    uint32_t diagRingSpMin_ = 0xFFFFFFFF;
    bool ringPost_ = false;
    bool ringHint_ = false;
    int ringBadAlt_ = 0;
    bool ringSperre_ = false;
    int ringSperreBis_ = 0;
    int diagRingNest_ = 0;
    std::map<uint32_t, int> spHisto_;
    uint32_t spRuhe_ = 0;
    int diagBusy_ = 0;
    int postZwang_ = -1;
    Uhr wechselUhr_;
    int ringAbschnitt_ = 1024;
    int ringTiefe_ = 8;
    int ringLetzte_ = -1;
    Uhr ringUhr_;
    bool ringLuecke_ = false;
    int diagRingResets_ = 0;
    int diagRingBilder_ = 0;
    int diagRingVerloren_ = 0;
    uint16_t diagRingWert_ = 0;

    // Diagnosestation: eine Zeile je Sekunde, dazu jede Logzeile mit
    // Zeitstempel. Wird im Speicher gesammelt und nur bei einem Fehler als
    // diagnose_live.txt neben die EXE geschrieben (diagSichern).
    void diagOeffnen();
    void diagZeile(const std::string& z);
    void diagSichern();
    void diagSekunde(mdsnoop::MdSnoop& snoop, const RamMap& ram);
    std::vector<uint32_t> diagExtra() const;
    bool diagDatei_ = false;     // Diagnose laeuft (Name wie in der Qt-Fassung)
    std::string diagPuffer_;
    std::string diagKopf_;
    bool diagFehler_ = false;    // Verbindung verloren - beim Ende sichern
    Uhr diagUhr_;
    Uhr diagTakt_;
    int diagPolls_ = 0;
    long long diagPollSumMs_ = 0;
    long long diagPollMaxMs_ = 0;
    int diagAenderungen_ = 0;
    int diagHokAlt_ = -1;
    long long diagVbAlt_ = -1;
    int diagFcAlt_ = -1;
    uint32_t diagPruef_ = 0;

    int consolePal_ = -1;
    int coreVerified_ = -1;
    bool hardcore_ = false;
    bool mapDirty_ = true;
    bool mapFailed_ = false;

    std::string port_;
    Game game_;
    std::string user_;
    std::string token_;
    std::shared_ptr<RaClient> client_;
    std::atomic<bool> running_{false};
    std::mutex gameMtx_;

    RamMap prevRam_;
    bool havePrevRam_ = false;

    std::thread thread_;
    std::atomic<bool> threadLaeuft_{false};
};
