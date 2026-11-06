// Intro -- Nachbau von intro_window.cpp der Qt-Fassung. Zeiten, Groessen,
// Farben, Bildmasse (FM) und Keyframes stammen von dort. Gezeichnet wird mit
// der Zeichenliste von Dear ImGui statt mit QPainter; der weichgezeichnete
// Schein des RAWR ist angenaehert.
#pragma once

#include "imgui.h"

namespace intro {

// Einmal nach dem Anlegen des OpenGL-Kontexts: Katzenbilder hochladen.
// fett/kursiv: Courier New fett und kursiv (nullptr = Standardschrift).
void Laden(ImFont* fett, ImFont* kursiv, float skalierung);

// Startet die Uhr. Wie in der Qt-Fassung beginnt die Animation 1000 ms nach
// dem Erscheinen des Fensters und dauert 7800 ms.
void Starten();

// Zeichnet ein Bild ueber das ganze Fenster. false, sobald die Sequenz durch ist.
bool Zeichnen(float skalierung);

void Freigeben();

}  // namespace intro
