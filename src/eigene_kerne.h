// Groessen aller bisher gebauten MEGA-RAW-Kerne (mega-core.x25 / mega-pro.rbf).
// Eine Datei dieser Groesse in einem Spielordner stammt von MEGA-RAW und ist
// kein fremdes Original: sie wird beim Einrichten nicht gesichert und beim
// Wiederherstellen nicht zurueckgeschrieben. Neue Kerne hier ergaenzen; die
// eingebetteten Kerne zaehlen ohnehin immer dazu.
#pragma once

#include <cstdint>

namespace megaraw {

inline constexpr uint32_t kEigeneKernGroessen[] = {
    // mega-core.x25
    164992, 165620, 167323, 167882, 168208, 168512, 168952, 169081, 169716, 169883,
    170459, 170550, 170586, 170821, 170856, 171093, 171525, 172213, 172276, 172715,
    175346, 175750, 175800, 175825, 175953, 176085, 176417, 176519, 176674, 176774,
    176942, 177010, 177088,
    176327,   // Zwischenbau Sept. 2026, nur auf der Entwicklerkarte gefunden (ROMS-Wurzel)
    177659,   // Kern 138
    176099,   // Kern 139
    // mega-pro.rbf
    220813, 220974, 223174, 223221, 223612, 223708, 224012, 224047, 224677, 225001,
    225458,
    226104,   // PRO 138
    224985,   // PRO 139
};

}  // namespace megaraw
