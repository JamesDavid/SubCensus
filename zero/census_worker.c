#include "census_worker.h"

#include <furi_hal_rtc.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/protocols/base.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <stdio.h>
#include <string.h>

#include "../shared/core/sc_feature.h"
#include "../shared/core/sc_sub.h"
#include "census_schema.h"
#include "census_taxonomy.h"

#define CENSUS_TIMINGS_CAP 4096
#define CENSUS_RECENT_CAP  16
#define CENSUS_SUB_BUF     20480

struct CensusWorker {
    FuriThread* thread;
    Storage* storage;
    const SubGhzDevice* device;
    volatile bool running;
    CensusWorkerMode mode;

    /* config */
    char place_id[CENSUS_PLACE_ID_LEN];
    FuriHalSubGhzPreset preset;
    char preset_name[24];
    float threshold_dbm;
    uint32_t signal_end_gap_ms;
    uint32_t capture_max_ms;
    uint32_t min_gap_ms;
    /* sweep list / camp freq */
    uint32_t freqs[16];
    size_t freq_count;
    uint32_t dwell_ms;

    /* capture buffer (ISR producer, worker consumer) */
    int32_t timings[CENSUS_TIMINGS_CAP];
    volatile size_t timings_len;
    volatile bool overflow;

    /* auto-classify (§5.1 step 2, M5) + Dual OOK/FSK (§5.3, M7) */
    SubGhzEnvironment* env;
    SubGhzReceiver* receiver;
    bool auto_classify;
    bool dual;
    volatile bool decoded;
    char decoded_name[24];
    volatile float capture_peak;

    /* live state */
    volatile float rssi;
    volatile uint32_t current_freq;
    volatile uint32_t hits;

    CensusHit recent[CENSUS_RECENT_CAP];
    size_t recent_len;
    FuriMutex* recent_mutex;

    CensusWorkerCallback callback;
    void* callback_context;
};

/* Opportunistic known-protocol decode (§5.1 step 2, M5): fires when a decoder matches. */
static void census_rx_callback(
    SubGhzReceiver* receiver,
    SubGhzProtocolDecoderBase* decoder,
    void* context) {
    CensusWorker* w = context;
    w->decoded = true;
    const char* name = (decoder && decoder->protocol) ? decoder->protocol->name : "decoded";
    strncpy(w->decoded_name, name ? name : "decoded", sizeof(w->decoded_name) - 1);
    w->decoded_name[sizeof(w->decoded_name) - 1] = '\0';
    subghz_receiver_reset(receiver);
}

/* --- async RX capture callback (interrupt context — keep minimal) --- */
static void census_capture_cb(bool level, uint32_t duration, void* context) {
    CensusWorker* w = context;
    size_t i = w->timings_len;
    if(i < CENSUS_TIMINGS_CAP) {
        w->timings[i] = level ? (int32_t)duration : -(int32_t)duration;
        w->timings_len = i + 1;
    } else {
        w->overflow = true;
    }
    if(w->auto_classify && w->receiver) subghz_receiver_decode(w->receiver, level, duration);
}

static void census_iso_now(char* out, size_t cap) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        out,
        cap,
        "%04u-%02u-%02uT%02u:%02u:%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
}

static ScModulation census_preset_modulation(FuriHalSubGhzPreset p) {
    switch(p) {
    case FuriHalSubGhzPreset2FSKDev238Async:
    case FuriHalSubGhzPreset2FSKDev476Async:
        return SC_MOD_2FSK;
    default:
        return SC_MOD_OOK;
    }
}

/* Finalize one capture: snapshot timings, write .sub, append census_log, notify (§5.1). */
static void census_process_capture(CensusWorker* w, uint32_t freq, float rssi, float peak) {
    size_t n = w->timings_len;
    if(n < 4) { /* too few edges — discard the .sub but still note the blip (§7) */
        w->timings_len = 0;
        w->overflow = false;
        return;
    }

    /* auto-classify result + Dual OOK/FSK hint (§5.3): OOK decode failed on a strong signal */
    const char* proto = w->decoded ? w->decoded_name : "";
    int fsk_suspected =
        (!w->decoded && peak >= -75.0f && census_preset_modulation(w->preset) == SC_MOD_OOK) ? 1 :
                                                                                               0;

    char ts[32];
    census_iso_now(ts, sizeof(ts));

    ScFeatureVector fv;
    sc_feature_compute(w->timings, n, (int32_t)freq, census_preset_modulation(w->preset), &fv);

    /* write the standard .sub into the active place's captures/ */
    char sub_rel[128];
    snprintf(
        sub_rel,
        sizeof(sub_rel),
        "captures/%s_%lu_%s.sub",
        ts,
        (unsigned long)(freq / 1000),
        w->preset_name);
    char sub_abs[160];
    census_place_file(w->place_id, sub_rel, sub_abs, sizeof(sub_abs));

    ScSubMeta meta = {(int32_t)freq, "", "RAW"};
    snprintf(meta.preset, sizeof(meta.preset), "%s", w->preset_name);
    char* subbuf = malloc(CENSUS_SUB_BUF);
    size_t sublen = 0;
    if(subbuf &&
       sc_sub_encode(&meta, w->timings, n, subbuf, CENSUS_SUB_BUF, 512, &sublen) == SC_OK) {
        File* f = storage_file_alloc(w->storage);
        if(storage_file_open(f, sub_abs, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
            storage_file_write(f, subbuf, sublen);
        }
        storage_file_close(f);
        storage_file_free(f);
    }
    if(subbuf) free(subbuf);

    /* append the census_log row (order = CENSUS_LOG_HEADER; classification match_* land in M6) */
    char row[256];
    snprintf(
        row,
        sizeof(row),
        "%s,%lu,%.1f,%lu,%s,%d,%s,,,,%.2f,,%s,\n",
        ts,
        (unsigned long)freq,
        (double)peak,
        (unsigned long)0,
        w->preset_name,
        fsk_suspected,
        proto,
        (double)0.0f,
        sub_rel);
    (void)rssi;
    char log_abs[160];
    census_place_file(w->place_id, "census_log.csv", log_abs, sizeof(log_abs));
    File* lf = storage_file_alloc(w->storage);
    if(storage_file_open(lf, log_abs, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(lf, row, strlen(row));
    }
    storage_file_close(lf);
    storage_file_free(lf);

    /* live state + recent-hits ring */
    w->hits++;
    furi_mutex_acquire(w->recent_mutex, FuriWaitForever);
    for(size_t i = (w->recent_len < CENSUS_RECENT_CAP ? w->recent_len : CENSUS_RECENT_CAP - 1);
        i > 0;
        i--) {
        w->recent[i] = w->recent[i - 1];
    }
    w->recent[0].freq_hz = freq;
    w->recent[0].rssi_dbm = rssi;
    strncpy(w->recent[0].match, "unknown", sizeof(w->recent[0].match) - 1);
    w->recent[0].match[sizeof(w->recent[0].match) - 1] = '\0';
    if(w->recent_len < CENSUS_RECENT_CAP) w->recent_len++;
    furi_mutex_release(w->recent_mutex);

    w->timings_len = 0;
    w->overflow = false;

    FURI_LOG_I(
        "SubCensus",
        "SC scene=%s action=capture freq=%lu rssi=%.1f",
        w->mode == CensusWorkerModeCamp ? "camp" : "sweep",
        (unsigned long)freq,
        (double)rssi);
    if(w->callback) w->callback(w->callback_context);
}

/* Monitor one frequency for up to `window_ms` (0 = until stopped). Captures on threshold.
 * Live RSSI/capture needs real airtime (TODO(hw)); the state machine + processing are real. */
static void census_monitor_freq(CensusWorker* w, uint32_t freq, uint32_t window_ms) {
    w->current_freq = freq;
    subghz_devices_idle(w->device);
    subghz_devices_set_frequency(w->device, freq);
    subghz_devices_load_preset(w->device, w->preset, NULL);
    w->timings_len = 0;
    subghz_devices_start_async_rx(w->device, census_capture_cb, w);

    uint32_t start = furi_get_tick();
    bool capturing = false;
    uint32_t capture_start = 0;
    uint32_t quiet_since = 0;

    while(w->running) {
        float rssi = subghz_devices_get_rssi(w->device);
        w->rssi = rssi;
        uint32_t now = furi_get_tick();

        if(rssi >= w->threshold_dbm) {
            if(!capturing) {
                capturing = true;
                capture_start = now;
                w->timings_len = 0;
                w->decoded = false;
                w->capture_peak = rssi;
                if(w->receiver) subghz_receiver_reset(w->receiver);
            }
            if(rssi > w->capture_peak) w->capture_peak = rssi;
            quiet_since = 0;
        } else if(capturing) {
            if(quiet_since == 0) quiet_since = now;
            if((now - quiet_since) >= w->signal_end_gap_ms) {
                census_process_capture(w, freq, rssi, w->capture_peak);
                capturing = false;
                furi_delay_ms(w->min_gap_ms); /* repeat suppression (§7) */
            }
        }
        if(capturing && (now - capture_start) >= w->capture_max_ms) {
            census_process_capture(w, freq, rssi, w->capture_peak);
            capturing = false;
        }
        if(window_ms && (now - start) >= window_ms) break;
        furi_delay_ms(2);
    }

    subghz_devices_stop_async_rx(w->device);
    subghz_devices_idle(w->device);
}

static int32_t census_worker_thread(void* context) {
    CensusWorker* w = context;
    subghz_devices_begin(w->device);
    subghz_devices_reset(w->device);

    while(w->running) {
        if(w->mode == CensusWorkerModeCamp) {
            census_monitor_freq(w, w->freqs[0], 0);
        } else {
            for(size_t i = 0; i < w->freq_count && w->running; i++) {
                census_monitor_freq(w, w->freqs[i], w->dwell_ms);
            }
        }
    }

    subghz_devices_end(w->device);
    return 0;
}

/* --- lifecycle --- */

CensusWorker* census_worker_alloc(Storage* storage) {
    CensusWorker* w = malloc(sizeof(CensusWorker));
    memset(w, 0, sizeof(CensusWorker));
    w->storage = storage;
    w->recent_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    subghz_devices_init();
    w->device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    /* standard SubGhz decoder registry for opportunistic protocol tagging (§5.1, M5) */
    w->env = subghz_environment_alloc();
    subghz_environment_set_protocol_registry(w->env, (void*)&subghz_protocol_registry);
    w->receiver = subghz_receiver_alloc_init(w->env);
    subghz_receiver_set_rx_callback(w->receiver, census_rx_callback, w);
    w->threshold_dbm = -80;
    w->signal_end_gap_ms = 120;
    w->capture_max_ms = 1500;
    w->min_gap_ms = 500;
    w->dwell_ms = 80;
    strncpy(w->preset_name, "OOK650", sizeof(w->preset_name) - 1);
    w->preset = FuriHalSubGhzPresetOok650Async;
    return w;
}

void census_worker_free(CensusWorker* w) {
    census_worker_stop(w);
    subghz_receiver_free(w->receiver);
    subghz_environment_free(w->env);
    subghz_devices_deinit();
    furi_mutex_free(w->recent_mutex);
    free(w);
}

void census_worker_set_callback(CensusWorker* w, CensusWorkerCallback cb, void* context) {
    w->callback = cb;
    w->callback_context = context;
}

void census_worker_configure(CensusWorker* w, const CensusSettings* s, const char* place_id) {
    strncpy(w->place_id, place_id, CENSUS_PLACE_ID_LEN - 1);
    w->place_id[CENSUS_PLACE_ID_LEN - 1] = '\0';
    w->threshold_dbm = s->rssi_auto ? -80 : (float)s->rssi_threshold;
    w->signal_end_gap_ms = s->signal_end_gap_ms;
    w->capture_max_ms = s->capture_max_ms;
    w->min_gap_ms = s->min_gap_ms;
    w->dwell_ms = s->dwell_ms;
    w->auto_classify = s->auto_classify;
    w->dual = (s->capture_preset == CensusCaptureDual);
    switch(s->capture_preset) {
    case CensusCaptureOok270:
        w->preset = FuriHalSubGhzPresetOok270Async;
        strncpy(w->preset_name, "OOK270", sizeof(w->preset_name) - 1);
        break;
    case CensusCaptureFsk:
        w->preset = FuriHalSubGhzPreset2FSKDev476Async;
        strncpy(w->preset_name, "2FSK", sizeof(w->preset_name) - 1);
        break;
    default:
        w->preset = FuriHalSubGhzPresetOok650Async;
        strncpy(w->preset_name, "OOK650", sizeof(w->preset_name) - 1);
        break;
    }
}

static void census_worker_run(CensusWorker* w) {
    if(w->running) return;
    w->running = true;
    w->hits = 0;
    w->thread = furi_thread_alloc_ex("CensusWorker", 4096, census_worker_thread, w);
    furi_thread_start(w->thread);
}

void census_worker_start_camp(CensusWorker* w, uint32_t freq_hz) {
    w->mode = CensusWorkerModeCamp;
    w->freqs[0] = freq_hz;
    w->freq_count = 1;
    census_worker_run(w);
}

void census_worker_start_sweep(
    CensusWorker* w,
    const uint32_t* freqs,
    size_t count,
    uint32_t dwell_ms) {
    w->mode = CensusWorkerModeSweep;
    w->freq_count = count < 16 ? count : 16;
    for(size_t i = 0; i < w->freq_count; i++)
        w->freqs[i] = freqs[i];
    w->dwell_ms = dwell_ms;
    census_worker_run(w);
}

void census_worker_stop(CensusWorker* w) {
    if(!w->running) return;
    w->running = false;
    furi_thread_join(w->thread);
    furi_thread_free(w->thread);
    w->thread = NULL;
}

bool census_worker_is_running(CensusWorker* w) {
    return w->running;
}

float census_worker_rssi(CensusWorker* w) {
    return w->rssi;
}

uint32_t census_worker_current_freq(CensusWorker* w) {
    return w->current_freq;
}

uint32_t census_worker_hits(CensusWorker* w) {
    return w->hits;
}

size_t census_worker_recent_hits(CensusWorker* w, CensusHit* out, size_t max) {
    furi_mutex_acquire(w->recent_mutex, FuriWaitForever);
    size_t n = w->recent_len < max ? w->recent_len : max;
    for(size_t i = 0; i < n; i++)
        out[i] = w->recent[i];
    furi_mutex_release(w->recent_mutex);
    return n;
}
