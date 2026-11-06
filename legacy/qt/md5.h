#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Reine RFC-1321-MD5-Implementierung. Keine externe Lib -> keine zusaetzliche
// DLL-Abhaengigkeit in der verteilten .exe.
std::string md5_hex(const std::vector<uint8_t>& data);
std::string md5_file(const std::string& path); // liest Datei komplett,
