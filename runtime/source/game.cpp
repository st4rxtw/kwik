#include "gml_runtime.h"

#include <cstdio>
#include <vector>

namespace gml {

int run_game(const ObjectDef* objects, int object_count, const RoomDef& room) {
    std::printf("room \"%s\" %dx%d, %d instances\n", room.name, room.width, room.height,
                room.instance_count);

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

    for (Instance& inst : instances) {
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn fn = objects[inst.object_index].create;
        if (fn) fn(inst);
    }

    const int frames = 3;
    for (int f = 0; f < frames; ++f) {
        std::printf("-- frame %d --\n", f);
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].step;
            if (fn) fn(inst);
        }
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
    }

    return 0;
}

}
