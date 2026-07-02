#include "gml_runtime.h"
#include "render.h"

#include <vector>

namespace gml {

int run_game(const ObjectDef* objects, int object_count, const RoomDef& room) {
    std::vector<Instance> instances;
    instances.reserve(room.instance_count);
    for (int i = 0; i < room.instance_count; ++i) {
        const InstanceInit& init = room.instances[i];
        Instance inst;
        inst.object_index = init.object_index;
        inst.x = init.x;
        inst.y = init.y;
        inst.id = init.id;
        instances.push_back(inst);
    }

    builtin_var("room_width") = Value(static_cast<double>(room.width));
    builtin_var("room_height") = Value(static_cast<double>(room.height));
    builtin_var("room_speed") = Value(30.0);
    builtin_var("fps") = Value(30.0);
    builtin_var("room") = Value(0.0);

    if (!render_init(room.name, room.width, room.height, room.bg_color)) return 1;

    for (Instance& inst : instances) {
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn fn = objects[inst.object_index].create;
        if (fn) fn(inst);
    }

    while (!render_should_close()) {
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].step;
            if (fn) fn(inst);
        }

        render_begin_frame();
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].draw;
            if (fn) fn(inst);
        }
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].draw_gui;
            if (fn) fn(inst);
        }
        render_end_frame();
    }

    render_shutdown();
    return 0;
}

}
