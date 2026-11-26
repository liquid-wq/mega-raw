// Erweiterungen des Kopierprogramms und ihre Steuerung am PC.
//
// Die Umsetzung liegt in einer eigenen Datei, die nicht zum veroeffentlichten
// Quelltext gehoert. Ohne sie wird zusatz_leer.cpp gebaut: das Kopierprogramm
// bleibt dann unveraendert und die Steuerung tut nichts.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "md_snoop.h"

namespace hookcopy { struct Programm; }

namespace zusatz {

struct Info;   // nur in der Umsetzung bekannt

// Ist die Erweiterung in diesem Bau vorhanden?
bool verfuegbar();

// Generator (aus hookcopy::erzeuge, in dieser Reihenfolge)
void beginn(hookcopy::Programm& p, bool an, bool wegW, bool bank2);
void vorspann(hookcopy::Programm& p);
void abschnitt(hookcopy::Programm& p);
void eintritt(hookcopy::Programm& p);
void hauptteil(hookcopy::Programm& p, int gesichert);
void anhang(hookcopy::Programm& p);
void abschluss(hookcopy::Programm& p);
bool aktiv(const hookcopy::Programm& p);

// Steuerung am PC (aus dem Monitor-Worker)
class Steuerung {
public:
    std::function<void(const std::string&)> log;

    Steuerung();
    ~Steuerung();
    // Weitere Teile des Programms hochladen; Rueckgabe = Abweichungen beim Zuruecklesen
    int hochladen(mdsnoop::MdSnoop& snoop, const hookcopy::Programm& p);
    // Nach erfolgreichem Hochladen, vor dem Einschalten
    void vorbereiten(mdsnoop::MdSnoop& snoop, const hookcopy::Programm& p);
    // Das Programm laeuft
    void aktivieren(const hookcopy::Programm& p);
    void aus();
    // Monitor endet (Spiel laeuft noch): Popup zuruecknehmen, Menuepuffer heil
    void aufraeumen(mdsnoop::MdSnoop& snoop);
    // Einmal je Sekunde
    void sekunde(mdsnoop::MdSnoop& snoop);
    // In jedem Durchlauf der Abfrageschleife
    void takt(mdsnoop::MdSnoop& snoop);
    // Je gueltigem Bild aus dem Bildpuffer
    void bild(mdsnoop::MdSnoop& snoop, const Bytes& bild);
    // Achievement freigeschaltet
    void freigeschaltet(const std::string& titel);
    // Bereit und nichts in Arbeit oder Warteschlange
    bool frei() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace zusatz
