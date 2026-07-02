#include "gml_runtime.h"
#include "render.h"

#include <cmath>
#include <vector>

namespace gml {

static int g_current_room = -1;
static int g_pending_room = -1;
static int g_room_total = 0;

void kwik_room_goto(int index) {
    if (index >= 0 && index < g_room_total) g_pending_room = index;
}
void kwik_room_goto_next() {
    if (g_current_room + 1 < g_room_total) g_pending_room = g_current_room + 1;
}
void kwik_room_goto_previous() {
    if (g_current_room - 1 >= 0) g_pending_room = g_current_room - 1;
}

static void load_room(const ObjectDef* objects, int object_count, const RoomDef& room, int index,
                      std::vector<Instance>& instances) {
    instances.clear();
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

    g_current_room = index;
    g_pending_room = -1;
    builtin_var("room") = Value((double)index);
    builtin_var("room_width") = Value((double)room.width);
    builtin_var("room_height") = Value((double)room.height);
    render_set_room(room.width, room.height, room.bg_color);
    render_set_view(room.view_x, room.view_y, room.view_w, room.view_h);

    for (Instance& inst : instances) {
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn fn = objects[inst.object_index].create;
        if (fn) fn(inst);
    }
}

int run_game(const ObjectDef* objects, int object_count, const RoomDef* rooms, int room_count) {
    if (room_count <= 0) return 1;
    g_room_total = room_count;

    builtin_var("room_speed") = Value(30.0);
    builtin_var("fps") = Value(30.0);

    if (!render_init(rooms[0].name, rooms[0].width, rooms[0].height, rooms[0].bg_color)) return 1;

    std::vector<Instance> instances;
    load_room(objects, object_count, rooms[0], 0, instances);

    const double game_fps = 30.0;
    while (!render_should_close()) {
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].step;
            if (fn) fn(inst);
        }

        if (g_pending_room >= 0 && g_pending_room < room_count) {
            load_room(objects, object_count, rooms[g_pending_room], g_pending_room, instances);
            continue;
        }

        double dt = render_delta_time();
        for (Instance& inst : instances) {
            int spr = (int)(double)inst.var("sprite_index");
            if (spr < 0 || spr >= g_sprite_count) continue;
            const KwikSprite& s = g_sprites[spr];
            if (s.frame_count <= 1) continue;
            double fps = s.speed_type == 0 ? s.speed : s.speed * game_fps;
            double adv = fps * (double)inst.var("image_speed") * dt;
            double idx = (double)inst.var("image_index") + adv;
            idx = idx - s.frame_count * std::floor(idx / s.frame_count);
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
        render_begin_gui();
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
