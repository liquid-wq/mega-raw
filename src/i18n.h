// Uebersetzung, im Code hinterlegt.
//
// Muster wie in der Qt-Fassung: Der deutsche Text steht im Quelltext und ist
// zugleich der Schluessel. Englisch kommt aus i18n_tabelle.inc (unveraendert
// aus der Qt-Fassung) und den Ergaenzungen in i18n.cpp, die weiteren Sprachen
// aus i18n_<code>.inc (erzeugt von sprachen/erzeuge.py). Fehlt ein Eintrag,
// gilt der englische Text, fehlt auch der, der deutsche.
//
// Dear ImGui zeichnet jedes Bild neu und ruft T() dabei frisch auf. Ein
// Sprachwechsel wirkt damit sofort.
#pragma once

#include <array>
#include <initializer_list>
#include <string>
#include <vector>

namespace i18n {

// Reihenfolge = Index in Zweisprachig. 0 deutsch, 1 englisch.
constexpr int kSprachen = 7;
struct SpracheInfo { const char* kennung; const char* name; };
const std::vector<SpracheInfo>& Sprachen();

void SpracheSetzen(const std::string& kennung);  // "de", "en", "es", ...
const std::string& Sprache();
int SpracheIndex();
// Fuer Inhalte, die es nur deutsch und englisch gibt (Nachricht von der
// Projektseite, Katzentext): alles ausser Deutsch zeigt die englische Fassung.
bool Englisch();

// Deutscher Quelltext -> Text in der aktuellen Sprache. Aus jedem Thread.
const char* T(const char* deutsch);
std::string T(const std::string& deutsch);

// Wie QString::arg: ersetzt %1, %2, ... der Reihe nach.
std::string Fmt(const std::string& muster, std::initializer_list<std::string> werte);

std::string Zahl(long long n);
// Hex mit fester Breite und fuehrenden Nullen; gross = Grossbuchstaben.
std::string Hex(unsigned long long n, int breite, bool gross = true);

// Legt die Sprache fuer den laufenden Thread fest, solange das Objekt lebt.
// Andere Threads sehen weiter die eingestellte Sprache.
class SpracheFest {
public:
    explicit SpracheFest(int index);
    ~SpracheFest();
    SpracheFest(const SpracheFest&) = delete;
    SpracheFest& operator=(const SpracheFest&) = delete;
private:
    int alt_;
};

// Ein Text in allen Sprachen. Log- und Statuszeilen werden so gespeichert,
// damit sie nach einem Sprachwechsel in der neuen Sprache dastehen. (Der
// Name stammt aus der Zeit mit zwei Sprachen.)
struct Zweisprachig {
    std::array<std::string, kSprachen> t;
    Zweisprachig() = default;
    Zweisprachig(const std::string& s) { t.fill(s); }   // ohne Uebersetzung
    Zweisprachig(const char* s) { t.fill(s); }
    const std::string& text() const { return t[SpracheIndex()]; }
    bool empty() const { return t[0].empty(); }
};

// Erzeugt den Text einmal je Sprache: f wird fuer jede Sprache aufgerufen.
template <class F>
Zweisprachig Beide(F&& f) {
    Zweisprachig z;
    for (int i = 0; i < kSprachen; ++i) {
        SpracheFest s(i);
        z.t[i] = f();
    }
    return z;
}

}  // namespace i18n

using i18n::Zweisprachig;

// Text in allen Sprachen aus einem Ausdruck, der T()/Fmt() benutzt.
#define BEIDE(ausdruck) (::i18n::Beide([&]() { return std::string(ausdruck); }))

// Markiert einen deutschen Text als Schluessel, der erst bei der Anzeige
// uebersetzt wird. Tut zur Laufzeit nichts.
#define TK(de) (de)

using i18n::T;
using i18n::Fmt;
