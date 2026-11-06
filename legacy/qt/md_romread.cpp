#include "md_romread.h"
#include <QThread>
#include <QElapsedTimer>

namespace mdrom {

bool startBlock(EdSerial& ed, uint32_t byteAddr, uint32_t base, int retries) {
    QByteArray cmd(4, '\0');
    cmd[0] = static_cast<char>((byteAddr >> 16) & 0x7F);
    cmd[1] = static_cast<char>((byteAddr >> 8) & 0xFF);
    cmd[2] = static_cast<char>(byteAddr & 0xFF);
    cmd[3] = 1;
    ed.memwr(base + kFcmOffset, cmd);

    for (int i = 0; i < retries; ++i) {
        QByteArray st = ed.memrd(base + kFcmOffset, 1);
        if (st.isEmpty()) return false;
        if ((static_cast<uint8_t>(st[0]) & 1) == 0) return true;
        if ((i & 0x0F) == 0x0F) QThread::msleep(1);
    }
    return false;
}

QByteArray fetchBuffer(EdSerial& ed, uint32_t base) {
    return ed.memrd(base + kBufOffset, kBlockSize);
}

QByteArray readBlockVerified(EdSerial& ed, uint32_t byteAddr, uint32_t base, Stats& st) {
    // Wichtig: den Block ZWEIMAL komplett neu holen lassen, nicht denselben
    // Puffer zweimal lesen. Zweimal lesen prueft nur die Uebertragung - die
    // ist fehlerfrei. Die falschen Bytes entstehen beim Fuellen im Mapper,
    // und das faengt nur ein zweiter Fuellvorgang ab.
    QByteArray a, b;
    QElapsedTimer t;
    for (int k = 0; k < 6; ++k) {
        t.start();
        if (!startBlock(ed, byteAddr, base)) return QByteArray();
        st.msFill += t.elapsed();
        t.start();
        b = fetchBuffer(ed, base);
        st.msXfer += t.elapsed();
        if (k == 0) {
            ++st.blocks;
            QByteArray rj = ed.memrd(base + kFcmOffset + 1, 2);
            if (rj.size() == 2)
                st.rejected += (static_cast<uint8_t>(rj[0]) << 8)
                             | static_cast<uint8_t>(rj[1]);
            a = b;
            continue;
        }
        if (a.size() == kBlockSize && a == b) return a;
        ++st.rereadNeeded;
        a = b;
    }
    ++st.giveUp;
    return a;
}

QByteArray readHeaderSafe(EdSerial& ed, uint32_t base) {
    const int n = kBlockSize;
    QByteArray keep = ed.memrd(base + kBufOffset, n);

    Stats st;
    QByteArray head = readBlockVerified(ed, 0x000100, base, st);

    if (keep.size() == n) ed.memwr(base + kBufOffset, keep);
    return head;
}

// Prueft, ob hinter der im Header gemeldeten Groesse noch echte ROM-Daten
// liegen, und verdoppelt so lange, bis nichts Neues mehr kommt.
//
// Hintergrund: Manche ROMs melden im Header eine zu kleine Groesse. Zero Wing
// (Europe) ist 1 MB gross, meldet aber 512 KB - der Mapper hasht dann nur die
// halbe Datei und RA findet den Hash nicht (auf Hardware belegt: gehasht
// 563c6c1c..., Datei 5f4f2fb8...).
//
// Zwei Abbruchbedingungen, weil hinter dem ROM zweierlei liegen kann:
//  - Fuellbytes (FF oder 00) -> Ende erreicht
//  - eine Spiegelung des ROM-Anfangs, weil mem_ctrl die Adresse auf die
//    ROM-Groesse faltet -> Block gleicht dem bei Adresse 0, also Ende
//
// Der Puffer bei 0x8000 wird einmal gesichert und am Ende zurueckgeschrieben
// (siehe readHeaderSafe - dort liegt der Menuecode).
uint32_t romSizeProbe(EdSerial& ed, uint32_t base, uint32_t hdrSize) {
    if (hdrSize == 0) return 0;
    const int n = kBlockSize;
    QByteArray keep = ed.memrd(base + kBufOffset, n);

    Stats st;
    QByteArray erster = readBlockVerified(ed, 0, base, st);
    uint32_t size = hdrSize;

    // HOECHSTENS EINE Verdopplung, und nur wenn das Ergebnis in den vom
    // 68000 adressierbaren Bereich passt (4 MB).
    //
    // Ohne diese beiden Grenzen lief die Probe bei Lion King (World) von
    // korrekt gemeldeten 3072 KB auf 6144 KB hoch: hinter dem ROM-Ende steht
    // dort weder ein Fuellmuster noch eine erkennbare Spiegelung, sondern
    // alter PSRAM-Inhalt. Der Hash war damit falsch und RA fand das Spiel
    // nicht. Zero Wing (Europe) braucht genau eine Verdopplung (512 -> 1024),
    // mehr ist in keinem bekannten Fall noetig.
    if (size * 2 <= 0x400000) {
        QByteArray blk = readBlockVerified(ed, size, base, st);
        if (blk.size() == n) {
            bool leer = true;
            for (int i = 0; i < n; ++i) {
                const uint8_t b = static_cast<uint8_t>(blk[i]);
                if (b != 0xFF && b != 0x00) { leer = false; break; }
            }
            const bool spiegel = (erster.size() == n && blk == erster);
            if (!leer && !spiegel) size *= 2;
        }
    }

    if (keep.size() == n) ed.memwr(base + kBufOffset, keep);
    return size;
}

uint32_t romSizeFromHeader(const QByteArray& headerBlock) {
    const int off = 0x1A4 - 0x100;
    if (headerBlock.size() < off + 4) return 0;
    uint32_t end = 0;
    for (int i = 0; i < 4; ++i)
        end = (end << 8) | static_cast<uint8_t>(headerBlock[off + i]);
    if (end == 0 || end > 0x3FFFFF) return 0;
    return end + 1;
}

QByteArray romMd5(EdSerial& ed, uint32_t size, uint32_t base,
                  const std::function<bool()>& keepGoing) {
    // Der Mapper rechnet den MD5 selbst und liest das ROM dabei in den
    // Busluecken des 68000 aus dem PSRAM. Der PC schreibt nur die Groesse,
    // pollt das Busy-Bit und holt 16 Byte ab. KEIN Parken der CPU: der
    // Core arbitriert den ROM-Bus ueber fetch_act = hsh_busy & cpu.as
    // selbst. (Das A005-Parken war ein spaeterer Zusatz fuer einen anderen
    // Core-Stand und gehoert hier nicht her - es schaltet den Hook scharf
    // und laesst das Spiel beim Menue abstuerzen.)
    QByteArray cmd(4, '\0');
    cmd[0] = static_cast<char>((size >> 16) & 0x7F);
    cmd[1] = static_cast<char>((size >> 8) & 0xFF);
    cmd[2] = static_cast<char>(size & 0xFF);
    cmd[3] = 1;                       // Schreiben auf +3 startet
    ed.memwr(base + kHshOffset, cmd);

    // A004 pollen bis Bit 0 auf null (hsh_busy | hsh_fin). Ueber USB gehen
    // dabei nur die 16 Byte statt zwei Megabyte.
    for (int i = 0; i < 60000; ++i) {
        if (keepGoing && !keepGoing()) return QByteArray();
        QByteArray st = ed.memrd(base + kHshOffset + 4, 1);
        if (st.isEmpty()) return QByteArray();
        if ((static_cast<uint8_t>(st[0]) & 1) == 0)
            return ed.memrd(base + kDigOffset, 16);
        QThread::msleep(2);
    }
    return QByteArray();
}

QByteArray readRom(EdSerial& ed, uint32_t size, uint32_t base, Stats& st,
                   const std::function<bool(uint32_t, uint32_t)>& progress) {
    QByteArray rom;
    rom.reserve(static_cast<int>(size));
    for (uint32_t a = 0; a < size; a += kBlockSize) {
        QByteArray blk = readBlockVerified(ed, a, base, st);
        if (blk.size() != kBlockSize) return QByteArray();
        rom.append(blk);
        if (progress && !progress(a + kBlockSize, size)) return QByteArray();
    }
    rom.truncate(static_cast<int>(size));
    return rom;
}

} // namespace mdrom
