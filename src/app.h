// Hauptfenster -- Nachbau von MainWindow der Qt-Fassung (mainwindow.h/.cpp).
//
// Aufbau, Texte, Farben und Ablaeufe folgen der Qt-Fassung. Weil Dear ImGui
// jedes Bild neu zeichnet, gibt es keine Widgets mit eigenem Zustand: der
// Zustand steht in Membern, Zeichnen() stellt ihn dar.
//
// Eigenstaendiges Modul im Namensraum megaraw: Zeichnen() fuellt den
// uebergebenen Bereich. main.cpp ist nur der Rahmen (Fenster, Intro,
// Schleife) -- so laesst sich das Modul spaeter in ein Werkzeug mit mehreren
// Programmen einhaengen, ohne die Logik anzufassen.
#pragma once

#include "bilder.h"
#include "einstellungen.h"
#include "i18n.h"
#include "ra_cache.h"
#include "ra_client.h"
#include "ra_engine.h"
#include "worker.h"

#include "imgui.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace megaraw {

struct Schriften {
    ImFont* ui = nullptr;            // Segoe UI (Qt-Standardschrift)
    ImFont* fett = nullptr;          // Segoe UI fett
    ImFont* kursiv = nullptr;        // Segoe UI kursiv
    ImFont* courier = nullptr;       // Courier New (Schnellstart, Spielstatus)
    ImFont* consolas = nullptr;      // RAM-Anzeige
    ImFont* consolasFett = nullptr;  // RAM-Anzeige, geaenderte Bytes
};

class App {
public:
    App(void* fenster, float dpi, const Schriften& schriften);
    ~App();

    // Groesse des Hauptfensters (resize(700, 820)), logische Punkte.
    static constexpr float kBreite = 700.0f;
    static constexpr float kHoehe = 820.0f;

    // Fenstertitel wie setWindowTitle() der Qt-Fassung.
    static std::string Fenstertitel();

    // Nach dem Anlegen des OpenGL-Kontexts.
    void Starten();
    // Ein Bild, in den Bereich pos/groesse.
    void Zeichnen(ImVec2 pos, ImVec2 groesse);

    // Aus beliebigem Thread: Auftrag fuer den Oberflaechen-Thread.
    void Post(std::function<void()> f);

    // Fuer die Absturzspur.
    std::vector<std::string> LogZeilen() const;

private:
    // ---------------- Ablaeufe (MainWindow-Slots) ----------------
    void onChooseRom();
    void onSetupMapper();
    void setupMapperIn(const std::string& root, const std::vector<uint8_t>& src);
    void onRestoreMappers();
    void restoreIn(const std::string& root);
    void onLogin();
    void onLogout();
    void speichereLogin();
    void ZeichneLoginStatus();
    static void playUnlockSound();
    void updateMonitorState();
    void onToggleMonitor();
    void onWorkerLog(const Zweisprachig& msg);
    void onWorkerRam(const Bytes& raw);
    void onWorkerUnlocked(const std::string& title, int points, long long achId);
    void onWorkerConnectionLost(const std::string& reason);
    void onWorkerCoreMissing();
    void onWorkerFinished();

    void appendLog(const Zweisprachig& line);
    void loadRom(std::string path);
    bool detectGame();
    void tryLoadGame(const std::string& md5);
    void stopMonitorIfRunning();
    void rebuildAcList();
    void checkForUpdate();

    // Der Kern, der auf die Karte kommt: mega-core.x25 neben der Exe, sonst
    // der eingebettete.
    std::vector<uint8_t> coreDaten() const;
    // Dasselbe fuer das PRO: mega-pro.rbf.
    std::vector<uint8_t> proDaten() const;

    // ---------------- Dialoge (QMessageBox, QDialog) ----------------
    struct Dialog {
        enum class Art { Info, Warnung, Frage, Optionen, Kofi, Katze, Nachricht, Ersteinrichtung, };
        int schritt = 0;         // Ersteinrichtung: 0 Sprache, 1 SD-Karte
        Art art = Art::Info;
        std::string titel, text;
        std::string link;        // Nachricht: optionaler Link (nur https)
        std::string textEn;      // Nachricht: englischer Text (text = deutsch)
        std::function<void()> ja, nein;
        int nummer = 0;
        bool geoeffnet = false;
        std::string sprache;     // Optionen: gewaehlte Sprache (Kennung)
    };
    void Info(const std::string& titel, const std::string& text, std::function<void()> ok = nullptr);
    void Warnung(const std::string& titel, const std::string& text);
    void Frage(const std::string& titel, const std::string& text, std::function<void()> ja);
    void ZeichneDialog();
    // Nachricht an die Nutzer: data/nachricht_megaraw.json auf der Seite,
    // jede id nur einmal (settings.json "message_read").
    void NachrichtHolen();
    std::deque<Dialog> dialoge_;
    int dialogNummer_ = 0;

    // ---------------- Badges (badge_loader.cpp) ----------------
    struct Badge {
        enum class Zustand { Laedt, Fertig, Fehlt } zustand = Zustand::Laedt;
        bilder::Textur tex;
    };
    Badge* BadgeHolen(const std::string& name);
    void BadgeThread();
    std::string BadgePng(const std::string& name);   // synchron, fuer das Popup

    // ---------------- Zeichnen ----------------
    void ZeichneMenue();
    void ZeichneKatze(const char* id);
    void ZeichneRam(float breite);
    void ZeichneListe(float hoehe);
    void ZeichneLog(float hoehe);
    struct Gruppe { ImVec2 start; float breite = 0, titelBreite = 0, titelHoehe = 0; std::string titel; };
    std::vector<Gruppe> gruppen_;
    void GruppeBeginn(const char* id, const std::string& titel);
    void GruppeEnde();

    // ---------------- Felder ----------------
    void* fenster_ = nullptr;
    float dpi_ = 1.0f;
    Schriften f_;

    std::string settingsPath_;
    Settings settings_;
    bool katzeDirekt_ = false;     // Ko-fi im Menue: Katzenfenster oeffnen
    bool ersterStart_ = false;   // noch keine settings.json -> Ersteinrichtung
    std::shared_ptr<RaCache> cache_;
    std::shared_ptr<RaClient> client_;

    // Ziel fuer Auftraege aus anderen Threads. Ueberlebt die App, damit ein
    // spaeter fertig werdender Thread nicht ins Leere schreibt.
    struct PostZiel {
        std::mutex m;
        App* app = nullptr;
        std::vector<std::function<void()>> auftraege;
    };
    std::shared_ptr<PostZiel> post_;

    std::unique_ptr<MonitorWorker> monWorker_;
    int monGeneration_ = 0;
    bool monitoring_ = false;

    std::optional<Game> game_;
    std::string token_;
    std::string romPath_;
    bool setHasSet_ = false;
    int consolePal_ = -1;

    // Eingabefelder und Anzeigen
    char userBuf_[128] = {};
    char passBuf_[128] = {};
    // Anzeigetexte in beiden Sprachen: nach einem Sprachwechsel steht die
    // gerade angezeigte Meldung sofort in der neuen Sprache.
    bool loginLaeuft_ = false;
    Zweisprachig loginStatus_;
    bool loginOk_ = false;
    bool merken_ = true;                 // "Eingeloggt bleiben"
    double anmeldStart_ = -1.0;          // Einblendung nach der Anmeldung (ImGui::GetTime)
    Zweisprachig anmeldText_;
    Zweisprachig romPathText_;           // leer: Platzhalter
    Zweisprachig statusText_;
    enum class StatusStil { Normal, Gruen, Rot, Grau } statusStil_ = StatusStil::Normal;
    Zweisprachig edStatus_;
    Zweisprachig monitorHint_;
    ImU32 monitorHintFarbe_ = 0;
    bool monitorBtnEnabled_ = false;

    // RAM-Anzeige (ramLabel_)
    enum class RamStil { Bytes, Text, Fehler } ramStil_ = RamStil::Text;
    Zweisprachig ramText_;
    Bytes lastRamBytes_;
    std::vector<long long> lastByteChangeMs_;

    // Achievement-Liste (acList_)
    struct ListZeile { std::string text, badge; ImU32 farbe = 0; long long id = 0; };
    std::vector<ListZeile> liste_;
    int auswahl_ = -1;

    std::deque<Zweisprachig> log_;
    mutable std::mutex logMutex_;
    bool logAmEnde_ = true;
    bool erkennungErklaert_ = false;  // detectGame hat den Grund schon ins Log geschrieben
    bool verbindungWeg_ = false;     // Monitor endete wegen Verbindungsverlust
    double logKopiertZeit_ = 0;      // "Kopiert!" fuer 2 s
    size_t logZeilenGezeichnet_ = 0;

    bilder::Textur jason_;
    bilder::Textur jasonGross_;   // Katzenfenster

    std::mutex badgeMutex_;
    std::map<std::string, Badge> badges_;
    std::deque<std::string> badgeWarteschlange_;
    std::condition_variable badgeCv_;
    std::thread badgeThread_;
    bool badgeEnde_ = false;
    struct BadgeErgebnis { std::string name; bool ok; int w, h; std::vector<uint8_t> rgba; };
    std::vector<BadgeErgebnis> badgeFertig_;

    std::vector<std::thread> hilfsThreads_;
};

}  // namespace megaraw
