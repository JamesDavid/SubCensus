/* main.cpp — SubCensusEsp firmware (Esp §8).
 * M1: WiFi + NTP + async web + CC1101 VSPI + settings + place.
 * M2: Camp mode (RMT GDO0 edge capture -> RAW), CC1101 RSSI, census_log.csv + capped/rotating
 *     capture files + WebSocket live feed, and a fixture-injection debug endpoint so the
 *     decode -> feature -> log -> WS path is exercised with NO live RF (Esp §3.4).
 *
 * Live radio (RMT capture, CC1101 RSSI/RX) is on-device (TODO(hw)); the processing path it
 * feeds is shared with the inject endpoint and unit-tested off-device (esp/test/).
 */

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

extern "C" {
#include "census_schema.h"
#include "esp_capture.h"
#include "census_taxonomy.h"
#include "esp_census_log.h"
#include "esp_fingerprints.h"
#include "esp_occupancy_csv.h"
#include "esp_place.h"
#include "esp_rotation.h"
#include "esp_settings.h"
#include "sc_crc.h"
#include "sc_feature.h"
#include "sc_knn.h"
#include "sc_occupancy.h"
#include "sc_sub.h"
}

static constexpr int PIN_SCK = 18, PIN_MISO = 19, PIN_MOSI = 23, PIN_CS = 5;
static constexpr int PIN_GDO0 = 34;
static constexpr int MAX_CAPTURES = 200; // capped/rotating on LittleFS (Esp §4)

static const char* FS_BASE = "";
static const char* SETTINGS_PATH = "/settings.txt";

static EspSettings g_settings;
static AsyncWebServer g_server(80);
static AsyncWebSocket g_ws("/ws");
static SPIClass g_vspi(VSPI);
static bool g_cc1101_present = false;
static uint8_t g_cc1101_version = 0;
static volatile bool g_camp_running = false;

// --- CC1101 thin VSPI access (full RMT capture driver is TODO(hw)) ---

static uint8_t cc1101_read_status(uint8_t addr) {
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer(addr | 0xC0);
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
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer(0x30); // SRES
    digitalWrite(PIN_CS, HIGH);
    g_vspi.endTransaction();
    delay(1);
    g_cc1101_version = cc1101_read_status(0x31);
    g_cc1101_present = (g_cc1101_version != 0x00 && g_cc1101_version != 0xFF);
    return g_cc1101_present;
}

// CC1101 RSSI status register (0x34) -> dBm (datasheet conversion). Meaningful only on hw.
static float cc1101_rssi_dbm() {
    int raw = cc1101_read_status(0x34);
    int r = (raw >= 128) ? (raw - 256) : raw;
    return (float)(r / 2) - 74.0f; // rssi_offset ~74 dB
}

// --- time ---

static void iso_now(char* out, size_t cap) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(out, cap, "%Y-%m-%dT%H:%M:%S", &tm);
}

// --- storage ---

static void place_file(const char* file, char* out, size_t cap) {
    esp_place_file(FS_BASE, g_settings.place_id, file, out, cap);
}

static void append_line(const char* path, const char* line) {
    File f = LittleFS.open(path, "a");
    if(!f) return;
    f.println(line);
    f.close();
}

static void place_meta_path(const char* id, char* out, size_t cap);
static void place_meta_write(const char* id, const char* name);

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
    LittleFS.mkdir(dir);
    char cap[128];
    place_file("captures", cap, sizeof(cap));
    LittleFS.mkdir(cap);
    char log[128];
    place_file("census_log.csv", log, sizeof(log));
    if(!LittleFS.exists(log)) append_line(log, CENSUS_LOG_HEADER);
    char meta[96];
    place_meta_path(g_settings.place_id, meta, sizeof(meta));
    if(!LittleFS.exists(meta)) place_meta_write(g_settings.place_id, "Home");
}

static String settings_json() {
    String s = "{";
    s += "\"place_id\":\"" + String(g_settings.place_id) + "\",";
    s += "\"mode\":" + String(g_settings.mode) + ",";
    s += "\"freq_preset\":" + String(g_settings.freq_preset) + ",";
    s += "\"capture_preset\":" + String(g_settings.capture_preset) + ",";
    s += "\"use_watchlist\":" + String(g_settings.use_watchlist ? "true" : "false") + ",";
    s += "\"rssi_auto\":" + String(g_settings.rssi_auto ? "true" : "false") + ",";
    s += "\"rssi_threshold\":" + String(g_settings.rssi_threshold) + ",";
    s += "\"dwell_ms\":" + String(g_settings.dwell_ms) + ",";
    s += "\"capture_max_ms\":" + String(g_settings.capture_max_ms) + ",";
    s += "\"survey_minutes\":" + String(g_settings.survey_minutes) + ",";
    s += "\"auto_classify\":" + String(g_settings.auto_classify ? "true" : "false") + ",";
    s += "\"match_db\":" + String(g_settings.match_db ? "true" : "false") + ",";
    s += "\"tx_enabled\":" + String(g_settings.tx_enabled ? "true" : "false") + ",";
    s += "\"mqtt_enabled\":" + String(g_settings.mqtt_enabled ? "true" : "false");
    s += "}";
    return s;
}

// --- place management (Esp §5) ---

static void place_meta_path(const char* id, char* out, size_t cap) {
    snprintf(out, cap, "/places/%s/place.meta", id);
}

static void place_meta_write(const char* id, const char* name) {
    char p[96];
    place_meta_path(id, p, sizeof(p));
    File f = LittleFS.open(p, "w");
    if(f) {
        f.print(name);
        f.close();
    }
}

static String place_name(const char* id) {
    char p[96];
    place_meta_path(id, p, sizeof(p));
    if(LittleFS.exists(p)) {
        File f = LittleFS.open(p, "r");
        String n = f.readStringUntil('\n');
        f.close();
        n.trim();
        if(n.length()) return n;
    }
    return String(id);
}

static void place_scaffold(const char* id) {
    char dir[96];
    esp_place_dir(FS_BASE, id, dir, sizeof(dir));
    LittleFS.mkdir(dir);
    char cap[128];
    esp_place_file(FS_BASE, id, "captures", cap, sizeof(cap));
    LittleFS.mkdir(cap);
    char log[128];
    esp_place_file(FS_BASE, id, "census_log.csv", log, sizeof(log));
    if(!LittleFS.exists(log)) append_line(log, CENSUS_LOG_HEADER);
}

static String places_json() {
    String s = "{\"active\":\"" + String(g_settings.place_id) + "\",\"places\":[";
    File dir = LittleFS.open("/places");
    bool first = true;
    if(dir) {
        for(File e = dir.openNextFile(); e; e = dir.openNextFile()) {
            if(e.isDirectory()) {
                String id = String(e.name());
                int slash = id.lastIndexOf('/');
                if(slash >= 0) id = id.substring(slash + 1);
                if(!first) s += ",";
                first = false;
                s += "{\"id\":\"" + id + "\",\"name\":\"" + place_name(id.c_str()) +
                     "\",\"active\":" + (id == g_settings.place_id ? "true" : "false") + "}";
            }
            e.close();
        }
        dir.close();
    }
    s += "]}";
    return s;
}

// Rotate oldest capture files so the count stays under MAX_CAPTURES (Esp §4). Names are
// timestamp-prefixed, so lexical order == chronological.
static void rotate_captures() {
    char cdir[128];
    place_file("captures", cdir, sizeof(cdir));
    // collect names (bounded)
    String names[MAX_CAPTURES + 8];
    int count = 0;
    File dir = LittleFS.open(cdir);
    if(!dir) return;
    for(File e = dir.openNextFile(); e && count < MAX_CAPTURES + 8; e = dir.openNextFile()) {
        names[count++] = String(e.name());
        e.close();
    }
    dir.close();
    int evict = esp_rotation_evict_for_count(count, MAX_CAPTURES);
    for(int i = 0; i < evict; i++) {
        // find + delete the lexically-smallest (oldest) remaining name
        int mn = -1;
        for(int j = 0; j < count; j++) {
            if(names[j].length() && (mn < 0 || names[j] < names[mn])) mn = j;
        }
        if(mn < 0) break;
        char path[192];
        snprintf(path, sizeof(path), "%s/%s", cdir, names[mn].c_str());
        LittleFS.remove(path);
        names[mn] = "";
    }
}

// --- classification brain (System §6): fingerprints.csv -> gated k-NN ---

#define MAX_FPS 64
static ScFingerprint g_fps[MAX_FPS];
static char g_fp_names[MAX_FPS][32];
static size_t g_fp_count = 0;

static void load_fingerprints() {
    g_fp_count = 0;
    char sig[64], path[96];
    esp_signatures_dir(FS_BASE, sig, sizeof(sig));
    snprintf(path, sizeof(path), "%s/fingerprints.csv", sig);
    if(!LittleFS.exists(path)) return;
    File f = LittleFS.open(path, "r");
    if(!f) return;
    bool header = true;
    while(f.available() && g_fp_count < MAX_FPS) {
        String line = f.readStringUntil('\n');
        if(header) {
            header = false;
            continue;
        }
        if(line.length() < 5) continue;
        if(esp_fingerprint_parse_line(line.c_str(), &g_fps[g_fp_count],
                                      g_fp_names[g_fp_count], sizeof(g_fp_names[0]))) {
            g_fp_count++;
        }
    }
    f.close();
    Serial.printf("SC brain fingerprints=%u\n", (unsigned)g_fp_count);
}

// Returns true if a candidate was found (advisory only — never auto-relabels, System §6).
static bool classify(const ScFeatureVector* fv, const char** name, const char** cls, float* conf) {
    if(g_fp_count == 0 || !g_settings.match_db) return false;
    ScKnnQuery q;
    memset(&q, 0, sizeof(q));
    q.fv = *fv;
    q.cadence_class = SC_CADENCE_NONE; // walk-around/single capture: cadence is a soft booster
    ScKnnMatch m[3];
    size_t k = sc_knn_match(&q, g_fps, g_fp_count, m, 3);
    if(k == 0) return false;
    int idx = m[0].index;
    *name = g_fps[idx].device_name ? g_fps[idx].device_name : "";
    *cls = (g_fps[idx].device_class >= 0)
               ? census_class_id((CensusDeviceClass)g_fps[idx].device_class)
               : "";
    *conf = m[0].confidence;
    return true;
}

// --- the shared processing path: timings -> feature -> classify -> .sub + census_log + WS ---

static ScModulation preset_modulation(uint8_t preset) {
    return (preset == EspCaptureFsk) ? SC_MOD_2FSK : SC_MOD_OOK;
}

static const char* preset_name(uint8_t preset) {
    switch(preset) {
    case EspCaptureOok650: return "OOK650";
    case EspCaptureOok270: return "OOK270";
    case EspCaptureFsk: return "2FSK";
    default: return "Dual";
    }
}

static void process_capture(
    const int32_t* timings, size_t n, int32_t freq_hz, float rssi, const char* source) {
    char ts[24];
    iso_now(ts, sizeof(ts));
    const char* preset = preset_name(g_settings.capture_preset);

    ScFeatureVector fv;
    sc_feature_compute(timings, n, freq_hz, preset_modulation(g_settings.capture_preset), &fv);

    // write the .sub (standard Flipper RAW so existing tools work) + rotate
    char sub_rel[96] = "";
    if(n > 0) {
        snprintf(sub_rel, sizeof(sub_rel), "captures/%s_%ld_%s.sub", ts, (long)(freq_hz / 1000), preset);
        char sub_abs[160];
        place_file(sub_rel, sub_abs, sizeof(sub_abs));
        static char subbuf[8192];
        ScSubMeta meta = {freq_hz, "", "RAW"};
        snprintf(meta.preset, sizeof(meta.preset), "%s", preset);
        size_t sublen = 0;
        if(sc_sub_encode(&meta, timings, n, subbuf, sizeof(subbuf), 512, &sublen) == SC_OK) {
            File f = LittleFS.open(sub_abs, "w");
            if(f) {
                f.write((const uint8_t*)subbuf, sublen);
                f.close();
            }
            rotate_captures();
        }
    }

    // classify via gated k-NN against the brain (System §6) — advisory, never auto-relabels
    const char* mname = "";
    const char* mclass = "";
    float mconf = 0.0f;
    bool matched = classify(&fv, &mname, &mclass, &mconf);

    // census_log row
    EspCensusRow row = {};
    row.ts_iso = ts;
    row.freq_hz = freq_hz;
    row.rssi_dbm = rssi;
    row.duration_ms = 0;
    row.preset = preset;
    row.sub_file = sub_rel;
    row.protocol = "";
    row.key = "";
    row.match_name = matched ? mname : "";
    row.match_class = matched ? mclass : "";
    row.match_conf = matched ? mconf : 0.0f;
    row.match_source = matched ? "fingerprint" : "";
    row.label = "";
    char line[256];
    if(esp_census_log_row(&row, line, sizeof(line)) > 0) {
        char log[128];
        place_file("census_log.csv", log, sizeof(log));
        append_line(log, line);
    }

    // WebSocket live feed (Esp §5) — includes the best match + confidence
    char msg[320];
    snprintf(msg, sizeof(msg),
             "{\"ts\":\"%s\",\"freq_hz\":%ld,\"rssi\":%.1f,\"preset\":\"%s\","
             "\"n_symbols\":%ld,\"sub\":\"%s\",\"match\":\"%s\",\"match_class\":\"%s\","
             "\"conf\":%.2f,\"source\":\"%s\"}",
             ts, (long)freq_hz, (double)rssi, preset, (long)fv.n_symbols, sub_rel,
             matched ? mname : "", matched ? mclass : "", (double)(matched ? mconf : 0.0f), source);
    g_ws.textAll(msg);
    Serial.printf("SC scene=camp action=capture freq=%ld rssi=%.1f source=%s\n",
                  (long)freq_hz, (double)rssi, source);
}

// --- Recon: occupancy.csv + watchlist.csv (Esp §3, System §9) ---

static void write_occupancy_and_watchlist(const ScOccupancyBin* bins, size_t n) {
    char occ_path[128], wl_path[128];
    place_file("occupancy.csv", occ_path, sizeof(occ_path));
    place_file("watchlist.csv", wl_path, sizeof(wl_path));

    File occ = LittleFS.open(occ_path, "w");
    if(occ) {
        occ.println(OCCUPANCY_HEADER);
        char row[128], ts[24];
        iso_now(ts, sizeof(ts));
        for(size_t i = 0; i < n; i++) {
            if(esp_occupancy_row(&bins[i], ts, row, sizeof(row)) > 0) occ.println(row);
        }
        occ.close();
    }

    // derive watchlist from occupancy (shared logic, System §9)
    static ScWatchlistEntry wl[64];
    size_t nwl = sc_watchlist_from_occupancy(bins, n, 0.10f, 12.0f, wl, 64);
    File w = LittleFS.open(wl_path, "w");
    if(w) {
        w.println(WATCHLIST_HEADER);
        char row[128];
        for(size_t i = 0; i < nwl; i++) {
            if(esp_watchlist_row(&wl[i], "recon", row, sizeof(row)) > 0) w.println(row);
        }
        w.close();
    }
}

static void recon_reset() {
    write_occupancy_and_watchlist(nullptr, 0);
}

// Fixture recon: synthesize a few bins through the shared occupancy accumulators so the
// occupancy -> watchlist derivation is exercised on-device with NO live RF (Esp §3.4).
// The real stepped-RSSI sweep is TODO(hw).
static void recon_fixture() {
    struct {
        int32_t freq;
        float base;
        int hot_of_10;
    } spec[] = {{433920000, -97, 10}, {315000000, -98, 5}, {915000000, -99, 8}, {390000000, -96, 0}};
    static ScOccupancyBin bins[8];
    size_t n = 0;
    for(auto& s : spec) {
        ScOccupancyAccum a;
        sc_occupancy_accum_init(&a, s.freq);
        float threshold = s.base + 12.0f;
        for(int i = 0; i < 10; i++) {
            float rssi = (i < s.hot_of_10) ? (s.base + 40.0f) : s.base; // hot vs quiet
            sc_occupancy_accum_sample(&a, rssi, threshold, 1000 + i);
        }
        sc_occupancy_accum_finish(&a, &bins[n++]);
    }
    write_occupancy_and_watchlist(bins, n);
    Serial.printf("SC scene=recon action=fixture bins=%u\n", (unsigned)n);
}

// Sweep/Camp consume the watchlist (System §9). Load its frequencies into freqs[].
static size_t load_watchlist_freqs(int32_t* freqs, size_t cap) {
    char wl_path[128];
    place_file("watchlist.csv", wl_path, sizeof(wl_path));
    if(!LittleFS.exists(wl_path)) return 0;
    File f = LittleFS.open(wl_path, "r");
    if(!f) return 0;
    size_t n = 0;
    bool header = true;
    while(f.available() && n < cap) {
        String line = f.readStringUntil('\n');
        if(header) {
            header = false;
            continue;
        }
        ScWatchlistEntry e;
        if(esp_watchlist_parse_line(line.c_str(), &e)) freqs[n++] = e.freq_hz;
    }
    f.close();
    return n;
}

// --- Camp mode (Esp §3) — capture pinned to its own core; live RMT/RSSI is TODO(hw) ---

static void camp_task(void* arg) {
    (void)arg;
    while(g_camp_running) {
        // TODO(hw): read CC1101 RSSI; on >= threshold, RMT-capture GDO0 edges into ScRmtItem[],
        // convert via esp_capture_rmt_to_timings(), then process_capture(). Needs a real radio.
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelete(nullptr);
}

static void camp_start() {
    if(g_camp_running) return;
    g_camp_running = true;
    // pin capture to core 0, WiFi/web stays on core 1 (Esp §3 concurrency)
    xTaskCreatePinnedToCore(camp_task, "camp", 4096, nullptr, 1, nullptr, 0);
}

static void camp_stop() {
    g_camp_running = false;
}

// Sweep: cycle the watchlist (or preset list), dwell + sample RSSI, capture on threshold
// (Esp §3, inherits the Zero pipeline). Live RSSI/capture is TODO(hw).
static void sweep_task(void* arg) {
    (void)arg;
    static int32_t freqs[64];
    size_t nf = load_watchlist_freqs(freqs, 64);
    if(nf == 0) {
        // no watchlist -> fall back to the preset list (US ISM), never blocked (System §9)
        static int32_t preset[] = {315000000, 390000000, 433920000, 915000000};
        for(size_t i = 0; i < 4; i++) freqs[i] = preset[i];
        nf = 4;
    }
    size_t idx = 0;
    while(g_camp_running) { // reuse the running flag; one monitor at a time
        int32_t freq = freqs[idx % nf];
        idx++;
        // TODO(hw): tune CC1101 to `freq`, dwell g_settings.dwell_ms sampling RSSI; on
        // >= threshold RMT-capture and process_capture().
        (void)freq;
        vTaskDelay(pdMS_TO_TICKS(g_settings.dwell_ms ? g_settings.dwell_ms : 80));
    }
    vTaskDelete(nullptr);
}

static void sweep_start() {
    if(g_camp_running) return;
    g_camp_running = true;
    xTaskCreatePinnedToCore(sweep_task, "sweep", 4096, nullptr, 1, nullptr, 0);
}

// --- networking ---

static void wifi_start() {
    if(strlen(g_settings.wifi_ssid) == 0) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SubCensusEsp-setup"); // TODO(hw): config portal (Esp §6)
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifi_ssid, g_settings.wifi_pass);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// --- web ---

static const char INDEX_HTML[] PROGMEM =
    "<!doctype html><meta charset=utf-8><title>SubCensusEsp</title>"
    "<style>body{font-family:system-ui;background:#0f1115;color:#e6e6e6;margin:2rem}"
    "code{color:#9fd0ff}#feed div{border-bottom:1px solid #232833;padding:2px 0}</style>"
    "<h1>SubCensusEsp</h1><pre id=s>loading...</pre><h2>Live feed</h2><div id=feed></div>"
    "<script>fetch('/api/status').then(r=>r.json()).then(j=>"
    "document.getElementById('s').textContent=JSON.stringify(j,null,2));"
    "var ws=new WebSocket('ws://'+location.host+'/ws');ws.onmessage=e=>{var f=document"
    ".getElementById('feed');var d=document.createElement('div');d.textContent=e.data;"
    "f.insertBefore(d,f.firstChild)};</script>";

static String status_json() {
    String s = "{";
    s += "\"node\":\"subcensusesp\",\"version\":\"0.1\",";
    s += "\"place\":\"" + String(g_settings.place_id) + "\",";
    s += "\"mode\":" + String(g_settings.mode) + ",";
    s += "\"camp_running\":" + String(g_camp_running ? "true" : "false") + ",";
    s += "\"wifi\":{\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
         ",\"ip\":\"" + WiFi.localIP().toString() + "\"},";
    s += "\"cc1101\":{\"present\":" + String(g_cc1101_present ? "true" : "false") +
         ",\"version\":" + String(g_cc1101_version) + "},";
    s += "\"tx_enabled\":" + String(g_settings.tx_enabled ? "true" : "false");
    s += "}";
    return s;
}

// POST /api/debug/inject — body is a .sub (RAW timings). Runs the FULL processing path with no
// live RF (Esp §3.4 fixture injection).
static void handle_inject(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
    static char text[8192];
    size_t n = len < sizeof(text) - 1 ? len : sizeof(text) - 1;
    memcpy(text, data, n);
    text[n] = '\0';

    ScSubMeta meta;
    static int32_t timings[1024];
    size_t tn = 0;
    if(sc_sub_parse(text, n, &meta, timings, 1024, &tn) == SC_ERR || tn == 0) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"bad .sub\"}");
        return;
    }
    int32_t freq = meta.frequency ? meta.frequency : 433920000;
    process_capture(timings, tn, freq, cc1101_rssi_dbm(), "inject");
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"timings\":%u,\"freq_hz\":%ld}",
             (unsigned)tn, (long)freq);
    req->send(200, "application/json", resp);
}

static void web_start() {
    g_server.addHandler(&g_ws);
    // Full UI ships as LittleFS data/ assets (uploaded via `pio run -t uploadfs`); the inline
    // PROGMEM page is the fallback when the filesystem image hasn't been flashed.
    if(LittleFS.exists("/index.html")) {
        g_server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    } else {
        g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send_P(200, "text/html", INDEX_HTML);
        });
    }
    g_server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", settings_json());
    });
    g_server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest* req) {
        auto ui = [&](const char* k, uint32_t& v) {
            if(req->hasParam(k, true)) v = (uint32_t)req->getParam(k, true)->value().toInt();
        };
        auto u8 = [&](const char* k, uint8_t& v) {
            if(req->hasParam(k, true)) v = (uint8_t)req->getParam(k, true)->value().toInt();
        };
        auto b = [&](const char* k, bool& v) {
            if(req->hasParam(k, true)) v = req->getParam(k, true)->value() != "0";
        };
        u8("mode", g_settings.mode);
        u8("freq_preset", g_settings.freq_preset);
        u8("capture_preset", g_settings.capture_preset);
        b("use_watchlist", g_settings.use_watchlist);
        b("rssi_auto", g_settings.rssi_auto);
        if(req->hasParam("rssi_threshold", true))
            g_settings.rssi_threshold = req->getParam("rssi_threshold", true)->value().toInt();
        ui("dwell_ms", g_settings.dwell_ms);
        ui("capture_max_ms", g_settings.capture_max_ms);
        if(req->hasParam("survey_minutes", true))
            g_settings.survey_minutes = req->getParam("survey_minutes", true)->value().toInt();
        b("auto_classify", g_settings.auto_classify);
        b("match_db", g_settings.match_db);
        b("tx_enabled", g_settings.tx_enabled);
        b("mqtt_enabled", g_settings.mqtt_enabled);
        if(req->hasParam("wifi_ssid", true))
            strncpy(g_settings.wifi_ssid, req->getParam("wifi_ssid", true)->value().c_str(), ESP_STR_LEN - 1);
        if(req->hasParam("wifi_pass", true))
            strncpy(g_settings.wifi_pass, req->getParam("wifi_pass", true)->value().c_str(), ESP_STR_LEN - 1);
        if(req->hasParam("mqtt_host", true))
            strncpy(g_settings.mqtt_host, req->getParam("mqtt_host", true)->value().c_str(), ESP_STR_LEN - 1);
        save_settings();
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/places", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", places_json());
    });
    g_server.on("/api/place", HTTP_POST, [](AsyncWebServerRequest* req) {
        String action = req->hasParam("action", true) ? req->getParam("action", true)->value() : "";
        String name = req->hasParam("name", true) ? req->getParam("name", true)->value() : "";
        String pid = req->hasParam("id", true) ? req->getParam("id", true)->value() : "";
        if(action == "create" && name.length()) {
            char id[ESP_PLACE_ID_LEN];
            esp_place_id_from_name(name.c_str(), id, sizeof(id));
            place_scaffold(id);
            place_meta_write(id, name.c_str());
            strncpy(g_settings.place_id, id, ESP_PLACE_ID_LEN - 1);
            save_settings();
        } else if(action == "switch" && pid.length()) {
            strncpy(g_settings.place_id, pid.c_str(), ESP_PLACE_ID_LEN - 1);
            save_settings();
        } else if(action == "rename" && pid.length() && name.length()) {
            place_meta_write(pid.c_str(), name.c_str());
        } else if(action == "delete" && pid.length()) {
            // never delete the last place or the global signatures/ (System §5.6)
            char dir[96];
            esp_place_dir(FS_BASE, pid.c_str(), dir, sizeof(dir));
            LittleFS.rmdir(dir); // best-effort; children removed by the fs layer / TODO(hw) recursive
            if(pid == g_settings.place_id) {
                esp_settings_defaults(&g_settings); // falls back to 'home'
                ensure_default_place();
                save_settings();
            }
        } else {
            req->send(400, "application/json", "{\"error\":\"bad action\"}");
            return;
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", status_json());
    });
    g_server.on("/api/captures", HTTP_GET, [](AsyncWebServerRequest* req) {
        char log[128];
        place_file("census_log.csv", log, sizeof(log));
        if(LittleFS.exists(log)) {
            req->send(LittleFS, log, "text/csv");
        } else {
            req->send(200, "text/csv", CENSUS_LOG_HEADER "\n");
        }
    });
    g_server.on("/api/occupancy", HTTP_GET, [](AsyncWebServerRequest* req) {
        char p[128];
        place_file("occupancy.csv", p, sizeof(p));
        if(LittleFS.exists(p))
            req->send(LittleFS, p, "text/csv");
        else
            req->send(200, "text/csv", OCCUPANCY_HEADER "\n");
    });
    g_server.on("/api/watchlist", HTTP_GET, [](AsyncWebServerRequest* req) {
        char p[128];
        place_file("watchlist.csv", p, sizeof(p));
        if(LittleFS.exists(p))
            req->send(LittleFS, p, "text/csv");
        else
            req->send(200, "text/csv", WATCHLIST_HEADER "\n");
    });
    g_server.on("/api/recon", HTTP_POST, [](AsyncWebServerRequest* req) {
        String action = req->hasParam("action", true) ? req->getParam("action", true)->value() : "";
        if(action == "reset") {
            recon_reset();
        } else if(action == "fixture") {
            recon_fixture(); // §3.4 no-RF path exercising occupancy -> watchlist
        } else {
            // TODO(hw): live stepped-RSSI sweep (accumulate/fresh, System §9)
            req->send(202, "application/json", "{\"ok\":true,\"note\":\"live recon needs hardware\"}");
            return;
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/sweep", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool start = req->hasParam("start", true) ? req->getParam("start", true)->value() != "0" : true;
        if(start)
            sweep_start();
        else
            camp_stop();
        req->send(200, "application/json", start ? "{\"sweep\":true}" : "{\"sweep\":false}");
    });
    g_server.on("/api/camp", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool start = req->hasParam("start", true) ? req->getParam("start", true)->value() != "0" : true;
        if(start)
            camp_start();
        else
            camp_stop();
        req->send(200, "application/json", start ? "{\"camp\":true}" : "{\"camp\":false}");
    });
    // Review candidates for a capture: recompute its feature vector from the .sub and run
    // k-NN (System §6). GET /api/candidates?sub=<place-relative path>
    g_server.on("/api/candidates", HTTP_GET, [](AsyncWebServerRequest* req) {
        if(!req->hasParam("sub")) {
            req->send(400, "application/json", "{\"error\":\"sub param required\"}");
            return;
        }
        char abs[192];
        place_file(req->getParam("sub")->value().c_str(), abs, sizeof(abs));
        if(!LittleFS.exists(abs)) {
            req->send(404, "application/json", "{\"error\":\"capture not found\"}");
            return;
        }
        File f = LittleFS.open(abs, "r");
        String text = f.readString();
        f.close();
        ScSubMeta meta;
        static int32_t timings[1024];
        size_t tn = 0;
        sc_sub_parse(text.c_str(), text.length(), &meta, timings, 1024, &tn);
        ScFeatureVector fv;
        sc_feature_compute(timings, tn, meta.frequency, preset_modulation(g_settings.capture_preset), &fv);
        ScKnnQuery q;
        memset(&q, 0, sizeof(q));
        q.fv = fv;
        q.cadence_class = SC_CADENCE_NONE;
        ScKnnMatch m[3];
        size_t k = sc_knn_match(&q, g_fps, g_fp_count, m, 3);
        String out = "{\"candidates\":[";
        for(size_t i = 0; i < k; i++) {
            int idx = m[i].index;
            if(i) out += ",";
            out += "{\"name\":\"" + String(g_fps[idx].device_name ? g_fps[idx].device_name : "") +
                   "\",\"class\":\"" +
                   String(g_fps[idx].device_class >= 0
                              ? census_class_id((CensusDeviceClass)g_fps[idx].device_class)
                              : "") +
                   "\",\"confidence\":" + String(m[i].confidence, 3) + ",\"source\":\"fingerprint\"}";
        }
        out += "]}";
        req->send(200, "application/json", out);
    });
    // Confirm a label -> append the capture's feature vector to the brain (System §6
    // active-learning loop). POST /api/label  sub=<path>&device_class=<id>[&name=]
    g_server.on("/api/label", HTTP_POST, [](AsyncWebServerRequest* req) {
        if(!req->hasParam("sub", true) || !req->hasParam("device_class", true)) {
            req->send(400, "application/json", "{\"error\":\"sub + device_class required\"}");
            return;
        }
        String sub = req->getParam("sub", true)->value();
        String cls = req->getParam("device_class", true)->value();
        String name = req->hasParam("name", true) ? req->getParam("name", true)->value() : "";
        if(census_class_from_id(cls.c_str()) < 0 && cls.length()) {
            req->send(400, "application/json", "{\"error\":\"unknown device_class\"}");
            return;
        }
        char abs[192];
        place_file(sub.c_str(), abs, sizeof(abs));
        if(!LittleFS.exists(abs)) {
            req->send(404, "application/json", "{\"error\":\"capture not found\"}");
            return;
        }
        File f = LittleFS.open(abs, "r");
        String text = f.readString();
        f.close();
        ScSubMeta meta;
        static int32_t timings[1024];
        size_t tn = 0;
        sc_sub_parse(text.c_str(), text.length(), &meta, timings, 1024, &tn);
        ScFeatureVector fv;
        sc_feature_compute(timings, tn, meta.frequency, preset_modulation(g_settings.capture_preset), &fv);

        char id[24];
        snprintf(id, sizeof(id), "fp%08lx", (unsigned long)millis());
        char row[192];
        if(esp_fingerprint_row(id, &fv, name.c_str(), cls.c_str(), row, sizeof(row)) > 0) {
            char sig[64], path[96];
            esp_signatures_dir(FS_BASE, sig, sizeof(sig));
            snprintf(path, sizeof(path), "%s/fingerprints.csv", sig);
            if(!LittleFS.exists(path)) append_line(path, FINGERPRINTS_HEADER);
            append_line(path, row);
            load_fingerprints(); // the brain gets smarter with use
        }
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on(
        "/api/debug/inject", HTTP_POST, [](AsyncWebServerRequest* req) { (void)req; },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            (void)index;
            (void)total;
            handle_inject(req, data, len);
        });
    g_server.begin();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("SC boot node=subcensusesp");

    if(!LittleFS.begin(true)) Serial.println("SC error fs=littlefs_mount_failed");
    load_settings();
    ensure_default_place();
    save_settings();

    cc1101_init();
    Serial.printf("SC cc1101 present=%d version=0x%02x\n", g_cc1101_present, g_cc1101_version);

    load_fingerprints(); // classification brain (System §6)
    wifi_start();
    web_start();

    uint8_t probe[] = {0xDE, 0xAD};
    Serial.printf("SC core_ok crc=%u place=%s\n",
                  sc_crc8(probe, sizeof(probe), 0x07, 0x00), g_settings.place_id);
}

void loop() {
    g_ws.cleanupClients();
    delay(500);
}
