#pragma once
#include <QWidget>
#include <QString>

// Zeigt mega_raw_intro.html rahmenlos an (exakt die Original-Animation samt
// Timing und Bildern) und schliesst automatisch, wenn das HTML
// Natives QPainter-Intro, endet nach 7.8 s
// oder nach 40 s Watchdog.
// Fehlt die HTML-Datei, wird das Intro stillschweigend uebersprungen.
class IntroWindow : public QWidget {
    Q_OBJECT
public:
    // Liefert true, wenn ein Intro angezeigt wurde (HTML vorhanden), sonst false.
    static bool showBlocking(const QString& htmlPath);
};
