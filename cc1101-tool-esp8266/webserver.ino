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
<title>cc1101</title>
<style>
:root{--bg:#f7f7f4;--ink:#222;--line:#ddd;--accent:#2e7d32}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--ink);margin:0;padding:18px;
 font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:14px;line-height:1.5}
h1{font-size:15px;font-weight:600;margin:0 0 2px}
.status{color:#666;margin-bottom:14px}
.status .cur::after{content:"_";animation:b 1s steps(1) infinite}
@keyframes b{50%{opacity:0}}
section{border-top:1px solid var(--line);padding:12px 0}
.lbl{color:#888;text-transform:lowercase;font-size:12px;margin-bottom:6px}
input,select,button{font:inherit;color:var(--ink);background:#fff;
 border:1px solid var(--line);border-radius:4px;padding:5px 8px}
button{cursor:pointer;background:#fff}
button:hover{border-color:var(--accent)}
button.on{border-color:var(--accent);color:var(--accent)}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.row>label{color:#888;font-size:12px;margin-right:2px}
#out{white-space:pre-wrap;background:#fff;border:1px solid var(--line);border-radius:4px;
 padding:8px;min-height:120px;max-height:340px;overflow:auto;margin-top:6px}
a{color:var(--accent)}
</style></head><body>
<h1>cc1101</h1>
<div class="status"><span id="st" class="cur">idle</span></div>

<section><div class="lbl">radio</div><div class="row">
 <label>mhz</label><input id="mhz" size="7" value="433.92">
 <label>mod</label><select id="mod">
  <option value="0">2-FSK</option><option value="1">GFSK</option>
  <option value="2" selected>ASK/OOK</option><option value="3">4-FSK</option><option value="4">MSK</option></select>
 <label>drate</label><input id="drate" size="5" value="9.6">
 <label>dev</label><input id="dev" size="5" value="47.60">
 <label>rxbw</label><input id="rxbw" size="6" value="812.50">
 <label>pa</label><input id="pa" size="3" value="10">
 <button onclick="apply()">apply</button>
</div></section>

<section><div class="lbl">actions</div><div class="row">
 <button id="b-sniff" onclick="tog('sniff','rxraw 100')">sniff</button>
 <button id="b-scan" onclick="scan()">scan</button>
 <button id="b-jam" onclick="tog('jam','jam')">jam</button>
 <button id="b-rx" onclick="tog('rx','rx')">rx</button>
 <button id="b-rec" onclick="tog('rec','rec')">rec</button>
 <button onclick="cmd('recraw 100')">rec-raw</button>
 <button onclick="cmd('playraw 100')">play-raw</button>
 <button onclick="brute()">brute</button>
 <button onclick="cmd('x')">stop</button>
</div></section>

<section><div class="lbl">buffer</div><div class="row">
 <button onclick="cmd('show')">frames</button>
 <button onclick="cmd('showraw')">raw</button>
 <button onclick="cmd('showbit')">bits</button>
 <button onclick="cmd('save')">save</button>
 <button onclick="cmd('load')">load</button>
 <button onclick="cmd('flush')">flush</button>
 <label>tx</label><input id="tx" size="16" placeholder="hex">
 <button onclick="cmd('tx '+v('tx'))">send</button>
</div></section>

<section><div class="lbl">console</div><div class="row">
 <input id="cli" size="30" placeholder="command" onkeydown="if(event.key=='Enter')run()">
 <button onclick="run()">run</button>
</div><div id="out"></div></section>

<script>
const $=id=>document.getElementById(id), v=id=>$(id).value.trim();
function log(t){const o=$('out');o.textContent+=t;o.scrollTop=o.scrollHeight}
async function cmd(c){const r=await fetch('/cmd',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:'c='+encodeURIComponent(c)});log('> '+c+'\n'+await r.text()+'\n')}
function run(){cmd(v('cli'));$('cli').value=''}
function apply(){cmd('setmhz '+v('mhz'));cmd('setmodulation '+v('mod'));
 cmd('setdrate '+v('drate'));cmd('setdeviation '+v('dev'));
 cmd('setrxbw '+v('rxbw'));cmd('setpa '+v('pa'))}
function scan(){const a=prompt('scan  start stop (MHz)','433 435');if(a)cmd('scan '+a)}
function brute(){const a=prompt('brute  usec bits','1000 8');if(a)cmd('brute '+a)}
let modes={sniff:'rxraw',jam:'jam',rx:'rx',rec:'rec'};
function tog(name,c){cmd(c)}
async function poll(){try{const s=await(await fetch('/status')).json();
 $('st').textContent=s.mode+'  '+s.freq+' MHz  up '+s.uptime+'s'
 +(s.scanRssi>-100?'  best '+s.scanFreq+' ('+s.scanRssi+')':'');
 for(const m of ['sniff','jam','rx','rec','scan','brute'])
  $('b-'+m)&&$('b-'+m).classList.toggle('on',s.mode==m);
}catch(e){}}
setInterval(poll,1500);poll();
</script></body></html>)HTML";

// Bring up the SoftAP with the configured static IP.
static void startAP(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);   // no modem sleep -> lower AP request latency
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    if (strlen(AP_PASSWORD) >= 8) WiFi.softAP(AP_SSID, AP_PASSWORD);
    else                          WiFi.softAP(AP_SSID);
    Serial.print(F("\r\nAP started. SSID: "));
    Serial.print(F(AP_SSID));
    Serial.print(F("  URL: http://"));
    Serial.println(WiFi.softAPIP());
}

static void handleRoot(void)
{
    server.send_P(200, "text/html", PAGE_INDEX);
}

// Run a CLI command string through exec() with output captured, return as text.
static void handleCmd(void)
{
    String c = server.arg("c");
    static char line[BUF_LENGTH];
    c.toCharArray(line, BUF_LENGTH);   // truncates safely at BUF_LENGTH-1
    StringPrint sink;
    out = &sink;                       // capture this command's output
    exec(line);                        // reuse the exact same command logic
    out = &Serial;                     // restore (single-threaded: safe)
    server.send(200, F("text/plain"), sink.buf);
}

// Return current radio/mode state as JSON for the polling UI.
static void handleStatus(void)
{
    String j = "{";
    j += "\"mode\":\"";     j += modeName();                        j += "\",";
    j += "\"freq\":";       j += String(currentFreq, 2);            j += ",";
    j += "\"frames\":";     j += framesinbigrecordingbuffer;        j += ",";
    j += "\"bufferPos\":";  j += bigrecordingbufferpos;             j += ",";
    j += "\"scanFreq\":";   j += String(scanBestFreq, 2);           j += ",";
    j += "\"scanRssi\":";   j += scanBestRssi;                      j += ",";
    j += "\"uptime\":";     j += (millis() / 1000);
    j += "}";
    server.send(200, F("application/json"), j);
}

static void setupWebServer(void)
{
    server.on("/", handleRoot);
    server.on("/cmd", HTTP_POST, handleCmd);
    server.on("/status", handleStatus);
    server.begin();
}
