#pragma once
#include <QString>

// Persistente Einstellungen, kompatibel zu settings.json des Python-Tools.
// hardcore ist bewusst gesperrt (Softcore-Zwang fuer diesen inoffiziellen
// Client) -- wird geladen/gespeichert, aber immer als false erzwungen.
struct Settings {
    bool hardcore = false;   // gesperrt -> effektiv immer false
    bool av_safe = false;    // Patches mit kurzer Pause (Ransomware-Fehlalarm-Schutz)
    bool tv_flash = true;    // TV-Goldblitz bei Freischaltung
    QString bucket = "A-E";  // SD-Export-Gruppierung: "A-E" oder "A-Z"
    QString language = "de"; // "de" oder "en"

    static Settings load(const QString& path = "settings.json");
    void save(const QString& path = "settings.json") const;
};
