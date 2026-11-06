// Einblendung bei freigeschaltetem Achievement.
//
// Gestaltung und Zeitachse wie achievement_popup.h der Qt-Fassung: die Karte
// schiebt sich unten rechts herein, federt nach, ein Glanz laeuft darueber,
// Funken stieben, die Punkteleiste laeuft voll, dann verschwindet sie wieder.
// Gezeichnet mit GDI+ in ein geschichtetes Win32-Fenster, das ueber allen
// Fenstern liegt und keine Eingaben annimmt.
//
// Abweichung zur Qt-Fassung: Kommt waehrend der Anzeige ein weiteres
// Achievement (zwei gleichzeitig oder direkt hintereinander), wird es danach
// gezeigt, statt das erste abzuschneiden.
#pragma once

#include <functional>
#include <string>

namespace popup {

// badgePng darf leer sein -- dann wird ein Pokal gezeichnet. beimStart laeuft,
// sobald genau diese Einblendung erscheint (Freischalt-Ton): so ueberlagern
// sich bei zwei Achievements hintereinander nicht zwei Melodien.
// kopf und einheit sind die bereits uebersetzten festen Texte
// ("ACHIEVEMENT FREIGESCHALTET!", "Pkt").
void Zeigen(const std::string& titel, const std::string& beschreibung, int punkte,
            const std::string& badgePng, const std::string& kopf, const std::string& einheit,
            float skalierung, std::function<void()> beimStart = nullptr);

// Lage des Fensters fuer die Diagnosezeile im Log (geometry()/isVisible()).
// Rueckgabe: sichtbar.
bool Lage(int* x, int* y, int* w, int* h);

// Beim Programmende aufrufen.
void Beenden();

}  // namespace popup
