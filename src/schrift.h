// Schriftgroessen wie in Qt.
//
// Qt gibt eine Schriftgroesse als Em-Groesse an (Segoe UI 9 pt = 12 px), Dear
// ImGui als ganze Zeilenhoehe. Jede Schrift wird deshalb mit ihrem Faktor
// (Zeilenhoehe / Em, aus der Schriftdatei gelesen) angemeldet; Px() rechnet
// eine Qt-Groesse in die Groesse fuer Dear ImGui um. So stimmen Glyphen UND
// Zeilenabstaende mit der Qt-Fassung ueberein.
#pragma once

#include "imgui.h"

namespace schrift {

void Faktor(ImFont* f, float faktor);
// Em-Groesse in Pixeln (vor DPI) -> Groesse fuer PushFont/AddText.
float Px(ImFont* f, float em);

}  // namespace schrift
