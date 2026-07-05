#include <stdlib.h>
#include <string.h>

#include "../shared/core/sc_diff.h"
#include "../subcensuszero_i.h"

/* Field-map discovery on unknowns (Zero §6, System §7b): gather the same-device capture corpus,
 * run the passive differential bitfield analysis (sc_diff) to seed a proposed structure
 * (sc_fieldmap_from_diff), and open the segment-labeling editor. The user cycles each segment's
 * class; "Propose field-map" on the Edit menu persists it (user confirms; never auto-committed).
 * Active confirmation (transmit an edited frame to your own device, watch it react) is the Send
 * path on the Edit menu — TX-allow-list gated. All analysis here is PASSIVE (no TX). */

#define DISC_MAX_FRAMES 16
#define DISC_STRIDE     CENSUS_EDIT_MAXBYTES

void subcensus_scene_edit_discovery_on_enter(void* context) {
    SubCensusApp* app = context;

    uint8_t modulation = strstr(app->edit_preset, "FSK") ? 1 : 0;

    /* gather an aligned frame corpus (same freq bin) for the differential analysis */
    uint8_t* frames = malloc((size_t)DISC_MAX_FRAMES * DISC_STRIDE);
    size_t nbits = app->edit_nbits;
    size_t n_frames = 0;
    if(frames) {
        n_frames = census_edit_corpus(
            app->storage,
            app->settings.place_id,
            app->edit_freq,
            app->edit_unit_us,
            frames,
            DISC_MAX_FRAMES,
            DISC_STRIDE,
            &nbits);
    }
    /* fall back to the single loaded frame if the corpus is too small */
    if(n_frames < 1) {
        if(frames) memcpy(frames, app->edit_frame, DISC_STRIDE);
        n_frames = 1;
        nbits = app->edit_nbits;
    }
    if(nbits > DISC_STRIDE * 8) nbits = DISC_STRIDE * 8;

    ScBitProfile* prof = malloc(sizeof(ScBitProfile) * (DISC_STRIDE * 8));
    if(frames && prof) {
        sc_diff_analyze(frames, n_frames, nbits, DISC_STRIDE, prof);
        sc_fieldmap_from_diff(prof, nbits, app->edit_protocol, modulation, &app->edit_map);
        app->edit_has_map = true;
    }
    if(frames) free(frames);
    if(prof) free(prof);

    FURI_LOG_I(
        "SubCensus",
        "SC scene=edit_discovery action=seed frames=%u nbits=%u fields=%u",
        (unsigned)n_frames,
        (unsigned)nbits,
        (unsigned)app->edit_map.n_fields);

    census_editor_view_configure(
        app->editor_view,
        CensusEditorDiscovery,
        app->edit_frame,
        app->edit_nbits,
        &app->edit_map,
        &app->edit_dirty);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewEditor);
}

bool subcensus_scene_edit_discovery_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subcensus_scene_edit_discovery_on_exit(void* context) {
    UNUSED(context);
}
