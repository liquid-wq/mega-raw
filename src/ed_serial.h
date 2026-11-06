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

    const std::string& portName() const { return portname_; }

private:
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
std::pair<std::unique_ptr<EdSerial>, std::string> find_everdrive(const std::string& preferred = std::string(),
                                                                  std::vector<std::string>* versuche = nullptr);
