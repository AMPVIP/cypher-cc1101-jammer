// Web layer for the CC1101 tool: WiFi AP + HTTP control panel.
// This file is concatenated with the main sketch by the Arduino build.

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
