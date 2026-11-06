#include "intro.h"

#include "bilder.h"
#include "schrift.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace intro {
namespace {

constexpr int kFrameCount = 12;
constexpr int kFrameRessource = 200;   // cat00.png .. cat11.png, siehe app.rc.in
constexpr double kVorlaufMs = 1000.0;  // startSequence(): show, nach 1000 ms Animation
constexpr double kEndeMs = 7800.0;     // tick(): bei t >= 7800 Schluss
constexpr double kAusblendMs = 450.0;  // showBlocking(): sanftes Ausblenden

// exakt FMETA aus der HTML
struct FMeta { int w, h; double cx, cy; int bottom; };
const FMeta FM[12] = {
    {158,149, 74.0, 73.5,144},{256,159,150.4, 84.0,154},{197,169, 97.9, 87.5,164},
    {233,143,126.4, 69.6,138},{194,158, 99.7, 81.6,153},{208,184,116.1, 91.7,179},
    {201,183,112.9, 91.6,178},{146,186, 72.5, 95.5,181},{181,193, 71.0, 98.8,188},
    {144,199, 71.8,103.9,194},{143,200, 68.4,107.0,196},{144,200, 69.5,107.2,195}
};

struct KFrame { int t, f; double x, arc; };
const KFrame KF[5] = { {0,11,0,0},{3700,8,0,0},{4500,9,0,0},{4800,10,0,0},{5100,11,0,0} };

double easeInOut(double x) { return x < 0.5 ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3) / 2; }
double lerp(double a, double b, double t) { return a + (b - a) * t; }

struct Star { double x, y, z, s; };
struct TextInfo { std::string txt; bool cursor = false, rawr = false; double rawrP = 0; double raFade = -1; };

const std::string FULL = "MEGA-RETROACHIEVEMENTS";

TextInfo textForTime(double t) {
    TextInfo r;
    const int L = (int)FULL.size();
    if (t < 1600) { int n = int((t / 1600.0) * L); r.txt = FULL.substr(0, (size_t)std::min(L, n + 1)); r.cursor = true; return r; }
    if (t < 2300) { r.txt = FULL; return r; }
    if (t < 3050) { const int tg = 11; double p = (t - 2300) / 750.0;
        int rem = int(std::floor(easeInOut(std::min(1.0, p)) * (L - tg)));
        r.txt = FULL.substr(0, (size_t)std::max(tg, L - rem)); return r; }
    if (t < 3400) { double p = easeInOut(std::min(1.0, (t - 3050) / 350.0));
        int del = int(std::round(p * 4)); std::string mid = std::string("ETRO").substr(0, (size_t)(4 - del));
        r.txt = "MEGA-R" + mid + "A"; return r; }
    if (t < 3700) { r.txt = "MEGA-RA"; return r; }
    if (t < 4500) { double p = (t - 3700) / 800.0; int n = int(std::floor(p * 8));
        r.txt = "MEGA-RAW" + std::string((size_t)std::max(0, n), 'R'); r.rawr = true; r.rawrP = p; return r; }
    if (t < 5100) { double p = easeInOut((t - 4500) / 600.0); int n = int(std::round(8 * (1 - p)));
        r.txt = "MEGA-RAW" + std::string((size_t)std::max(0, n), 'R'); return r; }
    r.txt = "MEGA-RAW";
    r.raFade = (t > 5900) ? std::max(0.0, 1 - (t - 5900) / 600.0) : 1.0;
    return r;
}

struct JasonState { int f; double x, arc; };
JasonState jasonState(double t) {
    if (t < KF[0].t) return {-1, -0.50, 0};
    int ki = 4;
    for (int i = 0; i < 4; i++) { if (t >= KF[i].t && t < KF[i + 1].t) { ki = i; break; } }
    const KFrame &c = KF[ki], &n = (ki < 4) ? KF[ki + 1] : KF[ki];
    double p = (ki < 4) ? (t - c.t) / double(n.t - c.t) : 1.0;
    double ep = easeInOut(std::min(1.0, p));
    return {c.f, lerp(c.x, n.x, ep), lerp(c.arc, n.arc, ep)};
}

ImFont* g_fett = nullptr;
ImFont* g_kursiv = nullptr;
bilder::Textur g_frames[kFrameCount];
bilder::Textur g_verlauf;
bool g_geladen = false;

std::vector<Star> g_stars;
std::mt19937 g_zufall;
ULONGLONG g_beginn = 0;
double g_shockT = -1, g_shockX = 0, g_shockY = 0;
bool g_sterneVerteilt = false;
bool g_fertig = false;

double Zufall() { return std::uniform_real_distribution<double>(0.0, 1.0)(g_zufall); }

ImU32 Farbe(int r, int g, int b, double a = 1.0) {
    return IM_COL32(r, g, b, (int)std::lround(std::max(0.0, std::min(1.0, a)) * 255.0));
}

// QRadialGradient(W/2,H/2, max(W,H)*0.7): (30,26,18,153) -> (5,5,10,153).
void VerlaufErzeugen() {
    const int n = 64;
    std::vector<uint8_t> px((size_t)n * n * 4);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const double dx = (x + 0.5) / n * 2.0 - 1.0, dy = (y + 0.5) / n * 2.0 - 1.0;
            const double t = std::min(1.0, std::sqrt(dx * dx + dy * dy));
            uint8_t* p = &px[((size_t)y * n + x) * 4];
            p[0] = (uint8_t)std::lround(30 + (5 - 30) * t);
            p[1] = (uint8_t)std::lround(26 + (5 - 26) * t);
            p[2] = (uint8_t)std::lround(18 + (10 - 18) * t);
            p[3] = 153;
        }
    }
    g_verlauf = bilder::Hochladen(px.data(), n, n, false);
}

}  // namespace

void Laden(ImFont* fett, ImFont* kursiv, float skalierung) {
    g_fett = fett;
    g_kursiv = kursiv;
    if (g_geladen) return;
    for (int i = 0; i < kFrameCount; ++i) {
        const void* d = nullptr;
        size_t n = 0;
        // SmoothPixmapTransform wie in der Qt-Fassung: geglaettet skaliert.
        // Anzeigehoehe dispH = min(H * 0.20, 100) = 100 bei 840 hohem Fenster.
        if (bilder::Ressource(kFrameRessource + i, &d, &n))
            g_frames[i] = bilder::PngHochladenSkaliert(d, n, (int)std::lround(100.0 * skalierung));
    }
    VerlaufErzeugen();
    g_geladen = true;
}

void Starten() {
    g_zufall.seed((unsigned)::GetTickCount());
    g_beginn = ::GetTickCount64();
    g_shockT = -1;
    g_sterneVerteilt = false;
    g_fertig = false;
}

bool Zeichnen(float s) {
    if (g_fertig) return false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const double W = vp->Size.x, H = vp->Size.y;
    const ImVec2 o = vp->Pos;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto P = [&](double x, double y) { return ImVec2(o.x + (float)x, o.y + (float)y); };

    dl->AddRectFilled(P(0, 0), P(W, H), Farbe(5, 5, 10));

    const double seit = (double)(::GetTickCount64() - g_beginn);
    if (seit < kVorlaufMs) return true;           // !running_: nur Grundfarbe
    double t = seit - kVorlaufMs;
    // Nach 7800 ms steht das letzte Bild, und das Fenster blendet ueber
    // 450 ms aus (QPropertyAnimation windowOpacity). Hier: zur Grundfarbe.
    double aus = 0.0;
    if (t >= kEndeMs) {
        aus = (t - kEndeMs) / kAusblendMs;
        if (aus >= 1.0) { g_fertig = true; return false; }
        t = kEndeMs - 1.0;
    }

    if (!g_sterneVerteilt) {
        g_stars.clear();
        for (int i = 0; i < 70; i++)
            g_stars.push_back({Zufall() * W, Zufall() * H, Zufall() * 0.6 + 0.4, (Zufall() * 2.5 + 1.2) * s});
        g_sterneVerteilt = true;
    }

    // Hintergrund + radialer Schein
    if (g_verlauf.ok()) {
        const double R = std::max(W, H) * 0.7;
        dl->AddRectFilled(P(0, 0), P(W, H), IM_COL32(5, 5, 10, 153));
        dl->PushClipRect(P(0, 0), P(W, H), true);
        dl->AddImage((ImTextureID)(intptr_t)g_verlauf.id, P(W / 2 - R, H / 2 - R), P(W / 2 + R, H / 2 + R),
                     ImVec2(0, 0), ImVec2(g_verlauf.u, g_verlauf.v));
        dl->PopClipRect();
    }

    // Sterne (Drift wie im Original, pro gezeichnetem Frame)
    for (Star& st : g_stars) {
        if (aus == 0.0) { st.x -= st.z * 0.3 * s; if (st.x < 0) st.x = W; }
        dl->AddRectFilled(P(st.x, st.y), P(st.x + st.s, st.y + st.s), Farbe(255, 242, 192, st.z * 0.7 + 0.3));
    }

    const double cx = W / 2, cy = H / 2;
    const TextInfo ti = textForTime(t);
    const JasonState js = jasonState(t);
    const double shk = (ti.rawr ? (1 - ti.rawrP) * 14 : 0) * s;
    const double sx = (Zufall() - 0.5) * shk, sy = (Zufall() - 0.5) * shk * 0.4;
    const double dispH = std::min(H * 0.20, 100.0 * s);
    const double groundY = cy + H * 0.05;

    // Shockwave beim RAWR
    if (js.f == 8 && g_shockT < 0) {
        g_shockT = t; g_shockX = cx + js.x * W;
        const FMeta& mm = FM[8]; const double sc = dispH / mm.h;
        g_shockY = groundY - (mm.bottom - mm.cy) * sc;
    }
    if (g_shockT >= 0) {
        const double rt = (t - g_shockT) / 600.0;
        if (rt >= 0 && rt < 1) {
            const double rad = rt * W * 0.6;
            // Leuchtband 70 %..100 % des Radius, am hellsten bei 80 %.
            const int ringe = 14;
            for (int i = 0; i < ringe; ++i) {
                const double q = 0.7 + 0.3 * (i + 0.5) / ringe;
                const double spitze = q <= 0.8 ? (q - 0.7) / 0.1 : (1.0 - q) / 0.2;
                dl->AddCircle(P(g_shockX, g_shockY), (float)(rad * q),
                              Farbe(255, 246, 216, (1 - rt) * 0.5 * spitze), 64, (float)(rad * 0.3 / ringe) + 1.0f);
            }
            dl->AddCircle(P(g_shockX, g_shockY), (float)rad, Farbe(255, 255, 255, 1 - rt), 96, 2.0f * s);
        }
    }

    // Katze bodenverankert
    if (js.f >= 0 && js.f < 12 && g_frames[js.f].ok()) {
        const FMeta& m = FM[js.f];
        const double scale = dispH / m.h, dispW = m.w * scale;
        const double txx = cx + js.x * W + sx, tyy = groundY + js.arc * H + sy;
        const double dx = std::round(txx - m.cx * scale), dy = std::round(tyy - m.bottom * scale);
        // Bilder sind beim Laden schon auf die Anzeigehoehe gebracht; passt
        // die Hoehe, 1:1 zeichnen.
        const bool passt = g_frames[js.f].h == (int)std::lround(dispH);
        const double bw = passt ? g_frames[js.f].w : std::round(dispW);
        const double bh = passt ? g_frames[js.f].h : std::round(dispH);
        dl->AddImage((ImTextureID)(intptr_t)g_frames[js.f].id, P(dx, dy),
                     P(dx + bw, dy + bh), ImVec2(0, 0),
                     ImVec2(g_frames[js.f].u, g_frames[js.f].v));
        // Sprechblase
        double ba = 0;
        if (js.f == 11 && t >= 3400 && t < 3700) ba = std::min(1.0, (t - 3400) / 250.0);
        else if (js.f == 8) ba = std::max(0.0, 1 - (t - 3700) / 250.0);
        if (ba > 0.01) {
            const double bx = txx + dispW * 0.18 + 12 * s, by = tyy - m.cy * scale - dispH * 0.22;
            const double bw = 50 * s, bh = 26 * s, r = 7 * s;
            const ImU32 weiss = Farbe(0xff, 0xfd, 0xf0, ba);
            dl->AddRectFilled(P(bx, by), P(bx + bw, by + bh), weiss, (float)r);
            dl->AddTriangleFilled(P(bx + 4 * s, by + bh), P(bx - 7 * s, by + bh + 11 * s), P(bx + 16 * s, by + bh), weiss);
            ImFont* bf = g_fett ? g_fett : ImGui::GetFont();
            const float gr = schrift::Px(bf, 15.0f) * s;
            const ImVec2 ts = bf->CalcTextSizeA(gr, FLT_MAX, 0.0f, "...");
            dl->AddText(bf, gr, P(bx + bw / 2 - ts.x / 2, by + bh / 2 - ts.y / 2), Farbe(0x66, 0x66, 0x66, ba), "...");
        }
    }

    // Titeltext, zeichenweise, rotes R/A, RAWR-Glow
    ImFont* f = g_fett ? g_fett : ImGui::GetFont();
    const float ts2 = schrift::Px(f, (float)std::round(std::min(W * 0.06, 56.0 * s)));
    const std::string& full = ti.txt;
    const bool cursor = ti.cursor && ((int(t / 250)) % 2 == 0);
    const int bR = ti.rawr ? 0xff : 0xe8, bG = ti.rawr ? 0xf6 : 0xb8, bB = ti.rawr ? 0xd8 : 0x4b;
    const double raFade = (ti.raFade >= 0) ? ti.raFade : (ti.rawr ? 0.0 : 1.0);
    auto mix = [&](int a, int b) { return int(std::round(a + (b - a) * raFade)); };
    const ImU32 baseCol = Farbe(bR, bG, bB);
    const ImU32 HL = Farbe(mix(bR, 255), mix(bG, 77), mix(bB, 77));
    const int n = (int)full.size();
    const int rIdx = (n > 5 && full[5] == 'R') ? 5 : -1;
    int aIdx = -1;
    if (full.rfind("MEGA-R", 0) == 0 && n >= 7 && full.back() == 'A') aIdx = n - 1;
    else if (full.rfind("MEGA-RETROA", 0) == 0) aIdx = 10;
    else if (full.rfind("MEGA-RA", 0) == 0) aIdx = 6;

    const std::string mitCursor = full + (cursor ? "_" : "");
    const float tw = f->CalcTextSizeA(ts2, FLT_MAX, 0.0f, mitCursor.c_str()).x;
    const float th = f->CalcTextSizeA(ts2, FLT_MAX, 0.0f, "M").y;
    const double ox = std::round(cx + sx), oy = std::round(cy + H * 0.19 + sy);
    const float oben = (float)oy - th / 2;

    auto drawText = [&](float offX, float offY, bool glanz, double alpha) {
        float x = offX - tw / 2;
        for (int i = 0; i < n; i++) {
            const char z[2] = {full[(size_t)i], 0};
            ImU32 c = glanz ? Farbe(232, 184, 75, alpha) : ((i == rIdx || i == aIdx) ? HL : baseCol);
            dl->AddText(f, ts2, P(std::round(x), offY), c, z);
            x += f->CalcTextSizeA(ts2, FLT_MAX, 0.0f, z).x;
        }
        if (cursor && !glanz) dl->AddText(f, ts2, P(std::round(x), offY), baseCol, "_");
    };

    if (ti.rawr) {
        // shadowBlur-Aequivalent: der Text mehrfach versetzt, golden, schwach deckend.
        const double ga = 0.6 + ti.rawrP * 0.4;
        const float radien[3] = {3.0f, 6.0f, 10.0f};
        for (float r : radien)
            for (int k = 0; k < 8; ++k) {
                const double w = k * 3.14159265358979 / 4.0;
                drawText((float)ox + (float)std::cos(w) * r * s, oben + (float)std::sin(w) * r * s, true, 0.10 * ga);
            }
    }
    drawText((float)ox, oben, false, 1.0);

    // © Liqui
    {
        ImFont* f2 = g_kursiv ? g_kursiv : ImGui::GetFont();
        const float gr = schrift::Px(f2, (float)(int)std::min(W * 0.02, 18.0 * s));
        const char* lq = "\xC2\xA9 Liqui";
        const ImVec2 ts = f2->CalcTextSizeA(gr, FLT_MAX, 0.0f, lq);
        dl->AddText(f2, gr, P(cx - ts.x / 2, cy + H * 0.22 - ts.y / 2), Farbe(0x8a, 0x7a, 0x4a, 0.5), lq);
    }
    if (aus > 0.0) dl->AddRectFilled(P(0, 0), P(W, H), Farbe(5, 5, 10, aus));
    return true;
}

void Freigeben() {
    for (auto& t : g_frames) bilder::Freigeben(t);
    bilder::Freigeben(g_verlauf);
    g_geladen = false;
}

}  // namespace intro
