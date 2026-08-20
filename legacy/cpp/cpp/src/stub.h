#pragma once
#include <cstdint>
#include <vector>
#include <utility>

// STUB_GEN = 0x48 (Kennung, die der Stub ins BRAM[197] schreibt)
constexpr uint8_t STUB_GEN = 0x48;

// addr_list: Liste von (ram_addr, nbytes) wie in md_ra_tool.py
using AddrEntry = std::pair<uint32_t, int>;

std::vector<uint8_t> build_stub(uint32_t stub_addr, uint32_t orig_vblank,
                                 const std::vector<AddrEntry>& addr_list,
                                 uint32_t gameid = 0);

// STUB_SLIM_CHUNK = 1 (Adressen pro Rotationsblock)
constexpr int STUB_SLIM_CHUNK = 1;

std::vector<uint8_t> build_stub_slim(uint32_t stub_addr, uint32_t orig_vblank,
                                      const std::vector<AddrEntry>& addr_list,
                                      uint32_t gameid = 0);

// Ein IPS-Patch-Eintrag: (Offset, Daten)
using IpsPatch = std::pair<uint32_t, std::vector<uint8_t>>;

std::vector<uint8_t> make_ips(const std::vector<IpsPatch>& patches);

// rom_end = -1 bedeutet "nicht gesetzt"
uint16_t calc_checksum(const std::vector<uint8_t>& data, int64_t rom_end = -1);
