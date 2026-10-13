/***************************************************************************************
**  HTML page of the web dashboard, stored in flash (PROGMEM).
**  Plain HTML/CSS/JavaScript, no external files, so it also works in setup mode
**  without internet. Texts in Greek and English, following the display language.
**  The page talks to the JSON API in WebConfig.h (/api/config, /api/status, ...).
***************************************************************************************/

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="el">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Weather Station</title>
<style>
  :root { --bg:#0b0f14; --card:#151b23; --line:#263040; --text:#e6edf3; --muted:#8b98a8;
          --accent:#ffa630; --ok:#3fb950; --err:#f85149; }
  * { box-sizing:border-box; }
  body { margin:0; background:var(--bg); color:var(--text);
         font:15px/1.4 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  header { padding:16px; display:flex; align-items:center; justify-content:space-between;
           border-bottom:1px solid var(--line); }
  h1 { font-size:18px; margin:0; color:var(--accent); }
  main { max-width:640px; margin:0 auto; padding:16px; }
  .card { background:var(--card); border:1px solid var(--line); border-radius:10px;
          padding:16px; margin-bottom:16px; }
  .card h2 { font-size:15px; margin:0 0 12px; color:var(--accent); }
  label { display:block; margin:10px 0 4px; color:var(--muted); font-size:13px; }
  input, select { width:100%; padding:9px 10px; border-radius:6px; border:1px solid var(--line);
                  background:#0d1117; color:var(--text); font-size:15px; }
  input:focus, select:focus { outline:2px solid var(--accent); border-color:transparent; }
  .row { display:flex; gap:10px; } .row > div { flex:1; min-width:0; }
  .hint { color:var(--muted); font-size:12px; margin-top:4px; }
  .check { display:flex; align-items:center; gap:8px; margin-top:8px; color:var(--muted); font-size:13px; }
  .check input { width:auto; }
  button { padding:10px 14px; border-radius:6px; border:1px solid var(--line); background:#21262d;
           color:var(--text); font-size:14px; cursor:pointer; }
  button.primary { background:var(--accent); border-color:var(--accent); color:#111; font-weight:600; }
  button.danger { color:var(--err); }
  button:disabled { opacity:.5; cursor:default; }
  .actions { display:flex; flex-wrap:wrap; gap:10px; }
  .status { display:grid; grid-template-columns:auto 1fr; gap:4px 14px; font-size:14px; }
  .status span:nth-child(odd) { color:var(--muted); }
  .big { font-size:28px; font-weight:600; color:var(--accent); }
  .msg { padding:10px 12px; border-radius:6px; margin-bottom:16px; display:none; }
  .msg.ok { display:block; background:rgba(63,185,80,.15); border:1px solid var(--ok); }
  .msg.err { display:block; background:rgba(248,81,73,.15); border:1px solid var(--err); }
  .scanbtn { flex:0 0 auto; align-self:flex-end; }
  .lang { display:flex; gap:4px; }
  .lang button { padding:4px 8px; font-size:12px; }
  .lang button.on { border-color:var(--accent); color:var(--accent); }
</style>
</head>
<body>
<header>
  <h1 data-t="title">Weather Station</h1>
  <div class="lang"><button type="button" id="lg0">ΕΛ</button><button type="button" id="lg1">EN</button></div>
</header>
<main>
  <div id="msg" class="msg"></div>

  <div class="card" id="statusCard">
    <h2 data-t="status">Status</h2>
    <div class="big" id="stTemp">-</div>
    <div id="stDesc" style="margin-bottom:10px"></div>
    <div class="status">
      <span data-t="time">Time</span><span id="stTime">-</span>
      <span data-t="lastUpdate">Last update</span><span id="stLast">-</span>
      <span data-t="network">Network</span><span id="stNet">-</span>
      <span data-t="address">Address</span><span id="stIp">-</span>
      <span data-t="uptime">Uptime</span><span id="stUp">-</span>
    </div>
    <div class="actions" style="margin-top:12px">
      <button type="button" id="btnRefresh" data-t="refresh">Refresh weather now</button>
    </div>
  </div>

  <form id="form" autocomplete="off">
    <div class="card">
      <h2>WiFi</h2>
      <div class="row">
        <div>
          <label for="ssid" data-t="ssid">Network name (SSID)</label>
          <input id="ssid" name="ssid" list="ssidList" required maxlength="32">
          <datalist id="ssidList"></datalist>
        </div>
        <button type="button" class="scanbtn" id="btnScan" data-t="scan">Scan</button>
      </div>
      <label for="pass" data-t="pass">Password</label>
      <input id="pass" name="pass" type="password" maxlength="63">
      <div class="hint" data-t="keepHint">Leave empty to keep the saved value.</div>
    </div>

    <div class="card">
      <h2 data-t="weather">Weather</h2>
      <label for="apikey" data-t="apikey">OpenWeather API key</label>
      <input id="apikey" name="apikey" maxlength="32">
      <div class="hint" id="apikeyHint"></div>
      <div class="row">
        <div><label for="lat" data-t="lat">Latitude</label><input id="lat" name="lat" required inputmode="decimal"></div>
        <div><label for="lon" data-t="lon">Longitude</label><input id="lon" name="lon" required inputmode="decimal"></div>
      </div>
      <div class="hint" data-t="latHint">Decimal degrees, e.g. 38.0482 / 23.5407. Negative for South / West.</div>
      <div class="row">
        <div>
          <label for="units" data-t="units">Units</label>
          <select id="units" name="units">
            <option value="metric" data-t="metric">Metric (°C, m/s)</option>
            <option value="imperial" data-t="imperial">Imperial (°F, mph)</option>
          </select>
        </div>
        <div>
          <label for="interval" data-t="interval">Update every (minutes)</label>
          <input id="interval" name="interval" type="number" min="5" max="180" required>
        </div>
      </div>
      <label for="owmlang" data-t="owmlang">OpenWeather description language</label>
      <input id="owmlang" name="owmlang" maxlength="5" required>
      <div class="hint" data-t="owmHint">"en" = the station's own descriptions in the display language. Other codes (el, de, fr...) = OpenWeather translation.</div>
    </div>

    <div class="card">
      <h2 data-t="display">Display</h2>
      <div class="row">
        <div>
          <label for="uilang" data-t="uilang">Language</label>
          <select id="uilang" name="uilang"><option value="0">Ελληνικά</option><option value="1">English</option></select>
        </div>
        <div>
          <label for="rotation" data-t="rotation">Rotation</label>
          <select id="rotation" name="rotation">
            <option value="0" data-t="rot0">Normal</option><option value="2" data-t="rot2">Upside down (180°)</option>
          </select>
        </div>
      </div>
      <label for="tz" data-t="tz">Time zone</label>
      <select id="tz" name="tz"></select>
    </div>

    <div class="card">
      <h2 data-t="security">Dashboard password</h2>
      <label for="admin" data-t="admin">New password (user: admin)</label>
      <input id="admin" name="admin" type="password" maxlength="32">
      <div class="hint" id="adminHint"></div>
      <label class="check"><input type="checkbox" id="clearadmin" name="clearadmin" value="1"><span data-t="clearadmin">Remove the password</span></label>
    </div>

    <div class="card">
      <h2 data-t="otaTitle">Firmware update</h2>
      <div class="hint" id="otaHint"></div>
      <div class="actions" style="margin-top:10px"><a id="otaLink" href="/update"><button type="button" data-t="otaOpen">Open update page</button></a></div>
    </div>

    <div class="actions">
      <button type="submit" class="primary" id="btnSave" data-t="save">Save and restart</button>
      <button type="button" class="danger" id="btnReset" data-t="reset">Factory reset</button>
    </div>
  </form>
</main>

<script>
const T = {
  0: { title:"Μετεωρολογικός Σταθμός", status:"Κατάσταση", time:"Ώρα", lastUpdate:"Τελευταία ενημέρωση",
       network:"Δίκτυο", address:"Διεύθυνση", uptime:"Χρόνος λειτουργίας", refresh:"Ανανέωση καιρού τώρα",
       ssid:"Όνομα δικτύου (SSID)", scan:"Αναζήτηση", pass:"Κωδικός",
       keepHint:"Αφήστε κενό για να κρατήσετε την αποθηκευμένη τιμή.",
       weather:"Καιρός", apikey:"Κλειδί OpenWeather API", lat:"Γεωγρ. πλάτος", lon:"Γεωγρ. μήκος",
       latHint:"Δεκαδικές μοίρες, π.χ. 38.0482 / 23.5407. Αρνητικό για Νότο / Δύση.",
       units:"Μονάδες", metric:"Μετρικές (°C, m/s)", imperial:"Αγγλοσαξονικές (°F, mph)",
       interval:"Ανανέωση κάθε (λεπτά)", owmlang:"Γλώσσα περιγραφής OpenWeather",
       owmHint:"\"en\" = οι περιγραφές του σταθμού στη γλώσσα της οθόνης. Άλλοι κωδικοί (el, de, fr...) = μετάφραση του OpenWeather.",
       display:"Οθόνη", uilang:"Γλώσσα", rotation:"Περιστροφή", rot0:"Κανονική", rot2:"Ανάποδα (180°)",
       tz:"Ζώνη ώρας", security:"Κωδικός πίνακα ελέγχου", admin:"Νέος κωδικός (χρήστης: admin)",
       clearadmin:"Αφαίρεση κωδικού", save:"Αποθήκευση και επανεκκίνηση", reset:"Επαναφορά εργοστασιακών",
       keySet:"Αποθηκευμένο κλειδί: ****", keyNone:"Δεν υπάρχει κλειδί. Δωρεάν εγγραφή στο openweathermap.org",
       adminSet:"Υπάρχει κωδικός. Αφήστε κενό για να τον κρατήσετε.", adminNone:"Χωρίς κωδικό: όποιος είναι στο δίκτυο μπορεί να αλλάξει τις ρυθμίσεις.",
       saved:"Αποθηκεύτηκε. Επανεκκίνηση... Αν άλλαξε το δίκτυο, συνδεθείτε στο νέο δίκτυο.",
       resetAsk:"Διαγραφή όλων των ρυθμίσεων (WiFi, κλειδί, τοποθεσία);", resetDone:"Οι ρυθμίσεις διαγράφηκαν. Επανεκκίνηση σε λειτουργία ρύθμισης...",
       refreshed:"Η ανανέωση ξεκίνησε, ελέγξτε την οθόνη σε λίγα δευτερόλεπτα.", scanning:"Αναζήτηση...",
       setupMode:"Λειτουργία ρύθμισης", days:"ημ", error:"Σφάλμα",
       otaTitle:"Ενημέρωση λογισμικού", otaOpen:"Σελίδα ενημέρωσης",
       otaOn:"Ανεβάστε νέο firmware ή αρχεία από τον browser, ή από το Arduino IDE (θύρα δικτύου \"weather\").",
       otaOff:"Ορίστε κωδικό πίνακα ελέγχου για να ενεργοποιηθούν οι ενημερώσεις." },
  1: { title:"Weather Station", status:"Status", time:"Time", lastUpdate:"Last update",
       network:"Network", address:"Address", uptime:"Uptime", refresh:"Refresh weather now",
       ssid:"Network name (SSID)", scan:"Scan", pass:"Password",
       keepHint:"Leave empty to keep the saved value.",
       weather:"Weather", apikey:"OpenWeather API key", lat:"Latitude", lon:"Longitude",
       latHint:"Decimal degrees, e.g. 38.0482 / 23.5407. Negative for South / West.",
       units:"Units", metric:"Metric (°C, m/s)", imperial:"Imperial (°F, mph)",
       interval:"Update every (minutes)", owmlang:"OpenWeather description language",
       owmHint:"\"en\" = the station's own descriptions in the display language. Other codes (el, de, fr...) = OpenWeather translation.",
       display:"Display", uilang:"Language", rotation:"Rotation", rot0:"Normal", rot2:"Upside down (180°)",
       tz:"Time zone", security:"Dashboard password", admin:"New password (user: admin)",
       clearadmin:"Remove the password", save:"Save and restart", reset:"Factory reset",
       keySet:"Saved key: ****", keyNone:"No key saved. Free sign up at openweathermap.org",
       adminSet:"A password is set. Leave empty to keep it.", adminNone:"No password: anyone on the network can change the settings.",
       saved:"Saved. Restarting... If you changed the network, connect to the new network.",
       resetAsk:"Delete all settings (WiFi, API key, location)?", resetDone:"Settings deleted. Restarting in setup mode...",
       refreshed:"Refresh started, check the display in a few seconds.", scanning:"Scanning...",
       setupMode:"Setup mode", days:"d", error:"Error",
       otaTitle:"Firmware update", otaOpen:"Open update page",
       otaOn:"Upload new firmware or files from the browser, or from the Arduino IDE (network port \"weather\").",
       otaOff:"Set a dashboard password to enable updates." }
};
let L = 0, cfg = null;
const $ = id => document.getElementById(id);
const t = k => T[L][k] || k;

function applyLang() {
  document.documentElement.lang = L ? "en" : "el";
  document.querySelectorAll("[data-t]").forEach(e => e.textContent = t(e.dataset.t));
  $("lg0").classList.toggle("on", L === 0);
  $("lg1").classList.toggle("on", L === 1);
  if (cfg) {
    $("apikeyHint").textContent = cfg.apiKeySet ? t("keySet") + cfg.apiKeyEnd + " · " + t("keepHint") : t("keyNone");
    $("adminHint").textContent = cfg.adminSet ? t("adminSet") : t("adminNone");
    $("otaHint").textContent = cfg.otaEnabled ? t("otaOn") : t("otaOff");
    $("otaLink").style.display = cfg.otaEnabled ? "" : "none";
  }
}
function showMsg(text, ok) { const m = $("msg"); m.textContent = text; m.className = "msg " + (ok ? "ok" : "err"); scrollTo(0, 0); }
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
  return (d ? d + t("days") + " " : "") + String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0");
}

async function loadConfig() {
  cfg = await (await fetch("/api/config")).json();
  L = cfg.uiLang;
  $("ssid").value = cfg.ssid; $("lat").value = cfg.lat; $("lon").value = cfg.lon;
  $("units").value = cfg.units; $("interval").value = cfg.interval; $("owmlang").value = cfg.owmLang;
  $("uilang").value = cfg.uiLang; $("rotation").value = cfg.rotation;
  $("tz").innerHTML = cfg.tzNames.map((n, i) => `<option value="${i}">${n}</option>`).join("");
  $("tz").value = cfg.tz;
  $("pass").placeholder = cfg.passSet ? "••••••••" : "";
  $("apikey").placeholder = cfg.apiKeySet ? "••••••••••••••••" : "";
  applyLang();
}

async function loadStatus() {
  try {
    const s = await (await fetch("/api/status")).json();
    const u = s.units === "imperial" ? "°F" : "°C";
    $("stTemp").textContent = s.temp === null ? "-" : s.temp.toFixed(1) + " " + u;
    $("stDesc").textContent = s.desc;
    $("stTime").textContent = s.time;
    $("stLast").textContent = s.lastUpdate + (s.lastOk ? " ✓" : (s.lastUpdate === "-" ? "" : " ✗"));
    $("stNet").textContent = s.mode === "setup" ? t("setupMode") + " (" + s.ssid + ")" : s.ssid + " (" + s.rssi + " dBm)";
    $("stIp").textContent = s.ip;
    $("stUp").textContent = fmtUptime(s.uptime);
    $("btnRefresh").disabled = s.mode === "setup";
  } catch (e) { /* station busy downloading, try again later */ }
}

$("form").addEventListener("submit", async ev => {
  ev.preventDefault();
  $("btnSave").disabled = true;
  try {
    const r = await fetch("/api/save", { method:"POST", body:new URLSearchParams(new FormData($("form"))) });
    const j = await r.json();
    if (j.ok) showMsg(t("saved"), true); else { showMsg(t("error") + ": " + j.error, false); $("btnSave").disabled = false; }
  } catch (e) { showMsg(t("error") + ": " + e, false); $("btnSave").disabled = false; }
});

$("btnScan").addEventListener("click", async () => {
  const b = $("btnScan"); b.disabled = true; b.textContent = t("scanning");
  try {
    const list = await (await fetch("/api/scan")).json();
    $("ssidList").innerHTML = list.map(n => `<option value="${n.ssid.replace(/"/g, "&quot;")}">${n.rssi} dBm</option>`).join("");
    $("ssid").focus();
  } finally { b.disabled = false; b.textContent = t("scan"); }
});

$("btnRefresh").addEventListener("click", async () => {
  await fetch("/api/refresh", { method:"POST" }); showMsg(t("refreshed"), true);
});

$("btnReset").addEventListener("click", async () => {
  if (!confirm(t("resetAsk"))) return;
  await fetch("/api/reset", { method:"POST" }); showMsg(t("resetDone"), true);
});

$("lg0").onclick = () => { L = 0; applyLang(); };
$("lg1").onclick = () => { L = 1; applyLang(); };

loadConfig().then(loadStatus);
setInterval(loadStatus, 10000);
</script>
</body>
</html>
)rawliteral";

/***************************************************************************************
**  Firmware / LittleFS update page (/update). Sends the file with XMLHttpRequest so
**  the upload progress can be shown. Needs the dashboard password (asked by the browser).
***************************************************************************************/
const char UPDATE_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Weather Station update</title>
<style>
  body { margin:0; background:#0b0f14; color:#e6edf3; font:15px/1.4 system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width:560px; margin:0 auto; padding:16px; }
  h1 { font-size:18px; color:#ffa630; }
  .card { background:#151b23; border:1px solid #263040; border-radius:10px; padding:16px; margin-bottom:16px; }
  h2 { font-size:15px; margin:0 0 8px; color:#ffa630; }
  p { color:#8b98a8; font-size:13px; margin:0 0 10px; }
  code { color:#e6edf3; }
  input { width:100%; margin-bottom:10px; color:#e6edf3; }
  button { padding:10px 14px; border-radius:6px; border:1px solid #ffa630; background:#ffa630; color:#111; font-weight:600; cursor:pointer; }
  button:disabled { opacity:.5; }
  progress { width:100%; height:18px; margin-top:12px; }
  #msg { margin-top:10px; }
  a { color:#ffa630; }
</style>
</head>
<body>
<main>
  <h1>Firmware update / Ενημέρωση</h1>

  <div class="card">
    <h2>Firmware (.bin)</h2>
    <p>Arduino IDE: <code>Sketch &rarr; Export Compiled Binary</code>, then choose
       <code>build/.../ESP32_ILI9341_openweathermap_16.ino.bin</code>
       (not <code>.merged.bin</code> or <code>.bootloader.bin</code>).</p>
    <input type="file" id="fw" accept=".bin">
    <button onclick="send('fw')">Upload firmware</button>
  </div>

  <div class="card">
    <h2>Files / Αρχεία (LittleFS image)</h2>
    <p>Image of the <code>data</code> folder made with <code>mklittlefs</code> at the size of the
       LittleFS partition, see README. Replaces all fonts and icons.</p>
    <input type="file" id="fs" accept=".bin">
    <button onclick="send('fs')">Upload files</button>
  </div>

  <progress id="bar" max="100" value="0" hidden></progress>
  <div id="msg"></div>
  <p style="margin-top:16px"><a href="/">&larr; Dashboard</a></p>
</main>
<script>
function send(type) {
  const f = document.getElementById(type).files[0];
  const msg = document.getElementById("msg"), bar = document.getElementById("bar");
  if (!f) { msg.textContent = "Choose a file first."; return; }
  if (type === "fw" && f.name.indexOf("merged") >= 0) { msg.textContent = "Use the .ino.bin file, not .merged.bin."; return; }
  document.querySelectorAll("button").forEach(b => b.disabled = true);
  const fd = new FormData(); fd.append("file", f, f.name);
  const x = new XMLHttpRequest();
  x.open("POST", "/api/update?type=" + type);
  x.upload.onprogress = e => { if (e.lengthComputable) { bar.hidden = false; bar.value = e.loaded * 100 / e.total; } };
  x.onload = () => {
    let r = {}; try { r = JSON.parse(x.responseText); } catch (e) {}
    if (x.status === 200 && r.ok) { msg.textContent = "Done, restarting... The dashboard is back in about 20 seconds."; setTimeout(() => location.href = "/", 20000); }
    else { msg.textContent = "Update failed: " + (r.error || x.status) + ". The station restarts with the old version."; }
  };
  x.onerror = () => { msg.textContent = "Connection lost during the upload."; };
  msg.textContent = "Uploading, do not switch off...";
  x.send(fd);
}
</script>
</body>
</html>
)rawliteral";
