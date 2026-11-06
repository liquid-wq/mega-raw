#pragma once
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Kopierprogramm fuer den Hook (Mapper ab Build 128/129).
//
// Das Programm laeuft bei jedem VBlank vor dem Hook bei $3FFF00 und bringt
// die ueberwachten Work-RAM-Werte in den Spiegel des Mappers - ohne dass
// der Mapper die Work-RAM-Zugriffe des Spiels mithoeren muss.
//
//   Weg W: move.b/.w/.l ($FFxxxx).l,($1Fxxxx).l - der Mapper nimmt die
//          Schreibzugriffe nach $1F0000-$1FFFFF als Werte.
//   Weg R: der Wert steckt in der Adresse eines Lesezugriffs unter
//          $200000: addr[20:15] Slot, addr[14:9] Byte, addr[8:1] Wert.
//
// Am Ende steht ein Pruefwert (A55A) an einer freien Stelle im Spiegel,
// danach jmp $3FFF00. Die Ausgabe ist byteweise gleich mit dem
// Python-Generator, der jede Zeile per Disassembler prueft.

namespace hookcopy {

constexpr uint32_t kZielBasis = 0x1F0000;   // unter $200000, sonst greift der Batteriespeicher ab
constexpr uint32_t kHook      = 0x3FFF00;
constexpr uint16_t kPruefwert = 0xA55A;
constexpr int      kMaxWorte  = 1024;       // Groesse des Programmspeichers im Mapper
constexpr int      kBlock     = 64;
constexpr int      kMaxSlotsR = 64;         // Weg R: 6 Bit fuer den Slot
// Obergrenze je Bild. Das Programm laeuft vor dem VBlank-Handler des
// Spiels; ein VBlank hat rund 18000 Takte. Sonic: W ca. 800, R ca. 1800.
constexpr int      kMaxTakte  = 4000;

// Bildpuffer (Mapper ab Build 131): jeder Lauf schreibt in einen eigenen
// Abschnitt des 8-KB-Spiegels. Abschnittsgroesse rsz 0-3 = 1024/512/256/128
// Byte, also 8/16/32/64 Bilder im Ring - gewaehlt wird der kleinste
// Abschnitt, in den das Set passt. Die Werte liegen darin dicht gepackt.
constexpr int      kRingAbschnitt = 1024;      // groesster Abschnitt
constexpr int      kRingSpeicher  = 8192;
// Weg W im Bildpuffer ist ueber Adressregister kodiert (3 statt 5 Worte je
// Befehl) und darf laenger laufen: grosse Sets wie Castlevania (303 Byte).
constexpr int      kMaxTakteRing  = 8000;
// Darunter waehlt der Generator den tiefsten Ring; darueber den Plan mit den
// wenigsten Takten (Blockkopie fuer dicht liegende Adressen).
constexpr int      kZielTakte     = 2500;
// Nachher-Modus (Build 133): Hauptteil beginnt bei diesem Wort ($3FF020)
constexpr int      kPostWort      = 16;

enum class Weg { W, R };

struct Programm {
    std::vector<uint16_t> worte;
    int      takte    = 0;      // 68000-Takte je Bild, ohne Wartezyklen
    uint32_t pruefOff = 0;      // Work-RAM-Offset der Pruefstelle (gerade)
    // nur mit Bildpuffer:
    bool     ring     = false;
    bool     post     = false;  // nach dem VBlank-Teil des Spiels kopieren (Vorspann + rte)
    bool     hint     = false;  // am HBlank-Vektor, Ende per rts in den echten Handler (Build 135)
    std::map<uint32_t, uint32_t> lage;   // Work-RAM-Offset -> Position im Abschnitt
    uint32_t pruefPos = 0;      // Position des Pruefwerts (gerade)
    uint32_t spPos    = 0;      // Weg W: Stapelzeiger beim Eintritt (Langwort), 0 = keiner
    int      stapel   = 0;      // Byte, die das Programm unter den Stapelzeiger schreibt
    uint32_t groesse  = 0;      // belegte Bytes je Abschnitt
    int      rsz      = 0;      // Abschnittsgroesse fuer B00D Bit 3-2
    int      abschnitt = 0;     // Bytes je Abschnitt
    int      tiefe    = 0;      // Bilder im Ring
};

class HookCopyError : public std::runtime_error {
public:
    explicit HookCopyError(const std::string& w) : std::runtime_error(w) {}
};

// Physische Adressen -> sortierte, eindeutige Offsets im Work-RAM
std::vector<uint32_t> offsets(const std::vector<uint32_t>& adressen);
// Belegte 64-Byte-Bloecke, aufsteigend - Slot = Position in dieser Liste,
// genau wie MdSnoop::program() die Blockzuordnung vergibt.
std::vector<int> bloecke(const std::vector<uint32_t>& offs);
uint32_t pruefstelle(const std::vector<uint32_t>& offs);

Programm erzeuge(Weg weg, const std::vector<uint32_t>& adressen, bool ring = false, bool post = false,
                  bool hint = false);

// Programm als Bytefolge (hoeherwertiges Byte zuerst) zum Hochladen
std::vector<uint8_t> bytes(const Programm& p);

} // namespace hookcopy
