#include "../subcensuszero_i.h"

#include <stdio.h>

/* Settings scene (Zero §4) — the full settings table, persisted on exit. Place management is
 * on the main menu (§6); every other §4 knob lives here. Frequency preset Custom exposes an
 * "Edit custom list" row (OK -> the frequency-list editor, §6). */

static const char* const mode_names[] = {"Recon", "Sweep", "Camp"};
static const char* const preset_names[] = {"US ISM", "EU ISM", "Custom"};
static const char* const capture_names[] = {"OOK 650", "OOK 270", "2-FSK", "Dual"};
static const char* const grid_names[] = {"Hybrid", "Known-only", "Full-band"};
static const char* const off_on[] = {"OFF", "ON"};
static const char* const notify_names[] = {"Off", "LED", "LED+vibro"};

static const int32_t dwell_values[] = {20, 40, 60, 80, 100, 150, 200, 300, 500};
static const int32_t capmax_values[] = {200, 500, 1000, 1500, 2000, 3000, 5000};
static const int32_t thr_values[] =
    {-100, -95, -90, -85, -80, -75, -70, -65, -60, -55, -50, -45, -40};
static const int32_t step_values[] = {200000, 250000, 270000};
static const int32_t survey_values[] = {1, 5, 10, 15, 30, 45, 60, 90, 120};
static const int32_t endgap_values[] = {20, 40, 60, 80, 120, 200, 300, 500};
static const int32_t mingap_values[] = {0, 250, 500, 1000, 2000, 5000};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* index of the "Edit custom list" row (set in on_enter) so the OK/enter callback can find it */
static int g_custom_row = -1;

static uint8_t nearest_index(const int32_t* vals, size_t n, int32_t v) {
    uint8_t best = 0;
    int32_t bestd = INT32_MAX;
    for(size_t i = 0; i < n; i++) {
        int32_t d = vals[i] > v ? vals[i] - v : v - vals[i];
        if(d < bestd) {
            bestd = d;
            best = (uint8_t)i;
        }
    }
    return best;
}

static void mode_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.mode = i;
    variable_item_set_current_value_text(item, mode_names[i]);
}
static void preset_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.freq_preset = i;
    variable_item_set_current_value_text(item, preset_names[i]);
}
static void capture_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.capture_preset = i;
    variable_item_set_current_value_text(item, capture_names[i]);
}
static void grid_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.recon_grid = i;
    variable_item_set_current_value_text(item, grid_names[i]);
}
static void watchlist_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.use_watchlist = i;
    variable_item_set_current_value_text(item, off_on[i]);
}
static void classify_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.auto_classify = i;
    variable_item_set_current_value_text(item, off_on[i]);
}
static void matchdb_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.match_db = i;
    variable_item_set_current_value_text(item, off_on[i]);
}
static void notify_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.notify = i;
    variable_item_set_current_value_text(item, notify_names[i]);
}
static void threshold_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    if(i == 0) {
        app->settings.rssi_auto = true;
        variable_item_set_current_value_text(item, "Auto");
    } else {
        app->settings.rssi_auto = false;
        app->settings.rssi_threshold = thr_values[i - 1];
        snprintf(buf, sizeof(buf), "%ld dBm", (long)thr_values[i - 1]);
        variable_item_set_current_value_text(item, buf);
    }
}
static void dwell_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.dwell_ms = dwell_values[i];
    snprintf(buf, sizeof(buf), "%ld ms", (long)dwell_values[i]);
    variable_item_set_current_value_text(item, buf);
}
static void capmax_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.capture_max_ms = capmax_values[i];
    snprintf(buf, sizeof(buf), "%ld ms", (long)capmax_values[i]);
    variable_item_set_current_value_text(item, buf);
}
static void step_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.recon_step_hz = (uint32_t)step_values[i];
    snprintf(buf, sizeof(buf), "%ld kHz", (long)(step_values[i] / 1000));
    variable_item_set_current_value_text(item, buf);
}
static void survey_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.survey_minutes = (uint16_t)survey_values[i];
    snprintf(buf, sizeof(buf), "%ld min", (long)survey_values[i]);
    variable_item_set_current_value_text(item, buf);
}
static void endgap_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.signal_end_gap_ms = (uint32_t)endgap_values[i];
    snprintf(buf, sizeof(buf), "%ld ms", (long)endgap_values[i]);
    variable_item_set_current_value_text(item, buf);
}
static void mingap_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    app->settings.min_gap_ms = (uint32_t)mingap_values[i];
    snprintf(buf, sizeof(buf), "%ld ms", (long)mingap_values[i]);
    variable_item_set_current_value_text(item, buf);
}

/* Camp frequency: index 0 = Auto (busiest watchlist), 1.. = allowed presets (§3.2/§4). */
static void campfreq_cb(VariableItem* item) {
    SubCensusApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    static char buf[16];
    if(i == 0) {
        app->settings.camp_freq_hz = 0;
        variable_item_set_current_value_text(item, "Auto");
    } else {
        uint32_t f = census_freq_us[i - 1];
        app->settings.camp_freq_hz = f;
        census_freq_format_mhz(f, buf, sizeof(buf));
        variable_item_set_current_value_text(item, buf);
    }
}

/* OK on the "Edit custom list" row opens the frequency-list editor (§6). */
static void settings_enter_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    if((int)index == g_custom_row) {
        scene_manager_next_scene(app->scene_manager, SubCensusSceneFreqEdit);
    }
}

void subcensus_scene_settings_on_enter(void* context) {
    SubCensusApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    VariableItem* it;
    static char buf[16];
    int row = 0;
    g_custom_row = -1;

    it = variable_item_list_add(list, "Mode", 3, mode_cb, app);
    variable_item_set_current_value_index(it, app->settings.mode);
    variable_item_set_current_value_text(it, mode_names[app->settings.mode]);
    row++;

    it = variable_item_list_add(list, "Freq preset", 3, preset_cb, app);
    variable_item_set_current_value_index(it, app->settings.freq_preset);
    variable_item_set_current_value_text(it, preset_names[app->settings.freq_preset]);
    row++;

    /* Custom-list editor entry (OK opens it); shows the current custom count (§4 Custom) */
    it = variable_item_list_add(list, "Edit custom list", 1, NULL, app);
    snprintf(buf, sizeof(buf), "%u freqs", (unsigned)app->settings.custom_count);
    variable_item_set_current_value_text(it, buf);
    g_custom_row = row;
    row++;

    it = variable_item_list_add(list, "Use watchlist", 2, watchlist_cb, app);
    variable_item_set_current_value_index(it, app->settings.use_watchlist ? 1 : 0);
    variable_item_set_current_value_text(it, off_on[app->settings.use_watchlist ? 1 : 0]);
    row++;

    it = variable_item_list_add(
        list, "Camp freq", (uint8_t)(census_freq_us_count + 1), campfreq_cb, app);
    if(app->settings.camp_freq_hz == 0) {
        variable_item_set_current_value_index(it, 0);
        variable_item_set_current_value_text(it, "Auto");
    } else {
        uint8_t idx = 1;
        for(size_t i = 0; i < census_freq_us_count; i++)
            if(census_freq_us[i] == app->settings.camp_freq_hz) idx = (uint8_t)(i + 1);
        variable_item_set_current_value_index(it, idx);
        census_freq_format_mhz(census_freq_us[idx - 1], buf, sizeof(buf));
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Recon grid", CensusReconGridCount, grid_cb, app);
    variable_item_set_current_value_index(it, app->settings.recon_grid);
    variable_item_set_current_value_text(it, grid_names[app->settings.recon_grid]);
    row++;

    it = variable_item_list_add(list, "Recon step", ARRAY_LEN(step_values), step_cb, app);
    {
        uint8_t idx = nearest_index(
            step_values, ARRAY_LEN(step_values), (int32_t)app->settings.recon_step_hz);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld kHz", (long)(step_values[idx] / 1000));
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Survey min", ARRAY_LEN(survey_values), survey_cb, app);
    {
        uint8_t idx =
            nearest_index(survey_values, ARRAY_LEN(survey_values), app->settings.survey_minutes);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld min", (long)survey_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(
        list, "RSSI thresh", (uint8_t)(ARRAY_LEN(thr_values) + 1), threshold_cb, app);
    if(app->settings.rssi_auto) {
        variable_item_set_current_value_index(it, 0);
        variable_item_set_current_value_text(it, "Auto");
    } else {
        uint8_t idx =
            nearest_index(thr_values, ARRAY_LEN(thr_values), app->settings.rssi_threshold);
        variable_item_set_current_value_index(it, idx + 1);
        snprintf(buf, sizeof(buf), "%ld dBm", (long)thr_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Capture preset", 4, capture_cb, app);
    variable_item_set_current_value_index(it, app->settings.capture_preset);
    variable_item_set_current_value_text(it, capture_names[app->settings.capture_preset]);
    row++;

    it = variable_item_list_add(list, "Dwell", ARRAY_LEN(dwell_values), dwell_cb, app);
    {
        uint8_t idx = nearest_index(dwell_values, ARRAY_LEN(dwell_values), app->settings.dwell_ms);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld ms", (long)dwell_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Capture max", ARRAY_LEN(capmax_values), capmax_cb, app);
    {
        uint8_t idx =
            nearest_index(capmax_values, ARRAY_LEN(capmax_values), app->settings.capture_max_ms);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld ms", (long)capmax_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Signal-end gap", ARRAY_LEN(endgap_values), endgap_cb, app);
    {
        uint8_t idx = nearest_index(
            endgap_values, ARRAY_LEN(endgap_values), (int32_t)app->settings.signal_end_gap_ms);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld ms", (long)endgap_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Min gap", ARRAY_LEN(mingap_values), mingap_cb, app);
    {
        uint8_t idx = nearest_index(
            mingap_values, ARRAY_LEN(mingap_values), (int32_t)app->settings.min_gap_ms);
        variable_item_set_current_value_index(it, idx);
        snprintf(buf, sizeof(buf), "%ld ms", (long)mingap_values[idx]);
        variable_item_set_current_value_text(it, buf);
    }
    row++;

    it = variable_item_list_add(list, "Auto-classify", 2, classify_cb, app);
    variable_item_set_current_value_index(it, app->settings.auto_classify ? 1 : 0);
    variable_item_set_current_value_text(it, off_on[app->settings.auto_classify ? 1 : 0]);
    row++;

    it = variable_item_list_add(list, "Match DB", 2, matchdb_cb, app);
    variable_item_set_current_value_index(it, app->settings.match_db ? 1 : 0);
    variable_item_set_current_value_text(it, off_on[app->settings.match_db ? 1 : 0]);
    row++;

    it = variable_item_list_add(list, "Notify", 3, notify_cb, app);
    variable_item_set_current_value_index(it, app->settings.notify);
    variable_item_set_current_value_text(it, notify_names[app->settings.notify]);
    row++;

    variable_item_list_set_enter_callback(list, settings_enter_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewVarItemList);
}

bool subcensus_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_settings_on_exit(void* context) {
    SubCensusApp* app = context;
    variable_item_list_reset(app->var_item_list);
    census_settings_save(app->storage, &app->settings);
    FURI_LOG_I("SubCensus", "SC scene=settings action=save place=%s", app->settings.place_id);
}
