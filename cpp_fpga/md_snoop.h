#pragma once
#include <QByteArray>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "ed_serial_qt.h"

// Zugriff auf den Work-RAM-Spiegel im Custom-Mapper der Mega EverDrive CORE.
// 1:1 aus md_snoop.py portiert (wram_offset, MdSnoop).
//
// Fensteraufbau (PI-Adressraum, Basis kAddrSnoop):
//
//   0x0000-0x1FFF   Wertespiegel, 128 Slots a 64 Byte
//   0x2000-0x2FFF   Markierungen, ein Byte je Wort
//                   Bit 1 = High-Byte beschrieben, Bit 0 = Low-Byte
//   0x3000-0x33FF   Blockzuordnung, ein Byte je 64-Byte-Block des Work-RAM
//                   Bit 7 = aktiv, Bit 6..0 = Slot
//   0x4000          Lesen: Build-Nummer, Schreiben: Markierungen loeschen

namespace mdsnoop {

constexpr uint32_t kAddrSnoop  = 0x1830000;

constexpr uint32_t kValOffset  = 0x0000;
constexpr uint32_t kMarkOffset = 0x2000;
constexpr uint32_t kPmapOffset = 0x3000;
constexpr uint32_t kCtlOffset  = 0x4000;
// Build 96 des Mappers spiegelt Krikzz' In-Game-Menu-/Cheat-Einstellung
// nach B009: Bit0 = ct_ss_on (In-Game-Menu erlaubt -> unser Menue aktiv),
// Bit1 = ct_gg_on (Cheats an). Fuer Hardcore muessen beide 0 sein.
constexpr uint32_t kCtrlStatOffset = 0xB009;
// B001, Bit3 = hok_jmp: gesetzt, solange die CPU im In-Game-Menue haengt.
// Der Monitor setzt seinen RAM-Poll aus, solange das Menue offen ist -
// sonst kollidiert der USB-Zugriff mit dem Menue-Umschalten (Absturz).
constexpr uint32_t kStatOffset = 0xB001;

constexpr int kBlockSize  = 64;
constexpr int kSlotCount  = 128;
constexpr int kBlockCount = 1024;

constexpr uint32_t kWramBase = 0xFF0000;
constexpr uint32_t kWramSize = 0x10000;

class SnoopError : public std::runtime_error {
public:
    explicit SnoopError(const std::string& what) : std::runtime_error(what) {}
};

// MD-Adresse auf Offset 0..0xFFFF im Work-RAM. Leer wenn ausserhalb.
std::optional<uint32_t> wramOffset(uint32_t addr);

class MdSnoop {
public:
    explicit MdSnoop(EdSerial& ed, uint32_t base = kAddrSnoop);

    uint8_t build();
    // Liest B009 (ct_ss_on Bit0, ct_gg_on Bit1). Hardcore nur zulaessig,
    // wenn beide 0 sind.
    uint8_t ctrlStatus();
    // true, solange das In-Game-Menue aktiv ist (B001 Bit3). Poll pausiert dann.
    bool menuActive();
    void resetMarks();

    void clearMap();
    // Blockzuordnung fuer die uebergebenen Adressen schreiben. Wirft
    // SnoopError, wenn mehr als kSlotCount verschiedene Bloecke noetig sind.
    std::map<int, int> program(const std::vector<uint32_t>& addrs);

    std::optional<uint32_t> valueOffset(uint32_t addr) const;
    std::optional<uint32_t> markOffset(uint32_t addr) const;

    QByteArray read(uint32_t addr, uint32_t length = 1);
    QByteArray readSlot(int slot);
    std::map<int, QByteArray> readAll();

    bool written(uint32_t addr);

    const std::map<int, int>& slotMap() const { return slots_; }

private:
    EdSerial& ed_;
    uint32_t base_;
    std::map<int, int> slots_;   // Block -> Slot
};

} // namespace mdsnoop
