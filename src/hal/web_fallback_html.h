#pragma once

/*
 * Minimale Upload-Seite, in den Flash eingebettet. Wird ausgeliefert, wenn auf
 * der SD-Karte kein /sdcard/www/index.html liegt. Sendet die Datei per Raw-POST
 * (kein multipart) an /api/config.
 */
static const char WEB_FALLBACK_HTML[] =
"<!doctype html><html lang=\"de\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Dashboard-Upload</title></head><body style=\"font-family:sans-serif;max-width:40em;margin:2em auto\">"
"<h1>Dashboard-Konfiguration hochladen</h1>"
"<p>Wähle eine <code>dashboard.json</code> und lade sie hoch. Das Gerät prüft "
"die Datei und startet bei Erfolg neu.</p>"
"<input type=\"file\" id=\"f\" accept=\".json,application/json\">"
"<button id=\"b\">Hochladen</button>"
"<pre id=\"out\"></pre>"
"<script>"
"document.getElementById('b').onclick=async()=>{"
"const o=document.getElementById('out');const f=document.getElementById('f').files[0];"
"if(!f){o.textContent='Bitte zuerst eine Datei wählen.';return;}"
"o.textContent='Lade hoch …';"
"try{const r=await fetch('/api/config',{method:'POST',body:f});"
"const t=await r.text();"
"o.textContent=(r.ok?'OK: ':'Fehler '+r.status+': ')+t;}"
"catch(e){o.textContent='Netzwerkfehler: '+e;}};"
"</script></body></html>";
