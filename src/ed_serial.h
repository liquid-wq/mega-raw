// Serieller Anschluss zur Mega EverDrive.
//
// Ablauf, Zeiten und Fehlertexte wie in der Qt-Fassung (ed_serial_qt.h/.cpp):
// 921600 Baud, 66 Nullbytes zum Wecken, Reststrom bis 0,3 s Funkstille
// absaugen, Status-Handshake, memrd mit 500 ms Zeitgrenze, bei einem Fehler
// einmal neu verbinden (recover). Ersetzt ist nur der Unterbau: Win32 statt
// QSerialPort, std::vector statt QByteArray.
//
// Blockiert den aufrufenden Thread -- nur aus dem Worker-Thread aufrufen.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using Bytes = std::vector<uint8_t>;

class EdSerial {
public:
    explicit EdSerial(std::string port = "COM5");
    ~EdSerial();

    // Wirft std::runtime_error bei Fehlschlag.
    bool open();
    void close();
    bool isOpen() const { return handle_ != nullptr; }

    Bytes memrd(uint32_t addr, uint32_t length);
    void memwr(uint32_t addr, const Bytes& data);
    // Ohne Wiederverbinden (kein Durchsuchen fremder Anschluesse)
    Bytes memrdNoRecover(uint32_t addr, uint32_t length) { return memrdRaw(addr, length); }
    // Systeminfo (CMD_SYS_INF 0x26, 64 Byte) wie RAW-NES; leer ohne Antwort.
    Bytes sysInfoNoRecover();
    // Anschluss schliessen und neu oeffnen (kein Durchsuchen fremder Anschluesse)
    bool neuOeffnen();

    const std::string& portName() const { return portname_; }

    // Dateizugriff auf die SD-Karte ueber die MCU des EverDrive (Befehle aus
    // Krikzz' edio/everdrive.c). Zahlen wie bei memrd big-endian.
    // Rueckgabe: 0 ok, >0 Fehlercode der Karte (FatFs), <0 keine/falsche Antwort.
    struct Eintrag {
        std::string name;
        uint32_t groesse = 0;
        bool ordner = false;
    };
    int ordnerListe(const std::string& pfad, std::vector<Eintrag>& aus, int maxEintraege = 4000);
    int ordnerAnlegen(const std::string& pfad);
    int loeschen(const std::string& pfad);
    int dateiInfo(const std::string& pfad, Eintrag& aus);
    int dateiSchreiben(const std::string& pfad, const Bytes& daten);
    int dateiLesen(const std::string& pfad, uint32_t groesse, Bytes& aus);
    // Pfad des zuletzt gestarteten ROMs (CMD_ROM_PATH 0x31, Firmware ab v24)
    int romPfad(std::string& aus);

private:
    int statusRaw();
    bool liesEintrag(Eintrag& e);
    Bytes readUpTo(int maxLen, int timeoutMs);
    bool write(const Bytes& data);
    void clearInput();
    bool recover();
    Bytes memrdRaw(uint32_t addr, uint32_t length);
    void memwrRaw(uint32_t addr, const Bytes& data);

    std::string portname_;
    void* handle_ = nullptr;
    std::mutex lock_;
};

// Alle COM-Anschluesse des Systems samt Beschreibung und USB-Kennung.
struct SerialPortInfo {
    std::string name;
    std::string beschreibung;
    std::string hersteller;
    std::string seriennummer;
    uint16_t vid = 0, pid = 0;
    bool hatKennung = false;
};
std::vector<SerialPortInfo> availableSerialPorts();

// Reihenfolge wie in der Qt-Fassung: zuerst `preferred`, dann echte
// USB-Geraete (mit Herstellerkennung, kein Bluetooth), zuletzt der Rest.
// {nullptr, ""}, wenn keiner antwortet. versuche: je erfolglosem Anschluss
// "COMx: Grund" (fuer das Protokoll).
// Wird vor jedem find_everdrive aufgerufen (die Oberflaeche schliesst dort
// ihre eigene Leerlauf-Verbindung, damit der Anschluss frei ist).
extern void (*g_vorFindEverdrive)();
std::pair<std::unique_ptr<EdSerial>, std::string> find_everdrive(const std::string& preferred = std::string(),
                                                                  std::vector<std::string>* versuche = nullptr);
