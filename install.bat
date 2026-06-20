@echo off
REM ============================================================
REM   MEGA-RAW - Installation der benoetigten Pakete
REM   Einfach doppelklicken. Installiert alles Noetige.
REM ============================================================
cd /d "%~dp0"

echo.
echo ============================================================
echo   MEGA-RAW  -  Installation
echo ============================================================
echo.

REM Pruefen, ob Python vorhanden ist
python --version >nul 2>&1
if errorlevel 1 (
    echo FEHLER: Python wurde nicht gefunden.
    echo.
    echo Bitte zuerst Python installieren von:
    echo     https://www.python.org/downloads/
    echo Beim Installieren WICHTIG: Haken bei "Add Python to PATH" setzen.
    echo.
    pause
    exit /b 1
)

echo Python gefunden:
python --version
echo.
echo Installiere benoetigte Pakete (pyserial, capstone, pywebview)...
echo.

python -m pip install --upgrade pip
python -m pip install -r requirements.txt

echo.
if errorlevel 1 (
    echo ============================================================
    echo   Es gab ein Problem bei der Installation.
    echo   Bitte den obigen Text pruefen oder erneut versuchen.
    echo ============================================================
) else (
    echo ============================================================
    echo   Fertig! Alle Pakete installiert.
    echo   Du kannst MEGA-RAW jetzt starten:  python md_ra_tool.py
    echo ============================================================
)
echo.
pause
