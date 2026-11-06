#pragma once
#include <cstdint>
#include <functional>

#include "ed_serial.h"

// Spielerkennung ueber den Mapper. Inhaltlich unveraendert aus der
// Qt-Fassung (md_romread.h); nur die Teile, die die Erkennung braucht.
//
// Der Mapper holt Bloecke aus dem Cartridge-PSRAM selbst, in den Luecken des
// 68000-Busses, und rechnet den MD5 ueber das ganze ROM ebenfalls selbst.
// Ueber USB gehen dabei nur 16 Byte statt des ganzen ROM.
//
// Registerfenster (Basis mdsnoop::kAddrSnoop):
//   0x7000  Startadresse Bit 22..16, Lesen: Bit 0 = beschaeftigt
//   0x7001  Startadresse Bit 15..8
//   0x7002  Startadresse Bit 7..0
//   0x7003  Schreiben startet den Block
//   0x8000  Puffer, Big-Endian
//   0xA000  Groesse schreiben (Hash starten), A004 Status
//   0xA010  16 Byte MD5

namespace mdrom {

constexpr uint32_t kFcmOffset = 0x7000;
constexpr uint32_t kHshOffset = 0xA000;
constexpr uint32_t kDigOffset = 0xA010;
constexpr uint32_t kBufOffset = 0x8000;
constexpr int kBlockSize = 8192;

struct Stats {
    int blocks = 0;
    int rereadNeeded = 0;
    int giveUp = 0;
    uint32_t rejected = 0;
};

bool startBlock(EdSerial& ed, uint32_t byteAddr, uint32_t base, int retries = 4000);
Bytes fetchBuffer(EdSerial& ed, uint32_t base);
// Holt einen Block zweimal neu und vergleicht (Fehler entstehen beim Fuellen).
Bytes readBlockVerified(EdSerial& ed, uint32_t byteAddr, uint32_t base, Stats& st);

uint32_t romSizeFromHeader(const Bytes& headerBlock);

// Korrigiert eine zu klein gemeldete Header-Groesse (Zero Wing Europe: 512 KB
// gemeldet, 1 MB gross). Hoechstens eine Verdopplung, hoechstens 4 MB.
uint32_t romSizeProbe(EdSerial& ed, uint32_t base, uint32_t hdrSize);

// Liest den Kopfblock und stellt den Puffer danach wieder her: bei 0x8000
// liegt der Menuecode, der sonst zerstoert waere.
Bytes readHeaderSafe(EdSerial& ed, uint32_t base);

// MD5 ueber das ganze ROM aus dem Mapper. Leer heisst: nicht fertig geworden.
Bytes romMd5(EdSerial& ed, uint32_t size, uint32_t base,
             const std::function<bool()>& keepGoing = {});

} // namespace mdrom
