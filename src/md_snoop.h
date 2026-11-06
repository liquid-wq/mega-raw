#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "ed_serial.h"

// Zugriff auf den Work-RAM-Spiegel im Custom-Mapper der Mega EverDrive CORE.
// Inhaltlich unveraendert aus der Qt-Fassung (md_snoop.h), Bytes statt
// QByteArray.
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
// B009: Bit0 = In-Game-Menu erlaubt, Bit1 = Cheats an (ab Mapper-Build 127).
// Fuer Hardcore muessen beide 0 sein.
constexpr uint32_t kCtrlStatOffset = 0xB009;
// B001, Bit3 = hok_jmp (Sprung ins Menue steht an).
constexpr uint32_t kStatOffset = 0xB001;
// Ab Mapper-Build 128: Kopierprogramm im Hook (siehe hook_copy.h).
// E000: Programmspeicher, 1024 Worte, schreib- und ruecklesbar.
// B00D schreiben: Bit0 = Freigabe. Lesen: Bit1 Freigabe, Bit2 wirksam.
constexpr uint32_t kCpyOffset    = 0xE000;
constexpr uint32_t kCpyCtlOffset = 0xB00D;
// Ab Build 131: Bildpuffer. B00D schreiben Bit1 = Bildpuffer ein, Bit3-2 =
// Abschnittsgroesse (1024 >> n); lesen Bit3 = wirksam, Bit5-4 = Groesse.
// B002 lesen: Bit7 Programm laeuft, Bit6-0 Zahl der beendeten Laeufe.
constexpr uint32_t kRingStatOffset = 0xB002;
constexpr int      kRingBuild      = 131;
// Ab Build 133: Kopieren nach dem VBlank-Teil des Spiels. B00D Bit4;
// lesen Bit6 = wirksam, Bit7 = Hauptteil steht aus. B004: Zaehler "neuer
// VBlank mitten im Hauptteil".
constexpr int      kPostBuild      = 133;
// Ab Build 135: Spiele ohne VBlank-Interrupt. B00D Bit5 = Programm am
// HBlank-Vektor $70. D0F7 lesen: Zahl der HBlank-Vektorabrufe.
constexpr int      kHintBuild      = 135;
constexpr uint32_t kHintCntOffset  = 0xD0F7;

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
    uint8_t ctrlStatus();
    bool menuActive();
    void resetMarks();

    // Kopierprogramm (Mapper ab Build 128)
    void cpyEnable(bool on, bool ring = false, int rsz = 0, bool post = false, bool hint = false);
    uint8_t cpyStatus();
    void programRing(int bloecke);
    uint8_t ringStatus();
    Bytes readRing(int lauf, int abschnittBytes, int laenge);
    void cpyUpload(const Bytes& programm);
    Bytes cpyRead(int laenge);
    // Rohes Lesen im Mapper-Fenster (Diagnoseregister fuer den Bericht)
    Bytes readReg(uint32_t offset, uint32_t laenge);

    void clearMap();
    // Wirft SnoopError, wenn mehr als kSlotCount verschiedene Bloecke noetig sind.
    std::map<int, int> program(const std::vector<uint32_t>& addrs);

    std::optional<uint32_t> valueOffset(uint32_t addr) const;
    std::optional<uint32_t> markOffset(uint32_t addr) const;

    Bytes read(uint32_t addr, uint32_t length = 1);
    Bytes readSlot(int slot);
    std::map<int, Bytes> readAll();

    bool written(uint32_t addr);

    const std::map<int, int>& slotMap() const { return slots_; }

private:
    EdSerial& ed_;
    uint32_t base_;
    std::map<int, int> slots_;   // Block -> Slot
};

} // namespace mdsnoop
