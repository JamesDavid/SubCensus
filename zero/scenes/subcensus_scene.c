/* subcensus_scene.c — assemble the SceneManagerHandlers dispatch tables (X-macro). */
#include "subcensus_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const subcensus_on_enter_handlers[])(void*) = {
#include "subcensus_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const subcensus_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "subcensus_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const subcensus_on_exit_handlers[])(void*) = {
#include "subcensus_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers subcensus_scene_handlers = {
    .on_enter_handlers = subcensus_on_enter_handlers,
    .on_event_handlers = subcensus_on_event_handlers,
    .on_exit_handlers = subcensus_on_exit_handlers,
    .scene_num = SubCensusSceneNum,
};
