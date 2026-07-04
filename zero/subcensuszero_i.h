/* subcensuszero_i.h — app state shared across scenes. */
#ifndef SUBCENSUSZERO_I_H
#define SUBCENSUSZERO_I_H

#include <furi.h>
#include <gui/gui.h>
#include <gui/scene_manager.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/popup.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include "census_camp_view.h"
#include "census_freq.h"
#include "census_storage.h"
#include "census_worker.h"
#include "scenes/subcensus_scene.h"

typedef enum {
    SubCensusViewSubmenu,
    SubCensusViewVarItemList,
    SubCensusViewTextInput,
    SubCensusViewWidget,
    SubCensusViewDialogEx,
    SubCensusViewPopup,
    SubCensusViewCamp,
} SubCensusView;

typedef enum {
    SubCensusTextModeNewPlace = 0,
    SubCensusTextModeRenamePlace = 1,
} SubCensusTextMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Storage* storage;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    TextInput* text_input;
    Widget* widget;
    DialogEx* dialog_ex;
    Popup* popup;
    CensusCampView* camp_view;

    CensusWorker* worker;
    FuriTimer* live_timer;
    uint32_t camp_freq;
    bool live_sweep;

    CensusSettings settings;

    /* scratch */
    char text_buf[CENSUS_PLACE_NAME_LEN];
    uint8_t text_mode; /* SubCensusTextMode */
    char selected_place[CENSUS_PLACE_ID_LEN];
    char place_ids[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN];
    size_t place_count;
    const char* todo_msg; /* placeholder text for not-yet-implemented milestones */
} SubCensusApp;

#endif /* SUBCENSUSZERO_I_H */
