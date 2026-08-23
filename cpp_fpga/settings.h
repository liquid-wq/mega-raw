#pragma once
#include <QString>

// Persistente Einstellungen.
// Hardcore: Der Mapper hat ab Build 96 ein In-Game-Menue mit Savestates.
// Hardcore ist daher nur zulaessig, wenn der Nutzer In-Game-Menue und
// Cheats in Krikzz' EverDrive-System-Menue abgeschaltet hat. Der Mapper
// spiegelt beides nach B009; der Monitor prueft es beim Start und stuft
// bei Verstoss auf Softcore zurueck.
struct Settings {
    bool hardcore = false;
    // Englisch als Vorgabe: beim allerersten Start liegt noch keine
    // settings.json vor, und ein deutschsprachiges Fenster macht die
    // Einrichtung fuer den Grossteil der Nutzer unlesbar. Wer Deutsch
    // will, stellt es in den Optionen um - das wirkt sofort.
    QString language = "en"; // "de" oder "en"
    QString rom_root;        // SD-Karte bzw. ROM-Sammlung
    bool save_login = false; // Benutzer und Sitzungsschluessel merken
    QString user;
    QString token;           // kein Passwort, nur der Sitzungsschluessel

    static Settings load(const QString& path = "settings.json");
    void save(const QString& path = "settings.json") const;
};
