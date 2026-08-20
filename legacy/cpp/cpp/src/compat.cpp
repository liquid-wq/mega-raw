#include "compat.h"
#include "rom_patch.h"

static uint32_t be32(const std::vector<uint8_t>& d, size_t off) {
    return (uint32_t(d[off]) << 24) | (uint32_t(d[off+1]) << 16) |
           (uint32_t(d[off+2]) << 8) | uint32_t(d[off+3]);
}

CompatResult check_compat(const std::vector<uint8_t>& rom, int achCount, int achUnsupported) {
    CompatResult r; r.ok = true;

    // 1) Eigenes SRAM? Header 0x1B0 = "RA"
    if (rom.size() > 0x1B2 && rom[0x1B0] == 'R' && rom[0x1B1] == 'A') {
        r.ok = false;
        r.lines << QString::fromUtf8("\u2717 Spiel nutzt eigenes Batterie-SRAM \u2014 Speicherstaende "
                 "wuerden ueberschrieben. NICHT KOMPATIBEL.");
    } else {
        r.lines << QString::fromUtf8("\u2713 Kein eigenes SRAM (BRAM frei nutzbar)");
    }

    // 2) VBlank-Vektor + Handler erreicht ein rte (0x4E73)
    if (rom.size() >= 0x7C) {
        uint32_t vec = be32(rom, 0x78);
        if (vec == 0 || (vec % 2)) {
            r.ok = false;
            r.lines << QString::fromUtf8("\u2717 VBlank-Vektor ungueltig (0x%1)")
                        .arg(vec, 6, 16, QChar('0')).toUpper();
        } else if (vec >= rom.size()) {
            r.lines << QString::fromUtf8("~ VBlank-Vektor zeigt ins RAM (0x%1) \u2014 "
                        "Trampolin, sollte funktionieren").arg(vec, 6, 16, QChar('0'));
        } else {
            size_t end = std::min(rom.size(), size_t(vec) + 0x400);
            bool hasRte = false;
            for (size_t i = vec; i + 1 < end; ++i)
                if (rom[i] == 0x4E && rom[i+1] == 0x73) { hasRte = true; break; }
            if (hasRte)
                r.lines << QString::fromUtf8("\u2713 VBlank-Handler 0x%1 endet mit rte")
                            .arg(vec, 6, 16, QChar('0'));
            else
                r.lines << QString::fromUtf8("~ Kein rte nahe Handler 0x%1 \u2014 untypisch, Test noetig")
                            .arg(vec, 6, 16, QChar('0'));
        }
    } else {
        r.ok = false;
        r.lines << QString::fromUtf8("\u2717 Header nicht lesbar");
    }

    // 3) Stub-Platz
    size_t needed = 60 + 10 * 40 + 100;
    auto sa = find_stub_addr(rom, needed);
    if (sa.stub_addr >= 0) {
        QString where = (size_t(sa.stub_addr) >= rom.size())
            ? QString::fromUtf8("hinter Dateiende (angehaengt)")
            : QString::fromUtf8("Padding 0x%1").arg(uint32_t(sa.stub_addr), 6, 16, QChar('0'));
        r.lines << QString::fromUtf8("\u2713 Stub-Platz: ") + where;
    } else {
        r.ok = false;
        r.lines << QString::fromUtf8("\u2717 Kein Platz fuer Stub (ROM zu gross)");
    }

    // 4) Engine-Abdeckung
    if (achCount >= 0) {
        int uns = achUnsupported < 0 ? 0 : achUnsupported;
        if (uns == 0)
            r.lines << QString::fromUtf8("\u2713 Alle %1 Achievements von der Engine unterstuetzt").arg(achCount);
        else
            r.lines << QString::fromUtf8("~ %1/%2 Achievements unterstuetzt (%3 mit nicht abbildbaren Bedingungen)")
                        .arg(achCount - uns).arg(achCount).arg(uns);
    }
    return r;
}
