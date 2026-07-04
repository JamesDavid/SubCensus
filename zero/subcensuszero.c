#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

#include "../shared/core/sc_crc.h"

/* Minimal skeleton — validates the toolchain + shared/core inclusion. Expanded into the
 * full ViewDispatcher + scene manager app in M1. */

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
} SubCensusApp;

#define VIEW_SUBMENU 0

static bool subcensus_nav_callback(void* context) {
    UNUSED(context);
    return false; /* back exits */
}

int32_t subcensuszero_app(void* p) {
    UNUSED(p);
    SubCensusApp* app = malloc(sizeof(SubCensusApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();

    FURI_LOG_I(
        "SubCensus",
        "SC boot app=subcensuszero core_ok=%u",
        (unsigned)(sc_crc8(NULL, 0, 0x07, 0x00) == 0));

    submenu_add_item(app->submenu, "SubCensusZero", 0, NULL, app);
    view_dispatcher_add_view(app->view_dispatcher, VIEW_SUBMENU, submenu_get_view(app->submenu));
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, subcensus_nav_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, VIEW_SUBMENU);

    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(app->view_dispatcher, VIEW_SUBMENU);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
