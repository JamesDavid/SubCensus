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

#include "../shared/core/sc_feature.h"
#include "../shared/core/sc_fieldmap.h"
#include "../shared/core/sc_slice.h"
#include "census_brain.h"
#include "census_camp_view.h"
#include "census_edit.h"
#include "census_editor_view.h"
#include "census_freq.h"
#include "census_recon.h"
#include "census_spectrum_view.h"
#include "census_storage.h"
#include "census_worker.h"
#include "scenes/subcensus_scene.h"

#define CENSUS_REVIEW_MAX    64
#define CENSUS_EDIT_MAXBYTES 64

typedef enum {
    SubCensusViewSubmenu,
    SubCensusViewVarItemList,
    SubCensusViewTextInput,
    SubCensusViewWidget,
    SubCensusViewDialogEx,
    SubCensusViewPopup,
    SubCensusViewCamp,
    SubCensusViewSpectrum,
    SubCensusViewEditor,
} SubCensusView;

typedef enum {
    SubCensusTextModeNewPlace = 0,
    SubCensusTextModeRenamePlace = 1,
    SubCensusTextModeManualFreqCustom = 2, /* manual MHz -> add to custom list (§6) */
    SubCensusTextModeManualFreqCamp = 3, /* manual MHz -> camp here (§6) */
    SubCensusTextModeSetLocation = 4, /* place location tag (§5.6) */
} SubCensusTextMode;

/* Edit-before-transmit / field-map discovery mode (M10, §6/§7b). */
typedef enum {
    SubCensusEditRaw = 0, /* raw bit/hex edit of any capture */
    SubCensusEditFields = 1, /* structured field editor (protocol known) */
    SubCensusEditDiscovery = 2, /* differential-seeded field-map discovery on unknowns */
} SubCensusEditMode;

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
    CensusEditorView* editor_view;

    CensusWorker* worker;
    CensusRecon* recon;
    CensusSpectrumView* spectrum_view;
    FuriTimer* live_timer;
    uint32_t camp_freq;
    bool live_sweep;
    bool recon_fresh;

    /* Recon results per-entry actions (§6): the selected watchlist entry. */
    uint32_t recon_sel_freq;
    char recon_sel_mod[8];

    /* Edit-before-transmit / field-map discovery (M10, §6/§7b). Loaded from a capture's .sub;
     * sliced to a bit frame (sc_slice) that the raw/structured/discovery editors operate on. */
    uint8_t edit_mode; /* SubCensusEditMode */
    uint8_t edit_frame[CENSUS_EDIT_MAXBYTES];
    uint8_t edit_orig[CENSUS_EDIT_MAXBYTES]; /* pre-edit snapshot for the before/after diff (§6) */
    size_t edit_nbits;
    int32_t edit_unit_us; /* slice unit = shortest dominant symbol (sc_feature) */
    uint32_t edit_freq;
    char edit_preset[24];
    char edit_protocol[24]; /* known protocol (brain/decoder) or "unknown" */
    ScFieldMap edit_map; /* labeled fields + checksum (structured/discovery) */
    bool edit_has_map;
    size_t edit_sel; /* selected field/byte/segment index */
    bool edit_dirty; /* edited since load (blocks a stale re-decode) */
    bool edit_from_tx; /* the pending TX frame is an edited frame (log distinctly, §6) */

    /* Review state */
    char review_subs[CENSUS_REVIEW_MAX][80];
    uint32_t review_freqs[CENSUS_REVIEW_MAX];
    size_t review_count;
    size_t review_sel;
    ScFeatureVector review_fv;
    char review_cand_class[24]; /* top k-NN candidate device_class ("" if none) — Label "Accept" */
    char review_cand_name[24]; /* top candidate device_name (for the Accept label) */
    uint32_t review_jump_freq; /* preselect this freq's newest row in Review (live-list jump, §6) */
    uint8_t replay_repeat; /* Replay repeat count 1..10 (§6); edited-TX is forced to 1 */

    CensusSettings settings;

    /* scratch */
    char text_buf[CENSUS_PLACE_NAME_LEN];
    uint8_t text_mode; /* SubCensusTextMode */
    char selected_place[CENSUS_PLACE_ID_LEN];
    char place_ids[CENSUS_MAX_PLACES][CENSUS_PLACE_ID_LEN];
    size_t place_count;
    const char* todo_msg; /* placeholder text for not-yet-implemented milestones */
} SubCensusApp;

/* Lazy allocation of the heavy capture subsystems (recon ~9KB, worker + brain) so the idle menu
 * keeps memory headroom on the Flipper's small app heap. The FAP loads its whole 109KB binary
 * into RAM; allocating recon+worker eagerly on top left <4KB free -> out-of-memory crash. Instead
 * allocate on first use in their flow and free when back at the main menu (Start scene). */
void subcensus_ensure_recon(SubCensusApp* app);
void subcensus_ensure_worker(SubCensusApp* app);
void subcensus_free_heavy(SubCensusApp* app);

#endif /* SUBCENSUSZERO_I_H */
