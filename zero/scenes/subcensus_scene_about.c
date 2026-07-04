#include "../subcensuszero_i.h"

#include <stdio.h>

/* About (Zero §6): version, built-against SDK/API, passive-RX note, active place. */

void subcensus_scene_about_on_enter(void* context) {
    SubCensusApp* app = context;
    Widget* w = app->widget;
    widget_reset(w);

    char place_name[CENSUS_PLACE_NAME_LEN];
    census_place_name(app->storage, app->settings.place_id, place_name, sizeof(place_name));

    static char text[512];
    snprintf(
        text,
        sizeof(text),
        "SubCensusZero v0.1\n"
        "SDK: release 1.4.3\n"
        "API: 87.1 (f7)\n"
        "\n"
        "PASSIVE census: Sweep/Camp/\n"
        "Recon never transmit. Replay\n"
        "& Edit-TX are explicit only,\n"
        "TX-allow-list gated.\n"
        "\n"
        "Storage: /ext (SD)\n"
        "Place: %s\n"
        "\n"
        "Long runs drain battery;\n"
        "screen may dim, worker runs.\n"
        "\n"
        "Prior art: ProtoView, Read RAW,\n"
        "Freq/Spectrum Analyzer, FlipRSDR.",
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
