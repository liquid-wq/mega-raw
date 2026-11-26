// settings.json neben der Exe -- dieselbe Datei mit denselben Schluesseln wie
// in der Qt-Fassung (settings.h/.cpp). Eine vorhandene Datei wird damit
// unveraendert weiterbenutzt.
#pragma once

#include <map>
#include <string>

struct Settings {
    bool hardcore = false;
    // Englisch als Vorgabe: beim allerersten Start liegt noch keine
    // settings.json vor.
    std::string language = "en"; // "de" oder "en"
    std::string rom_root;        // SD-Karte bzw. ROM-Sammlung
    bool save_login = true;      // "Eingeloggt bleiben", Vorgabe an
    std::string user;
    std::string token;           // kein Passwort, nur der Sitzungsschluessel
    long long message_read = 0;  // zuletzt gezeigte Nachricht (nachricht_megaraw.json)
    bool popup_in_game = true;   // Achievement-Einblendung im Spiel
    // Erkennung ueber die SD-Karte: "Pfad|Groesse" -> MD5 (einmal gelesen)
    std::map<std::string, std::string> md5_cache;

    static Settings load(const std::string& path);
    void save(const std::string& path) const;
};
