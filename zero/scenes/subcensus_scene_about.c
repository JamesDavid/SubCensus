#include "../subcensuszero_i.h"

#include <stdio.h>

/* About (Zero §6): version, built-against SDK/API, passive-RX note, active place, storage tier
 * + free space, and the expected battery drain for long census runs. */

void subcensus_scene_about_on_enter(void* context) {
    SubCensusApp* app = context;
    Widget* w = app->widget;
    widget_reset(w);

    char place_name[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->settings.place_id, place_name, sizeof(place_name));

    /* storage tier + free space (§6.1 surfaces remaining space here) */
    char storage_line[48];
    uint64_t freeb = 0, totalb = 0;
    if(census_sd_space(app->storage, &freeb, &totalb)) {
        snprintf(
            storage_line,
            sizeof(storage_line),
            "Storage: /ext SD %lu/%lu MB",
            (unsigned long)(freeb / (1024 * 1024)),
            (unsigned long)(totalb / (1024 * 1024)));
    } else {
        snprintf(storage_line, sizeof(storage_line), "Storage: /ext SD (absent)");
    }

    static char text[640];
    snprintf(
        text,
        sizeof(text),
        "SubCensusZero v0.1\n"
        "SDK: release 1.4.3\n"
        "API: 87.1 (f7)\n"
        "\n"
        "Monitoring is passive:\n"
        "Sweep/Camp/Recon never TX.\n"
        "Replay & Edit-TX DO transmit\n"
        "- explicit, single-frame,\n"
        "TX-allow-list gated.\n"
        "\n"
        "%s\n"
        "Place: %s\n"
        "\n"
        "Battery: continuous RX draws\n"
        "~30-40 mA; a full charge lasts\n"
        "~5-8 h of Camp/Sweep. Recon\n"
        "(hopping) is similar. Screen\n"
        "dims; the worker keeps running.\n"
        "\n"
        "Prior art: ProtoView, Read RAW,\n"
        "Freq/Spectrum Analyzer, FlipRSDR.",
        storage_line,
        place_name);

    widget_add_text_scroll_element(w, 0, 0, 128, 64, text);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewWidget);
}

bool subcensus_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_about_on_exit(void* context) {
    SubCensusApp* app = context;
    widget_reset(app->widget);
}
