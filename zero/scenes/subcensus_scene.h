/* subcensus_scene.h — scene ids + handler declarations (X-macro). */
#ifndef SUBCENSUS_SCENE_H
#define SUBCENSUS_SCENE_H

#include <gui/scene_manager.h>

typedef enum {
#define ADD_SCENE(prefix, name, id) SubCensusScene##id,
#include "subcensus_scene_config.h"
#undef ADD_SCENE
    SubCensusSceneNum,
} SubCensusScene;

extern const SceneManagerHandlers subcensus_scene_handlers;

#define ADD_SCENE(prefix, name, id)                                                \
    void prefix##_scene_##name##_on_enter(void* context);                          \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "subcensus_scene_config.h"
#undef ADD_SCENE

#endif /* SUBCENSUS_SCENE_H */
