// MEGA-RAW FPGA -- RetroAchievements auf einem echten Mega Drive ueber die
// Mega EverDrive.
//
// Einzelne Exe: Dear ImGui auf Win32 + OpenGL2, MinGW-Laufzeit statisch,
// Mapper-Kern, Intro-Bilder und Portrait als Ressourcen eingebettet. Keine
// Begleit-DLLs.
//
// Ablauf wie main.cpp der Qt-Fassung: erst das Intro (rahmenlos, obenauf,
// 768 x 840 mittig), dann das Hauptfenster (700 x 820), sanft eingeblendet.
// Dieser Teil ist nur der Rahmen; das Programm selbst steckt in megaraw::App.

#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <gl/gl.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <thread>

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_win32.h"

#include "app.h"
#include "bilder.h"
#include "einstellungen.h"
#include "i18n.h"
#include "intro.h"
#include "plattform.h"
#include "popup.h"
#include "schrift.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

HDC g_dc = nullptr;
HGLRC g_rc = nullptr;
megaraw::App* g_app = nullptr;

// ---------------------------------------------------------------------------
// Absturzspur. Ein Fensterprogramm, das abstuerzt, verschwindet sonst einfach.
// Geschrieben wird nach %TEMP%\megaraw_absturz.txt: Ursache, Adresse relativ
// zur Exe, Build-Nummer und die letzten Zeilen des Logs.
// ---------------------------------------------------------------------------
std::wstring AbsturzDatei() {
    wchar_t pfad[MAX_PATH] = {};
    ::GetTempPathW(MAX_PATH, pfad);
    return std::wstring(pfad) + L"megaraw_absturz.txt";
}

void AbsturzMelden(const char* ursache, unsigned long code, const void* adresse) {
    static volatile LONG schonGemeldet = 0;
    if (::InterlockedExchange(&schonGemeldet, 1)) return;

    const std::wstring datei = AbsturzDatei();
    if (std::FILE* f = _wfopen(datei.c_str(), L"w")) {
        SYSTEMTIME t;
        ::GetLocalTime(&t);
        std::fprintf(f, "MEGA-RAW FPGA %s build %d -- Absturz %04d-%02d-%02d %02d:%02d:%02d\n",
                     MEGA_RAW_VERSION, MEGA_RAW_CPP_BUILD, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        std::fprintf(f, "Ursache: %s\n", ursache);
        if (code) std::fprintf(f, "Code: 0x%08lX\n", code);
        if (adresse) {
            const uintptr_t basis = (uintptr_t)::GetModuleHandleW(nullptr);
            std::fprintf(f, "Adresse: exe+0x%llX\n", (unsigned long long)((uintptr_t)adresse - basis));
        }
        if (g_app) {
            std::fprintf(f, "\n--- letzte Log-Zeilen ---\n");
            const auto log = g_app->LogZeilen();
            const size_t ab = log.size() > 200 ? log.size() - 200 : 0;
            for (size_t i = ab; i < log.size(); ++i) std::fprintf(f, "%s\n", log[i].c_str());
        }
        std::fclose(f);
    }
    const std::string text = Fmt(T("Das Programm ist abgestürzt.\n\nEinzelheiten stehen in %1"),
                                 {plattform::Utf8(datei)});
    ::MessageBoxW(nullptr, plattform::Breit(text).c_str(), L"MEGA-RAW FPGA", MB_ICONERROR | MB_TOPMOST);
}

LONG WINAPI AufAbsturz(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e ? e->ExceptionRecord : nullptr;
    AbsturzMelden("Ausnahme des Systems", r ? r->ExceptionCode : 0, r ? r->ExceptionAddress : nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void AufTerminate() {
    std::string ursache = "unbehandelte C++-Ausnahme";
    try {
        if (auto ep = std::current_exception()) std::rethrow_exception(ep);
    } catch (const std::exception& ex) {
        ursache += std::string(": ") + ex.what();
    } catch (...) {
    }
    AbsturzMelden(ursache.c_str(), 0, nullptr);
    ::ExitProcess(3);
}

bool CreateGlContext(HWND hwnd) {
    g_dc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int pf = ::ChoosePixelFormat(g_dc, &pfd);
    if (pf == 0) return false;
    if (!::SetPixelFormat(g_dc, pf, &pfd)) return false;
    g_rc = ::wglCreateContext(g_dc);
    if (!g_rc) return false;
    return ::wglMakeCurrent(g_dc, g_rc) != FALSE;
}

void DestroyGlContext(HWND hwnd) {
    ::wglMakeCurrent(nullptr, nullptr);
    if (g_rc) { ::wglDeleteContext(g_rc); g_rc = nullptr; }
    if (g_dc) { ::ReleaseDC(hwnd, g_dc); g_dc = nullptr; }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) ::glViewport(0, 0, LOWORD(lp), HIWORD(lp));
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// Systemschriften aus dem Windows-Ordner; muessen nicht mitgeliefert werden.
std::string Schriftpfad(const char* datei) {
    wchar_t win[MAX_PATH] = {};
    ::GetWindowsDirectoryW(win, MAX_PATH);
    return plattform::Utf8(win) + "\\Fonts\\" + datei;
}

bool Vorhanden(const std::string& pfad) {
    return ::GetFileAttributesW(plattform::Breit(pfad).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Dear ImGui versteht eine Schriftgroesse als ganze Zeilenhoehe, Qt als
// Em-Groesse. Der Faktor wird aus der Schriftdatei gelesen ('hhea'/'head').
float EmFaktor(const std::string& pfad) {
    std::FILE* f = _wfopen(plattform::Breit(pfad).c_str(), L"rb");
    if (!f) return 1.0f;
    auto u16 = [](const unsigned char* p) { return (unsigned)((p[0] << 8) | p[1]); };
    auto u32 = [](const unsigned char* p) {
        return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
    };
    unsigned char kopf[12];
    float faktor = 1.0f;
    if (std::fread(kopf, 1, 12, f) == 12 && std::memcmp(kopf, "ttcf", 4) == 0) {
        unsigned char o[4];
        if (std::fread(o, 1, 4, f) != 4 || std::fseek(f, (long)u32(o), SEEK_SET) != 0 ||
            std::fread(kopf, 1, 12, f) != 12) {
            std::fclose(f);
            return 1.0f;
        }
    } else {
        std::fseek(f, 12, SEEK_SET);
    }
    {
        const unsigned n = u16(kopf + 4);
        unsigned head = 0, hhea = 0;
        for (unsigned i = 0; i < n && i < 64; ++i) {
            unsigned char e[16];
            if (std::fread(e, 1, 16, f) != 16) break;
            if (std::memcmp(e, "head", 4) == 0) head = u32(e + 8);
            if (std::memcmp(e, "hhea", 4) == 0) hhea = u32(e + 8);
        }
        unsigned char b[8];
        unsigned em = 0;
        int auf = 0, ab = 0;
        if (head && std::fseek(f, (long)head + 18, SEEK_SET) == 0 && std::fread(b, 1, 2, f) == 2) em = u16(b);
        if (hhea && std::fseek(f, (long)hhea + 4, SEEK_SET) == 0 && std::fread(b, 1, 4, f) == 4) {
            auf = (int16_t)u16(b);
            ab = (int16_t)u16(b + 2);
        }
        if (em > 0 && auf - ab > 0) faktor = (float)(auf - ab) / (float)em;
    }
    std::fclose(f);
    return (faktor > 0.5f && faktor < 2.5f) ? faktor : 1.0f;
}

ImFont* Lade(std::initializer_list<const char*> kandidaten, bool symbole) {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* k : kandidaten) {
        const std::string p = Schriftpfad(k);
        if (!Vorhanden(p)) continue;
        // Groesse = Zeilenhoehe, Glyphen in Em wie Qt: siehe schrift.h.
        const float faktor = EmFaktor(p);
        ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), 13.0f);
        if (!f) continue;
        schrift::Faktor(f, faktor);
        // Stern, Haken, Kreise (★ ✓ ✗ ○ ◐ ⚠) fehlen in Segoe UI -- aus
        // Segoe UI Symbol dazumischen; japanische Titel aus einer CJK-Schrift.
        if (symbole) {
            const std::string sym = Schriftpfad("seguisym.ttf");
            if (Vorhanden(sym)) {
                ImFontConfig m;
                m.MergeMode = true;
                m.ExtraSizeScale = EmFaktor(sym) / faktor;
                io.Fonts->AddFontFromFileTTF(sym.c_str(), 13.0f, &m);
            }
            for (const char* cjk : {"YuGothR.ttc", "msgothic.ttc"}) {
                const std::string p2 = Schriftpfad(cjk);
                if (!Vorhanden(p2)) continue;
                ImFontConfig m;
                m.MergeMode = true;
                m.ExtraSizeScale = EmFaktor(p2) / faktor;
                io.Fonts->AddFontFromFileTTF(p2.c_str(), 13.0f, &m);
                break;
            }
        }
        return f;
    }
    return nullptr;
}

// Die Qt-Fassung setzt kein eigenes Farbschema; Fensterflaeche, Felder und
// Knoepfe kommen aus der dunklen Windows-Palette. Hier dieselben Toene.
void StilSetzen(float dpi, ImFont* ui) {
    auto C = [](unsigned rgb, float a = 1.0f) {
        return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f, (rgb & 0xff) / 255.0f, a);
    };
    ImGuiStyle& s = ImGui::GetStyle();
    s.FontSizeBase = schrift::Px(ui, 12.0f);   // Segoe UI 9 pt = 12 px Em
    s.FontScaleDpi = dpi;
    s.WindowPadding = ImVec2(8, 8);
    s.FramePadding = ImVec2(7, 4);
    s.ItemSpacing = ImVec2(6, 4);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.ScrollbarSize = 12.0f;
    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;
    s.PopupBorderSize = 1.0f;
    s.WindowRounding = 6.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 6.0f;
    s.GrabRounding = 3.0f;
    s.ScrollbarRounding = 6.0f;
    s.ScaleAllSizes(dpi);

    ImVec4* f = s.Colors;
    f[ImGuiCol_Text] = C(0xffffff);
    f[ImGuiCol_TextDisabled] = C(0x9d9d9d);
    f[ImGuiCol_WindowBg] = C(0x202020);
    f[ImGuiCol_ChildBg] = C(0x202020);
    f[ImGuiCol_PopupBg] = C(0x2b2b2b);
    f[ImGuiCol_Border] = C(0x5a5a5a);
    f[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    f[ImGuiCol_FrameBg] = C(0x2b2b2b);
    f[ImGuiCol_FrameBgHovered] = C(0x323232);
    f[ImGuiCol_FrameBgActive] = C(0x1f1f1f);
    f[ImGuiCol_TitleBg] = C(0x2b2b2b);
    f[ImGuiCol_TitleBgActive] = C(0x2b2b2b);
    f[ImGuiCol_TitleBgCollapsed] = C(0x2b2b2b);
    f[ImGuiCol_MenuBarBg] = C(0x202020);
    f[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    f[ImGuiCol_ScrollbarGrab] = C(0x5a5a5a);
    f[ImGuiCol_ScrollbarGrabHovered] = C(0x787878);
    f[ImGuiCol_ScrollbarGrabActive] = C(0x9d9d9d);
    f[ImGuiCol_CheckMark] = C(0x4cc2ff);
    f[ImGuiCol_SliderGrab] = C(0x4cc2ff);
    f[ImGuiCol_SliderGrabActive] = C(0x4cc2ff);
    f[ImGuiCol_Button] = C(0x2d2d2d);
    f[ImGuiCol_ButtonHovered] = C(0x383838);
    f[ImGuiCol_ButtonActive] = C(0x262626);
    f[ImGuiCol_Header] = C(0x0067c0);
    f[ImGuiCol_HeaderHovered] = C(0x2d2d2d);
    f[ImGuiCol_HeaderActive] = C(0x0067c0);
    f[ImGuiCol_Separator] = C(0x3f3f3f);
    f[ImGuiCol_TextSelectedBg] = C(0x0067c0, 0.6f);
    f[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);
    f[ImGuiCol_NavCursor] = C(0x4cc2ff);
    f[ImGuiCol_TextLink] = C(0x4cc2ff);
}

// Dunkle Titelleiste wie bei der Qt-Fassung im dunklen Windows-Modus.
// Attribut 20 ab Windows 10 20H1, 19 bei aelteren Fassungen.
void DunklerRahmen(HWND hwnd) {
    const BOOL an = TRUE;
    if (FAILED(::DwmSetWindowAttribute(hwnd, 20, &an, sizeof(an))))
        ::DwmSetWindowAttribute(hwnd, 19, &an, sizeof(an));
}

// Fensterrahmen setzen, so dass die Innenflaeche genau `innen` bleibt.
void RahmenAnlegen(HWND hwnd, const RECT& innen) {
    const DWORD stil = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    ::SetWindowLongPtrW(hwnd, GWL_STYLE, stil);
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE, 0);
    RECT r = innen;
    ::AdjustWindowRectEx(&r, stil, FALSE, 0);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
        const RECT& a = mi.rcWork;
        if (r.top < a.top) { const LONG d = a.top - r.top; r.top += d; r.bottom += d; }
        if (r.bottom > a.bottom) r.bottom = a.bottom;
    }
    DunklerRahmen(hwnd);
    ::SetWindowPos(hwnd, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

RECT Mittig(const RECT& flaeche, int b, int h) {
    RECT r;
    b = std::min(b, (int)(flaeche.right - flaeche.left));
    h = std::min(h, (int)(flaeche.bottom - flaeche.top));
    r.left = flaeche.left + ((flaeche.right - flaeche.left) - b) / 2;
    r.top = flaeche.top + ((flaeche.bottom - flaeche.top) - h) / 2;
    r.right = r.left + b;
    r.bottom = r.top + h;
    return r;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    ::SetUnhandledExceptionFilter(AufAbsturz);
    std::set_terminate(AufTerminate);

    if (HMODULE u = ::GetModuleHandleW(L"user32.dll")) {
        using Fn = BOOL(WINAPI*)(HANDLE);
        if (auto fn = reinterpret_cast<Fn>(::GetProcAddress(u, "SetProcessDpiAwarenessContext")))
            fn(reinterpret_cast<HANDLE>(-2) /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */);
        else ::SetProcessDPIAware();
    }
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bilder::Start();

    // Sprache VOR dem Aufbau setzen.
    i18n::SpracheSetzen(Settings::load(plattform::ExeOrdner() + "/settings.json").language);

    HDC bildschirm = ::GetDC(nullptr);
    float dpi = (float)::GetDeviceCaps(bildschirm, LOGPIXELSX) / 96.0f;
    ::ReleaseDC(nullptr, bildschirm);
    if (dpi < 0.5f || dpi > 4.0f) dpi = 1.0f;

    // Intro: 768 x 840, mittig auf dem Bildschirm (IntroWindow::showBlocking).
    RECT schirm = {0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    const RECT introGeo = Mittig(schirm, (int)(768 * dpi), (int)(840 * dpi));
    RECT arbeit;
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &arbeit, 0);
    const RECT hauptGeo = Mittig(arbeit, (int)(megaraw::App::kBreite * dpi), (int)(megaraw::App::kHoehe * dpi));

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXICON),
                                   ::GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)::LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON),
                                     ::GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"MegaRawFpga";
    ::RegisterClassExW(&wc);

    const std::wstring titel = plattform::Breit(megaraw::App::Fenstertitel());
    // Intro: rahmenlos und obenauf (Qt::FramelessWindowHint | WindowStaysOnTopHint).
    HWND hwnd = ::CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, titel.c_str(), WS_POPUP,
                                  introGeo.left, introGeo.top, introGeo.right - introGeo.left,
                                  introGeo.bottom - introGeo.top, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    if (!CreateGlContext(hwnd)) {
        ::MessageBoxW(nullptr, plattform::Breit(T("OpenGL-Kontext konnte nicht angelegt werden.")).c_str(),
                      L"MEGA-RAW FPGA", MB_ICONERROR);
        ::DestroyWindow(hwnd);
        return 1;
    }
    bool vsync = false;
    {
        using SwapFn = BOOL(WINAPI*)(int);
        if (auto swap = reinterpret_cast<SwapFn>(::wglGetProcAddress("wglSwapIntervalEXT"))) vsync = swap(1) != FALSE;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    megaraw::Schriften schriften;
    schriften.ui = Lade({"segoeui.ttf", "tahoma.ttf", "arial.ttf"}, true);
    schriften.fett = Lade({"segoeuib.ttf", "arialbd.ttf"}, true);
    schriften.kursiv = Lade({"segoeuii.ttf", "ariali.ttf"}, true);
    schriften.courier = Lade({"cour.ttf", "consola.ttf"}, false);
    schriften.consolas = Lade({"consola.ttf", "cour.ttf"}, false);
    schriften.consolasFett = Lade({"consolab.ttf", "courbd.ttf"}, false);
    ImFont* courierFett = Lade({"courbd.ttf", "consolab.ttf"}, false);
    ImFont* courierKursiv = Lade({"couri.ttf", "consolai.ttf"}, false);
    ImFont* standard = ImGui::GetIO().Fonts->AddFontDefault();
    if (!schriften.ui) schriften.ui = standard;
    if (!schriften.fett) schriften.fett = schriften.ui;
    if (!schriften.kursiv) schriften.kursiv = schriften.ui;
    if (!schriften.courier) schriften.courier = schriften.ui;
    if (!schriften.consolas) schriften.consolas = schriften.ui;
    if (!schriften.consolasFett) schriften.consolasFett = schriften.consolas;
    ImGui::GetIO().FontDefault = schriften.ui;
    StilSetzen(dpi, schriften.ui);

    ImGui_ImplWin32_InitForOpenGL(hwnd);
    ImGui_ImplOpenGL2_Init();

    intro::Laden(courierFett, courierKursiv, dpi);

    // Original-Timing: 2000 ms warten, erst dann erscheint das Intro.
    const ULONGLONG programmStart = ::GetTickCount64();
    bool introSichtbar = false;

    std::unique_ptr<megaraw::App> app;
    bool imIntro = true;
    ULONGLONG ausblendenAb = 0;
    constexpr ULONGLONG kEinblendMs = 450;   // Hauptfenster aus dem Nichts einblenden

    bool running = true;
    while (running) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;
        if (imIntro && !introSichtbar) {
            if (::GetTickCount64() - programmStart < 2000) { ::Sleep(15); continue; }
            ::ShowWindow(hwnd, SW_SHOW);
            ::UpdateWindow(hwnd);
            intro::Starten();
            introSichtbar = true;
        }
        if (::IsIconic(hwnd)) { ::Sleep(30); continue; }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (imIntro) {
            if (!intro::Zeichnen(dpi)) {
                // Intro vorbei: Hauptfenster.
                imIntro = false;
                RahmenAnlegen(hwnd, hauptGeo);
                app = std::make_unique<megaraw::App>(hwnd, dpi, schriften);
                g_app = app.get();
                app->Starten();
                ausblendenAb = ::GetTickCount64();
            }
        } else {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            app->Zeichnen(vp->Pos, vp->Size);
            const ULONGLONG lauf = ::GetTickCount64() - ausblendenAb;
            if (lauf < kEinblendMs) {
                const float a = 1.0f - (float)lauf / (float)kEinblendMs;
                ImGui::GetForegroundDrawList()->AddRectFilled(
                    vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                    IM_COL32(0x05, 0x05, 0x0a, (int)(a * 255)));
            }
        }

        ImGui::Render();
        if (imIntro) ::glClearColor(5 / 255.0f, 5 / 255.0f, 10 / 255.0f, 1.0f);
        else ::glClearColor(0x20 / 255.0f, 0x20 / 255.0f, 0x20 / 255.0f, 1.0f);
        ::glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        ::SwapBuffers(g_dc);
        if (!vsync) ::Sleep(10);
    }

    // Fenster sofort weg -- das Aufraeumen darunter kann dauern (Monitor-Stopp
    // wartet bis zu 5 s). Letzte Rettung, falls ein Thread haengt.
    ::ShowWindow(hwnd, SW_HIDE);
    std::thread([]() {
        ::Sleep(3000);
        // Hart beenden: ExitProcess kann selbst haengen, wenn ein Hintergrund-
        // faden gerade eine Sperre haelt (Anschluss, Netz). Dann liefe der Prozess
        // unsichtbar weiter und hielte den COM-Anschluss belegt.
        ::TerminateProcess(::GetCurrentProcess(), 0);
    }).detach();

    g_app = nullptr;
    app.reset();
    popup::Beenden();
    intro::Freigeben();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyGlContext(hwnd);
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, inst);
    bilder::Ende();
    ::CoUninitialize();
    return 0;
}
