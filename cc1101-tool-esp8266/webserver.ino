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

static void setupWebServer(void)
{
    server.on("/", handleRoot);
    server.begin();
}
