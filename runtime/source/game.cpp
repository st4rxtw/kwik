#include "gml_runtime.h"
#include "render.h"

#include <cmath>
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
        if (init.object_index >= 0 && init.object_index < object_count)
            inst.var("sprite_index") = Value((double)objects[init.object_index].sprite_index);
        inst.var("image_index") = Value(0.0);
        inst.var("image_speed") = Value(1.0);
        inst.var("image_xscale") = Value(1.0);
        inst.var("image_yscale") = Value(1.0);
        inst.var("image_angle") = Value(0.0);
        inst.var("image_alpha") = Value(1.0);
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

        for (Instance& inst : instances) {
            int spr = (int)(double)inst.var("sprite_index");
            if (spr < 0 || spr >= g_sprite_count) continue;
            int fc = g_sprites[spr].frame_count;
            if (fc <= 1) continue;
            double idx = (double)inst.var("image_index") + (double)inst.var("image_speed");
            idx = idx - fc * std::floor(idx / fc);
            inst.var("image_index") = Value(idx);
        }

        render_begin_frame();
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            const ObjectDef& obj = objects[inst.object_index];
            if (obj.draw)
                obj.draw(inst);
            else if (obj.sprite_index >= 0)
                draw_self(inst);
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
