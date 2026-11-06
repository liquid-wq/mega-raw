#include "md_romread.h"

#include <windows.h>

namespace mdrom {

bool startBlock(EdSerial& ed, uint32_t byteAddr, uint32_t base, int retries) {
    Bytes cmd(4, 0);
    cmd[0] = (uint8_t)((byteAddr >> 16) & 0x7F);
    cmd[1] = (uint8_t)((byteAddr >> 8) & 0xFF);
    cmd[2] = (uint8_t)(byteAddr & 0xFF);
    cmd[3] = 1;
    ed.memwr(base + kFcmOffset, cmd);

    for (int i = 0; i < retries; ++i) {
        Bytes st = ed.memrd(base + kFcmOffset, 1);
        if (st.empty()) return false;
        if ((st[0] & 1) == 0) return true;
        if ((i & 0x0F) == 0x0F) ::Sleep(1);
    }
    return false;
}

Bytes fetchBuffer(EdSerial& ed, uint32_t base) {
    return ed.memrd(base + kBufOffset, kBlockSize);
}

Bytes readBlockVerified(EdSerial& ed, uint32_t byteAddr, uint32_t base, Stats& st) {
    // Den Block ZWEIMAL komplett neu holen lassen, nicht denselben Puffer
    // zweimal lesen: die falschen Bytes entstehen beim Fuellen im Mapper.
    Bytes a, b;
    for (int k = 0; k < 6; ++k) {
        if (!startBlock(ed, byteAddr, base)) return Bytes();
        b = fetchBuffer(ed, base);
        if (k == 0) {
            ++st.blocks;
            Bytes rj = ed.memrd(base + kFcmOffset + 1, 2);
            if (rj.size() == 2) st.rejected += ((uint32_t)rj[0] << 8) | rj[1];
            a = b;
            continue;
        }
        if ((int)a.size() == kBlockSize && a == b) return a;
        ++st.rereadNeeded;
        a = b;
    }
    ++st.giveUp;
    return a;
}

Bytes readHeaderSafe(EdSerial& ed, uint32_t base) {
    const int n = kBlockSize;
    Bytes keep = ed.memrd(base + kBufOffset, n);

    Stats st;
    Bytes head = readBlockVerified(ed, 0x000100, base, st);

    if ((int)keep.size() == n) ed.memwr(base + kBufOffset, keep);
    return head;
}

// Prueft, ob hinter der im Header gemeldeten Groesse noch echte ROM-Daten
// liegen. Abbruch bei Fuellbytes (FF/00) oder einer Spiegelung des
// ROM-Anfangs. Hoechstens eine Verdopplung und nur bis 4 MB: ohne diese
// Grenzen lief die Probe bei Lion King (World) von korrekten 3072 KB auf
// 6144 KB hoch, weil hinter dem ROM alter PSRAM-Inhalt steht.
uint32_t romSizeProbe(EdSerial& ed, uint32_t base, uint32_t hdrSize) {
    if (hdrSize == 0) return 0;
    const int n = kBlockSize;
    Bytes keep = ed.memrd(base + kBufOffset, n);

    Stats st;
    Bytes erster = readBlockVerified(ed, 0, base, st);
    uint32_t size = hdrSize;

    if (size * 2 <= 0x400000) {
        Bytes blk = readBlockVerified(ed, size, base, st);
        if ((int)blk.size() == n) {
            bool leer = true;
            for (int i = 0; i < n; ++i) {
                const uint8_t b = blk[(size_t)i];
                if (b != 0xFF && b != 0x00) { leer = false; break; }
            }
            const bool spiegel = ((int)erster.size() == n && blk == erster);
            if (!leer && !spiegel) size *= 2;
        }
    }

    if ((int)keep.size() == n) ed.memwr(base + kBufOffset, keep);
    return size;
}

uint32_t romSizeFromHeader(const Bytes& headerBlock) {
    const int off = 0x1A4 - 0x100;
    if ((int)headerBlock.size() < off + 4) return 0;
    uint32_t end = 0;
    for (int i = 0; i < 4; ++i) end = (end << 8) | headerBlock[(size_t)(off + i)];
    if (end == 0 || end > 0x3FFFFF) return 0;
    return end + 1;
}

Bytes romMd5(EdSerial& ed, uint32_t size, uint32_t base, const std::function<bool()>& keepGoing) {
    // Der Mapper rechnet den MD5 selbst. Der PC schreibt nur die Groesse,
    // pollt das Busy-Bit und holt 16 Byte ab. KEIN Parken der CPU: der Core
    // arbitriert den ROM-Bus selbst.
    Bytes cmd(4, 0);
    cmd[0] = (uint8_t)((size >> 16) & 0x7F);
    cmd[1] = (uint8_t)((size >> 8) & 0xFF);
    cmd[2] = (uint8_t)(size & 0xFF);
    cmd[3] = 1;                       // Schreiben auf +3 startet
    ed.memwr(base + kHshOffset, cmd);

    for (int i = 0; i < 60000; ++i) {
        if (keepGoing && !keepGoing()) return Bytes();
        Bytes st = ed.memrd(base + kHshOffset + 4, 1);
        if (st.empty()) return Bytes();
        if ((st[0] & 1) == 0) return ed.memrd(base + kDigOffset, 16);
        ::Sleep(2);
    }
    return Bytes();
}

} // namespace mdrom
