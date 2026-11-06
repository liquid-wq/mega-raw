#pragma once
#include <QByteArray>
#include <cstdint>
#include <functional>
#include <QtGlobal>

#include "ed_serial_qt.h"

// Liest das ROM aus dem Cartridge-PSRAM, waehrend das Spiel laeuft.
//
// Direkt geht das nicht: der PC fragt und nimmt die Antwort im selben Zuge
// (pi.sv haelt pi_act nur ueber wenige SPI-Bits). Bei laufendem Spiel liegt
// in diesem Fenster keine Buslücke, der 68000 belegt den Bus fast durchgehend.
//
// Deshalb holt der Mapper selbst: er wartet auf MD_ASn hoch, liest ein Wort,
// legt es im Puffer ab und macht beim naechsten Mal weiter. Ist der 512-Byte-
// Block voll, meldet er fertig und der PC holt ihn am Stueck.
//
// Registerfenster (Basis mdsnoop::kAddrSnoop):
//   0x7000  Startadresse Bit 22..16, Lesen: Bit 0 = beschaeftigt
//   0x7001  Startadresse Bit 15..8
//   0x7002  Startadresse Bit 7..0
//   0x7003  Schreiben startet den Block
//   0x8000  512 Byte Puffer, Big-Endian

namespace mdrom {

constexpr uint32_t kFcmOffset = 0x7000;
constexpr uint32_t kHshOffset = 0xA000;   // Groesse schreiben, Status lesen
constexpr uint32_t kDigOffset = 0xA010;   // 16 Byte Ergebnis
constexpr uint32_t kBufOffset = 0x8000;
// Build 98: Erkennung parkt die CPU in der Menue-Warteschleife, damit der
// Hash-Fetch den ROM-Bus allein hat. A005 = Park-Befehl (1=parken,
// 0=freigeben). B001 Bit3 (hok_jmp) bestaetigt, dass die CPU geparkt ist.
constexpr uint32_t kParkOffset = 0xA005;
constexpr uint32_t kJmpStatOffset = 0xB001;
constexpr int kBlockSize = 8192;

struct Stats {
    int blocks = 0;          // gelesene Bloecke
    int rereadNeeded = 0;    // Bloecke, bei denen zwei Lesungen abwichen
    int giveUp = 0;          // Bloecke, die auch nach Wiederholung abwichen
    uint32_t rejected = 0;   // vom Mapper verworfene Uebernahmen (Zaehler im Core)
    qint64 msFill = 0;       // Zeit im Mapper: Block anstossen und warten
    qint64 msXfer = 0;       // Zeit fuer die Uebertragung des Puffers
};

// Loest einen Block aus und wartet, bis der Mapper fertig ist.
bool startBlock(EdSerial& ed, uint32_t byteAddr, uint32_t base, int retries = 4000);

// Liest den Pufferinhalt. Kein Auslesen, nur Uebertragung.
QByteArray fetchBuffer(EdSerial& ed, uint32_t base);

// Holt einen Block und prueft die Uebertragung, indem der Puffer zweimal
// gelesen und verglichen wird. Weichen die Lesungen ab, liegt der Fehler in
// der Uebertragung; stimmen sie ueberein und sind trotzdem falsch, liegt er
// beim Fuellen im Mapper. Diese Unterscheidung ist der Zweck.
QByteArray readBlockVerified(EdSerial& ed, uint32_t byteAddr, uint32_t base, Stats& st);

uint32_t romSizeFromHeader(const QByteArray& headerBlock);

// Korrigiert eine zu klein gemeldete Header-Groesse, indem hinter dem
// gemeldeten Ende nach echten Daten gesucht wird. Siehe Kommentar in der
// .cpp - ohne das scheitert die Erkennung bei ROMs mit falschem Header.
uint32_t romSizeProbe(EdSerial& ed, uint32_t base, uint32_t hdrSize);

// Liest den Kopfblock und stellt den Pufferinhalt danach wieder her.
//
// Der Puffer bei 0x8000 enthaelt den Menuecode (ra_snoop.sv laedt ihn per
// $readmemh beim Start hinein). Der Blockleser wuerde ihn ueberschreiben -
// danach springt die CPU beim Menueaufruf in zerstoerten Code und friert
// ein. Deshalb vorher sichern, nachher zurueckschreiben.
QByteArray readHeaderSafe(EdSerial& ed, uint32_t base);

// Laesst den Mapper den MD5 ueber das ganze ROM rechnen und holt die 16 Byte
// ab. Ueber USB gehen dabei nur diese 16 Byte statt zwei Megabyte - das
// Auslesen selbst passiert in der FPGA, in den Luecken des 68000-Busses.
// Leerer Rueckgabewert heisst: nicht fertig geworden.
QByteArray romMd5(EdSerial& ed, uint32_t size, uint32_t base,
                  const std::function<bool()>& keepGoing = {});

QByteArray readRom(EdSerial& ed, uint32_t size, uint32_t base, Stats& st,
                   const std::function<bool(uint32_t, uint32_t)>& progress = {});

} // namespace mdrom
