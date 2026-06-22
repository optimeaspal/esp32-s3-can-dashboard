@echo off
REM Startet den Dashboard-Editor offline (ohne Geraet) im Standardbrowser.
REM Liefert die Dateien dieses Ordners ueber einen lokalen HTTP-Server aus.
REM Da kein Geraet antwortet, faellt der Editor automatisch in den Offline-Modus
REM (gelbes Banner). Konfiguration laesst sich erstellen und als dashboard.json
REM exportieren bzw. eine vorhandene Datei importieren.

cd /d "%~dp0"

REM Python bevorzugen (Standardbibliothek, keine Abhaengigkeiten).
where python >nul 2>nul
if %ERRORLEVEL%==0 (
  start "" "http://localhost:8000/index.html"
  echo Dashboard-Editor offline unter http://localhost:8000/  ^(Strg+C beendet^)
  python -m http.server 8000
  goto :eof
)

REM Fallback: py-Launcher.
where py >nul 2>nul
if %ERRORLEVEL%==0 (
  start "" "http://localhost:8000/index.html"
  echo Dashboard-Editor offline unter http://localhost:8000/  ^(Strg+C beendet^)
  py -m http.server 8000
  goto :eof
)

echo Kein Python gefunden. Alternativ index.html direkt im Browser oeffnen
echo ^(Doppelklick^) - der Offline-Modus funktioniert auch ueber file://.
pause
