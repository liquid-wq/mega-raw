#include "archiv.h"

#include "miniz.h"
#include "plattform.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string Endung(const std::string& name) {
    std::string e = fs::u8path(name).extension().u8string();
    if (!e.empty() && e[0] == '.') e.erase(0, 1);
    for (char& c : e) c = (char)::tolower((unsigned char)c);
    return e;
}

bool isRomName(const std::string& n) {
    const std::string e = Endung(n);
    return e == "md" || e == "bin" || e == "gen" || e == "smd";
}

std::string Dateiname(const std::string& p) { return fs::u8path(p).filename().u8string(); }

std::string Verbinde(const std::string& ordner, const std::string& name) {
    return (fs::u8path(ordner) / fs::u8path(name)).u8string();
}

std::string unrarPath() {
    for (const char* p : {"C:/Program Files/WinRAR/UnRAR.exe", "C:/Program Files (x86)/WinRAR/UnRAR.exe"}) {
        std::error_code ec;
        if (fs::exists(p, ec)) return p;
    }
    return std::string();
}

// QProcess::start + waitForFinished: Programm ausfuehren, Ausgabe einsammeln.
bool Ausfuehren(const std::string& programm, const std::vector<std::string>& argumente, int zeitMs,
                std::string* ausgabe, DWORD* exitCode) {
    std::wstring kommando = L"\"" + plattform::Breit(programm) + L"\"";
    for (const auto& a : argumente) kommando += L" \"" + plattform::Breit(a) + L"\"";

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE lesen = nullptr, schreiben = nullptr;
    if (!::CreatePipe(&lesen, &schreiben, &sa, 0)) return false;
    ::SetHandleInformation(lesen, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = schreiben;
    si.hStdError = schreiben;
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> puffer(kommando.begin(), kommando.end());
    puffer.push_back(0);
    const BOOL ok = ::CreateProcessW(nullptr, puffer.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                     nullptr, nullptr, &si, &pi);
    ::CloseHandle(schreiben);
    if (!ok) { ::CloseHandle(lesen); return false; }

    std::string out;
    char b[4096];
    DWORD n = 0;
    while (::ReadFile(lesen, b, sizeof(b), &n, nullptr) && n > 0) out.append(b, n);
    ::CloseHandle(lesen);
    const bool fertig = ::WaitForSingleObject(pi.hProcess, (DWORD)zeitMs) == WAIT_OBJECT_0;
    DWORD code = 1;
    if (fertig) ::GetExitCodeProcess(pi.hProcess, &code);
    else ::TerminateProcess(pi.hProcess, 1);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
    if (ausgabe) {
        // Wie QString::fromLocal8Bit: Ausgabe in der ANSI-Codepage -> UTF-8.
        std::wstring w;
        if (!out.empty()) {
            const int wn = ::MultiByteToWideChar(CP_ACP, 0, out.data(), (int)out.size(), nullptr, 0);
            w.assign((size_t)wn, L'\0');
            ::MultiByteToWideChar(CP_ACP, 0, out.data(), (int)out.size(), &w[0], wn);
        }
        *ausgabe = plattform::Utf8(w);
    }
    if (exitCode) *exitCode = code;
    return fertig;
}

}  // namespace

std::optional<std::string> extract_archive_rom(const std::string& arc, const std::string& zielOrdner) {
    const std::string ext = Endung(arc);
    const std::string dest = zielOrdner.empty() ? fs::u8path(arc).parent_path().u8string() : zielOrdner;

    if (ext != "zip" && ext != "rar") return arc;   // ist schon eine ROM

    if (ext == "zip") {
        // Archiv selbst einlesen statt per Dateiname oeffnen: miniz nimmt
        // schmale Pfade, Umlaute im Pfad wuerden sonst scheitern.
        std::ifstream f(fs::u8path(arc), std::ios::binary);
        if (!f) return std::nullopt;
        std::vector<char> daten((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        mz_zip_archive za;
        std::memset(&za, 0, sizeof(za));
        if (!mz_zip_reader_init_mem(&za, daten.data(), daten.size(), 0)) return std::nullopt;
        int found = -1;
        std::string name;
        const int n = (int)mz_zip_reader_get_num_files(&za);
        for (int i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&za, (mz_uint)i, &st)) continue;
            if (isRomName(st.m_filename)) { found = i; name = st.m_filename; break; }
        }
        if (found < 0) { mz_zip_reader_end(&za); return std::nullopt; }
        size_t groesse = 0;
        void* inhalt = mz_zip_reader_extract_to_heap(&za, (mz_uint)found, &groesse, 0);
        mz_zip_reader_end(&za);
        if (!inhalt) return std::nullopt;
        const std::string out = Verbinde(dest, Dateiname(name));
        std::ofstream o(fs::u8path(out), std::ios::binary);
        o.write((const char*)inhalt, (std::streamsize)groesse);
        mz_free(inhalt);
        return o.good() ? std::optional<std::string>(out) : std::nullopt;
    }

    // RAR:
    const std::string tool = unrarPath();
    if (tool.empty()) return std::nullopt;
    std::string liste;
    if (!Ausfuehren(tool, {"lb", arc}, 30000, &liste, nullptr)) return std::nullopt;
    std::string name;
    size_t pos = 0;
    while (pos <= liste.size()) {
        size_t e = liste.find('\n', pos);
        if (e == std::string::npos) e = liste.size();
        std::string z = liste.substr(pos, e - pos);
        while (!z.empty() && (z.back() == '\r' || z.back() == ' ')) z.pop_back();
        while (!z.empty() && z.front() == ' ') z.erase(0, 1);
        if (!z.empty() && isRomName(z)) { name = z; break; }
        pos = e + 1;
    }
    if (name.empty()) return std::nullopt;
    std::string zielMitTrenner = dest;
    for (char& c : zielMitTrenner) if (c == '/') c = '\\';
    zielMitTrenner += "\\";
    DWORD code = 1;
    if (!Ausfuehren(tool, {"e", "-inul", "-o+", arc, name, zielMitTrenner}, 120000, nullptr, &code) || code != 0)
        return std::nullopt;
    const std::string out = Verbinde(dest, Dateiname(name));
    std::error_code ec;
    return fs::exists(fs::u8path(out), ec) ? std::optional<std::string>(out) : std::nullopt;
}
