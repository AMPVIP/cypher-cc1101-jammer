// Web layer for the CC1101 tool: WiFi AP + HTTP control panel.
// This file is concatenated with the main sketch by the Arduino build.

// A Print target that accumulates everything written to it into a String,
// so a command's output can be captured and returned over HTTP.
class StringPrint : public Print {
  public:
    String buf;
    size_t write(uint8_t c) override { buf += (char)c; return 1; }
    size_t write(const uint8_t *b, size_t n) override {
        for (size_t i = 0; i < n; i++) buf += (char)b[i];
        return n;
    }
};

// Temporary stub (replaced in Task 5). Remove when that lands.
float scanBestFreq = 0; int scanBestRssi = -100;

// Bring up the SoftAP with the configured static IP.
static void startAP(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apGateway, apSubnet);
    if (strlen(AP_PASSWORD) >= 8) WiFi.softAP(AP_SSID, AP_PASSWORD);
    else                          WiFi.softAP(AP_SSID);
    Serial.print(F("\r\nAP started. SSID: "));
    Serial.print(F(AP_SSID));
    Serial.print(F("  URL: http://"));
    Serial.println(WiFi.softAPIP());
}

// Placeholder page (replaced with the real UI in Task 8).
static void handleRoot(void)
{
    server.send(200, F("text/plain"), F("cc1101 web server up"));
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
