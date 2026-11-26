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
            {"Achievement-Einblendung im Spiel", "Achievement popup in the game"},
            {"Zeigt freigeschaltete Achievements direkt auf dem Fernseher an. Das Spiel hält dafür etwa 3 Sekunden an und läuft danach von selbst weiter (Mega EverDrive CORE).",
             "Shows unlocked achievements right on the TV. The game pauses for about 3 seconds and then continues by itself (Mega EverDrive CORE)."},
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
            {"  Dafür einmal \"Mapper auf SD-Karte einrichten...\" ausführen und das Spiel danach neu starten.",
             "  To fix this, run \"Set up mapper on SD card...\" once and then restart the game."},
                        {"mega-core.x25 und mega-pro.rbf in %1 Ordner kopiert.", "mega-core.x25 and mega-pro.rbf copied into %1 folder(s)."},
            {"Durchsucht die SD-Karte nach Ordnern mit ROMs und legt mega-core.x25 (CORE) und mega-pro.rbf (PRO) in jeden davon. Ohne diese Dateien lädt die Konsole den Standard-Mapper und es gibt keine Achievements.",
             "Scans the SD card for folders containing ROMs and puts mega-core.x25 (CORE) and mega-pro.rbf (PRO) into each of them. Without these files the console loads the standard mapper and there are no achievements."},
            {"Der RA-Mapper läuft nicht - mega-core.x25 (CORE) bzw. mega-pro.rbf (PRO) muss neben dem ROM liegen.",
             "The RA mapper is not running - mega-core.x25 (CORE) or mega-pro.rbf (PRO) must be in the same folder as the ROM."},
            {"Auf der Konsole läuft nicht der MEGA-RAW-Mapper, sondern der Standard-Mapper des EverDrive. Ohne ihn gibt es keine Achievements.\n\nSo geht es:\n1. Konsole ins EverDrive-Menü bringen (Reset)\n2. Im Tool \"Mapper auf SD-Karte einrichten...\" wählen\n3. Das Spiel neu starten",
             "The console is not running the MEGA-RAW mapper but the EverDrive's standard mapper. Without it there are no achievements.\n\nHere's how:\n1. Bring the console to the EverDrive menu (reset)\n2. In the tool, choose \"Set up mapper on SD card...\"\n3. Restart the game"},
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
            {"Damit Achievements funktionieren, müssen die Mapper-Dateien einmalig auf die SD-Karte des EverDrive.\n\n1. Konsole einschalten und im EverDrive-Menü lassen\n2. EverDrive per USB mit dem Computer verbinden\n3. \"Mapper jetzt einrichten\" klicken\n\nDen Rest erledigt das Programm. Ohne USB-Verbindung fragt es nach der SD-Karte im Kartenleser.",
             "For achievements to work, the mapper files have to go onto the EverDrive's SD card once.\n\n1. Switch on the console and leave it in the EverDrive menu\n2. Connect the EverDrive to the computer via USB\n3. Click \"Set up mapper now\"\n\nThe program does the rest. Without a USB connection it asks for the SD card in a card reader."},
            {"Mapper jetzt einrichten", "Set up mapper now"},
            {"Später", "Later"},
            {"Die Einrichtung geht jederzeit über \"Mapper auf SD-Karte einrichten...\".",
             "You can set it up at any time with \"Set up mapper on SD card...\"."},
            // Einrichtung und Wiederherstellen ueber USB
            {"Bitte zuerst den Monitor stoppen und die Konsole ins EverDrive-Menü zurückbringen (Reset), dann erneut einrichten.",
             "Please stop the monitor first and bring the console back to the EverDrive menu (reset), then set up again."},
            {"Suche die SD-Karte über USB...",
             "Looking for the SD card via USB..."},
            {"Kein EverDrive über USB gefunden - Einrichtung über das Laufwerk.",
             "No EverDrive found via USB - setting up via the drive."},
            {"Auf der Konsole läuft gerade ein Spiel. Bitte Reset drücken, damit das EverDrive-Menü erscheint, und dann erneut einrichten.",
             "A game is running on the console. Please press reset so the EverDrive menu appears, then set up again."},
            {"Die SD-Karte ließ sich über USB nicht vollständig lesen. Es wurde nichts geschrieben. Bitte das USB-Kabel kurz trennen und erneut versuchen.",
             "The SD card could not be read completely via USB. Nothing was written. Please unplug the USB cable briefly and try again."},
            {"Auf der SD-Karte wurden keine ROMs gefunden.",
             "No ROMs were found on the SD card."},
            {"SD-Karte über USB gelesen (%1): %2 Ordner mit Spielen.",
             "SD card read via USB (%1): %2 folder(s) with games."},
            {"Jetzt wird der MEGA-RAW-Kern in die Ordner kopiert, in denen er fehlt oder veraltet ist.\n\nDas Tool hat diese %1 Ordner auf der SD-Karte selbst gefunden. Du musst nichts auswählen - die Liste ist nur zur Kontrolle.\n\nDeine Spiele und Spielstände bleiben unverändert - es kommt nur je eine Mapper-Datei dazu.\n\nFremde Mapper-Dateien werden vorher gesichert und lassen sich jederzeit unter Optionen > \"Mapper wiederherstellen\" zurückholen.\n\nMit OK geht es los.",
             "The MEGA-RAW core will now be copied into the folders where it is missing or outdated.\n\nThe tool found these %1 folders on the SD card by itself. You don't need to select anything - the list is just for checking.\n\nYour games and saves stay unchanged - only one mapper file is added to each folder.\n\nOther mapper files are backed up first and can be brought back at any time under Options > \"Restore mappers\".\n\nPress OK to start."},
            {"Bitte warten ...", "Please wait ..."},
            {"Erkannt über die Datei auf der SD-Karte: %1 (%2 KB, %3 ms)", "Identified by the file on the SD card: %1 (%2 KB, %3 ms)"},
            {"Datei auf der SD-Karte nicht verwendbar - Erkennung über den Mapper.", "File on the SD card not usable - identifying through the mapper."},
            {"kein EverDrive", "no EverDrive"},
            {"kein Spiel mit MEGA-RAW-Kern", "no game with the MEGA-RAW core"},
            {"Pfad des Spiels nicht lesbar", "game path not readable"},
            {"Datei nicht lesbar", "file not readable"},
            {"Datei passt nicht zum laufenden Spiel", "file does not match the running game"},
            {"Spiel erkennen & Monitor starten", "Detect game & start monitor"},
            {"■ Monitor stoppen", "■ Stop monitor"},
            {"Monitor fortsetzen", "Resume monitor"},
            {"Das Tool arbeitet über USB mit der SD-Karte.", "The tool is working with the SD card via USB."},
            {"%1 von %2 Ordnern", "%1 of %2 folders"},
            {"%1 Ordner gelesen", "%1 folder(s) read"},
            {"Alle %1 Ordner haben schon den aktuellen MEGA-RAW-Kern.",
             "All %1 folders already have the current MEGA-RAW core."},
            {"Alle %1 Ordner mit Spielen haben schon den aktuellen MEGA-RAW-Kern. Es gibt nichts zu tun.",
             "All %1 folders with games already have the current MEGA-RAW core. There is nothing to do."},
            {"%1 Ordner haben schon den aktuellen MEGA-RAW-Kern und bleiben unverändert.",
             "%1 folder(s) already have the current MEGA-RAW core and stay unchanged."},
            {"Schreibe die Kerne in %1 Ordner...",
             "Writing the cores into %1 folder(s)..."},
            {"Die Verbindung zum EverDrive ist weg. Es wurde nichts geschrieben.",
             "The connection to the EverDrive is gone. Nothing was written."},
            {"Auf der Konsole läuft inzwischen ein Spiel. Es wurde nichts geschrieben. Bitte Reset drücken und erneut einrichten.",
             "A game is now running on the console. Nothing was written. Please press reset and set up again."},
            {"FEHLER",
             "ERROR"},
            {"Die Verbindung brach ab. Bitte das USB-Kabel kurz trennen und die Einrichtung wiederholen.",
             "The connection was lost. Please unplug the USB cable briefly and repeat the setup."},
            {"Bitte zuerst den Monitor stoppen und die Konsole ins EverDrive-Menü zurückbringen (Reset), dann erneut versuchen.",
             "Please stop the monitor first and bring the console back to the EverDrive menu (reset), then try again."},
            {"Kein EverDrive über USB gefunden - Wiederherstellen über das Laufwerk.",
             "No EverDrive found via USB - restoring via the drive."},
            {"Auf der Konsole läuft gerade ein Spiel. Bitte Reset drücken, damit das EverDrive-Menü erscheint, und dann erneut versuchen.",
             "A game is running on the console. Please press reset so the EverDrive menu appears, then try again."},
            {"Die SD-Karte ließ sich über USB nicht vollständig lesen. Es wurde nichts geändert. Bitte das USB-Kabel kurz trennen und erneut versuchen.",
             "The SD card could not be read completely via USB. Nothing was changed. Please unplug the USB cable briefly and try again."},
            {"Auf der SD-Karte gibt es nichts zurückzusetzen.",
             "There is nothing to restore on the SD card."},
            {"MEGA-RAW-Mapper raus, früheren Mapper zurück",
             "remove MEGA-RAW mapper, put back the previous mapper"},
            {"nur den MEGA-RAW-Mapper entfernen",
             "remove only the MEGA-RAW mapper"},
            {"Aus 1 Ordner wird der MEGA-RAW-Mapper wieder entfernt.",
             "The MEGA-RAW mapper will be removed from 1 folder."},
            {"Aus %1 Ordnern wird der MEGA-RAW-Mapper wieder entfernt.",
             "The MEGA-RAW mapper will be removed from %1 folders."},
            {"Deine Spiele und Spielstände bleiben unangetastet - gelöscht werden nur die Mapper-Dateien von MEGA-RAW.",
             "Your games and saves stay untouched - only the MEGA-RAW mapper files are deleted."},
            {"In 1 Ordner wird der Mapper zurückgeschrieben, der vor MEGA-RAW dort lag.",
             "In 1 folder, the mapper that was there before MEGA-RAW will be put back."},
            {"In %1 Ordnern wird der Mapper zurückgeschrieben, der vor MEGA-RAW dort lag.",
             "In %1 folders, the mapper that was there before MEGA-RAW will be put back."},
            {"Das Tool hat alles selbst gefunden - die Liste ist nur zur Kontrolle.\n\nMit OK geht es los.",
             "The tool found everything by itself - the list is just for checking.\n\nPress OK to start."},
            {"Die Verbindung zum EverDrive ist weg. Es wurde nichts geändert.",
             "The connection to the EverDrive is gone. Nothing was changed."},
            {"Auf der Konsole läuft inzwischen ein Spiel. Es wurde nichts geändert. Bitte Reset drücken und erneut versuchen.",
             "A game is now running on the console. Nothing was changed. Please press reset and try again."},
            {"Zurücksetzen: MEGA-RAW aus %1 Ordnern entfernt, früherer Mapper in %2 Ordnern zurück, %3 Datei(en) fehlgeschlagen.",
             "Restore: MEGA-RAW removed from %1 folder(s), previous mapper back in %2 folder(s), %3 file(s) failed."},
            {"Die MEGA-RAW-Mapper wurden erfolgreich entfernt.",
             "The MEGA-RAW mappers were removed successfully."},
            {"Auf der SD-Karte lagen keine MEGA-RAW-Mapper.",
             "There were no MEGA-RAW mappers on the SD card."},
            {"Die Kerne, die vorher dort lagen, wurden zurückgespielt.",
             "The cores that were there before have been put back."},
            {"\n%1 Datei(en) konnten nicht zurückgeschrieben werden.",
             "\n%1 file(s) could not be written back."},
            {"Die Verbindung brach ab. Bitte das USB-Kabel kurz trennen und erneut versuchen.",
             "The connection was lost. Please unplug the USB cable briefly and try again."},
            {"Konsole aus",
             "Console off"},
            {"EverDrive-Menü - der Monitor startet beim Spielstart von selbst",
             "EverDrive menu - the monitor starts by itself when a game starts"},
            {"Spiel läuft - Monitor startet...",
             "Game running - starting monitor..."},
            {"Spiel startet - warte, bis es läuft...",
             "Game is starting - waiting until it runs..."},
            {"Das EverDrive steckt am USB, antwortet aber nicht - der Anschluss ist belegt.",
             "The EverDrive is connected via USB but does not respond - the port is in use."},
            {"Das EverDrive ist per USB angeschlossen, aber der Anschluss ist belegt. Läuft noch "
             "ein zweites MEGA-RAW-Fenster oder ein anderes Programm mit dem EverDrive? Bitte "
             "schließen und erneut versuchen.",
             "The EverDrive is connected via USB, but the port is in use. Is a second MEGA-RAW "
             "window or another program using the EverDrive still running? Please close it and try again."},
            {"Monitor angehalten - startet beim nächsten Spiel von selbst",
             "Monitor paused - starts by itself with the next game"},
            {"Kein EverDrive gefunden - ist das USB-Kabel angeschlossen?",
             "No EverDrive found - is the USB cable connected?"},
            {"Für dieses Spiel gibt es bei RetroAchievements keine Achievements.",
             "RetroAchievements has no achievements for this game."},
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
