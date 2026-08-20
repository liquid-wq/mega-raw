#pragma once
#include <cstdint>
#include <vector>
#include <optional>

// Der eigentliche serielle I/O (open/close/read/write, Timing, Recovery)
// bleibt fuer die Qt-Phase (QSerialPort) offen -- hier nur die Byte-Frames,
// die auf die Leitung gehen bzw. von ihr erwartet werden.

constexpr uint8_t CMD_STATUS = 0x10;
constexpr uint8_t CMD_MEM_RD = 0x19;
constexpr uint8_t CMD_MEM_WR = 0x1A;
constexpr uint32_t ADDR_BRAM = 0x01080000;
constexpr uint32_t ADDR_MBX  = 0x01800300;

std::vector<uint8_t> ed_cmd(uint8_t code, std::optional<uint8_t> sub = std::nullopt);

// Paket fuer memrd: CMD_MEM_RD-Header + addr(4 big-endian) + length(4 big-endian) + 0x00
std::vector<uint8_t> ed_build_memrd_packet(uint32_t addr, uint32_t length);

// Paket fuer memwr: CMD_MEM_WR-Header + addr(4 BE) + len(4 BE) + 0x00 + data
std::vector<uint8_t> ed_build_memwr_packet(uint32_t addr, const std::vector<uint8_t>& data);
