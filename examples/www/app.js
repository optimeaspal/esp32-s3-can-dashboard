// Sendet die gewählte dashboard.json als Raw-Body an /api/config.
// (Kein multipart/form-data – der Server liest den Body direkt als Datei.)
document.getElementById('upload').addEventListener('click', async () => {
  const out = document.getElementById('status');
  const file = document.getElementById('file').files[0];
  if (!file) { out.textContent = 'Bitte zuerst eine Datei wählen.'; return; }
  out.textContent = 'Lade hoch …';
  try {
    const res = await fetch('/api/config', { method: 'POST', body: file });
    const text = await res.text();
    out.textContent = (res.ok ? 'OK: ' : 'Fehler ' + res.status + ': ') + text;
  } catch (e) {
    out.textContent = 'Netzwerkfehler: ' + e;
  }
});
