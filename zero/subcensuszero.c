#include "subcensuszero_i.h"

static bool subcensus_custom_event_callback(void* context, uint32_t event) {
    SubCensusApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool subcensus_back_event_callback(void* context) {
    SubCensusApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static SubCensusApp* subcensus_app_alloc(void) {
    SubCensusApp* app = malloc(sizeof(SubCensusApp));
    memset(app, 0, sizeof(SubCensusApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&subcensus_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, subcensus_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, subcensus_back_event_callback);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SubCensusViewSubmenu, submenu_get_view(app->submenu));
    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SubCensusViewVarItemList,
        variable_item_list_get_view(app->var_item_list));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SubCensusViewTextInput, text_input_get_view(app->text_input));
    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SubCensusViewWidget, widget_get_view(app->widget));
    app->dialog_ex = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, SubCensusViewDialogEx, dialog_ex_get_view(app->dialog_ex));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, SubCensusViewPopup, popup_get_view(app->popup));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    census_settings_load(app->storage, &app->settings);
    census_storage_init(app->storage, &app->settings);
    return app;
}

static void subcensus_app_free(SubCensusApp* app) {
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewDialogEx);
    view_dispatcher_remove_view(app->view_dispatcher, SubCensusViewPopup);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    text_input_free(app->text_input);
    widget_free(app->widget);
    dialog_ex_free(app->dialog_ex);
    popup_free(app->popup);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t subcensuszero_app(void* p) {
    UNUSED(p);
    SubCensusApp* app = subcensus_app_alloc();
    FURI_LOG_I("SubCensus", "SC boot app=subcensuszero place=%s", app->settings.place_id);
    scene_manager_next_scene(app->scene_manager, SubCensusSceneStart);
    view_dispatcher_run(app->view_dispatcher);
    subcensus_app_free(app);
    return 0;
}
