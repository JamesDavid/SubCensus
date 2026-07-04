/* main.cpp — SubCensusEsp firmware (Esp §8). M1 skeleton: WiFi + NTP + async web server
 * (static page + /api/status JSON) + CC1101 init over VSPI + settings (LittleFS) + place model.
 *
 * Headless node — the UI is served over WiFi (Esp §5). Capture (RMT/CC1101) lands in M2; live
 * WiFi/NTP/CC1101 behaviour is on-device (TODO(hw)). The hardware-independent logic
 * (esp_settings, esp_place) is unit-tested off-device (esp/test/).
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

extern "C" {
#include "esp_place.h"
#include "esp_settings.h"
#include "sc_crc.h"
}

// CC1101 on the VSPI hardware bus (Esp §2).
static constexpr int PIN_SCK = 18, PIN_MISO = 19, PIN_MOSI = 23, PIN_CS = 5;
static constexpr int PIN_GDO0 = 34; // input-only -> RMT edge capture (M2)

static const char* FS_BASE = ""; // LittleFS mounted at "/" -> paths like /places/<id>/...
static const char* SETTINGS_PATH = "/settings.txt";

static EspSettings g_settings;
static AsyncWebServer g_server(80);
static SPIClass g_vspi(VSPI);
static bool g_cc1101_present = false;
static uint8_t g_cc1101_version = 0;

// --- CC1101 thin VSPI probe (full RMT capture driver arrives in M2) ---

static uint8_t cc1101_read_status(uint8_t addr) {
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer(addr | 0xC0); // status-register read (burst+read bits)
    uint8_t v = g_vspi.transfer(0x00);
    digitalWrite(PIN_CS, HIGH);
    g_vspi.endTransaction();
    return v;
}

static bool cc1101_init() {
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    pinMode(PIN_GDO0, INPUT);
    g_vspi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
    // strobe SRES (reset)
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer(0x30);
    digitalWrite(PIN_CS, HIGH);
    g_vspi.endTransaction();
    delay(1);
    g_cc1101_version = cc1101_read_status(0x31); // VERSION register
    // a present CC1101 reports a plausible version (not 0x00 / 0xFF float)
    g_cc1101_present = (g_cc1101_version != 0x00 && g_cc1101_version != 0xFF);
    return g_cc1101_present;
}

// --- storage: settings + default place (Esp §4) ---

static void load_settings() {
    esp_settings_defaults(&g_settings);
    if(LittleFS.exists(SETTINGS_PATH)) {
        File f = LittleFS.open(SETTINGS_PATH, "r");
        String text = f.readString();
        f.close();
        esp_settings_deserialize(text.c_str(), &g_settings);
    }
}

static void save_settings() {
    char buf[1024];
    if(esp_settings_serialize(&g_settings, buf, sizeof(buf)) < 0) return;
    File f = LittleFS.open(SETTINGS_PATH, "w");
    f.print(buf);
    f.close();
}

static void ensure_default_place() {
    char sig[64];
    esp_signatures_dir(FS_BASE, sig, sizeof(sig));
    LittleFS.mkdir("/places");
    LittleFS.mkdir(sig);
    char dir[96];
    esp_place_dir(FS_BASE, g_settings.place_id, dir, sizeof(dir));
    if(!LittleFS.exists(dir)) {
        LittleFS.mkdir(dir);
        // per-place CSVs with the shared-schema headers land with the capture engine (M2)
    }
}

// --- networking (Esp §6) — live behaviour is TODO(hw) ---

static void wifi_start() {
    if(strlen(g_settings.wifi_ssid) == 0) {
        // TODO(hw): config portal / SoftAP fallback on first boot (Esp §6)
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SubCensusEsp-setup");
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifi_ssid, g_settings.wifi_pass);
    // NTP for real wall-clock timestamps on captures/log (Esp §6)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// --- web (Esp §5) — static status page + /api/status JSON ---

static const char INDEX_HTML[] PROGMEM =
    "<!doctype html><meta charset=utf-8><title>SubCensusEsp</title>"
    "<style>body{font-family:system-ui;background:#0f1115;color:#e6e6e6;margin:2rem}"
    "code{color:#9fd0ff}</style>"
    "<h1>SubCensusEsp</h1><p>Headless CC1101 census node. Review/label/config here.</p>"
    "<pre id=s>loading...</pre>"
    "<script>fetch('/api/status').then(r=>r.json()).then(j=>"
    "document.getElementById('s').textContent=JSON.stringify(j,null,2))</script>";

static String status_json() {
    String ip = WiFi.localIP().toString();
    String s = "{";
    s += "\"node\":\"subcensusesp\",\"version\":\"0.1\",";
    s += "\"place\":\"" + String(g_settings.place_id) + "\",";
    s += "\"mode\":" + String(g_settings.mode) + ",";
    s += "\"wifi\":{\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
         ",\"ip\":\"" + ip + "\"},";
    s += "\"cc1101\":{\"present\":" + String(g_cc1101_present ? "true" : "false") +
         ",\"version\":" + String(g_cc1101_version) + "},";
    s += "\"tx_enabled\":" + String(g_settings.tx_enabled ? "true" : "false");
    s += "}";
    return s;
}

static void web_start() {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", INDEX_HTML);
    });
    g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", status_json());
    });
    g_server.begin();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("SC boot node=subcensusesp");

    if(!LittleFS.begin(true)) {
        Serial.println("SC error fs=littlefs_mount_failed");
    }
    load_settings();
    ensure_default_place();
    save_settings();

    cc1101_init();
    Serial.printf("SC cc1101 present=%d version=0x%02x\n", g_cc1101_present, g_cc1101_version);

    wifi_start();
    web_start();

    // confirm shared/core linked into the firmware
    uint8_t probe[] = {0xDE, 0xAD};
    Serial.printf("SC core_ok crc=%u place=%s\n",
                  sc_crc8(probe, sizeof(probe), 0x07, 0x00), g_settings.place_id);
}

void loop() {
    delay(1000);
}
