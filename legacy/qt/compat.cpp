#include "compat.h"

// Bei der FPGA-Variante wird die ROM nicht angefasst: kein Stub, kein
// Patch, kein Zugriff auf das BRAM. Die frueheren Punkte 1-3 (eigenes
// SRAM, VBlank-Vektor, Stub-Platz) sind damit gegenstandslos. Uebrig
// bleibt die Frage, ob die Engine alle Achievements abbilden kann.
CompatResult check_compat(const std::vector<uint8_t>& rom, int achCount, int achUnsupported) {
    CompatResult r; r.ok = true;
    (void)rom;

    r.lines << QString::fromUtf8("\u2713 Keine ROM-Aenderung noetig (Custom-Mapper liest den Work-RAM direkt)");

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
