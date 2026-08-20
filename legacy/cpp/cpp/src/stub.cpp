#include "stub.h"

namespace {
void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >> 24) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back(x & 0xFF);
}
void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((x >> 8) & 0xFF);
    v.push_back(x & 0xFF);
}
void set_u32_at(std::vector<uint8_t>& v, size_t pos, uint32_t x) {
    v[pos]     = (x >> 24) & 0xFF;
    v[pos + 1] = (x >> 16) & 0xFF;
    v[pos + 2] = (x >> 8) & 0xFF;
    v[pos + 3] = x & 0xFF;
}
uint32_t bit_length(uint32_t x) {
    uint32_t n = 0;
    while (x) { x >>= 1; n++; }
    return n;
}
} // namespace

std::vector<uint8_t> build_stub(uint32_t stub_addr, uint32_t orig_vblank,
                                 const std::vector<AddrEntry>& addr_list,
                                 uint32_t gameid) {
    std::vector<uint8_t> stub;
    uint32_t part2 = stub_addr + 14;

    stub.insert(stub.end(), {0x2F, 0x3C});               // move.l #part2,-(sp)
    put_u32(stub, part2);
    stub.insert(stub.end(), {0x40, 0xE7});               // move sr,-(sp)
    stub.insert(stub.end(), {0x4E, 0xF9});               // jmp Original-Handler
    put_u32(stub, orig_vblank);

    // ---- Teil 2 ----
    stub.insert(stub.end(), {0x48, 0xE7, 0xE0, 0xC0,     // movem.l d0-d1/a0,-(a7)
                              0x13, 0xFC, 0x00, 0x01, 0x00, 0xA1, 0x30, 0x01}); // SRAM unlock
    stub.insert(stub.end(), {0x4E, 0x71, 0x4E, 0x71});   // NOP*2

    stub.insert(stub.end(), {0x41, 0xF9});               // lea ($200001),a0
    put_u32(stub, 0x200001);

    uint16_t off = 0;
    for (const auto& [ram_addr, nbytes] : addr_list) {
        for (int b = 0; b < nbytes; ++b) {
            uint32_t byte_addr = ram_addr + b;
            stub.insert(stub.end(), {0x10, 0x39});       // move.b (addr),d0
            put_u32(stub, byte_addr);
            if (off == 0) {
                stub.insert(stub.end(), {0x10, 0x80});
            } else {
                stub.insert(stub.end(), {0x11, 0x40});
                put_u16(stub, off);
            }
            off += 2;
        }
    }

    // TV-Notification: MBX ($A130DE)
    stub.insert(stub.end(), {0x30, 0x39});
    put_u32(stub, 0xA130DE);

    stub.insert(stub.end(), {0x32, 0x00});               // move.w d0,d1
    stub.insert(stub.end(), {0x02, 0x41, 0xF0, 0x00});   // andi.w #$F000,d1
    stub.insert(stub.end(), {0x0C, 0x41, 0xA0, 0x00});   // cmpi.w #$A000,d1
    stub.insert(stub.end(), {0x66, 0x14});               // bne.s +20
    stub.insert(stub.end(), {0x02, 0x40, 0x0E, 0xEE});   // andi.w #$0EEE,d0
    stub.insert(stub.end(), {0x23, 0xFC, 0xC0, 0x00, 0x00, 0x00,
                              0x00, 0xC0, 0x00, 0x04});  // CRAM addr 0
    stub.insert(stub.end(), {0x33, 0xC0, 0x00, 0xC0, 0x00, 0x00}); // move.w d0,(VDP data)

    // Stub-Generations-Kennung -> BRAM[197]
    stub.insert(stub.end(), {0x13, 0xFC, 0x00, STUB_GEN});
    put_u32(stub, 0x2000C5);

    if (gameid > 0xFFFF) gameid = 0; // RA-Platzhalter-IDs nicht schreiben
    stub.insert(stub.end(), {0x13, 0xFC, 0x00, static_cast<uint8_t>(gameid & 0xFF)});
    put_u32(stub, 0x2000CB); // Game-ID lo
    stub.insert(stub.end(), {0x13, 0xFC, 0x00, static_cast<uint8_t>((gameid >> 8) & 0xFF)});
    put_u32(stub, 0x2000CD); // Game-ID hi

    // Frame-Handshake
    stub.insert(stub.end(), {0x13, 0xFC, 0x00, 0x00});
    put_u32(stub, 0x2000C7);

    stub.insert(stub.end(), {0x4C, 0xDF, 0x03, 0x07});   // movem.l (a7)+,d0-d2/a0-a1
    stub.insert(stub.end(), {0x4E, 0x73});               // rte

    return stub;
}

std::vector<uint8_t> build_stub_slim(uint32_t stub_addr, uint32_t orig_vblank,
                                      const std::vector<AddrEntry>& addr_list,
                                      uint32_t gameid) {
    if (gameid > 0xFFFF) gameid = 0;
    int CHUNK = STUB_SLIM_CHUNK > 1 ? STUB_SLIM_CHUNK : 1;

    std::vector<uint32_t> slots;
    for (const auto& [ram_addr, nbytes] : addr_list)
        for (int b = 0; b < nbytes; ++b)
            slots.push_back(ram_addr + b);
    if (slots.empty()) slots.push_back(0xFF0000);

    int TOTAL = static_cast<int>(slots.size());
    int G = (TOTAL + CHUNK - 1) / CHUNK;

    // Marker ueber die Rotations-Bloecke verteilen
    struct Marker { uint32_t addr; uint8_t val; };
    std::vector<Marker> markers = {
        {0x2000C5, STUB_GEN}, {0x2000C7, 0x00},
        {0x2000CB, static_cast<uint8_t>(gameid & 0xFF)},
        {0x2000CD, static_cast<uint8_t>((gameid >> 8) & 0xFF)}
    };
    std::vector<std::vector<Marker>> block_markers(G);
    for (size_t i = 0; i < markers.size(); ++i)
        block_markers[i % G].push_back(markers[i]);

    uint32_t part2 = stub_addr + 14;
    std::vector<uint8_t> stub;
    stub.insert(stub.end(), {0x2F, 0x3C});
    put_u32(stub, part2);
    stub.insert(stub.end(), {0x40, 0xE7});
    stub.insert(stub.end(), {0x4E, 0xF9});
    put_u32(stub, orig_vblank);

    // --- Dispatch ---
    uint32_t G2 = (G > 1) ? (1u << bit_length(static_cast<uint32_t>(G - 1))) : 1u;
    std::vector<uint8_t> disp;
    disp.insert(disp.end(), {0x48, 0xE7, 0x80, 0xC0});
    disp.insert(disp.end(), {0x13, 0xFC, 0x00, 0x01, 0x00, 0xA1, 0x30, 0x01});
    disp.insert(disp.end(), {0x41, 0xF9});
    put_u32(disp, 0x200001);
    disp.insert(disp.end(), {0x10, 0x39});
    put_u32(disp, 0x200203);
    disp.insert(disp.end(), {0x52, 0x00});
    disp.insert(disp.end(), {0x02, 0x40});
    put_u16(disp, static_cast<uint16_t>(G2 - 1));
    disp.insert(disp.end(), {0x13, 0xC0});
    put_u32(disp, 0x200203);
    disp.insert(disp.end(), {0xE5, 0x48});
    size_t jt_lea_pos = disp.size() + 2;
    disp.insert(disp.end(), {0x43, 0xF9});
    put_u32(disp, 0); // Platzhalter: JUMPTAB
    disp.insert(disp.end(), {0x22, 0x71, 0x00, 0x00});
    disp.insert(disp.end(), {0x4E, 0xD1});

    // --- Rotations-Bloecke ---
    uint32_t base_blocks = part2 + static_cast<uint32_t>(disp.size());
    std::vector<uint32_t> block_addrs;
    std::vector<uint8_t> blocks;
    std::vector<size_t> jmp_exit_pos;

    for (int g = 0; g < G; ++g) {
        block_addrs.push_back(base_blocks + static_cast<uint32_t>(blocks.size()));
        for (int k = g * CHUNK; k < std::min((g + 1) * CHUNK, TOTAL); ++k) {
            blocks.insert(blocks.end(), {0x10, 0x39});
            put_u32(blocks, slots[k]);
            blocks.insert(blocks.end(), {0x11, 0x40});
            put_u16(blocks, static_cast<uint16_t>(2 * k));
        }
        for (const auto& m : block_markers[g]) {
            blocks.insert(blocks.end(), {0x13, 0xFC, 0x00, m.val});
            put_u32(blocks, m.addr);
        }
        jmp_exit_pos.push_back(blocks.size() + 2);
        blocks.insert(blocks.end(), {0x4E, 0xF9});
        put_u32(blocks, 0); // Platzhalter: EXIT
    }
    uint32_t EXIT = base_blocks + static_cast<uint32_t>(blocks.size());
    for (size_t pos : jmp_exit_pos)
        set_u32_at(blocks, pos, EXIT);

    std::vector<uint8_t> exit_code = {0x4C, 0xDF, 0x03, 0x01, 0x4E, 0x73};
    uint32_t JUMPTAB = EXIT + static_cast<uint32_t>(exit_code.size());
    set_u32_at(disp, jt_lea_pos, JUMPTAB);

    // Sprungtabelle: G Bloecke + Phantom-Eintraege (G..G2-1) -> EXIT
    std::vector<uint8_t> jt;
    for (uint32_t a : block_addrs) put_u32(jt, a);
    for (uint32_t i = static_cast<uint32_t>(G); i < G2; ++i) put_u32(jt, EXIT);

    std::vector<uint8_t> result = std::move(stub);
    result.insert(result.end(), disp.begin(), disp.end());
    result.insert(result.end(), blocks.begin(), blocks.end());
    result.insert(result.end(), exit_code.begin(), exit_code.end());
    result.insert(result.end(), jt.begin(), jt.end());
    return result;
}

std::vector<uint8_t> make_ips(const std::vector<IpsPatch>& patches) {
    std::vector<uint8_t> ips = {'P', 'A', 'T', 'C', 'H'};
    for (const auto& [off, d] : patches) {
        ips.push_back((off >> 16) & 0xFF);
        ips.push_back((off >> 8) & 0xFF);
        ips.push_back(off & 0xFF);
        uint16_t len = static_cast<uint16_t>(d.size());
        ips.push_back((len >> 8) & 0xFF);
        ips.push_back(len & 0xFF);
        ips.insert(ips.end(), d.begin(), d.end());
    }
    ips.insert(ips.end(), {'E', 'O', 'F'});
    return ips;
}

uint16_t calc_checksum(const std::vector<uint8_t>& data, int64_t rom_end) {
    uint32_t cs = 0;
    size_t end = (rom_end != 0 && rom_end != -1)
        ? std::min(data.size(), static_cast<size_t>(rom_end + 1))
        : data.size();
    if (end % 2) end -= 1;
    for (size_t i = 0x200; i < end; i += 2) {
        uint16_t val = (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
        cs = (cs + val) & 0xFFFF;
    }
    return static_cast<uint16_t>(cs);
}
