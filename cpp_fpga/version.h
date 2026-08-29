#pragma once
// FPGA-Variante: eigene, fortlaufende Reihe. Beginnt bei 1, eigene
// Versionsdatei, daher keine Ueberschneidung mit der Stub-Fassung.
// Niemals Nummern wiederverwenden.
// 19: Oberflaeche startet auf Englisch; saemtliche Texte der
// Einrichtung und Spielerkennung laufen jetzt durch T() und haben
// einen Eintrag in der Tabelle (vorher blieben 68 Texte deutsch).
// 20: Hardcore-Pruefung. Mapper Build 96 spiegelt In-Game-Menu- und
// Cheat-Status nach B009; der Monitor stuft bei aktivem Menue/Cheats
// auf Softcore zurueck (RAW-NES-Muster).
// 21: Hardcore-Haken ins Hauptfenster (neben Monitor-Button), Ko-fi als
// Menueeintrag mit Dank-Popup, Support-Link aus den Optionen entfernt.
// 22: RAM-Poll pausiert, solange das In-Game-Menue offen ist (B001 Bit3).
// Verhindert den USB/Menue-Buskonflikt (Sprite-Flackern, dann Freeze).
// Zusammen mit Mapper Build 97 (Hash nur bei angehaltener CPU).
// 23: Autoerkennung zurueck auf den funktionierenden Snapshot-Match
// (md_romindex): Mapper-Mitschnitt lesen + gegen lokale ROM-Sammlung
// matchen. Kein FPGA-MD5-Vollhash mehr - schnell und absturzfrei.
// 24: Erste Fassung, die den vollstaendigen Durchlauf auf Hardware
// geschafft hat - Erkennung, Set laden, Freischaltung gebucht, Popup.
// Dazu: readHeaderSafe wird jetzt auch AUFGERUFEN (der Aufruf fehlte,
// die Funktion allein hat nichts bewirkt - Menue fror nach jeder
// Erkennung ein); romSizeProbe korrigiert zu klein gemeldete
// Header-Groessen (Zero Wing meldet 512 KB bei 1 MB Datei), begrenzt
// auf eine Verdopplung und 4 MB; Spielzustand wird beim Monitor-Stopp
// verworfen, sonst arbeitet der naechste Start mit dem alten Spiel
// weiter; Freischalt-Popup aus RAW-NES uebernommen; Ko-fi im Menue,
// Fehler melden in den Optionen, Erkennungsknopf entfernt; restliche
// deutsche Protokoll- und Monitor-Texte laufen durch T().
// 25: Speicherstaende liegen als Datei auf der SD-Karte statt nur im
// fluechtigen PSRAM. SICHERN schreibt den 192-KB-Slot ueber den MCU
// nach rawslotN.sst, LADEN liest ihn vor der Kennwortpruefung zurueck -
// faellt die Datei aus, bleibt der PSRAM-Inhalt stehen wie bisher.
// Vier Slots, vier Dateien, Ziffer aus der Menueauswahl. Der MCU holt
// die Daten selbst per DMA aus dem PSRAM; die Warteroutine wird dafuer
// nach $FFF000 ins Work-RAM kopiert, aus dem Mapper-Puffer heraus
// friert die Konsole ein. Setzt den Core mit 170821 Bytes voraus.
#define MEGA_RAW_CPP_BUILD 25
// 1.0.0: vollstaendiger Durchlauf bis zur gebuchten Freischaltung ist
// auf echter Hardware belegt (Mortal Kombat, 23.08.2026).
// 1.1.0: Speicherstaende ueberleben das Ausschalten - sichern und
// laden als Datei auf der SD-Karte, ohne PC (28.08.2026).
#define MEGA_RAW_VERSION "1.1.0"
