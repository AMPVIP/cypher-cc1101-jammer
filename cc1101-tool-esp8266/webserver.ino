// Web layer for the CC1101 tool: WiFi AP + HTTP control panel.
// This file is concatenated with the main sketch by the Arduino build.

// A Print target that accumulates everything written to it into a String,
// so a command's output can be captured and returned over HTTP.
class StringPrint : public Print {
  public:
    String buf;
    StringPrint() { buf.reserve(256); }   // avoid early reallocs on large command output
    size_t write(uint8_t c) override { buf += (char)c; return 1; }
    size_t write(const uint8_t *b, size_t n) override {
        for (size_t i = 0; i < n; i++) buf += (char)b[i];
        return n;
    }
};

static const char PAGE_INDEX[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CC1101 Control Deck</title>
<style>
:root{--bg:#07100f;--panel:#0d1917;--panel2:#11211e;--line:#203c36;
 --ink:#e7fff8;--muted:#7da098;--accent:#57efbd;--accent2:#20b889;--warn:#ffbf69;--danger:#ff6b75}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;color:var(--ink);background:
 radial-gradient(circle at 15% 0,#12332b 0,transparent 34%),var(--bg);
 font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;line-height:1.45}
.shell{width:min(1080px,100%);margin:auto;padding:22px}
header{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}
.eyebrow{color:var(--accent);font-size:10px;letter-spacing:.18em;text-transform:uppercase}
h1{font-size:23px;letter-spacing:-.04em;margin:2px 0 0}.sub{color:var(--muted);font-size:11px}
.online{display:flex;align-items:center;gap:8px;border:1px solid var(--line);border-radius:99px;padding:7px 11px;background:#0b1715}
.dot{width:8px;height:8px;border-radius:50%;background:var(--warn);box-shadow:0 0 10px currentColor}.dot.ok{background:var(--accent)}
.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:10px}
.metric,.card{background:linear-gradient(145deg,var(--panel2),var(--panel));border:1px solid var(--line);border-radius:12px}
.metric{padding:12px 14px}.metric span,.label{display:block;color:var(--muted);font-size:9px;letter-spacing:.14em;text-transform:uppercase}
.metric strong{display:block;margin-top:5px;font-size:18px;font-weight:650}.metric strong.accent{color:var(--accent);text-transform:uppercase}
.activity{border:1px solid var(--line);border-radius:12px;background:#091512;padding:11px 14px;margin-bottom:10px}
.activity-top{display:flex;justify-content:space-between;gap:12px}.activity-top strong{color:var(--accent);font-weight:600}
.track{height:4px;background:#182b27;border-radius:9px;overflow:hidden;margin-top:9px}.fill{height:100%;width:0;background:linear-gradient(90deg,var(--accent2),var(--accent));transition:width .35s}
.grid{display:grid;grid-template-columns:minmax(0,1.08fr) minmax(0,.92fr);gap:10px}.card{padding:15px;min-width:0}.wide{grid-column:1/-1}
.card-head{display:flex;align-items:center;justify-content:space-between;margin-bottom:13px}.card-head h2{font-size:12px;margin:0;letter-spacing:.08em;text-transform:uppercase}
.hint{color:var(--muted);font-size:10px}.card>summary{list-style:none;position:relative;cursor:pointer;padding-right:25px}.card>summary::-webkit-details-marker{display:none}.card>summary::after{content:"+";position:absolute;right:2px;top:0;color:var(--accent);font-size:20px;line-height:1}.card[open]>summary::after{content:"−"}.card[open]>summary{margin-bottom:13px}.card:not([open])>summary{margin-bottom:0}.fields{display:grid;grid-template-columns:repeat(6,1fr);gap:8px}.field label{display:block;color:var(--muted);font-size:9px;margin:0 0 4px;text-transform:uppercase}
input,select,button{font:inherit;border:1px solid var(--line);border-radius:7px;outline:none}
input,select{width:100%;color:var(--ink);background:#081310;padding:8px}input:focus,select:focus{border-color:var(--accent2)}
button{cursor:pointer;color:var(--ink);background:#12231f;padding:8px 11px;transition:.15s}button:hover{border-color:var(--accent);transform:translateY(-1px)}
button.primary{background:var(--accent2);border-color:var(--accent2);color:#03110d;font-weight:700}button.danger{color:var(--danger);border-color:#5f3035}
button.on{color:#03110d;background:var(--accent);border-color:var(--accent);box-shadow:0 0 18px #57efbd33}
.row{display:flex;flex-wrap:wrap;gap:7px;align-items:center;min-width:0}.row+.row{margin-top:9px}.grow{flex:1;min-width:140px;width:auto}.mini{width:78px}
.console{background:#030b09;border:1px solid #19332d;border-radius:8px;overflow:hidden}.console-bar{display:flex;justify-content:space-between;align-items:center;padding:8px 11px;border-bottom:1px solid #19332d;color:var(--muted);font-size:10px}
#out{white-space:pre-wrap;overflow-wrap:anywhere;padding:12px;min-height:190px;max-height:390px;overflow:auto;color:#bdebdc}
.clear{padding:3px 7px;font-size:9px}.cmdline{margin-top:9px;display:flex;gap:7px}.cmdline input{flex:1}
@media(max-width:760px){.shell{padding:14px}.metrics{grid-template-columns:1fr 1fr}.grid{grid-template-columns:minmax(0,1fr)}.wide{grid-column:auto}.fields{grid-template-columns:1fr 1fr 1fr}h1{font-size:19px}.activity-top{flex-wrap:wrap}input,select{font-size:16px;min-height:42px}button{min-height:42px}.clear{min-height:28px}}
@media(max-width:440px){header{align-items:flex-start;flex-wrap:wrap}.online{padding:6px 8px}.fields{grid-template-columns:1fr 1fr}.metric strong{font-size:15px}.sub,.card-head .hint{display:none}.cmdline{flex-direction:column}.cmdline button{width:100%}#tx{flex-basis:100%}}
</style></head><body><div class="shell">
<header><div><div class="eyebrow">RF field instrument</div><h1>CC1101 Control Deck</h1>
 <div class="sub">ESP8266 access point / 192.168.1.100</div></div>
 <div class="online"><span id="dot" class="dot"></span><span id="conn">connecting</span></div></header>

<div class="metrics">
 <div class="metric"><span>Mode</span><strong id="m-mode" class="accent">idle</strong></div>
 <div class="metric"><span>Frequency</span><strong id="m-freq">433.92 MHz</strong></div>
 <div class="metric"><span>Buffer</span><strong id="m-buffer">0 / 4096</strong></div>
 <div class="metric"><span>Uptime</span><strong id="m-up">0s</strong></div>
</div>
<div class="activity"><div class="activity-top"><strong id="detail">Ready for command</strong><span id="activity">0 events</span></div>
 <div class="track"><div id="progress" class="fill"></div></div></div>

<main class="grid">
<details class="card wide" open><summary class="card-head"><h2>Radio configuration</h2><span class="hint">Changes apply as one ordered command set</span></summary>
 <div class="fields">
  <div class="field"><label>MHz</label><input id="mhz" value="433.92"></div>
  <div class="field"><label>Modulation</label><select id="mod"><option value="0">2-FSK</option><option value="1">GFSK</option><option value="2" selected>ASK/OOK</option><option value="3">4-FSK</option><option value="4">MSK</option></select></div>
  <div class="field"><label>Data rate</label><input id="drate" value="9.6"></div>
  <div class="field"><label>Deviation</label><input id="dev" value="47.60"></div>
  <div class="field"><label>RX BW</label><input id="rxbw" value="812.50"></div>
  <div class="field"><label>TX dBm</label><input id="pa" value="10"></div>
 </div><div class="row" style="margin-top:10px"><button class="primary" onclick="applyRadio()">Apply radio profile</button><button onclick="cmd('getrssi')">Read RSSI / LQI</button><button onclick="cmd('status')">System status</button></div>
</details>

<details class="card" open><summary class="card-head"><h2>Live operations</h2><span class="hint">Active controls glow</span></summary>
 <div class="row"><button id="b-rx" onclick="cmd('rx')">Packet RX</button><button id="b-rec" onclick="cmd('rec')">Record packets</button><button id="b-jam" onclick="cmd('jam')">Jam</button><button class="danger" onclick="cmd('x')">Stop mode</button></div>
 <div class="row"><input id="scanFrom" class="mini" value="433" title="scan start"><span>to</span><input id="scanTo" class="mini" value="435" title="scan stop"><button id="b-scan" onclick="startScan()">Start scan</button></div>
 <div class="row"><input id="rawUsec" class="mini" value="100" title="sample microseconds"><span>us</span><button id="b-sniff" onclick="cmd('rxraw '+v('rawUsec'))">RX raw</button><button id="b-recraw" onclick="cmd('recraw '+v('rawUsec'))">Record raw</button><button id="b-playraw" onclick="cmd('playraw '+v('rawUsec'))">Play raw</button></div>
 <div class="row"><input id="bruteUsec" class="mini" value="1000"><span>us</span><input id="bruteBits" class="mini" value="8"><span>bits</span><button id="b-brute" onclick="startBrute()">Start brute</button></div>
</details>

<details class="card"><summary class="card-head"><h2>Capture buffer</h2><span id="frames" class="hint">0 frames stored</span></summary>
 <div class="row"><button onclick="cmd('show')">Packet frames</button><button onclick="cmd('showraw')">Raw hex</button><button onclick="cmd('showbit')">Bit view</button></div>
 <div class="row"><button onclick="cmd('save')">Save EEPROM</button><button onclick="cmd('load')">Load EEPROM</button><button class="danger" onclick="cmd('flush')">Clear buffer</button></div>
 <div class="row"><input id="tx" class="grow" placeholder="hex payload, e.g. A1B2C3"><button class="primary" onclick="cmd('tx '+v('tx'))">Transmit</button></div>
</details>

<details class="card wide" open><summary class="card-head"><h2>Command terminal</h2><span class="hint">Same command engine as USB serial</span></summary>
 <div class="console"><div class="console-bar"><span>LIVE COMMAND LOG</span><button class="clear" onclick="$('out').textContent=''">CLEAR</button></div><div id="out">[SYSTEM] control deck ready</div></div>
 <div class="cmdline"><input id="cli" placeholder="Enter any command" autocomplete="off" onkeydown="if(event.key=='Enter')run()"><button class="primary" onclick="run()">Run command</button></div>
</details>
</main>

<script>
const $=id=>document.getElementById(id), v=id=>$(id).value.trim();
function log(t){const o=$('out'),stamp=new Date().toLocaleTimeString();o.textContent+='['+stamp+'] '+t;o.scrollTop=o.scrollHeight}
let commandQueue=Promise.resolve(), commandBusy=false;
let profileHydrated=false;
async function sendCommand(c){
 commandBusy=true;log('> '+c+'\n');
 try{
  const r=await fetch('/cmd?c='+encodeURIComponent(c),{method:'POST',cache:'no-store'});
  const t=await r.text();
  if(!r.ok)throw new Error(t||('HTTP '+r.status));
  log((t||'[OK] command accepted').trim()+'\n');
 }catch(e){log('[ERROR] '+e.message+'\n')}
 commandBusy=false;
}
function cmd(c){
 c=(c||'').trim();if(!c)return Promise.resolve();
 commandQueue=commandQueue.then(()=>sendCommand(c),()=>sendCommand(c));
 return commandQueue;
}
function run(){cmd(v('cli'));$('cli').value=''}
function applyRadio(){profileHydrated=false;return cmd('applyradio '+[
 v('mhz'),v('mod'),v('drate'),v('dev'),v('rxbw'),v('pa')].join(' '))}
function startScan(){cmd('scan '+v('scanFrom')+' '+v('scanTo'))}
function startBrute(){cmd('brute '+v('bruteUsec')+' '+v('bruteBits'))}
function fmtTime(sec){const h=Math.floor(sec/3600),m=Math.floor(sec%3600/60),s=sec%60;return h?(h+'h '+m+'m'):(m?(m+'m '+s+'s'):(s+'s'))}
function detailFor(s){
 if(s.mode==='scan')return 'Sweeping '+s.cursor.toFixed(2)+' MHz / pass '+(s.activity+1)+(s.scanRssi>-100?' / best '+s.scanFreq.toFixed(2)+' MHz at '+s.scanRssi+' dBm':'');
 if(s.mode==='sniff')return 'Streaming raw samples / '+s.activity+' bytes captured';
 if(s.mode==='recraw')return s.waiting?'Armed and waiting for RF signal':'Capturing raw waveform';
 if(s.mode==='rec')return 'Recording packets / '+s.frames+' frames stored';
 if(s.mode==='rx')return 'Listening for packets / '+s.activity+' received';
 if(s.mode==='jam')return 'Transmitting random frames / '+s.activity+' sent';
 if(s.mode==='brute')return 'Testing code '+s.activity+' of '+s.total;
 if(s.mode==='playraw')return 'Replaying raw capture';
 return 'Ready for command';
}
async function poll(){if(commandBusy)return;try{
	 const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw new Error('offline');const s=await r.json();
	 $('dot').classList.add('ok');$('conn').textContent='device online';
	 $('m-mode').textContent=s.mode;$('m-freq').textContent=s.freq.toFixed(2)+' MHz';
	 if(!profileHydrated){$('mhz').value=s.freq.toFixed(4);$('rxbw').value=s.rxbw.toFixed(4);profileHydrated=true}
 $('m-buffer').textContent=s.bufferPos+' / '+s.bufferSize;$('m-up').textContent=fmtTime(s.uptime);
 $('frames').textContent=s.frames+' frame'+(s.frames===1?'':'s')+' stored';
 $('detail').textContent=detailFor(s);$('activity').textContent=s.elapsed+'s active / '+s.activity+' events';
 $('progress').style.width=(s.progress<0?0:s.progress)+'%';
 for(const m of ['sniff','jam','rx','rec','scan','brute','recraw','playraw']){
  const b=$('b-'+m);if(b)b.classList.toggle('on',s.mode===m);
 }
}catch(e){$('dot').classList.remove('ok');$('conn').textContent='device offline'}}
setInterval(poll,1000);poll();
</script></div></body></html>)HTML";

// Bring up the SoftAP with the configured static IP.
static void startAP(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // no modem sleep -> lower AP request latency
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    if (strlen(AP_PASSWORD) >= 8) WiFi.softAP(AP_SSID, AP_PASSWORD);
    else                          WiFi.softAP(AP_SSID);
    Serial.print(F("[WIFI] AP online | SSID "));
    Serial.print(F(AP_SSID));
    Serial.print(F(" | web http://"));
    Serial.println(WiFi.softAPIP());
}

static void handleRoot(void)
{
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send_P(200, "text/html", PAGE_INDEX);
}

// Run a CLI command string through exec() with output captured, return as text.
static void handleCmd(void)
{
    String c = server.arg("c");
    if (!c.length() && server.hasArg("plain")) c = server.arg("plain");
    c.trim();

    if (!c.length()) {
        server.send(400, F("text/plain"), F("Missing command"));
        return;
    }
    if (c.length() >= BUF_LENGTH) {
        server.send(413, F("text/plain"), F("Command too long"));
        return;
    }

    // This is also the device-side proof that a browser request reached /cmd.
    Serial.print(F("[web] "));
    Serial.println(c);

    static char line[BUF_LENGTH];
    c.toCharArray(line, BUF_LENGTH);
    StringPrint sink;
    out = &sink;                       // capture this command's output
    exec(line);                        // reuse the exact same command logic
    out = &Serial;                     // restore (single-threaded: safe)
    if (!sink.buf.length()) sink.buf = F("OK");
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("text/plain"), sink.buf);
}

// Return current radio/mode state as JSON for the polling UI.
static void handleStatus(void)
{
    currentFreq = readRadioFrequencyMHz();
    currentRxBw = readRadioRxBwKHz();
    byte radioState = readRadioState();
    String j = "{";
    j += "\"mode\":\"";     j += modeName();                        j += "\",";
    j += "\"freq\":";       j += String(currentFreq, 4);            j += ",";
    j += "\"rxbw\":";       j += String(currentRxBw, 4);            j += ",";
    j += "\"marcstate\":";  j += radioState;                         j += ",";
    j += "\"frames\":";     j += framesinbigrecordingbuffer;        j += ",";
    j += "\"bufferPos\":";  j += bigrecordingbufferpos;             j += ",";
    j += "\"bufferSize\":"; j += RECORDINGBUFFERSIZE;               j += ",";
    j += "\"activity\":";   j += modeActivityCount;                 j += ",";
    j += "\"elapsed\":";    j += modeElapsedSeconds();              j += ",";
    j += "\"progress\":";   j += modeProgressPercent();             j += ",";
    j += "\"cursor\":";     j += String(scanCursor, 2);             j += ",";
    j += "\"waiting\":";    j += recrawWaiting ? "true" : "false"; j += ",";
    j += "\"total\":";      j += bruteMax;                          j += ",";
    j += "\"scanFreq\":";   j += String(scanBestFreq, 2);           j += ",";
    j += "\"scanRssi\":";   j += scanBestRssi;                      j += ",";
    j += "\"uptime\":";     j += (millis() / 1000);
    j += "}";
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.send(200, F("application/json"), j);
}

static void setupWebServer(void)
{
    server.on("/", handleRoot);
    server.on("/cmd", HTTP_POST, handleCmd);
    server.on("/status", handleStatus);
    server.begin();
}
