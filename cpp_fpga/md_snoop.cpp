#include "md_snoop.h"

#include <QString>

namespace mdsnoop {

std::optional<uint32_t> wramOffset(uint32_t addr) {
    if (addr < kWramSize) return addr;
    if ((addr & 0xE00000) == 0xE00000) return addr & 0xFFFF;
    return std::nullopt;
}

static std::string hexAddr(uint32_t a) {
    return QString("0x%1").arg(a, 6, 16, QChar('0')).toUpper().toStdString();
}

MdSnoop::MdSnoop(EdSerial& ed, uint32_t base) : ed_(ed), base_(base) {}

uint8_t MdSnoop::build() {
    QByteArray b = ed_.memrd(base_ + kCtlOffset, 1);
    if (b.isEmpty()) throw SnoopError("Build-Byte nicht lesbar");
    return static_cast<uint8_t>(b[0]);
}

uint8_t MdSnoop::ctrlStatus() {
    // B009: Bit0 ct_ss_on, Bit1 ct_gg_on. Reines Lesen im Diagnosefenster,
    // im Spielbetrieb erlaubt (kein PSRAM-Zugriff).
    QByteArray b = ed_.memrd(base_ + kCtrlStatOffset, 1);
    if (b.isEmpty()) throw SnoopError("Ctrl-Status (B009) nicht lesbar");
    return static_cast<uint8_t>(b[0]);
}

bool MdSnoop::menuActive() {
    QByteArray b = ed_.memrd(base_ + kStatOffset, 1);
    if (b.isEmpty()) return false;
    return (static_cast<uint8_t>(b[0]) & 0x08) != 0;   // Bit3 = hok_jmp
}

void MdSnoop::resetMarks() {
    ed_.memwr(base_ + kCtlOffset, QByteArray(1, '\0'));
}

void MdSnoop::clearMap() {
    ed_.memwr(base_ + kPmapOffset, QByteArray(kBlockCount, '\0'));
    slots_.clear();
}

std::map<int, int> MdSnoop::program(const std::vector<uint32_t>& addrs) {
    std::set<int> blocks;
    for (uint32_t a : addrs) {
        auto off = wramOffset(a);
        if (!off) throw SnoopError("Adresse ausserhalb des Work-RAM: " + hexAddr(a));
        blocks.insert(static_cast<int>(*off / kBlockSize));
    }

    if (static_cast<int>(blocks.size()) > kSlotCount)
        throw SnoopError(std::to_string(blocks.size()) + " Bloecke gebraucht, nur "
                         + std::to_string(kSlotCount) + " Slots vorhanden");

    QByteArray table(kBlockCount, '\0');
    std::map<int, int> smap;
    int slot = 0;
    for (int block : blocks) {           // std::set ist bereits sortiert
        table[block] = static_cast<char>(0x80 | slot);
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
    auto it = slots_.find(static_cast<int>(*off / kBlockSize));
    if (it == slots_.end()) return std::nullopt;
    return kValOffset + it->second * kBlockSize + (*off % kBlockSize);
}

std::optional<uint32_t> MdSnoop::markOffset(uint32_t addr) const {
    auto off = wramOffset(addr);
    if (!off) return std::nullopt;
    auto it = slots_.find(static_cast<int>(*off / kBlockSize));
    if (it == slots_.end()) return std::nullopt;
    return kMarkOffset + it->second * (kBlockSize / 2) + (*off % kBlockSize) / 2;
}

QByteArray MdSnoop::read(uint32_t addr, uint32_t length) {
    auto off = wramOffset(addr);
    if (!off) throw SnoopError("Adresse ausserhalb des Work-RAM: " + hexAddr(addr));
    if (*off / kBlockSize != (*off + length - 1) / kBlockSize)
        throw SnoopError("Bereich ueberschreitet eine Blockgrenze");
    auto vo = valueOffset(addr);
    if (!vo) throw SnoopError("Adresse nicht zugeordnet: " + hexAddr(addr));
    return ed_.memrd(base_ + *vo, length);
}

QByteArray MdSnoop::readSlot(int slot) {
    return ed_.memrd(base_ + kValOffset + slot * kBlockSize, kBlockSize);
}

std::map<int, QByteArray> MdSnoop::readAll() {
    std::map<int, QByteArray> out;
    for (auto& kv : slots_) out[kv.first] = readSlot(kv.second);
    return out;
}

bool MdSnoop::written(uint32_t addr) {
    auto mo = markOffset(addr);
    if (!mo) return false;
    QByteArray b = ed_.memrd(base_ + *mo, 1);
    if (b.isEmpty()) return false;
    auto off = wramOffset(addr);
    uint8_t bit = (*off % 2 == 0) ? 0x02 : 0x01;
    return (static_cast<uint8_t>(b[0]) & bit) != 0;
}

} // namespace mdsnoop
