#include "ed_serial.h"

#include "ed_protocol.h"

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {

HANDLE H(void* h) { return static_cast<HANDLE>(h); }

std::string NachUtf8(const wchar_t* w) {
    if (!w) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s((size_t)n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

// "USB\VID_38DF&PID_0018\..." -> vid/pid
bool LiesVidPid(const std::string& hardwareId, uint16_t* vid, uint16_t* pid) {
    std::string s = hardwareId;
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    const size_t v = s.find("vid_");
    const size_t p = s.find("pid_");
    if (v == std::string::npos || p == std::string::npos) return false;
    *vid = (uint16_t)std::strtoul(s.substr(v + 4, 4).c_str(), nullptr, 16);
    *pid = (uint16_t)std::strtoul(s.substr(p + 4, 4).c_str(), nullptr, 16);
    return true;
}

std::string ToHex(const Bytes& b) {
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (uint8_t c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 0xF]); }
    return s;
}

std::string Eigenschaft(HDEVINFO info, SP_DEVINFO_DATA* daten, DWORD welche) {
    wchar_t puffer[512] = {};
    if (!::SetupDiGetDeviceRegistryPropertyW(info, daten, welche, nullptr, (PBYTE)puffer,
                                             sizeof(puffer) - sizeof(wchar_t), nullptr))
        return {};
    return NachUtf8(puffer);
}

}  // namespace

EdSerial::EdSerial(std::string port) : portname_(std::move(port)) {}

EdSerial::~EdSerial() { close(); }

// Wie readUpTo() der Qt-Fassung (waitForReadyRead + readAll): kehrt zurueck,
// sobald maxLen Bytes da sind oder die Zeit um ist. Gewartet wird im Treiber
// (ReadFile mit Zeitgrenze), nicht mit Sleep -- dessen Aufloesung von rund
// 15 ms wuerde jede Abfrage im Poll-Takt ausbremsen.
Bytes EdSerial::readUpTo(int maxLen, int timeoutMs) {
    Bytes out;
    if (!handle_) return out;
    const ULONGLONG start = ::GetTickCount64();
    while ((int)out.size() < maxLen) {
        const long long remaining = (long long)timeoutMs - (long long)(::GetTickCount64() - start);
        if (remaining <= 0) break;

        COMMTIMEOUTS t = {};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = (DWORD)remaining;
        t.WriteTotalTimeoutConstant = 500;
        ::SetCommTimeouts(H(handle_), &t);

        uint8_t buf[8192];
        const DWORD want = (DWORD)std::min<int>(maxLen - (int)out.size(), (int)sizeof(buf));
        DWORD got = 0;
        if (!::ReadFile(H(handle_), buf, want, &got, nullptr)) break;
        if (got == 0) break;
        out.insert(out.end(), buf, buf + got);
    }
    return out;
}

bool EdSerial::write(const Bytes& data) {
    if (!handle_) return false;
    size_t raus = 0;
    while (raus < data.size()) {
        DWORD n = 0;
        if (!::WriteFile(H(handle_), data.data() + raus, (DWORD)(data.size() - raus), &n, nullptr))
            return false;
        if (n == 0) return false;
        raus += n;
    }
    return true;
}

void EdSerial::clearInput() {
    if (handle_) ::PurgeComm(H(handle_), PURGE_RXCLEAR);
}

bool EdSerial::open() {
    close();

    // Ab COM10 ist die \\.\-Schreibweise Pflicht.
    const std::string pfad = "\\\\.\\" + portname_;
    HANDLE h = ::CreateFileA(pfad.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD fehler = ::GetLastError();
        char t[96];
        if (fehler == ERROR_ACCESS_DENIED)
            std::snprintf(t, sizeof(t), "Fehler %lu (belegt - läuft ein anderes Programm mit diesem Anschluss?)", fehler);
        else
            std::snprintf(t, sizeof(t), "Fehler %lu", fehler);
        throw std::runtime_error("Port " + portname_ + " konnte nicht geöffnet werden: " + t);
    }

    // 921600 Baud, 8N1, keine Flusssteuerung (QSerialPort-Vorgaben).
    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    ::GetCommState(h, &dcb);
    dcb.BaudRate = 921600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fErrorChar = FALSE;
    // DTR und RTS wie QSerialPort: so lassen, wie der Treiber sie vorgibt,
    // nur einen eingestellten Handshake-Modus abschalten (qt_set_common_props,
    // qt_set_flowcontrol). Fest gesetzt hatte sie nur die RAW-NES-Vorlage.
    if (dcb.fDtrControl == DTR_CONTROL_HANDSHAKE) dcb.fDtrControl = DTR_CONTROL_DISABLE;
    if (dcb.fRtsControl == RTS_CONTROL_HANDSHAKE) dcb.fRtsControl = RTS_CONTROL_DISABLE;
    if (!::SetCommState(h, &dcb)) {
        ::CloseHandle(h);
        throw std::runtime_error("Port " + portname_ + " konnte nicht geöffnet werden: "
                                 "Baudrate/Rahmenformat nicht setzbar");
    }
    ::SetupComm(h, 1 << 16, 1 << 14);
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = h;

    // Wie edlink: 66 Null-Bytes senden, Eingang leeren.
    write(Bytes(66, 0));

    // Eventuellen Reststrom aktiv leersaugen bis 0.3s Funkstille.
    double quietMs = 0.0;
    const ULONGLONG t0 = ::GetTickCount64();
    while (quietMs < 300.0 && ::GetTickCount64() - t0 < 3000) {
        Bytes chunk = readUpTo(4096, 50);
        if (!chunk.empty()) quietMs = 0.0;
        else quietMs += 50.0;
    }
    clearInput();

    // Status-Handshake
    const auto cmdVec = ed_cmd(CMD_STATUS);
    write(Bytes(cmdVec.begin(), cmdVec.end()));
    ::Sleep(50);
    const Bytes resp = readUpTo(4, 500);

    const bool has5A = std::find(resp.begin(), resp.end(), (uint8_t)0x5A) != resp.end();
    const bool hasA5 = std::find(resp.begin(), resp.end(), (uint8_t)0xA5) != resp.end();
    if (resp.empty() || (!has5A && !hasA5)) {
        const std::string hex = resp.empty() ? "leer" : ToHex(resp);
        close();
        throw std::runtime_error("EverDrive antwortet nicht (Status: " + hex);
    }
    return true;
}

void EdSerial::close() {
    if (handle_) {
        ::CloseHandle(H(handle_));
        handle_ = nullptr;
    }
}

bool EdSerial::recover() {
    close();
    ::Sleep(800);
    try {
        open();
        return true;
    } catch (...) {}

    for (const auto& info : availableSerialPorts()) {
        try {
            portname_ = info.name;
            open();
            return true;
        } catch (...) {
            close();
        }
    }
    return false;
}

Bytes EdSerial::memrd(uint32_t addr, uint32_t length) {
    try {
        return memrdRaw(addr, length);
    } catch (...) {
        if (recover()) return memrdRaw(addr, length);
        throw;
    }
}

void EdSerial::memwr(uint32_t addr, const Bytes& data) {
    try {
        memwrRaw(addr, data);
    } catch (...) {
        if (recover()) { memwrRaw(addr, data); return; }
        throw;
    }
}

Bytes EdSerial::memrdRaw(uint32_t addr, uint32_t length) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_) throw std::runtime_error("memrd: nicht verbunden");
    clearInput();
    const auto pkt = ed_build_memrd_packet(addr, length);
    write(Bytes(pkt.begin(), pkt.end()));
    Bytes data = readUpTo((int)length, 500);
    if ((uint32_t)data.size() != length) {
        throw std::runtime_error("memrd: " + std::to_string(data.size()) + "/" +
                                 std::to_string(length) + " Bytes erhalten");
    }
    return data;
}

void EdSerial::memwrRaw(uint32_t addr, const Bytes& data) {
    std::lock_guard<std::mutex> lock(lock_);
    if (!handle_) throw std::runtime_error("memwr: nicht verbunden");
    const auto pkt = ed_build_memwr_packet(addr, data);
    // Wie QSerialPort::write: ein Fehlschlag wirft nicht, er faellt beim
    // naechsten Lesen auf.
    write(Bytes(pkt.begin(), pkt.end()));
}

std::vector<SerialPortInfo> availableSerialPorts() {
    std::vector<SerialPortInfo> aus;
    HDEVINFO info = ::SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (info == INVALID_HANDLE_VALUE) return aus;

    SP_DEVINFO_DATA daten = {};
    daten.cbSize = sizeof(daten);
    for (DWORD i = 0; ::SetupDiEnumDeviceInfo(info, i, &daten); ++i) {
        HKEY key = ::SetupDiOpenDevRegKey(info, &daten, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t name[64] = {};
        DWORD typ = 0, groesse = sizeof(name) - sizeof(wchar_t);
        const LONG r = ::RegQueryValueExW(key, L"PortName", nullptr, &typ, (LPBYTE)name, &groesse);
        ::RegCloseKey(key);
        if (r != ERROR_SUCCESS || name[0] == 0) continue;

        SerialPortInfo p;
        p.name = NachUtf8(name);
        p.beschreibung = Eigenschaft(info, &daten, SPDRP_DEVICEDESC);
        p.hersteller = Eigenschaft(info, &daten, SPDRP_MFG);
        p.hatKennung = LiesVidPid(Eigenschaft(info, &daten, SPDRP_HARDWAREID), &p.vid, &p.pid);
        wchar_t inst[512] = {};
        if (::SetupDiGetDeviceInstanceIdW(info, &daten, inst, 511, nullptr)) {
            // "USB\VID_xxxx&PID_xxxx\<Seriennummer>"
            const std::string id = NachUtf8(inst);
            const size_t s = id.find_last_of('\\');
            if (s != std::string::npos && id.compare(0, 4, "USB\\") == 0) p.seriennummer = id.substr(s + 1);
        }
        aus.push_back(std::move(p));
    }
    ::SetupDiDestroyDeviceInfoList(info);
    std::sort(aus.begin(), aus.end(),
              [](const SerialPortInfo& a, const SerialPortInfo& b) { return a.name < b.name; });
    return aus;
}

std::pair<std::unique_ptr<EdSerial>, std::string> find_everdrive(const std::string& preferred,
                                                                  std::vector<std::string>* versuche) {
    // Reihenfolge: zuerst der vorgemerkte Port, dann echte USB-Geraete
    // (mit Hersteller-Kennung), zuletzt virtuelle Ports. Bluetooth-Ports
    // lassen sich oeffnen, antworten aber nie - jeder Fehlversuch kostet
    // Leerlaufschleife plus Handshake, das summiert sich auf Sekunden.
    std::vector<std::string> candidates, likely, unlikely;
    if (!preferred.empty()) candidates.push_back(preferred);
    for (const auto& info : availableSerialPorts()) {
        if (std::find(candidates.begin(), candidates.end(), info.name) != candidates.end()) continue;
        std::string b = info.beschreibung;
        for (char& c : b) c = (char)::tolower((unsigned char)c);
        const bool bt = b.find("bluetooth") != std::string::npos;
        if (!bt && info.hatKennung) likely.push_back(info.name);
        else unlikely.push_back(info.name);
    }
    candidates.insert(candidates.end(), likely.begin(), likely.end());
    candidates.insert(candidates.end(), unlikely.begin(), unlikely.end());
    for (const auto& port : candidates) {
        auto ed = std::make_unique<EdSerial>(port);
        try {
            ed->open();
            return {std::move(ed), port};
        } catch (const std::exception& e) {
            if (versuche) versuche->push_back(port + ": " + e.what());
            ed->close();
        } catch (...) {
            if (versuche) versuche->push_back(port + ": ?");
            ed->close();
        }
    }
    if (versuche && candidates.empty()) versuche->push_back("keine seriellen Anschlüsse");
    return {nullptr, std::string()};
}
