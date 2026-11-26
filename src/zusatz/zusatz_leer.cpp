// Platzhalter: ohne die Erweiterung bleibt das Kopierprogramm unveraendert.
#include "zusatz.h"

#include "hook_copy.h"

namespace zusatz {

struct Info {};

bool verfuegbar() { return false; }

void beginn(hookcopy::Programm&, bool, bool, bool) {}
void vorspann(hookcopy::Programm&) {}
void abschnitt(hookcopy::Programm&) {}
void eintritt(hookcopy::Programm&) {}
void hauptteil(hookcopy::Programm&, int) {}
void anhang(hookcopy::Programm&) {}
void abschluss(hookcopy::Programm&) {}
bool aktiv(const hookcopy::Programm&) { return false; }

struct Steuerung::Impl {};

Steuerung::Steuerung() = default;
Steuerung::~Steuerung() = default;
int Steuerung::hochladen(mdsnoop::MdSnoop&, const hookcopy::Programm&) { return 0; }
void Steuerung::vorbereiten(mdsnoop::MdSnoop&, const hookcopy::Programm&) {}
void Steuerung::aktivieren(const hookcopy::Programm&) {}
void Steuerung::aus() {}
void Steuerung::aufraeumen(mdsnoop::MdSnoop&) {}
void Steuerung::sekunde(mdsnoop::MdSnoop&) {}
void Steuerung::takt(mdsnoop::MdSnoop&) {}
void Steuerung::bild(mdsnoop::MdSnoop&, const Bytes&) {}
void Steuerung::freigeschaltet(const std::string&) {}
bool Steuerung::frei() const { return false; }

}  // namespace zusatz
