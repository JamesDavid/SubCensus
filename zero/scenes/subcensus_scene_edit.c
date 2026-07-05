#include <stdio.h>
#include <string.h>

#include "../subcensuszero_i.h"

/* Edit-before-transmit / field-map discovery menu (Zero §6, System §7b). Loads the selected
 * capture's `.sub` into an aligned bit frame (sc_slice) and offers: raw bit/hex edit · structured
 * field editor (known protocol) · field-map discovery (unknowns) · Send edited frame (the only TX
 * path — single-frame, TX-allow-list gated, logged distinctly) · Propose field-map (user confirms;
 * never auto-committed). Live TX = TODO(hw). */

enum {
    EditItemRaw = 0,
    EditItemFields = 1,
    EditItemDiscovery = 2,
    EditItemSend = 3,
    EditItemPropose = 4,
};

static void edit_cb(void* context, uint32_t index) {
    SubCensusApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void subcensus_scene_edit_on_enter(void* context) {
    SubCensusApp* app = context;

    /* load + slice the capture once, on first entry (edit_nbits==0 or a fresh selection) */
    if(app->edit_nbits == 0) {
        app->edit_nbits = census_edit_load_sub(
            app->storage,
            app->settings.place_id,
            app->review_subs[app->review_sel],
            app->edit_frame,
            CENSUS_EDIT_MAXBYTES,
            &app->edit_unit_us,
            &app->edit_freq,
            app->edit_preset,
            sizeof(app->edit_preset));
        app->edit_dirty = false;
        app->edit_from_tx = false;
        /* protocol from the top brain candidate (structured editor needs a known protocol) */
        strncpy(
            app->edit_protocol,
            app->review_cand_name[0] ? app->review_cand_name : "unknown",
            sizeof(app->edit_protocol) - 1);
        app->edit_protocol[sizeof(app->edit_protocol) - 1] = '\0';
        /* try to load a field-map for this protocol */
        app->edit_has_map = census_fieldmap_load(app->storage, app->edit_protocol, &app->edit_map);
    }

    Submenu* menu = app->submenu;
    submenu_reset(menu);
    char header[32];
    snprintf(header, sizeof(header), "Edit (%u bits)", (unsigned)app->edit_nbits);
    submenu_set_header(menu, header);
    submenu_add_item(menu, "Raw bit/hex edit", EditItemRaw, edit_cb, app);
    submenu_add_item(menu, "Structured fields", EditItemFields, edit_cb, app);
    submenu_add_item(menu, "Field-map discovery", EditItemDiscovery, edit_cb, app);
    submenu_add_item(menu, "Send edited frame", EditItemSend, edit_cb, app);
    if(app->edit_has_map)
        submenu_add_item(menu, "Propose field-map", EditItemPropose, edit_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, SubCensusViewSubmenu);
}

bool subcensus_scene_edit_on_event(void* context, SceneManagerEvent event) {
    SubCensusApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        /* leaving the editor entirely — clear the loaded frame so re-entry reloads */
        app->edit_nbits = 0;
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case EditItemRaw:
        scene_manager_next_scene(app->scene_manager, SubCensusSceneEditRaw);
        return true;
    case EditItemFields:
        scene_manager_next_scene(app->scene_manager, SubCensusSceneEditFields);
        return true;
    case EditItemDiscovery:
        scene_manager_next_scene(app->scene_manager, SubCensusSceneEditDiscovery);
        return true;
    case EditItemSend:
        app->edit_from_tx = true;
        scene_manager_next_scene(app->scene_manager, SubCensusSceneReplayConfirm);
        return true;
    case EditItemPropose:
        if(app->edit_has_map) {
            app->edit_map.user_confirmed = true; /* user chose to propose == confirmation (§7b) */
            bool ok = census_fieldmap_save(app->storage, &app->edit_map);
            notification_message(
                app->notifications, ok ? &sequence_success : &sequence_blink_red_100);
            FURI_LOG_I(
                "SubCensus",
                "SC scene=edit action=propose_fieldmap proto=%s ok=%d",
                app->edit_map.protocol,
                ok);
        }
        return true;
    default:
        return false;
    }
}

void subcensus_scene_edit_on_exit(void* context) {
    SubCensusApp* app = context;
    submenu_reset(app->submenu);
}
