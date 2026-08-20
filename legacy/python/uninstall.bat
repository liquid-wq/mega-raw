@echo off
REM ============================================================
REM   MEGA-RAW - Entfernen der installierten Pakete
REM   Gegenstueck zu install.bat. Entfernt genau die Pakete,
REM   die install.bat zuvor installiert hat.
REM ============================================================
cd /d "%~dp0"

echo.
echo ============================================================
echo   MEGA-RAW  -  Deinstallation
echo ============================================================
echo.
echo Es werden genau die Pakete entfernt, die install.bat
echo zuvor installiert hat:
echo     - pyserial
echo     - capstone
echo     - pywebview
echo.
echo Python selbst wird NICHT entfernt.
echo.
echo Hinweis: Falls du eines dieser Pakete auch fuer andere
echo Programme nutzt, wird es diesen danach ebenfalls fehlen.
echo Bitte vor dem Entfernen bedenken.
echo.
echo WICHTIG: Bitte schliesse MEGA-RAW vorher komplett.
echo Laeuft das Tool oder das Intro noch, koennen Dateien
echo gesperrt sein und die Deinstallation schlaegt fehl.
echo.

python --version >nul 2>&1
if errorlevel 1 (
    echo FEHLER: Python wurde nicht gefunden. Es ist nichts zu tun.
    echo.
    pause
    exit /b 1
)

set /p ANTWORT="Wirklich entfernen? (j/n): "
if /i "%ANTWORT%"=="j" goto entfernen
if /i "%ANTWORT%"=="ja" goto entfernen
if /i "%ANTWORT%"=="y" goto entfernen
if /i "%ANTWORT%"=="yes" goto entfernen

echo.
echo Abgebrochen. Es wurde nichts entfernt.
echo.
pause
exit /b 0

:entfernen
echo.
echo Entferne Pakete...
echo.

python -m pip uninstall -y pyserial capstone pywebview

echo.
if errorlevel 1 goto fehler

echo ============================================================
echo   Fertig. Die Pakete wurden entfernt.
echo ============================================================
echo.
pause
exit /b 0

:fehler
echo ============================================================
echo   Es gab ein Problem beim Entfernen.
echo.
echo   Meist liegt es daran, dass MEGA-RAW noch offen war und
echo   Dateien gesperrt hat. So loest du es:
echo     1. MEGA-RAW komplett schliessen
echo        (ggf. im Task-Manager Python / msedgewebview2 beenden)
echo     2. Dieses Skript erneut ausfuehren
echo.
echo   Bleiben danach im site-packages-Ordner Reste mit einer
echo   Tilde davor (z.B. ~ywebview, ~apstone): Das sind nur
echo   Ueberbleibsel und koennen gefahrlos von Hand geloescht
echo   werden.
echo ============================================================
echo.
pause
exit /b 1
