#include "i18n.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>

namespace i18n {
namespace {

std::atomic<int> g_index{1};    // Vorgabe englisch
std::string g_sprache = "en";
std::mutex g_mutex;
thread_local int g_fest = -1;   // SpracheFest: -1 frei, sonst Sprachindex

int Aktiv() { return g_fest >= 0 ? g_fest : g_index.load(); }

const std::unordered_map<std::string, std::string>& TabelleEn() {
    static const std::unordered_map<std::string, std::string> t = []() {
        std::unordered_map<std::string, std::string> m = {
#include "i18n_tabelle.inc"
        };
        // Texte, die in der Qt-Fassung ohne T() im Code standen oder erst
        // diese Fassung braucht. Ein vorhandener Eintrag hat Vorrang.
        const std::pair<const char*, const char*> neu[] = {
            {"  [bereits freigeschaltet]", "  [already unlocked]"},
            {"  [gerade freigeschaltet]", "  [just unlocked]"},
            {"  [nicht unterstützt]", "  [not supported]"},
            {"  [PAL-GESPERRT: Konsole läuft 50Hz]", "  [PAL-LOCKED: console running at 50Hz]"},
            {"  [region-abhängig: braucht 60Hz]", "  [region-dependent: needs 60Hz]"},
            {"Das Programm ist abgestürzt.\n\nEinzelheiten stehen in %1",
             "The program crashed.\n\nDetails are in %1"},
            {"OpenGL-Kontext konnte nicht angelegt werden.", "Could not create the OpenGL context."},
            {"Datei fehlt", "File missing"},
            {"Nachricht", "Message"},
            {"Mehr erfahren", "Learn more"},
            {"Kein EverDrive gefunden. Ist das USB-Kabel angeschlossen? Ist noch ein anderes Programm offen, das den Anschluss belegt?",
             "No EverDrive found. Is the USB cable connected? Is another program still open that is using the port?"},
            {"Das EverDrive antwortet, die Konsole nicht. Ist die Konsole eingeschaltet?",
             "The EverDrive responds, the console does not. Is the console switched on?"},
            {"Die Konsole läuft, aber ohne RA-Mapper. Steht sie noch im EverDrive-Menü? Dann ein Spiel starten. Läuft schon ein Spiel: mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) muss neben dem ROM liegen.",
             "The console is running, but without the RA mapper. Is it still in the EverDrive menu? Then start a game. If a game is already running: mega-core.x25 (CORE) or mega-pro.rbf (PRO) must be in the same folder as the ROM."},
            {"Verbindung verloren - Monitor gestoppt.", "Connection lost - monitor stopped."},
            {"Verbindung verloren: %1", "Connection lost: %1"},
            {"Log kopieren", "Copy log"},
            {"Kopiert!", "Copied!"},
            {"Falsche Anmeldedaten.", "Wrong login details."},
            {"Unplausible Werte (alle FF oder alle 00) - nicht ausgewertet.",
             "Implausible values (all FF or all 00) - not evaluated."},
            // Katzenfenster: Tierheim, dann - nur zusaetzlich - Ko-fi
            {"Falls Sie kein Tier in Ihrer Familie haben: Auch die Tiere in den Tierheimen freuen sich, denn sie sind ebenfalls auf Hilfe angewiesen.",
             "If there is no pet in your family: the animals in shelters will be glad too, as they depend on help as well."},
            {"Und wer zusätzlich dieses Projekt unterstützen oder aus Wertschätzung etwas dalassen möchte, kann das hier tun:",
             "And anyone who would also like to support this project, or leave something out of appreciation, can do so here:"},
            {"zusätzlich",
             "also"},
            // 1.3.0: Anmeldung merken, Mega EverDrive PRO
            {"Eingeloggt bleiben", "Stay logged in"},
            {"Benutzername und Anmeldeschlüssel speichern (das Passwort wird nicht gespeichert)",
             "Save user name and login key (the password is not saved)"},
            {"Anmeldung abgelaufen -- bitte das Passwort eingeben.", "Login expired -- please enter your password."},
            {"Abmelden", "Log out"},
            {"Abgemeldet.", "Logged out."},
            {"Automatisch angemeldet als %1 (per Sitzungstoken)", "Automatically logged in as %1 (via session token)"},
            {"Angemeldet als %1", "Logged in as %1"},
            {"Gerät: Mega EverDrive PRO - Unterstützung testweise, das Ingame-Menü geht dort noch nicht.",
             "Device: Mega EverDrive PRO - experimental support, the in-game menu does not work there yet."},
            {"Gerät: Mega EverDrive CORE", "Device: Mega EverDrive CORE"},
            {"  über USB   -  edlink run --file SPIEL.md --fpga mega-core.x25   (PRO: mega-pro.rbf)",
             "  via USB   -  edlink run --file GAME.md --fpga mega-core.x25   (PRO: mega-pro.rbf)"},
            {"  über SD    -  mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) in denselben Ordner wie das ROM legen",
             "  via SD    -  put mega-core.x25 (CORE) or mega-pro.rbf (PRO) in the same folder as the ROM"},
            {"mega-core.x25 und mega-pro.rbf in %1 Ordner kopiert.", "mega-core.x25 and mega-pro.rbf copied into %1 folder(s)."},
            {"Durchsucht die SD-Karte nach Ordnern mit ROMs und legt mega-core.x25 (CORE) und mega-pro.rbf (PRO) in jeden davon. Ohne diese Dateien lädt die Konsole den Standard-Mapper und es gibt keine Achievements.",
             "Scans the SD card for folders containing ROMs and puts mega-core.x25 (CORE) and mega-pro.rbf (PRO) into each of them. Without these files the console loads the standard mapper and there are no achievements."},
            {"Der RA-Mapper läuft nicht - mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) muss neben dem ROM liegen.",
             "The RA mapper is not running - mega-core.x25 (CORE) or mega-pro.rbf (PRO) must be in the same folder as the ROM."},
            {"Auf der Konsole läuft nicht unser Custom-Mapper, sondern der Standard-Core. Ohne ihn gibt es keinen Speicherspiegel und keine Achievements.\n\nDas Spiel so starten:\n\nüber USB:\n  edlink run --file SPIEL.md --fpga mega-core.x25   (PRO: mega-pro.rbf)\n\nüber SD-Karte:\n  mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) in denselben Ordner wie das ROM legen",
             "The console is running the standard core, not our custom mapper. Without it there is no memory mirror and no achievements.\n\nStart the game like this:\n\nvia USB:\n  edlink run --file GAME.md --fpga mega-core.x25   (PRO: mega-pro.rbf)\n\nvia SD card:\n  put mega-core.x25 (CORE) or mega-pro.rbf (PRO) in the same folder as the ROM"},
            // Bisher ohne Eintrag (erschienen auch im Englischen deutsch),
            // dazu die festen Texte des Popups und der Update-Meldung.
            {"\n%1 Ordner konnten nicht beschrieben werden.", "\n%1 folder(s) could not be written to."},
            {"Hash nicht fertig geworden.", "Hash did not finish."},
            {"In diesem Verzeichnis wurden keine ROMs gefunden.", "No ROMs were found in this directory."},
            {"mega-core.x25 in %1 Ordner kopiert.", "mega-core.x25 copied into %1 folder(s)."},
            {"ACHIEVEMENT FREIGESCHALTET!", "ACHIEVEMENT UNLOCKED!"},
            {"Pkt", "pts"},
            {"Eine neue Version ist verfügbar (Build %1, du hast Build %2).\n\nBesuche die Support-Seite zum Download.",
             "A new version is available (Build %1, you have Build %2).\n\nVisit the support page to download."},
            // Ersteinrichtung
            {"Weiter", "Next"},
            {"Willkommen bei MEGA-RAW", "Welcome to MEGA-RAW"},
            {"Damit Achievements funktionieren, müssen die Mapper-Dateien einmalig auf die SD-Karte des EverDrive (mega-core.x25 für das CORE, mega-pro.rbf für das PRO).\n\n1. SD-Karte aus der Konsole nehmen\n2. In den Kartenleser am Computer stecken\n3. Im nächsten Fenster die Karte auswählen\n\nDen Rest erledigt das Programm.",
             "For achievements to work, the mapper files have to go onto the EverDrive's SD card once (mega-core.x25 for the CORE, mega-pro.rbf for the PRO).\n\n1. Take the SD card out of the console\n2. Put it into the card reader on your computer\n3. Select the card in the next window\n\nThe program does the rest."},
            {"Mapper jetzt einrichten", "Set up mapper now"},
            {"Später", "Later"},
            {"Die Einrichtung geht jederzeit über \"Mapper auf SD-Karte einrichten...\".",
             "You can set it up at any time with \"Set up mapper on SD card...\"."},
        };
        for (const auto& p : neu) m.emplace(p.first, p.second);
        return m;
    }();
    return t;
}

using Tabelle = std::unordered_map<std::string, std::string>;

// Weitere Sprachen, Index wie in Sprachen(). Erzeugt aus sprachen/<code>.json.
const Tabelle& TabelleFuer(int index) {
    static const Tabelle es = {
#include "i18n_es.inc"
    };
    static const Tabelle pt = {
#include "i18n_pt.inc"
    };
    static const Tabelle fr = {
#include "i18n_fr.inc"
    };
    static const Tabelle it = {
#include "i18n_it.inc"
    };
    static const Tabelle pl = {
#include "i18n_pl.inc"
    };
    static const Tabelle leer;
    switch (index) {
        case 1: return TabelleEn();
        case 2: return es;
        case 3: return pt;
        case 4: return fr;
        case 5: return it;
        case 6: return pl;
        default: return leer;
    }
}

// Deutsch -> aktuelle Sprache; fehlt der Eintrag, englisch; fehlt auch der,
// nullptr (dann bleibt der deutsche Text).
const std::string* Suche(const std::string& deutsch) {
    const int i = Aktiv();
    if (i <= 0) return nullptr;
    const Tabelle& t = TabelleFuer(i);
    auto it = t.find(deutsch);
    if (it != t.end()) return &it->second;
    if (i != 1) {
        const Tabelle& en = TabelleEn();
        auto e = en.find(deutsch);
        if (e != en.end()) return &e->second;
    }
    return nullptr;
}

}  // namespace

const std::vector<SpracheInfo>& Sprachen() {
    static const std::vector<SpracheInfo> s = {
        {"de", "Deutsch"},
        {"en", "English"},
        {"es", "Espa\xC3\xB1ol"},
        {"pt", "Portugu\xC3\xAAs (Brasil)"},
        {"fr", "Fran\xC3\xA7" "ais"},
        {"it", "Italiano"},
        {"pl", "Polski"},
    };
    return s;
}

void SpracheSetzen(const std::string& kennung) {
    std::lock_guard<std::mutex> l(g_mutex);
    const auto& s = Sprachen();
    int index = 1;                                   // Unbekanntes -> englisch
    for (size_t i = 0; i < s.size(); ++i)
        if (kennung == s[i].kennung) index = (int)i;
    g_sprache = s[index].kennung;
    g_index = index;
}

const std::string& Sprache() {
    std::lock_guard<std::mutex> l(g_mutex);
    return g_sprache;
}

int SpracheIndex() { return Aktiv(); }

bool Englisch() { return Aktiv() != 0; }

SpracheFest::SpracheFest(int index) : alt_(g_fest) { g_fest = index; }
SpracheFest::~SpracheFest() { g_fest = alt_; }

const char* T(const char* deutsch) {
    if (!deutsch) return deutsch;
    const std::string* s = Suche(deutsch);
    return s ? s->c_str() : deutsch;
}

std::string T(const std::string& deutsch) {
    const std::string* s = Suche(deutsch);
    return s ? *s : deutsch;
}

std::string Fmt(const std::string& muster, std::initializer_list<std::string> werte) {
    // In einem Durchgang, damit ein eingesetzter Wert, der selbst "%2"
    // enthaelt, nicht nachtraeglich ersetzt wird.
    std::string aus;
    aus.reserve(muster.size() + 32);
    const std::string* w = werte.begin();
    const size_t n = werte.size();
    for (size_t i = 0; i < muster.size(); ++i) {
        const char c = muster[i];
        if (c == '%' && i + 1 < muster.size() && muster[i + 1] >= '1' && muster[i + 1] <= '9') {
            const size_t idx = (size_t)(muster[i + 1] - '1');
            if (idx < n) {
                aus += w[idx];
                ++i;
                continue;
            }
        }
        aus.push_back(c);
    }
    return aus;
}

std::string Zahl(long long n) { return std::to_string(n); }

std::string Hex(unsigned long long n, int breite, bool gross) {
    char t[32];
    std::snprintf(t, sizeof(t), gross ? "%0*llX" : "%0*llx", breite, n);
    return t;
}

}  // namespace i18n
