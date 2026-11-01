#include "hook_copy.h"

#include <algorithm>
#include <map>
#include <set>

namespace hookcopy {

std::vector<uint32_t> offsets(const std::vector<uint32_t>& adressen) {
    std::set<uint32_t> s;
    for (uint32_t a : adressen) {
        if (a < 0x10000) s.insert(a);
        else if ((a & 0xE00000) == 0xE00000) s.insert(a & 0xFFFF);
        else throw HookCopyError("Adresse ausserhalb des Work-RAM");
    }
    return std::vector<uint32_t>(s.begin(), s.end());
}

std::vector<int> bloecke(const std::vector<uint32_t>& offs) {
    std::set<int> s;
    for (uint32_t o : offs) s.insert(static_cast<int>(o / kBlock));
    return std::vector<int>(s.begin(), s.end());
}

uint32_t pruefstelle(const std::vector<uint32_t>& offs) {
    std::set<uint32_t> belegt(offs.begin(), offs.end());
    for (int b : bloecke(offs)) {
        for (int o = b * kBlock + kBlock - 2; o >= b * kBlock; o -= 2) {
            if (!belegt.count(static_cast<uint32_t>(o)) && !belegt.count(static_cast<uint32_t>(o + 1)))
                return static_cast<uint32_t>(o);
        }
    }
    throw HookCopyError("kein freier Platz fuer den Pruefwert");
}

static void lang(std::vector<uint16_t>& w, uint32_t v) {
    w.push_back(static_cast<uint16_t>(v >> 16));
    w.push_back(static_cast<uint16_t>(v & 0xFFFF));
}

// Ziel eines Bytes im Spiegel: ohne Bildpuffer der Work-RAM-Offset selbst
// (ueber die Blockzuordnung), mit Bildpuffer die Position im Abschnitt.
static uint32_t ziel(const Programm& p, uint32_t o) {
    return p.ring ? p.lage.at(o) : o;
}

// Weg W: zusammenhaengende Bytes als .l/.w/.b - .w/.l nur auf geraden
// Adressen, sonst loest der 68000 bei jedem Bild einen Adressfehler aus.
// Im Bildpuffer behaelt jede Folge ihre Paritaet, die Regel gilt also fuer
// Quelle und Ziel zugleich.
static void erzeugeW(Programm& p, const std::vector<uint32_t>& offs) {
    size_t i = 0;
    while (i < offs.size()) {
        uint32_t start = offs[i];
        size_t n = 1;
        while (i + n < offs.size() && offs[i + n] == start + n) ++n;
        uint32_t o = start;
        while (n > 0) {
            uint16_t opc;
            int laenge;
            if (o % 2 == 0 && n >= 4)      { opc = 0x23F9; laenge = 4; p.takte += 36; }
            else if (o % 2 == 0 && n >= 2) { opc = 0x33F9; laenge = 2; p.takte += 28; }
            else                           { opc = 0x13F9; laenge = 1; p.takte += 28; }
            p.worte.push_back(opc);                     // move.x (abs).l,(abs).l
            lang(p.worte, 0xFF0000u | o);
            lang(p.worte, kZielBasis | ziel(p, o));
            o += laenge;
            n -= laenge;
        }
        i += (o - start);
    }
    p.worte.push_back(0x33FC);                          // move.w #imm,(abs).l
    p.worte.push_back(kPruefwert);
    lang(p.worte, kZielBasis | (p.ring ? p.pruefPos : p.pruefOff));
    p.takte += 20;
}

static uint32_t rBasis(int slot, uint32_t byte) {
    return (static_cast<uint32_t>(slot) << 15) | (byte << 9);
}

// Weg R: Wert verdoppeln (addr[0] gibt es beim Lesen nicht), dann an
// Basis + Wert*2 lesen. d0/a0 werden gesichert, das Spiel merkt nichts.
static void erzeugeR(Programm& p, const std::vector<uint32_t>& offs) {
    std::vector<int> bl = bloecke(offs);
    if (!p.ring && static_cast<int>(bl.size()) > kMaxSlotsR)
        throw HookCopyError("Weg R: zu viele Bloecke");
    std::map<int, int> slotTab;
    for (size_t k = 0; k < bl.size(); ++k) slotTab[bl[k]] = static_cast<int>(k);
    // Lage eines Bytes als Basisadresse: Slot und Byte im Slot
    auto basis = [&](uint32_t o) {
        if (p.ring) return rBasis(static_cast<int>(p.lage.at(o) / kBlock), p.lage.at(o) % kBlock);
        return rBasis(slotTab[static_cast<int>(o / kBlock)], o % kBlock);
    };
    auto basisPruef = [&](uint32_t k) {
        if (p.ring) return rBasis(static_cast<int>((p.pruefPos + k) / kBlock), (p.pruefPos + k) % kBlock);
        uint32_t o = p.pruefOff + k;
        return rBasis(slotTab[static_cast<int>(o / kBlock)], o % kBlock);
    };

    p.worte.push_back(0x48E7);                          // movem.l d0/a0,-(sp)
    p.worte.push_back(0x8080);
    p.takte += 8 + 8 * 2;
    for (uint32_t o : offs) {
        p.worte.push_back(0x7000);                      // moveq #0,d0
        p.worte.push_back(0x1039);                      // move.b (abs).l,d0
        lang(p.worte, 0xFF0000u | o);
        p.worte.push_back(0xD040);                      // add.w d0,d0
        p.worte.push_back(0x41F9);                      // lea (abs).l,a0
        lang(p.worte, basis(o));
        p.worte.push_back(0x4A30);                      // tst.b 0(a0,d0.w)
        p.worte.push_back(0x0000);
        p.takte += 4 + 16 + 4 + 12 + 14;
    }
    const uint32_t pruef[2][2] = {
        { 0, static_cast<uint32_t>(kPruefwert >> 8) },
        { 1, static_cast<uint32_t>(kPruefwert & 0xFF) } };
    for (auto& pv : pruef) {
        p.worte.push_back(0x41F9);                      // lea (abs).l,a0
        lang(p.worte, basisPruef(pv[0]) + 2 * pv[1]);
        p.worte.push_back(0x4A10);                      // tst.b (a0)
        p.takte += 12 + 8;
    }
    p.worte.push_back(0x4CDF);                          // movem.l (sp)+,d0/a0
    p.worte.push_back(0x0101);
    p.takte += 12 + 8 * 2;
}

// Bildpuffer: Bytes dicht hintereinander. Jede zusammenhaengende Folge
// beginnt auf derselben Paritaet wie im Work-RAM (sonst gingen .w/.l nicht),
// der Pruefwert steht gerade am Ende.
static void legeAus(Programm& p, const std::vector<uint32_t>& offs) {
    uint32_t q = 0;
    size_t i = 0;
    while (i < offs.size()) {
        uint32_t start = offs[i];
        size_t n = 1;
        while (i + n < offs.size() && offs[i + n] == start + n) ++n;
        if ((q & 1) != (start & 1)) ++q;
        for (size_t k = 0; k < n; ++k) p.lage[start + static_cast<uint32_t>(k)] = q + static_cast<uint32_t>(k);
        q += static_cast<uint32_t>(n);
        i += n;
    }
    if (q & 1) ++q;
    p.pruefPos = q;
    p.groesse  = q + 2;
}

static void waehleAbschnitt(Programm& p) {
    if (p.groesse > static_cast<uint32_t>(kRingAbschnitt))
        throw HookCopyError("zu viele Adressen fuer den Bildpuffer");
    p.rsz = 0;
    while (p.rsz < 3 && p.groesse <= static_cast<uint32_t>(kRingAbschnitt >> (p.rsz + 1))) ++p.rsz;
    p.abschnitt = kRingAbschnitt >> p.rsz;
    p.tiefe     = kRingSpeicher / p.abschnitt;
}

// Weg W im Bildpuffer: a0 = $FF8000 (Quelle, d16 erreicht das ganze
// Work-RAM), a1 = $1F0000 (Ziel). Zwei Arten von Abschnitten:
//   Folge: zusammenhaengende Bytes als move.x d16(a0),d16(a1) (3 Worte,
//          24/24/32 Takte fuer .b/.w/.l)
//   Block: ein ganzer Bereich, auch mit unbenutzten Bytes dazwischen, per
//          movem.l d16(a0),Regs / movem.l Regs,d16(a1) mit bis zu 13
//          Registern (28 + 16 je Langwort) - bei dicht liegenden Adressen
//          ein Bruchteil der Takte, kostet aber Platz im Abschnitt.
// Die Aufteilung waehlt ein Optimierer (Takte + lambda * Bytes).

namespace {
struct Teil { bool block; uint32_t start; uint32_t laenge; };
struct Plan { std::vector<Teil> teile; int takte = 0; uint32_t groesse = 0; bool bloecke = false; };

constexpr int kBlockRegs = 13;                          // d0-d7, a2-a6

int takteFolge(uint32_t start, uint32_t n) {
    int t = 0;
    uint32_t o = start;
    while (n > 0) {
        if (o % 2 == 0 && n >= 4)      { t += 32; o += 4; n -= 4; }
        else if (o % 2 == 0 && n >= 2) { t += 24; o += 2; n -= 2; }
        else                           { t += 24; o += 1; n -= 1; }
    }
    return t;
}

int takteBlock(uint32_t laenge) {
    int lw = static_cast<int>(laenge / 4);
    int stuecke = (lw + kBlockRegs - 1) / kBlockRegs;
    return 28 * stuecke + 16 * lw;
}

uint32_t blockLaenge(uint32_t start, uint32_t ende) {     // start gerade, ende exklusiv
    return (ende - start + 3) & ~3u;
}

// Lage festlegen und Takte zaehlen
void legePlanAus(Plan& pl, Programm& p, const std::vector<uint32_t>& offs) {
    std::set<uint32_t> belegt(offs.begin(), offs.end());
    uint32_t q = 0;
    int t = 0;
    p.lage.clear();
    for (const Teil& teil : pl.teile) {
        if (teil.block) {
            if (q & 1) ++q;
            for (uint32_t k = 0; k < teil.laenge; ++k)
                if (belegt.count(teil.start + k)) p.lage[teil.start + k] = q + k;
            t += takteBlock(teil.laenge);
        } else {
            if ((q & 1) != (teil.start & 1)) ++q;
            for (uint32_t k = 0; k < teil.laenge; ++k) p.lage[teil.start + k] = q + k;
            t += takteFolge(teil.start, teil.laenge);
        }
        q += teil.laenge;
    }
    if (q & 1) ++q;
    pl.groesse = q + 2 + 4;                             // Pruefwert, dann Stapelzeiger
    // Rahmen: Register sichern/zurueck, zwei lea, Pruefwert
    t += pl.bloecke ? (128 + 132) : (24 + 28);
    pl.takte = t + 12 + 12 + 16 + 20;                   // lea, lea, Pruefwert, Stapelzeiger
}

Plan plane(const std::vector<uint32_t>& offs, double lambda, bool mitBloecken) {
    const size_t n = offs.size();
    std::vector<double> dp(n + 1, 1e18);
    std::vector<Teil> wahl(n + 1);
    std::vector<size_t> von(n + 1, 0);
    dp[0] = 0;
    for (size_t i = 0; i < n; ++i) {
        if (dp[i] >= 1e17) continue;
        // Folge: jedes Anfangsstueck einer zusammenhaengenden Folge
        for (size_t j = i + 1; j <= n; ++j) {
            if (j > i + 1 && offs[j - 1] != offs[j - 2] + 1) break;
            uint32_t len = static_cast<uint32_t>(j - i);
            double c = dp[i] + takteFolge(offs[i], len) + lambda * len;
            if (c < dp[j]) { dp[j] = c; wahl[j] = {false, offs[i], len}; von[j] = i; }
        }
        if (!mitBloecken) continue;
        uint32_t start = offs[i] & ~1u;
        for (size_t j = i + 2; j <= n; ++j) {
            uint32_t len = blockLaenge(start, offs[j - 1] + 1);
            if (len > static_cast<uint32_t>(kRingAbschnitt) / 2 || start + len > 0x10000) break;
            double c = dp[i] + takteBlock(len) + lambda * len;
            if (c < dp[j]) { dp[j] = c; wahl[j] = {true, start, len}; von[j] = i; }
        }
    }
    Plan pl;
    for (size_t j = n; j > 0; j = von[j]) pl.teile.push_back(wahl[j]);
    std::reverse(pl.teile.begin(), pl.teile.end());
    for (const Teil& t : pl.teile) pl.bloecke |= t.block;
    return pl;
}
} // namespace

static void erzeugeWRing(Programm& p, const std::vector<uint32_t>& offs) {
    // Kandidaten: verschiedene Gewichte fuer den Platz, mit und ohne Bloecke.
    // Gewaehlt wird der kleinste Abschnitt (tiefster Ring), der unter
    // kZielTakte bleibt; sonst der Plan mit den wenigsten Takten.
    Plan best;
    bool hatBest = false;
    bool bestImZiel = false;
    for (bool mb : {false, true}) {
        for (double lambda : {64.0, 16.0, 4.0, 1.0, 0.25, 0.0}) {
            Plan pl = plane(offs, lambda, mb);
            Programm probe;
            legePlanAus(pl, probe, offs);
            if (pl.groesse > static_cast<uint32_t>(kRingAbschnitt)) continue;
            bool imZiel = pl.takte <= kZielTakte;
            bool besser;
            if (!hatBest) besser = true;
            else if (imZiel != bestImZiel) besser = imZiel;
            else if (imZiel) besser = pl.groesse < best.groesse ||
                                      (pl.groesse == best.groesse && pl.takte < best.takte);
            else besser = pl.takte < best.takte;
            if (besser) { best = pl; hatBest = true; bestImZiel = imZiel; }
        }
    }
    if (!hatBest) throw HookCopyError("zu viele Adressen fuer den Bildpuffer");
    legePlanAus(best, p, offs);
    p.groesse  = best.groesse;
    p.pruefPos = best.groesse - 6;
    p.spPos    = best.groesse - 4;
    // Stapelzeiger des Spiels festhalten, BEVOR Register gesichert werden -
    // zeigt, ob unser Sichern in ueberwachte Adressen reicht.
    p.worte.push_back(0x23CF);                          // move.l a7,(abs).l
    lang(p.worte, kZielBasis | p.spPos);

    p.worte.push_back(0x48E7);                          // movem.l ...,-(sp)
    p.worte.push_back(best.bloecke ? 0xFFFE : 0x00C0);  // d0-d7/a0-a6 bzw. a0/a1
    p.worte.push_back(0x41F9);                          // lea $FF8000,a0
    lang(p.worte, 0xFF8000);
    p.worte.push_back(0x43F9);                          // lea $1F0000,a1
    lang(p.worte, kZielBasis);
    for (const Teil& teil : best.teile) {
        if (teil.block) {
            // Zielposition des Blockanfangs: aus einem belegten Byte im Block
            uint32_t pos = 0;
            for (uint32_t k = 0; k < teil.laenge; ++k) {
                auto it = p.lage.find(teil.start + k);
                if (it != p.lage.end()) { pos = it->second - k; break; }
            }
            uint32_t o = teil.start;
            uint32_t rest = teil.laenge / 4;
            while (rest > 0) {
                int nreg = static_cast<int>(std::min<uint32_t>(rest, kBlockRegs));
                uint16_t maske = 0;
                for (int r = 0; r < nreg; ++r)
                    maske |= static_cast<uint16_t>(1u << (r < 8 ? r : r + 2));   // d0-d7, dann a2-a6
                p.worte.push_back(0x4CE8);              // movem.l d16(a0),Regs
                p.worte.push_back(maske);
                p.worte.push_back(static_cast<uint16_t>(o - 0x8000));
                p.worte.push_back(0x48E9);              // movem.l Regs,d16(a1)
                p.worte.push_back(maske);
                p.worte.push_back(static_cast<uint16_t>(pos + (o - teil.start)));
                o += 4u * static_cast<uint32_t>(nreg);
                rest -= static_cast<uint32_t>(nreg);
            }
        } else {
            uint32_t o = teil.start;
            uint32_t n = teil.laenge;
            while (n > 0) {
                uint16_t opc;
                uint32_t laenge;
                if (o % 2 == 0 && n >= 4)      { opc = 0x2368; laenge = 4; }
                else if (o % 2 == 0 && n >= 2) { opc = 0x3368; laenge = 2; }
                else                           { opc = 0x1368; laenge = 1; }
                p.worte.push_back(opc);                 // move.x d16(a0),d16(a1)
                p.worte.push_back(static_cast<uint16_t>(o - 0x8000));
                p.worte.push_back(static_cast<uint16_t>(p.lage.at(o)));
                o += laenge;
                n -= laenge;
            }
        }
    }
    p.worte.push_back(0x337C);                          // move.w #imm,d16(a1)
    p.worte.push_back(kPruefwert);
    p.worte.push_back(static_cast<uint16_t>(p.pruefPos));
    p.worte.push_back(0x4CDF);                          // movem.l (sp)+,...
    p.worte.push_back(best.bloecke ? 0x7FFF : 0x0300);
    p.takte += best.takte;                              // ggf. zum Vorspann dazu
    p.stapel = best.bloecke ? 60 : 8;                   // gesicherte Register in Byte
    waehleAbschnitt(p);
}

// Nachher-Modus (Mapper ab Build 133): Vorspann in Wort 0-15. Er legt eine
// Ruecksprungadresse auf den Hauptteil (Wort 16) samt Statusregister auf den
// Stapel und springt zum Hook - das rte am Ende des VBlank-Teils des Spiels
// landet dann im Hauptteil. Das S-Bit wird gesetzt, damit der Hauptteil
// auch bei einem Spiel im Usermodus im Supervisormodus laeuft; die
// Interruptmaske bleibt die des Spiels (HBlank-Effekte laufen weiter).
static void vorspann(Programm& p) {
    p.worte.push_back(0x4879);                          // pea ($3FF020).l
    lang(p.worte, 0x3FF000 + 2 * kPostWort);
    p.worte.push_back(0x3F2F);                          // move.w 4(a7),-(a7)
    p.worte.push_back(0x0004);
    p.worte.push_back(0x0057);                          // ori.w #$2000,(a7)
    p.worte.push_back(0x2000);
    p.worte.push_back(0x4EF9);                          // jmp (abs).l
    lang(p.worte, kHook);
    while (p.worte.size() < static_cast<size_t>(kPostWort)) p.worte.push_back(0x4E71);   // nop
    p.takte += 20 + 18 + 16 + 12;
}

Programm erzeuge(Weg weg, const std::vector<uint32_t>& adressen, bool ring, bool post, bool hint) {
    Programm p;
    std::vector<uint32_t> offs = offsets(adressen);
    if (offs.empty()) throw HookCopyError("keine Adressen");
    p.ring = ring;
    p.post = post && !hint;
    p.hint = hint;
    if (p.post) vorspann(p);
    if (!ring) {
        p.pruefOff = pruefstelle(offs);
        if (weg == Weg::W) erzeugeW(p, offs); else erzeugeR(p, offs);
    } else if (weg == Weg::W) {
        erzeugeWRing(p, offs);                          // legt selbst aus
    } else {
        legeAus(p, offs);
        waehleAbschnitt(p);
        erzeugeR(p, offs);
    }
    if (hint) {
        // HBlank-Betrieb (Mapper ab Build 135): echten HBlank-Handler auf den
        // Stapel, Lauf beenden, per rts dorthin. Der Mapper liefert die
        // Adresse bei $3FF7FC (letzte zwei Worte des Programmbereichs).
        p.worte.push_back(0x2F39);                      // move.l (abs).l,-(a7)
        lang(p.worte, 0x3FF000 + 2 * (kMaxWorte - 2));
        p.worte.push_back(0x4A79);                      // tst.w (abs).l
        lang(p.worte, kHook);
        p.worte.push_back(0x4E75);                      // rts
        p.takte += 28 + 16 + 16;
        if (static_cast<int>(p.worte.size()) > kMaxWorte - 2)
            throw HookCopyError("Programm zu gross");
    } else if (p.post) {
        // Lauf beenden: ein Lesezugriff auf den Hook (Wort 0, ohne
        // Nebenwirkung) meldet dem Mapper "Abschnitt vollstaendig".
        p.worte.push_back(0x4A79);                      // tst.w (abs).l
        lang(p.worte, kHook);
        p.worte.push_back(0x4E73);                      // rte
        p.takte += 16 + 20;
    } else {
        p.worte.push_back(0x4EF9);                      // jmp (abs).l
        lang(p.worte, kHook);
        p.takte += 12;
    }
    if (static_cast<int>(p.worte.size()) > kMaxWorte)
        throw HookCopyError("Programm zu gross");
    return p;
}

std::vector<uint8_t> bytes(const Programm& p) {
    std::vector<uint8_t> b;
    b.reserve(p.worte.size() * 2);
    for (uint16_t w : p.worte) {
        b.push_back(static_cast<uint8_t>(w >> 8));
        b.push_back(static_cast<uint8_t>(w & 0xFF));
    }
    return b;
}

} // namespace hookcopy
