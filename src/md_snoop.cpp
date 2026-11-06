#include "md_snoop.h"

#include <cstdio>

namespace mdsnoop {

std::optional<uint32_t> wramOffset(uint32_t addr) {
    if (addr < kWramSize) return addr;
    if ((addr & 0xE00000) == 0xE00000) return addr & 0xFFFF;
    return std::nullopt;
}

static std::string hexAddr(uint32_t a) {
    char t[16];
    std::snprintf(t, sizeof(t), "0x%06X", a);
    return t;
}

MdSnoop::MdSnoop(EdSerial& ed, uint32_t base) : ed_(ed), base_(base) {}

uint8_t MdSnoop::build() {
    Bytes b = ed_.memrd(base_ + kCtlOffset, 1);
    if (b.empty()) throw SnoopError("Build-Byte nicht lesbar");
    return b[0];
}

uint8_t MdSnoop::ctrlStatus() {
    // B009: Bit0 ct_ss_on, Bit1 ct_gg_on. Reines Lesen im Diagnosefenster,
    // im Spielbetrieb erlaubt (kein PSRAM-Zugriff).
    Bytes b = ed_.memrd(base_ + kCtrlStatOffset, 1);
    if (b.empty()) throw SnoopError("Ctrl-Status (B009) nicht lesbar");
    return b[0];
}

bool MdSnoop::menuActive() {
    Bytes b = ed_.memrd(base_ + kStatOffset, 1);
    if (b.empty()) return false;
    return (b[0] & 0x08) != 0;   // Bit3 = hok_jmp
}

void MdSnoop::resetMarks() {
    ed_.memwr(base_ + kCtlOffset, Bytes(1, 0));
}

void MdSnoop::cpyEnable(bool on, bool ring, int rsz, bool post, bool hint) {
    int v = on ? (ring ? 0x03 | ((rsz & 3) << 2) : 0x01) | (post ? 0x10 : 0x00) | (hint ? 0x20 : 0x00) : 0x00;
    ed_.memwr(base_ + kCpyCtlOffset, Bytes(1, (uint8_t)v));
}

void MdSnoop::programRing(int bloecke) {
    // Ziel $1F0000 + Position: der Mapper nimmt Block = Position / 64 als
    // Index der Zuordnung, also Block k -> Slot k.
    Bytes table(kBlockCount, 0);
    for (int k = 0; k < bloecke && k < 16; ++k) table[(size_t)k] = (uint8_t)(0x80 | k);
    ed_.memwr(base_ + kPmapOffset, table);
    slots_.clear();
}

uint8_t MdSnoop::ringStatus() {
    Bytes b = ed_.memrd(base_ + kRingStatOffset, 1);
    if (b.empty()) throw SnoopError("Bildzähler (B002) nicht lesbar");
    return b[0];
}

Bytes MdSnoop::readRing(int lauf, int abschnittBytes, int laenge) {
    const int tiefe = 8192 / abschnittBytes;
    return ed_.memrd(base_ + kValOffset + (uint32_t)((lauf % tiefe) * abschnittBytes), (uint32_t)laenge);
}

uint8_t MdSnoop::cpyStatus() {
    Bytes b = ed_.memrd(base_ + kCpyCtlOffset, 1);
    if (b.empty()) throw SnoopError("Kopierstatus (B00D) nicht lesbar");
    return b[0];
}

void MdSnoop::cpyUpload(const Bytes& programm) {
    ed_.memwr(base_ + kCpyOffset, programm);
}

Bytes MdSnoop::cpyRead(int laenge) {
    return ed_.memrd(base_ + kCpyOffset, (uint32_t)laenge);
}

Bytes MdSnoop::readReg(uint32_t offset, uint32_t laenge) {
    return ed_.memrd(base_ + offset, laenge);
}

void MdSnoop::clearMap() {
    ed_.memwr(base_ + kPmapOffset, Bytes(kBlockCount, 0));
    slots_.clear();
}

std::map<int, int> MdSnoop::program(const std::vector<uint32_t>& addrs) {
    std::set<int> blocks;
    for (uint32_t a : addrs) {
        auto off = wramOffset(a);
        if (!off) throw SnoopError("Adresse ausserhalb des Work-RAM: " + hexAddr(a));
        blocks.insert((int)(*off / kBlockSize));
    }

    if ((int)blocks.size() > kSlotCount)
        throw SnoopError(std::to_string(blocks.size()) + " Blöcke gebraucht, nur "
                         + std::to_string(kSlotCount) + " Slots vorhanden");

    Bytes table(kBlockCount, 0);
    std::map<int, int> smap;
    int slot = 0;
    for (int block : blocks) {           // std::set ist bereits sortiert
        table[(size_t)block] = (uint8_t)(0x80 | slot);
        smap[block] = slot;
        ++slot;
    }

    ed_.memwr(base_ + kPmapOffset, table);
    slots_ = smap;
    return smap;
}

std::optional<uint32_t> MdSnoop::valueOffset(uint32_t addr) const {
    auto off = wramOffset(addr);
    if (!off) return std::nullopt;
    auto it = slots_.find((int)(*off / kBlockSize));
    if (it == slots_.end()) return std::nullopt;
    return kValOffset + it->second * kBlockSize + (*off % kBlockSize);
}

std::optional<uint32_t> MdSnoop::markOffset(uint32_t addr) const {
    auto off = wramOffset(addr);
    if (!off) return std::nullopt;
    auto it = slots_.find((int)(*off / kBlockSize));
    if (it == slots_.end()) return std::nullopt;
    return kMarkOffset + it->second * (kBlockSize / 2) + (*off % kBlockSize) / 2;
}

Bytes MdSnoop::read(uint32_t addr, uint32_t length) {
    auto off = wramOffset(addr);
    if (!off) throw SnoopError("Adresse ausserhalb des Work-RAM: " + hexAddr(addr));
    if (*off / kBlockSize != (*off + length - 1) / kBlockSize)
        throw SnoopError("Bereich überschreitet eine Blockgrenze");
    auto vo = valueOffset(addr);
    if (!vo) throw SnoopError("Adresse nicht zugeordnet: " + hexAddr(addr));
    return ed_.memrd(base_ + *vo, length);
}

Bytes MdSnoop::readSlot(int slot) {
    return ed_.memrd(base_ + kValOffset + slot * kBlockSize, kBlockSize);
}

std::map<int, Bytes> MdSnoop::readAll() {
    std::map<int, Bytes> out;
    for (auto& kv : slots_) out[kv.first] = readSlot(kv.second);
    return out;
}

bool MdSnoop::written(uint32_t addr) {
    auto mo = markOffset(addr);
    if (!mo) return false;
    Bytes b = ed_.memrd(base_ + *mo, 1);
    if (b.empty()) return false;
    auto off = wramOffset(addr);
    uint8_t bit = (*off % 2 == 0) ? 0x02 : 0x01;
    return (b[0] & bit) != 0;
}

} // namespace mdsnoop
