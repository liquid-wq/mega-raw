#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>

#include "ed_serial_qt.h"

// Liest den Mega-Drive-ROM-Header direkt aus dem Cartridge-PSRAM.
// Der Mapper bildet ROM0 im PI-Adressraum ab kAddrRom0 ab
// (pi_map.sv: ce_rom0, 16M @ 0x0000000 bei HWC_ROM1_OFF).
//
// Headeraufbau ab 0x000100:
//   0x100  16  Systemkennung ("SEGA MEGA DRIVE " / "SEGA GENESIS    ")
//   0x110  16  Copyright
//   0x120  48  Titel Inland
//   0x150  48  Titel International
//   0x180  14  Seriennummer
//   0x18E   2  Pruefsumme
//   0x1F0  16  Regionskennung

namespace mdrom {

constexpr uint32_t kAddrRom0    = 0x0000000;
constexpr uint32_t kHeaderStart = 0x000100;
constexpr uint32_t kHeaderLen   = 0x000100;

struct Header {
    bool     valid = false;
    QString  system;
    QString  copyright;
    QString  titleDomestic;
    QString  titleInternational;
    QString  serial;
    uint16_t checksum = 0;
    QString  region;

    // Kurzform fuer den Abgleich gegen die lokale ROM-Sammlung.
    QString fingerprint() const;
};

// Wirft nichts; bei Lesefehler kommt ein Header mit valid == false zurueck.
Header readHeader(EdSerial& ed, uint32_t base = kAddrRom0);

// Header aus bereits gelesenen 256 Bytes (ab 0x000100) auswerten.
Header parseHeader(const QByteArray& raw);

} // namespace mdrom
