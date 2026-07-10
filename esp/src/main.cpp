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
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <DNSServer.h> // ESP32 Arduino core — captive-portal DNS in SoftAP mode (Esp §6)
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>

extern "C" {
#include "census_schema.h"
#include "esp_capture.h"
#include "census_taxonomy.h"
#include "esp_catalog.h"
#include "esp_cc1101_regs.h"
#include "esp_census_log.h"
#include "esp_fieldmap.h"
#include "esp_fingerprints.h"
#include "esp_mqtt.h"
#include "esp_occupancy_csv.h"
#include "esp_place.h"
#include "esp_rotation.h"
#include "esp_settings.h"
#include "esp_storage.h"
#include "esp_tx.h"
#include "sc_crc.h"
#include "sc_feature.h"
#include "sc_knn.h"
#include "sc_occupancy.h"
#include "sc_slice.h"
#include "sc_sub.h"
}

static constexpr int PIN_SCK = 18, PIN_MISO = 19, PIN_MOSI = 23, PIN_CS = 5;
static constexpr int PIN_GDO0 = 34;
static constexpr int PIN_SD_CS = 13;                 // optional microSD on VSPI (Esp §2, §4)
static constexpr int MAX_CAPTURES = 200;             // capped/rotating on LittleFS (Esp §4)

// Data filesystem tier (Esp §4): LittleFS by default, SD if a card is detected at boot.
// Settings + web assets always live on internal LittleFS; per-place DATA lives on g_dfs.
static fs::FS* g_dfs = &LittleFS;
static bool g_sd_present = false;
static const char* g_base = "";
static const char* SETTINGS_PATH = "/settings.txt";

static EspSettings g_settings;
static AsyncWebServer g_server(80);
static AsyncWebSocket g_ws("/ws");
static SPIClass g_vspi(VSPI);
static bool g_cc1101_present = false;
static uint8_t g_cc1101_version = 0;
static volatile bool g_camp_running = false;
static volatile int32_t g_camp_freq = 0; // Camp target (Esp §5, System §9); 0 = unset
// Per-device running cadence + derived catalog record (Esp §3, System §7a). Compact RAM
// estimators keyed by waveform signature — the always-on ESP is a strong cadence measurer.
// Heap-allocated (the ~11 KB table lives in DRAM heap, not the static .bss segment).
static EspCatalog* g_catalog = nullptr;
// Confidence at/above which an identified device gets its own HA discovery entity (Esp §6).
static constexpr float MQTT_IDENTIFY_CONF = 0.5f;
static WiFiClient g_wifi_client;
static PubSubClient g_mqtt(g_wifi_client);
static const char* MQTT_BASE = "subcensusesp";

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

// --- CC1101 preset config + carrier tuning (Esp §2/§3). The register tables + tuning math are
// hardware-independent (esp_cc1101_regs, mirroring the stock Flipper presets); the SPI writes
// below run on-device but only a real radio proves reception — on-device RX validation is
// TODO(hw). ---

static void cc1101_write_reg(uint8_t addr, uint8_t val) {
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer(addr & 0x3F); // single write (no burst/read bits)
    g_vspi.transfer(val);
    digitalWrite(PIN_CS, HIGH);
    g_vspi.endTransaction();
}

static void cc1101_write_burst(uint8_t addr, const uint8_t* data, size_t n) {
    g_vspi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    g_vspi.transfer((addr & 0x3F) | 0x40); // burst write
    for(size_t i = 0; i < n; i++) g_vspi.transfer(data[i]);
    digitalWrite(PIN_CS, HIGH);
    g_vspi.endTransaction();
}

// Map the capture preset (settings) to the CC1101 register preset (Esp §3). Dual falls back to
// the wideband OOK650 preset for the survey pass.
static EspCc1101Preset cc1101_preset_for(uint8_t capture_preset) {
    switch(capture_preset) {
    case EspCaptureOok650: return ESP_CC1101_PRESET_OOK650;
    case EspCaptureOok270: return ESP_CC1101_PRESET_OOK270;
    case EspCaptureFsk: return ESP_CC1101_PRESET_2FSK;
    default: return ESP_CC1101_PRESET_OOK650;
    }
}

// Push a preset's register table + PATABLE into the radio (Esp §3). Safe (config only, never TX).
static void cc1101_configure(uint8_t capture_preset) {
    EspCc1101Preset p = cc1101_preset_for(capture_preset);
    size_t nregs = 0;
    const EspCc1101Reg* regs = esp_cc1101_preset_regs(p, &nregs);
    if(!g_cc1101_present || !regs) return; // no radio -> tables still exist, writes are a no-op
    for(size_t i = 0; i < nregs; i++) cc1101_write_reg(regs[i].addr, regs[i].value);
    cc1101_write_burst(0x3E, esp_cc1101_preset_patable(p), ESP_CC1101_PATABLE_LEN); // 0x3E PATABLE
}

// Tune the carrier via the FREQ2/1/0 registers (esp_cc1101_freq_regs). TODO(hw): a real strobe
// (SIDLE->SRX) and antenna prove the tune received anything.
static void cc1101_tune(int32_t freq_hz) {
    uint8_t f2, f1, f0;
    esp_cc1101_freq_regs(freq_hz, &f2, &f1, &f0);
    if(!g_cc1101_present) return;
    cc1101_write_reg(CC1101_FREQ2, f2);
    cc1101_write_reg(CC1101_FREQ1, f1);
    cc1101_write_reg(CC1101_FREQ0, f0);
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
    esp_place_file(g_base, g_settings.place_id, file, out, cap);
}

static void append_line(const char* path, const char* line) {
    File f = g_dfs->open(path, "a");
    if(!f) return;
    f.println(line);
    f.close();
}

static void place_meta_path(const char* id, char* out, size_t cap);
static void place_meta_write(const char* id, const char* name);
static void mqtt_publish_capture(int32_t freq, float rssi, const char* match);
static void mqtt_publish_device_discovery(int32_t freq, const char* cls, const char* name, float rssi);
static void write_catalog();

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
    esp_signatures_dir(g_base, sig, sizeof(sig));
    g_dfs->mkdir("/places");
    g_dfs->mkdir(sig);
    char dir[96];
    esp_place_dir(g_base, g_settings.place_id, dir, sizeof(dir));
    g_dfs->mkdir(dir);
    char cap[128];
    place_file("captures", cap, sizeof(cap));
    g_dfs->mkdir(cap);
    char log[128];
    place_file("census_log.csv", log, sizeof(log));
    if(!g_dfs->exists(log)) append_line(log, CENSUS_LOG_HEADER);
    char meta[96];
    place_meta_path(g_settings.place_id, meta, sizeof(meta));
    if(!g_dfs->exists(meta)) place_meta_write(g_settings.place_id, "Home");
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
    s += "\"mqtt_enabled\":" + String(g_settings.mqtt_enabled ? "true" : "false") + ",";
    // WiFi/MQTT config surfaced for the Settings UI (Esp §5); the password is write-only (never
    // echoed back).
    s += "\"wifi_ssid\":\"" + String(g_settings.wifi_ssid) + "\",";
    s += "\"mqtt_host\":\"" + String(g_settings.mqtt_host) + "\",";
    s += "\"mqtt_port\":" + String(g_settings.mqtt_port);
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
    File f = g_dfs->open(p, "w");
    if(f) {
        f.print(name);
        f.close();
    }
}

static String place_name(const char* id) {
    char p[96];
    place_meta_path(id, p, sizeof(p));
    if(g_dfs->exists(p)) {
        File f = g_dfs->open(p, "r");
        String n = f.readStringUntil('\n');
        f.close();
        n.trim();
        if(n.length()) return n;
    }
    return String(id);
}

static void place_scaffold(const char* id) {
    char dir[96];
    esp_place_dir(g_base, id, dir, sizeof(dir));
    g_dfs->mkdir(dir);
    char cap[128];
    esp_place_file(g_base, id, "captures", cap, sizeof(cap));
    g_dfs->mkdir(cap);
    char log[128];
    esp_place_file(g_base, id, "census_log.csv", log, sizeof(log));
    if(!g_dfs->exists(log)) append_line(log, CENSUS_LOG_HEADER);
}

static String places_json() {
    String s = "{\"active\":\"" + String(g_settings.place_id) + "\",\"places\":[";
    File dir = g_dfs->open("/places");
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
    if(!esp_storage_rotation_enabled(g_sd_present)) return; // SD: bounded by card size (Esp §4)
    char cdir[128];
    place_file("captures", cdir, sizeof(cdir));
    // collect names (bounded)
    String names[MAX_CAPTURES + 8];
    int count = 0;
    File dir = g_dfs->open(cdir);
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
        g_dfs->remove(path);
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
    esp_signatures_dir(g_base, sig, sizeof(sig));
    snprintf(path, sizeof(path), "%s/fingerprints.csv", sig);
    if(!g_dfs->exists(path)) return;
    File f = g_dfs->open(path, "r");
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
// The running per-device cadence estimate (System §7a) is passed in as a SOFT booster/penalty
// (never a gate) — the always-on ESP actually has a cadence to offer (unlike a single capture).
static bool classify(
    const ScFeatureVector* fv, ScCadenceClass cadence_class, float period_s, const char** name,
    const char** cls, float* conf) {
    if(g_fp_count == 0 || !g_settings.match_db) return false;
    ScKnnQuery q;
    memset(&q, 0, sizeof(q));
    q.fv = *fv;
    q.cadence_class = cadence_class;
    q.period_s = period_s;
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
            File f = g_dfs->open(sub_abs, "w");
            if(f) {
                f.write((const uint8_t*)subbuf, sublen);
                f.close();
            }
            rotate_captures();
        }
    }

    // Update this device's running cadence estimator (System §7a) BEFORE classifying, so the
    // cadence can feed the k-NN query as a soft booster. Keyed by waveform signature; the derived
    // catalog record (with cadence_*) is flushed below.
    ScCadenceEstimate cad;
    memset(&cad, 0, sizeof(cad));
    cad.cls = SC_CADENCE_NONE;
    int cat_slot = g_catalog ? esp_catalog_observe(g_catalog, &fv, freq_hz,
                                                    (int64_t)time(nullptr), ts, &cad)
                             : -1;

    // classify via gated k-NN against the brain (System §6) — advisory, never auto-relabels
    const char* mname = "";
    const char* mclass = "";
    float mconf = 0.0f;
    bool matched = classify(&fv, cad.cls, cad.period_s, &mname, &mclass, &mconf);
    if(g_catalog)
        esp_catalog_set_match(g_catalog, cat_slot, matched ? mname : "", matched ? mclass : "",
                              matched ? mconf : 0.0f, matched ? "fingerprint" : "");

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
    mqtt_publish_capture(freq_hz, rssi, matched ? mname : "");
    // On a confident classification, surface this device as its own HA entity (Esp §6).
    if(matched && mconf >= MQTT_IDENTIFY_CONF)
        mqtt_publish_device_discovery(freq_hz, mclass, mname, rssi);
    // Flush the derived catalog record(s) — the running cadence estimate lands here (System §9).
    write_catalog();
    Serial.printf("SC scene=camp action=capture freq=%ld rssi=%.1f cadence=%s source=%s\n",
                  (long)freq_hz, (double)rssi, sc_cadence_str(cad.cls), source);
}

// Flush the in-RAM per-device catalog (System §9) to catalog.csv. The catalog is an AGGREGATE
// keyed by signature, so it's rewritten from the RAM source of truth rather than appended.
static void write_catalog() {
    if(!g_catalog) return;
    char path[128];
    place_file("catalog.csv", path, sizeof(path));
    File f = g_dfs->open(path, "w");
    if(!f) return;
    f.println(CATALOG_RECORD_HEADER);
    char row[288];
    for(int i = 0; i < ESP_CATALOG_MAX; i++) {
        if(esp_catalog_row(g_catalog, i, row, sizeof(row)) > 0) f.println(row);
    }
    f.close();
}

// --- Recon: occupancy.csv + watchlist.csv (Esp §3, System §9) ---

// Collect existing user-pin/user-exclude rows (raw lines) so Recon preserves them across re-runs
// and Reset (System §9). Excluded frequencies are also gathered into excl[] so the re-derived
// watchlist drops them.
static int collect_user_watchlist(
    String* lines, int cap, int32_t* excl, int excl_cap, int* n_excl) {
    *n_excl = 0;
    int n = 0;
    char wl_path[128];
    place_file("watchlist.csv", wl_path, sizeof(wl_path));
    if(!g_dfs->exists(wl_path)) return 0;
    File f = g_dfs->open(wl_path, "r");
    if(!f) return 0;
    bool header = true;
    while(f.available() && n < cap) {
        String line = f.readStringUntil('\n');
        if(header) {
            header = false;
            continue;
        }
        line.trim();
        if(line.length() < 5) continue;
        char src[16];
        if(!esp_watchlist_parse_source(line.c_str(), src, sizeof(src))) continue;
        if(strcmp(src, "user-pin") == 0 || strcmp(src, "user-exclude") == 0) {
            lines[n++] = line;
            if(strcmp(src, "user-exclude") == 0 && *n_excl < excl_cap) {
                ScWatchlistEntry e;
                if(esp_watchlist_parse_line(line.c_str(), &e)) excl[(*n_excl)++] = e.freq_hz;
            }
        }
    }
    f.close();
    return n;
}

// Write occupancy.csv from `bins` and re-derive watchlist.csv. When keep_pins, user-pin/
// user-exclude rows are preserved (and excluded freqs dropped from the recon rows) — System §9.
static void write_occupancy_and_watchlist(const ScOccupancyBin* bins, size_t n, bool keep_pins) {
    static String user_lines[32];
    static int32_t excl[16];
    int n_excl = 0;
    int n_user = keep_pins ? collect_user_watchlist(user_lines, 32, excl, 16, &n_excl) : 0;

    char occ_path[128], wl_path[128];
    place_file("occupancy.csv", occ_path, sizeof(occ_path));
    place_file("watchlist.csv", wl_path, sizeof(wl_path));

    File occ = g_dfs->open(occ_path, "w");
    if(occ) {
        occ.println(OCCUPANCY_HEADER);
        char row[128], ts[24];
        iso_now(ts, sizeof(ts));
        for(size_t i = 0; i < n; i++) {
            if(esp_occupancy_row(&bins[i], ts, row, sizeof(row)) > 0) occ.println(row);
        }
        occ.close();
    }

    // derive watchlist from occupancy (shared logic, System §9); drop excluded freqs, then append
    // the preserved user pins/exclusions verbatim.
    static ScWatchlistEntry wl[64];
    size_t nwl = sc_watchlist_from_occupancy(bins, n, 0.10f, 12.0f, wl, 64);
    File w = g_dfs->open(wl_path, "w");
    if(w) {
        w.println(WATCHLIST_HEADER);
        char row[128];
        for(size_t i = 0; i < nwl; i++) {
            bool excluded = false;
            for(int j = 0; j < n_excl; j++)
                if(wl[i].freq_hz == excl[j]) excluded = true;
            if(excluded) continue;
            if(esp_watchlist_row(&wl[i], "recon", row, sizeof(row)) > 0) w.println(row);
        }
        for(int i = 0; i < n_user; i++) w.println(user_lines[i]);
        w.close();
    }
}

// Read occupancy.csv back into bins[] for the cumulative-Recon accumulate merge (System §9).
static size_t read_occupancy(ScOccupancyBin* out, size_t cap) {
    char p[128];
    place_file("occupancy.csv", p, sizeof(p));
    if(!g_dfs->exists(p)) return 0;
    File f = g_dfs->open(p, "r");
    if(!f) return 0;
    size_t n = 0;
    bool header = true;
    while(f.available() && n < cap) {
        String line = f.readStringUntil('\n');
        if(header) {
            header = false;
            continue;
        }
        line.trim();
        if(line.length() < 5) continue;
        if(esp_occupancy_parse_line(line.c_str(), &out[n])) n++;
    }
    f.close();
    return n;
}

// Reset (System §9): wipe occupancy; keep_pins decides whether user pins/exclusions survive.
static void recon_reset(bool keep_pins) {
    write_occupancy_and_watchlist(nullptr, 0, keep_pins);
}

// Fixture recon: synthesize a few bins through the shared occupancy accumulators so the
// occupancy -> watchlist derivation is exercised on-device with NO live RF (Esp §3.4). When
// `accumulate` (the default, System §9), merge into the prior pass's occupancy (sc_occupancy_merge)
// rather than replacing it; Fresh clears first. The real stepped-RSSI sweep is TODO(hw).
static void recon_fixture(bool accumulate) {
    struct {
        int32_t freq;
        float base;
        int hot_of_10;
    } spec[] = {{433920000, -97, 10}, {315000000, -98, 5}, {915000000, -99, 8}, {390000000, -96, 0}};
    static ScOccupancyBin bins[16];
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
    if(accumulate) {
        static ScOccupancyBin prev[16];
        size_t pn = read_occupancy(prev, 16);
        for(size_t i = 0; i < pn && n < 16; i++) {
            int hit = -1;
            for(size_t j = 0; j < n; j++)
                if(bins[j].freq_hz == prev[i].freq_hz) hit = (int)j;
            if(hit >= 0)
                sc_occupancy_merge(&bins[hit], 1, &prev[i], 1); // pass 1,1 CSV accumulate
            else
                bins[n++] = prev[i];
        }
    }
    write_occupancy_and_watchlist(bins, n, true);
    Serial.printf("SC scene=recon action=fixture mode=%s bins=%u\n",
                  accumulate ? "accumulate" : "fresh", (unsigned)n);
}

// Pin / exclude / remove a frequency in watchlist.csv (source=user-pin|user-exclude); these
// survive re-runs and Reset (System §9). source=NULL removes any user row for `freq`. Drops any
// prior user row for the same freq first (so pin<->exclude toggles cleanly).
static void set_watchlist_pin(int32_t freq, const char* source) {
    char wl_path[128];
    place_file("watchlist.csv", wl_path, sizeof(wl_path));
    static String lines[96];
    int n = 0;
    if(g_dfs->exists(wl_path)) {
        File f = g_dfs->open(wl_path, "r");
        bool header = true;
        while(f.available() && n < 96) {
            String l = f.readStringUntil('\n');
            if(header) {
                header = false;
                continue;
            }
            l.trim();
            if(l.length() >= 5) lines[n++] = l;
        }
        f.close();
    }
    File w = g_dfs->open(wl_path, "w");
    if(!w) return;
    w.println(WATCHLIST_HEADER);
    for(int i = 0; i < n; i++) {
        ScWatchlistEntry e;
        char src[16];
        if(esp_watchlist_parse_line(lines[i].c_str(), &e) &&
           esp_watchlist_parse_source(lines[i].c_str(), src, sizeof(src))) {
            bool is_user = strcmp(src, "user-pin") == 0 || strcmp(src, "user-exclude") == 0;
            if(is_user && e.freq_hz == freq) continue; // drop the old user row for this freq
        }
        w.println(lines[i]);
    }
    if(source) {
        ScWatchlistEntry e = {freq, SC_MOD_OOK, 0.0f, 0.0f};
        char row[128];
        if(esp_watchlist_row(&e, source, row, sizeof(row)) > 0) w.println(row);
    }
    w.close();
}

// Sweep/Camp consume the watchlist (System §9). Load its frequencies into freqs[].
static size_t load_watchlist_freqs(int32_t* freqs, size_t cap) {
    char wl_path[128];
    place_file("watchlist.csv", wl_path, sizeof(wl_path));
    if(!g_dfs->exists(wl_path)) return 0;
    File f = g_dfs->open(wl_path, "r");
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

// Auto-pick the busiest watchlist frequency (highest occupancy) for Camp when none is given
// (System §9 Auto=busiest). Returns 0 if there's no watchlist (Camp then needs an explicit freq).
static int32_t pick_busiest_watchlist() {
    char wl_path[128];
    place_file("watchlist.csv", wl_path, sizeof(wl_path));
    if(!g_dfs->exists(wl_path)) return 0;
    File f = g_dfs->open(wl_path, "r");
    if(!f) return 0;
    int32_t best = 0;
    float best_occ = -1.0f;
    bool header = true;
    while(f.available()) {
        String line = f.readStringUntil('\n');
        if(header) {
            header = false;
            continue;
        }
        ScWatchlistEntry e;
        if(esp_watchlist_parse_line(line.c_str(), &e) && e.occupancy > best_occ) {
            best_occ = e.occupancy;
            best = e.freq_hz;
        }
    }
    f.close();
    return best;
}

// --- Camp mode (Esp §3) — capture pinned to its own core; live RMT/RSSI is TODO(hw) ---

static void camp_task(void* arg) {
    (void)arg;
    cc1101_tune(g_camp_freq); // tune the CC1101 to the Camp target (SPI runs; RX is TODO(hw))
    while(g_camp_running) {
        // TODO(hw): read CC1101 RSSI at g_camp_freq; on >= threshold, RMT-capture GDO0 edges into
        // ScRmtItem[], convert via esp_capture_rmt_to_timings(), then process_capture(). Needs a
        // real radio.
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
        // no watchlist -> fall back to the preset list (US ISM + security bands), never blocked
        // (System §9). 319.5 = GE/Interlogix/2GIG alarm sensors; 345 = Honeywell 5800-series.
        static int32_t preset[] = {315000000, 319500000, 345000000, 390000000,
                                   433920000, 915000000};
        size_t np = sizeof(preset) / sizeof(preset[0]);
        for(size_t i = 0; i < np; i++) freqs[i] = preset[i];
        nf = np;
    }
    size_t idx = 0;
    while(g_camp_running) { // reuse the running flag; one monitor at a time
        int32_t freq = freqs[idx % nf];
        idx++;
        cc1101_tune(freq); // tune to the band (SPI runs on-device)
        // TODO(hw): dwell g_settings.dwell_ms sampling CC1101 RSSI; on >= threshold RMT-capture
        // and process_capture(). Needs a real radio.
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

// Captive portal (Esp §6 WiFi provisioning): when no credentials are committed we fall back to an
// open SoftAP so the browser Settings form can take WiFi/MQTT config. To make a phone auto-pop that
// form on join, run a wildcard DNS server (resolves every lookup to our AP IP) and 302 the OS
// connectivity-probe URLs to the config page. Only active in AP mode — never when joined to a real
// network. The DNS + redirect wiring is hardware-independent (exercised here); a phone actually
// popping the portal is TODO(hw).
static DNSServer g_dns;
static volatile bool g_ap_mode = false;

// 302 a request to the node's config page (the single-page UI that hosts the Settings form).
static void captive_redirect(AsyncWebServerRequest* req) {
    req->redirect("http://" + WiFi.softAPIP().toString() + "/");
}

// Register the OS captive-portal probe URLs (Android /generate_204+/gen_204, Apple
// /hotspot-detect.html, Windows /ncsi.txt+/connecttest.txt) plus a catch-all onNotFound. In AP mode
// each 302s to the config page so the portal pops; in STA mode the node isn't the gateway so these
// aren't probed — the guard just returns an empty 204 rather than redirecting.
static void captive_register(AsyncWebServer& s) {
    auto probe = [](AsyncWebServerRequest* req) {
        if(g_ap_mode)
            captive_redirect(req);
        else
            req->send(204);
    };
    const char* probes[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                            "/ncsi.txt", "/connecttest.txt"};
    for(auto p : probes) s.on(p, HTTP_GET, probe);
    s.onNotFound([](AsyncWebServerRequest* req) {
        if(g_ap_mode)
            captive_redirect(req); // unknown path in AP mode -> config page (catch-all portal)
        else
            req->send(404, "application/json", "{\"error\":\"not found\"}");
    });
}

static void wifi_start() {
    if(strlen(g_settings.wifi_ssid) == 0) {
        g_ap_mode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SubCensusEsp-setup"); // open AP; Settings form takes WiFi/MQTT config (Esp §6)
        // Wildcard DNS: resolve EVERY lookup to our AP IP so the phone's probe hits us and the
        // captive-portal handlers (captive_register) redirect it to the config page.
        g_dns.setErrorReplyCode(DNSReplyCode::NoError);
        g_dns.start(53, "*", WiFi.softAPIP());
        Serial.printf("SC wifi mode=ap ssid=SubCensusEsp-setup ip=%s captive=on\n",
                      WiFi.softAPIP().toString().c_str());
        return;
    }
    g_ap_mode = false;
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifi_ssid, g_settings.wifi_pass);
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// --- MQTT -> Home Assistant (Esp §6). Live broker connectivity is TODO(hw) ---

static bool g_mqtt_discovered = false;

static void mqtt_publish_discovery() {
    char topic[128], payload[512];
    esp_mqtt_discovery_topic(MQTT_BASE, "node", "rssi", topic, sizeof(topic));
    esp_mqtt_discovery_payload(MQTT_BASE, "node", "SubCensusEsp node", "rssi", "Last RSSI",
                               "{{ value_json.rssi }}", "dBm", "signal_strength", payload, sizeof(payload));
    g_mqtt.publish(topic, payload, true); // retained
    g_mqtt_discovered = true;
}

static void mqtt_ensure() {
    if(!g_settings.mqtt_enabled || WiFi.status() != WL_CONNECTED) return;
    if(g_mqtt.connected()) return;
    // honor the configured broker port (Esp §6 Settings); default 1883 when unset
    g_mqtt.setServer(g_settings.mqtt_host, g_settings.mqtt_port ? g_settings.mqtt_port : 1883);
    if(g_mqtt.connect("subcensusesp")) { // TODO(hw): needs a reachable broker
        if(g_settings.mqtt_enabled) mqtt_publish_discovery();
    }
}

// Per-identified-device HA discovery (Esp §6): on a confident classification, publish a retained
// discovery config so the device surfaces as its own HA entity (unique_id per device/class),
// grouped under its own HA device — not just the one node-level rssi sensor. Deduped so each
// device is announced once; state then flows to the device's state topic per capture.
#define MQTT_MAX_DEVICES 16
static char g_disc_ids[MQTT_MAX_DEVICES][32];
static int g_disc_n = 0;

static bool mqtt_device_known(const char* id) {
    for(int i = 0; i < g_disc_n; i++)
        if(strcmp(g_disc_ids[i], id) == 0) return true;
    return false;
}

static void mqtt_publish_device_discovery(int32_t freq, const char* cls, const char* name, float rssi) {
    if(!g_settings.mqtt_enabled || !g_mqtt.connected()) return;
    char dev[32];
    if(esp_mqtt_device_id(cls && cls[0] ? cls : "unknown", freq, dev, sizeof(dev)) < 0) return;
    if(!mqtt_device_known(dev)) {
        char topic[160], payload[512];
        // one rssi sensor per identified device (esp_mqtt payload builder is generic + tested)
        esp_mqtt_discovery_topic(MQTT_BASE, dev, "rssi", topic, sizeof(topic));
        esp_mqtt_discovery_payload(MQTT_BASE, dev, name && name[0] ? name : cls, "rssi", "RSSI",
                                   "{{ value_json.rssi }}", "dBm", "signal_strength", payload,
                                   sizeof(payload));
        g_mqtt.publish(topic, payload, true); // retained
        if(g_disc_n < MQTT_MAX_DEVICES) snprintf(g_disc_ids[g_disc_n++], 32, "%s", dev);
    }
    // per-device state (populates the HA entity)
    char stopic[128], smsg[160];
    esp_mqtt_state_topic(MQTT_BASE, dev, stopic, sizeof(stopic));
    snprintf(smsg, sizeof(smsg), "{\"rssi\":%.1f,\"freq_hz\":%ld,\"name\":\"%s\"}", (double)rssi,
             (long)freq, name ? name : "");
    g_mqtt.publish(stopic, smsg);
}

static void mqtt_publish_capture(int32_t freq, float rssi, const char* match) {
    if(!g_settings.mqtt_enabled || !g_mqtt.connected()) return;
    if(!g_mqtt_discovered) mqtt_publish_discovery();
    char topic[96], msg[192];
    esp_mqtt_state_topic(MQTT_BASE, "node", topic, sizeof(topic));
    snprintf(msg, sizeof(msg), "{\"rssi\":%.1f,\"freq_hz\":%ld,\"match\":\"%s\"}",
             (double)rssi, (long)freq, match ? match : "");
    g_mqtt.publish(topic, msg);
}

// --- brain sync over WiFi (Esp §6): pull/push signatures/ to participate in the shared brain.
// Host-side build_signatures.py remains the merge point (System §8). Live HTTP is TODO(hw). ---

static bool brain_pull(const char* base_url) {
    if(WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    bool ok = true;
    const char* files[] = {"protocol_map.csv", "fingerprints.csv"};
    for(auto fn : files) {
        char url[160], path[96], sig[64];
        snprintf(url, sizeof(url), "%s/%s", base_url, fn);
        http.begin(g_wifi_client, url);
        int code = http.GET();
        if(code == 200) {
            esp_signatures_dir(g_base, sig, sizeof(sig));
            snprintf(path, sizeof(path), "%s/%s", sig, fn);
            File f = g_dfs->open(path, "w");
            if(f) {
                http.writeToStream(&f);
                f.close();
            }
        } else {
            ok = false;
        }
        http.end();
    }
    load_fingerprints();
    return ok;
}

// Push this node's user-confirmed fingerprints back to the shared brain (Esp §6): HTTP PUT the
// local signatures/fingerprints.csv so it participates without an SD card. build_signatures.py
// remains the merge point (System §8) — the host dedups/merges. Live HTTP is TODO(hw).
static bool brain_push(const char* base_url) {
    if(WiFi.status() != WL_CONNECTED) return false;
    char sig[64], path[96];
    esp_signatures_dir(g_base, sig, sizeof(sig));
    snprintf(path, sizeof(path), "%s/fingerprints.csv", sig);
    if(!g_dfs->exists(path)) return false; // nothing to contribute yet
    File f = g_dfs->open(path, "r");
    if(!f) return false;
    String body = f.readString();
    f.close();
    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url), "%s/fingerprints.csv", base_url);
    http.begin(g_wifi_client, url);
    http.addHeader("Content-Type", "text/csv");
    int code = http.PUT((uint8_t*)body.c_str(), body.length());
    http.end();
    Serial.printf("SC action=brain_push code=%d bytes=%u\n", code, (unsigned)body.length());
    return code >= 200 && code < 300;
}

// --- OTA (Esp §6) ---

static void ota_setup() {
    ArduinoOTA.setHostname("subcensusesp");
    ArduinoOTA.begin(); // TODO(hw): exercised once flashed + on WiFi
}

// --- replay / edit-before-transmit (Esp §5, System §7b) — the ONLY TX path ---
// Passive-while-scanning is unchanged; TX is opt-in, explicit, single-frame, allow-list gated.
// Live CC1101 transmit is TODO(hw). The passive field-map DIFFERENTIAL OVERLAY + checksum
// discovery run ON-DEVICE now (esp_fieldmap, reusing shared/core sc_diff + sc_crc); the node
// does the segment labeling + confirmation (writes a proposed field_maps/ entry, never
// auto-committed) and active own-device confirmation via this guarded TX path.

static bool cc1101_tx_sub(const int32_t* timings, size_t n, int32_t freq, const char* preset) {
    (void)timings;
    (void)n;
    // TODO(hw): tune CC1101 to freq+preset, then furi-style async TX of the RAW timing frame
    // ONCE (single frame — no auto-increment / sweeping). Needs the radio.
    Serial.printf("SC action=tx freq=%ld preset=%s frames=1 (TODO hw)\n", (long)freq, preset);
    return g_cc1101_present; // pretends success only if a radio is actually present
}

// Parse a .sub (stored capture or an edited frame), guard, and transmit once.
static void tx_from_sub(AsyncWebServerRequest* req, const String& sub_text, const char* action) {
    ScSubMeta meta;
    static int32_t timings[1024];
    size_t tn = 0;
    if(sc_sub_parse(sub_text.c_str(), sub_text.length(), &meta, timings, 1024, &tn) == SC_ERR ||
       tn == 0) {
        req->send(400, "application/json", "{\"error\":\"bad .sub\"}");
        return;
    }
    int32_t freq = meta.frequency ? meta.frequency : 433920000;
    if(!esp_tx_allowed(freq, g_settings.tx_enabled)) {
        req->send(403, "application/json",
                  "{\"error\":\"TX not allowed\",\"note\":\"enable TX in settings; freq must be "
                  "in a CC1101 legal segment\"}");
        return;
    }
    bool ok = cc1101_tx_sub(timings, tn, freq, meta.preset[0] ? meta.preset : "OOK650");
    // Edited/replayed TX is logged DISTINCTLY (Serial), never into census_log — an edited TX is
    // not a census observation (Esp §5), so the catalog isn't polluted.
    Serial.printf("SC action=%s freq=%ld frames=1 ok=%d\n", action, (long)freq, ok);
    char resp[96];
    snprintf(resp, sizeof(resp), "{\"ok\":%s,\"freq_hz\":%ld}", ok ? "true" : "false", (long)freq);
    req->send(ok ? 200 : 502, "application/json", resp);
}

// --- field-map discovery / edit (Esp §5, System §7b) — passive differential overlay + segment
// labeling + confirm, ON-DEVICE via esp_fieldmap (shared/core sc_diff + sc_crc). No TX here;
// active confirmation of an edited (re-signed) frame rides the guarded /api/edit_tx path. ---

static const char* FIELDMAPS_DIR_LEAF = "field_maps";

static void field_maps_dir(char* out, size_t cap) {
    char sig[64];
    esp_signatures_dir(g_base, sig, sizeof(sig));
    snprintf(out, cap, "%s/%s", sig, FIELDMAPS_DIR_LEAF);
}

// Slice a stored .sub capture into an MSB-first bit frame (sc_slice) using its own dominant
// symbol unit (sc_feature sym_dur_us[0]). Returns the bit count, 0 on failure. This is the
// on-device path that turns real captures into the aligned byte frames the differential overlay
// operates on — a crude line-code-agnostic slice, not a protocol decode (Zero §6 scope).
static size_t sub_to_bits(const char* rel, uint8_t* out, size_t cap_bytes, int32_t* unit_out) {
    char abs[192];
    place_file(rel, abs, sizeof(abs));
    if(!g_dfs->exists(abs)) return 0;
    File f = g_dfs->open(abs, "r");
    if(!f) return 0;
    String text = f.readString();
    f.close();
    ScSubMeta meta;
    static int32_t timings[1024];
    size_t tn = 0;
    if(sc_sub_parse(text.c_str(), text.length(), &meta, timings, 1024, &tn) == SC_ERR || tn == 0)
        return 0;
    ScFeatureVector fv;
    sc_feature_compute(timings, tn, meta.frequency, preset_modulation(g_settings.capture_preset), &fv);
    int32_t unit = fv.sym_dur_us[0] > 0 ? fv.sym_dur_us[0] : 100;
    if(unit_out) *unit_out = unit;
    return sc_slice_bits(timings, tn, unit, out, cap_bytes);
}

// Build an aligned byte corpus from a comma/newline-separated list of place-relative .sub paths.
// Frames are truncated to the shortest common byte length (captures of one device align). Returns
// the frame count and sets *nbytes; 0 if fewer than 2 usable frames.
static size_t build_corpus_from_subs(const char* list, uint8_t* corpus, size_t* nbytes) {
    size_t nf = 0;
    size_t minbytes = ESP_FIELDMAP_MAX_BYTES;
    String s(list);
    int start = 0;
    while(start < (int)s.length() && nf < ESP_FIELDMAP_MAX_FRAMES) {
        int comma = s.indexOf(',', start);
        int nl = s.indexOf('\n', start);
        int end = comma < 0 ? nl : (nl < 0 ? comma : (comma < nl ? comma : nl));
        if(end < 0) end = s.length();
        String path = s.substring(start, end);
        path.trim();
        start = end + 1;
        if(!path.length()) continue;
        uint8_t bits[ESP_FIELDMAP_MAX_BYTES];
        size_t nbits = sub_to_bits(path.c_str(), bits, ESP_FIELDMAP_MAX_BYTES, nullptr);
        size_t nb = nbits / 8; // whole bytes only, for byte-granular segmentation
        if(nb < 1) continue;
        if(nb > ESP_FIELDMAP_MAX_BYTES) nb = ESP_FIELDMAP_MAX_BYTES;
        if(nb < minbytes) minbytes = nb;
        memcpy(corpus + nf * ESP_FIELDMAP_MAX_BYTES, bits, nb);
        nf++;
    }
    if(nf < 2) return 0;
    *nbytes = minbytes;
    return nf;
}

// Analyze a posted corpus into a proposal (JSON for the browser). The corpus is either `subs` (a
// list of place-relative .sub capture paths, sliced on-device via sc_slice) or `frames` (aligned
// hex, one per line). Optional `signature`, and per-field user labels
// `nameN`/`semN`/`clsN` (the segment-labeling step). When `write` is true the (user-confirmed)
// structure is persisted as a signatures/field_maps/<slug>.fmap entry via sc_fieldmap_emit — the
// field_maps/ entry (System §7b). Passive — NEVER transmits.
static void handle_fieldmap(AsyncWebServerRequest* req, bool write) {
    static uint8_t corpus[ESP_FIELDMAP_MAX_FRAMES * ESP_FIELDMAP_MAX_BYTES];
    size_t nbytes = 0;
    size_t nf = 0;
    if(req->hasParam("subs", true)) {
        // corpus from real captures — sliced on-device (sc_slice), no pasted hex needed
        nf = build_corpus_from_subs(req->getParam("subs", true)->value().c_str(), corpus, &nbytes);
    } else if(req->hasParam("frames", true)) {
        nf = esp_fieldmap_parse_hex(req->getParam("frames", true)->value().c_str(), corpus,
                                    ESP_FIELDMAP_MAX_FRAMES, &nbytes);
    } else {
        req->send(400, "application/json",
                  "{\"error\":\"subs (.sub paths) or frames (hex, one per line) required\"}");
        return;
    }
    if(nf < 2) {
        req->send(422, "application/json",
                  "{\"error\":\"need >=2 aligned (equal-length) frames for differential\"}");
        return;
    }
    String signature = req->hasParam("signature", true) ? req->getParam("signature", true)->value()
                                                        : String("unknown");
    uint8_t modulation = preset_modulation(g_settings.capture_preset) == SC_MOD_2FSK ? 1 : 0;

    static ScFieldMap map; // shared/core structure (identical to the Zero's proposal)
    float conf = 0.0f;
    if(!esp_fieldmap_analyze(corpus, nf, nbytes, signature.c_str(), modulation, &map, &conf)) {
        req->send(422, "application/json", "{\"error\":\"not analyzable\"}");
        return;
    }

    // apply per-field user labels (name/semantics/class overrides) — the labeling step (§7b)
    for(size_t i = 0; i < map.n_fields; i++) {
        char k[8];
        snprintf(k, sizeof(k), "name%u", (unsigned)i);
        if(req->hasParam(k, true))
            strncpy(map.fields[i].name, req->getParam(k, true)->value().c_str(),
                    sizeof(map.fields[i].name) - 1);
        snprintf(k, sizeof(k), "sem%u", (unsigned)i);
        if(req->hasParam(k, true))
            strncpy(map.fields[i].semantics, req->getParam(k, true)->value().c_str(),
                    sizeof(map.fields[i].semantics) - 1);
        snprintf(k, sizeof(k), "cls%u", (unsigned)i);
        if(req->hasParam(k, true))
            map.fields[i].cls = sc_field_class_from_str(req->getParam(k, true)->value().c_str());
    }

    if(write) {
        // Persist the user-confirmed field_maps/ entry. NEVER auto-committed: reaching here needs
        // an explicit user confirm; the brain never writes this on its own (System §7b). Only a
        // confirmed map carries source=user.
        map.user_confirmed = true;
        char dir[96];
        field_maps_dir(dir, sizeof(dir));
        g_dfs->mkdir(dir);
        char slug[ESP_PLACE_ID_LEN];
        esp_place_id_from_name(signature.c_str(), slug, sizeof(slug));
        char path[160];
        snprintf(path, sizeof(path), "%s/%s.fmap", dir, slug);
        static char fmap[2048];
        size_t fn = sc_fieldmap_emit(&map, fmap, sizeof(fmap));
        File f = g_dfs->open(path, "w");
        if(f) {
            f.write((const uint8_t*)fmap, fn);
            f.close();
        }
        Serial.printf("SC action=fieldmap_confirm sig=%s frames=%u -> %s (user-confirmed)\n",
                      signature.c_str(), (unsigned)nf, path);
    }

    static char json[4096];
    int n = esp_fieldmap_to_json(&map, conf, json, sizeof(json));
    if(n < 0) {
        req->send(500, "application/json", "{\"error\":\"proposal too large\"}");
        return;
    }
    req->send(200, "application/json", json);
}

// Re-sign an edited byte frame: recompute its trailing check byte for the named checksum family
// (shared sc_checksum_compute) so an edit stays valid before an active-confirmation TX (System
// §7b). Returns the re-signed hex. The actual transmit-to-own-device is the guarded single-frame
// /api/edit_tx path (byte-frame -> RAW timing re-encode for a live send is protocol-specific and
// TODO(hw)).
static void handle_fieldmap_resign(AsyncWebServerRequest* req) {
    if(!req->hasParam("frame", true) || !req->hasParam("kind", true)) {
        req->send(400, "application/json", "{\"error\":\"frame (hex) + kind required\"}");
        return;
    }
    String frame_hex = req->getParam("frame", true)->value();
    static uint8_t corpus[ESP_FIELDMAP_MAX_FRAMES * ESP_FIELDMAP_MAX_BYTES];
    size_t nbytes = 0;
    if(esp_fieldmap_parse_hex(frame_hex.c_str(), corpus, 1, &nbytes) != 1 || nbytes < 2) {
        req->send(422, "application/json", "{\"error\":\"one hex frame of >=2 bytes required\"}");
        return;
    }
    String kind = req->getParam("kind", true)->value();
    ScChecksumSpec spec;
    memset(&spec, 0, sizeof(spec));
    if(kind == "xor") spec.kind = SC_CK_XOR;
    else if(kind == "sum") spec.kind = SC_CK_SUM;
    else if(kind == "crc8") spec.kind = SC_CK_CRC8;
    else if(kind == "crc8le") spec.kind = SC_CK_CRC8LE;
    else if(kind == "lfsr8") spec.kind = SC_CK_LFSR8;
    else {
        req->send(400, "application/json", "{\"error\":\"unknown checksum kind\"}");
        return;
    }
    auto u8p = [&](const char* k) -> uint8_t {
        return req->hasParam(k, true) ? (uint8_t)req->getParam(k, true)->value().toInt() : 0;
    };
    spec.poly = u8p("poly");
    spec.init = u8p("init");
    spec.gen = u8p("gen");
    spec.key = u8p("key");
    size_t over = req->hasParam("over_bytes", true)
                      ? (size_t)req->getParam("over_bytes", true)->value().toInt()
                      : nbytes - 1;
    if(over >= nbytes) over = nbytes - 1;
    uint8_t ck = sc_checksum_compute(&spec, corpus, over);
    corpus[over] = ck; // re-sign in place (check byte immediately after the covered span)
    String hex;
    char b[4];
    for(size_t i = 0; i < nbytes; i++) {
        snprintf(b, sizeof(b), "%02X", corpus[i]);
        hex += b;
    }
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"frame\":\"%s\",\"check\":%d}", hex.c_str(), ck);
    req->send(200, "application/json", resp);
}

// Active own-device confirmation: encode an edited hex byte frame back to RAW timings (sc_slice,
// the exact inverse of the slice) and transmit ONCE through the guarded single-frame path. This
// is the "transmit an edited frame to your own device and watch it react" loop (System §7b).
// Same guards as replay/edit-TX: opt-in, TX-allow-list, single-frame; the live CC1101 send is
// TODO(hw). POST /api/fieldmap/tx  frame=<hex>&unit_us=&freq=&preset=
static void handle_fieldmap_tx(AsyncWebServerRequest* req) {
    if(!req->hasParam("frame", true)) {
        req->send(400, "application/json", "{\"error\":\"frame (hex) required\"}");
        return;
    }
    static uint8_t bytes[ESP_FIELDMAP_MAX_BYTES];
    size_t nbytes = 0;
    if(esp_fieldmap_parse_hex(req->getParam("frame", true)->value().c_str(), bytes, 1, &nbytes) != 1) {
        req->send(422, "application/json", "{\"error\":\"one hex frame required\"}");
        return;
    }
    int32_t unit = req->hasParam("unit_us", true)
                       ? (int32_t)req->getParam("unit_us", true)->value().toInt()
                       : 0;
    if(unit <= 0) unit = 350; // default OOK short-symbol unit (µs) when unspecified
    static int32_t timings[1024];
    size_t tn = sc_slice_encode(bytes, nbytes * 8, unit, timings, 1024);
    if(tn == 0) {
        req->send(422, "application/json", "{\"error\":\"encode produced no timings\"}");
        return;
    }
    int32_t freq = req->hasParam("freq", true) ? (int32_t)req->getParam("freq", true)->value().toInt()
                                               : 433920000;
    String preset = req->hasParam("preset", true) ? req->getParam("preset", true)->value()
                                                   : String("OOK650");
    // wrap into a .sub and reuse the guarded single-frame TX path (guard + distinct logging)
    ScSubMeta meta = {freq, "", ""};
    snprintf(meta.preset, sizeof(meta.preset), "%s", preset.c_str());
    static char subbuf[8192];
    size_t sublen = 0;
    if(sc_sub_encode(&meta, timings, tn, subbuf, sizeof(subbuf), 512, &sublen) != SC_OK) {
        req->send(500, "application/json", "{\"error\":\"sub encode failed\"}");
        return;
    }
    tx_from_sub(req, String(subbuf), "fieldmap_tx");
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
        if(req->hasParam("mqtt_port", true))
            g_settings.mqtt_port = (uint16_t)req->getParam("mqtt_port", true)->value().toInt();
        save_settings();
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/places", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", places_json());
    });
    // pull the global signatures/ brain from a host over WiFi (Esp §6). build_signatures.py
    // remains the merge point (System §8). POST /api/brain/sync  url=http://host/signatures
    // Sync the global signatures/ brain with a host over WiFi (Esp §6): PULL the shared brain and
    // PUSH this node's user-confirmed fingerprints back so it participates without an SD card.
    // build_signatures.py remains the merge point (System §8). Live HTTP is TODO(hw).
    // POST /api/brain/sync  url=http://host/signatures  [dir=pull|push|sync (default sync)]
    g_server.on("/api/brain/sync", HTTP_POST, [](AsyncWebServerRequest* req) {
        String url = req->hasParam("url", true) ? req->getParam("url", true)->value() : "";
        if(!url.length()) {
            req->send(400, "application/json", "{\"error\":\"url required\"}");
            return;
        }
        String dir = req->hasParam("dir", true) ? req->getParam("dir", true)->value() : "sync";
        bool pulled = true, pushed = true;
        if(dir == "pull" || dir == "sync") pulled = brain_pull(url.c_str());
        if(dir == "push" || dir == "sync") pushed = brain_push(url.c_str());
        bool ok = pulled && pushed;
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"ok\":%s,\"pulled\":%s,\"pushed\":%s,\"note\":\"needs a reachable host\"}",
                 ok ? "true" : "false", pulled ? "true" : "false", pushed ? "true" : "false");
        req->send(ok ? 200 : 502, "application/json", resp);
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
            esp_place_dir(g_base, pid.c_str(), dir, sizeof(dir));
            g_dfs->rmdir(dir); // best-effort; children removed by the fs layer / TODO(hw) recursive
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
        if(g_dfs->exists(log)) {
            req->send(*g_dfs, log, "text/csv");
        } else {
            req->send(200, "text/csv", CENSUS_LOG_HEADER "\n");
        }
    });
    g_server.on("/api/occupancy", HTTP_GET, [](AsyncWebServerRequest* req) {
        char p[128];
        place_file("occupancy.csv", p, sizeof(p));
        if(g_dfs->exists(p))
            req->send(*g_dfs, p, "text/csv");
        else
            req->send(200, "text/csv", OCCUPANCY_HEADER "\n");
    });
    // Derived catalog record with the per-device running cadence (System §7a/§9). GET /api/catalog
    g_server.on("/api/catalog", HTTP_GET, [](AsyncWebServerRequest* req) {
        char p[128];
        place_file("catalog.csv", p, sizeof(p));
        if(g_dfs->exists(p))
            req->send(*g_dfs, p, "text/csv");
        else
            req->send(200, "text/csv", CATALOG_RECORD_HEADER "\n");
    });
    g_server.on("/api/watchlist", HTTP_GET, [](AsyncWebServerRequest* req) {
        char p[128];
        place_file("watchlist.csv", p, sizeof(p));
        if(g_dfs->exists(p))
            req->send(*g_dfs, p, "text/csv");
        else
            req->send(200, "text/csv", WATCHLIST_HEADER "\n");
    });
    g_server.on("/api/recon", HTTP_POST, [](AsyncWebServerRequest* req) {
        String action = req->hasParam("action", true) ? req->getParam("action", true)->value() : "";
        String mode = req->hasParam("mode", true) ? req->getParam("mode", true)->value() : "";
        if(action == "reset") {
            // keep user pins unless explicitly wiping (System §9 reset prompts keep-or-wipe)
            recon_reset(mode != "wipe");
        } else if(action == "fixture" || action == "run") {
            // accumulate (default) merges into the prior pass; fresh clears first (System §9)
            recon_fixture(mode != "fresh"); // §3.4 no-RF path exercising occupancy -> watchlist
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
    // Camp on an explicit frequency (Esp §5 "camp here"); auto-picks the busiest watchlist entry
    // when none is given (System §9). POST /api/camp  start=0|1[&freq=<hz>]
    g_server.on("/api/camp", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool start = req->hasParam("start", true) ? req->getParam("start", true)->value() != "0" : true;
        if(start) {
            int32_t freq = req->hasParam("freq", true)
                               ? (int32_t)req->getParam("freq", true)->value().toInt()
                               : pick_busiest_watchlist();
            g_camp_freq = freq; // 0 => no watchlist yet; Camp still runs, tune is a no-op
            camp_start();
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"camp\":true,\"freq_hz\":%ld}", (long)freq);
            req->send(200, "application/json", resp);
        } else {
            camp_stop();
            req->send(200, "application/json", "{\"camp\":false}");
        }
    });
    // Per-entry watchlist pin / exclude / clear (Esp §5 Bands). These user rows survive Recon
    // re-runs and Reset (System §9). POST /api/watchlist  freq=<hz>&action=pin|exclude|clear
    g_server.on("/api/watchlist", HTTP_POST, [](AsyncWebServerRequest* req) {
        if(!req->hasParam("freq", true) || !req->hasParam("action", true)) {
            req->send(400, "application/json", "{\"error\":\"freq + action required\"}");
            return;
        }
        int32_t freq = (int32_t)req->getParam("freq", true)->value().toInt();
        String action = req->getParam("action", true)->value();
        const char* source = nullptr;
        if(action == "pin")
            source = "user-pin";
        else if(action == "exclude")
            source = "user-exclude";
        else if(action != "clear") {
            req->send(400, "application/json", "{\"error\":\"action must be pin|exclude|clear\"}");
            return;
        }
        set_watchlist_pin(freq, source); // source=NULL for clear (removes the user row)
        req->send(200, "application/json", "{\"ok\":true}");
    });
    // Expose the label taxonomy (System §5) so the Review tab can offer a class dropdown instead
    // of a free-text prompt. GET /api/taxonomy
    g_server.on("/api/taxonomy", HTTP_GET, [](AsyncWebServerRequest* req) {
        String out = "{\"classes\":[";
        for(int i = 0; i < CENSUS_CLASS_COUNT; i++) {
            if(i) out += ",";
            out += "{\"id\":\"" + String(census_class_id((CensusDeviceClass)i)) + "\",\"name\":\"" +
                   String(census_class_name((CensusDeviceClass)i)) + "\"}";
        }
        out += "]}";
        req->send(200, "application/json", out);
    });
    // Replay a stored capture on its own freq/preset (identify-your-own-device). Guarded.
    // POST /api/replay  sub=<place-relative path>
    g_server.on("/api/replay", HTTP_POST, [](AsyncWebServerRequest* req) {
        if(!req->hasParam("sub", true)) {
            req->send(400, "application/json", "{\"error\":\"sub required\"}");
            return;
        }
        char abs[192];
        place_file(req->getParam("sub", true)->value().c_str(), abs, sizeof(abs));
        if(!g_dfs->exists(abs)) {
            req->send(404, "application/json", "{\"error\":\"capture not found\"}");
            return;
        }
        File f = g_dfs->open(abs, "r");
        String text = f.readString();
        f.close();
        tx_from_sub(req, text, "replay");
    });
    // Edit-before-transmit: the body is an edited .sub frame (raw/structured edit done in the
    // browser; checksum recompute uses the shared CRC family). Guarded, single-frame.
    g_server.on(
        "/api/edit_tx", HTTP_POST, [](AsyncWebServerRequest* req) { (void)req; }, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            (void)index;
            (void)total;
            String body((const char*)data, len);
            tx_from_sub(req, body, "edit_tx");
        });
    // Field-map differential overlay (System §7b tier 1+2): POST an aligned hex-frame corpus,
    // get back the passive per-byte segmentation (static/slow/counter/checksum) + named checksum
    // as a PROPOSED field_maps/ entry. Passive — no TX. POST /api/fieldmap  frames=<hex lines>
    g_server.on("/api/fieldmap", HTTP_POST,
                [](AsyncWebServerRequest* req) { handle_fieldmap(req, false); });
    // Confirm the labeled structure -> write signatures/field_maps/<slug>.json (user-confirmed;
    // never auto-committed, System §7b). POST /api/fieldmap/confirm  frames, signature, nameN, semN
    g_server.on("/api/fieldmap/confirm", HTTP_POST,
                [](AsyncWebServerRequest* req) { handle_fieldmap(req, true); });
    // Re-sign an edited byte frame for active own-device confirmation (recompute the check byte
    // via the shared CRC family). POST /api/fieldmap/resign  frame=<hex>&kind=&poly=&init=&over_bytes=
    g_server.on("/api/fieldmap/resign", HTTP_POST,
                [](AsyncWebServerRequest* req) { handle_fieldmap_resign(req); });
    // Active own-device confirmation: re-encode an edited frame to timings (sc_slice) + transmit
    // ONE frame, guarded. POST /api/fieldmap/tx  frame=<hex>&unit_us=&freq=&preset=
    g_server.on("/api/fieldmap/tx", HTTP_POST,
                [](AsyncWebServerRequest* req) { handle_fieldmap_tx(req); });
    // List proposed field_maps/ entries. GET /api/fieldmaps
    g_server.on("/api/fieldmaps", HTTP_GET, [](AsyncWebServerRequest* req) {
        char dir[96];
        field_maps_dir(dir, sizeof(dir));
        String out = "{\"field_maps\":[";
        File d = g_dfs->open(dir);
        bool first = true;
        if(d) {
            for(File e = d.openNextFile(); e; e = d.openNextFile()) {
                if(!e.isDirectory()) {
                    String nm = String(e.name());
                    int slash = nm.lastIndexOf('/');
                    if(slash >= 0) nm = nm.substring(slash + 1);
                    if(!first) out += ",";
                    first = false;
                    out += "\"" + nm + "\"";
                }
                e.close();
            }
            d.close();
        }
        out += "]}";
        req->send(200, "application/json", out);
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
        if(!g_dfs->exists(abs)) {
            req->send(404, "application/json", "{\"error\":\"capture not found\"}");
            return;
        }
        File f = g_dfs->open(abs, "r");
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
        // reuse the device's running cadence (System §7a) as a soft booster, if we've seen it
        ScCadenceEstimate ce;
        q.cadence_class =
            (g_catalog && esp_catalog_peek(g_catalog, &fv, &ce)) ? ce.cls : SC_CADENCE_NONE;
        q.period_s = (q.cadence_class != SC_CADENCE_NONE) ? ce.period_s : 0.0f;
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
        if(!g_dfs->exists(abs)) {
            req->send(404, "application/json", "{\"error\":\"capture not found\"}");
            return;
        }
        File f = g_dfs->open(abs, "r");
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
            esp_signatures_dir(g_base, sig, sizeof(sig));
            snprintf(path, sizeof(path), "%s/fingerprints.csv", sig);
            if(!g_dfs->exists(path)) append_line(path, FINGERPRINTS_HEADER);
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
    // Captive-portal probe URLs + catch-all (Esp §6) — active only in SoftAP provisioning mode.
    // Registered after the API routes so specific handlers win; onNotFound is the fallback.
    captive_register(g_server);
    g_server.begin();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("SC boot node=subcensusesp");

    if(!LittleFS.begin(true)) Serial.println("SC error fs=littlefs_mount_failed");

    // Storage tier (Esp §4): use microSD (full per-place model, no rotation) if present on VSPI,
    // else internal LittleFS (capped/rotating). Settings + web assets stay on LittleFS.
    g_sd_present = SD.begin(PIN_SD_CS, g_vspi);
    g_dfs = g_sd_present ? (fs::FS*)&SD : (fs::FS*)&LittleFS;
    g_base = esp_storage_base(g_sd_present);
    Serial.printf("SC storage tier=%s base=%s\n", g_sd_present ? "sd" : "littlefs", g_base);
    load_settings();
    ensure_default_place();
    save_settings();

    cc1101_init();
    cc1101_configure(g_settings.capture_preset); // push the CC1101 preset regs (Esp §3; RX TODO(hw))
    Serial.printf("SC cc1101 present=%d version=0x%02x\n", g_cc1101_present, g_cc1101_version);

    // per-device running cadence estimators (System §7a) — heap-allocated (~11 KB, off .bss)
    g_catalog = (EspCatalog*)calloc(1, sizeof(EspCatalog));
    if(g_catalog) esp_catalog_init(g_catalog, 1.0f);
    load_fingerprints(); // classification brain (System §6)
    wifi_start();
    web_start();
    ota_setup();

    uint8_t probe[] = {0xDE, 0xAD};
    Serial.printf("SC core_ok crc=%u place=%s\n",
                  sc_crc8(probe, sizeof(probe), 0x07, 0x00), g_settings.place_id);
}

void loop() {
    if(g_ap_mode) g_dns.processNextRequest(); // service captive-portal DNS while in SoftAP mode
    ArduinoOTA.handle();
    mqtt_ensure();
    g_mqtt.loop();
    g_ws.cleanupClients();
    delay(500);
}
