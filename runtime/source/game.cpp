#include "gml_runtime.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gml {

static int g_current_room = -1;
static int g_pending_room = -1;
static int g_room_total = 0;
static const RoomDef* g_room_defs = nullptr;
static std::vector<Instance>* g_live = nullptr;

static Instance* find_instance(int who) {
    if (!g_live) return nullptr;
    for (Instance& inst : *g_live) {
        if (who >= 100000 ? (inst.id == who) : (inst.object_index == who))
            return &inst;
    }
    return nullptr;
}

Value kwik_inst_get(const Value& who, const std::string& name) {
    Instance* inst = find_instance((int)(double)who);
    if (!inst) return Value();
    if (name == "x") return Value(inst->x);
    if (name == "y") return Value(inst->y);
    return inst->var(name);
}

void kwik_inst_set(const Value& who, const std::string& name, const Value& val) {
    Instance* inst = find_instance((int)(double)who);
    if (!inst) return;
    if (name == "x") { inst->x = (double)val; return; }
    if (name == "y") { inst->y = (double)val; return; }
    inst->var(name) = val;
}

Value instance_exists(const Value& obj) {
    return Value(find_instance((int)(double)obj) != nullptr ? 1.0 : 0.0);
}

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
        inst.var("image_index") = Value((double)init.image_index);
        inst.var("image_speed") = Value(1.0);
        inst.var("image_xscale") = Value(init.scale_x);
        inst.var("image_yscale") = Value(init.scale_y);
        inst.var("image_angle") = Value(init.angle);
        inst.var("image_alpha") = Value(1.0);
        inst.var("depth") = Value(init.depth);
        instances.push_back(inst);
    }

    g_live = &instances;
    g_current_room = index;
    g_pending_room = -1;
    builtin_var("room") = Value((double)index);
    builtin_var("room_width") = Value((double)room.width);
    builtin_var("room_height") = Value((double)room.height);
    render_set_room(room.width, room.height, room.bg_color);
    render_set_view(room.view_x, room.view_y, room.view_w, room.view_h);
    render_set_title(room.name);

    for (Instance& inst : instances) {
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn fn = objects[inst.object_index].create;
        if (fn) fn(inst);
    }
}

int run_game(const ObjectDef* objects, int object_count, const RoomDef* rooms, int room_count) {
    if (room_count <= 0) return 1;
    g_room_total = room_count;
    g_room_defs = rooms;

    builtin_var("room_speed") = Value(30.0);
    builtin_var("fps") = Value(30.0);

    if (!render_init(rooms[0].name, rooms[0].width, rooms[0].height, rooms[0].bg_color)) return 1;

    std::vector<Instance> instances;
    load_room(objects, object_count, rooms[0], 0, instances);

    const double game_fps = 30.0;
    const double step_time = 1.0 / game_fps;
    double accumulator = 0.0;
    std::vector<size_t> draw_order;
    while (!render_should_close()) {
        accumulator += render_delta_time();
        if (accumulator > 0.25) accumulator = 0.25;

        int guard = 0;
        while (accumulator >= step_time && guard++ < 5) {
            accumulator -= step_time;

            for (Instance& inst : instances) {
                if (inst.object_index < 0 || inst.object_index >= object_count) continue;
                EventFn fn = objects[inst.object_index].step;
                if (fn) fn(inst);
            }

            if (g_pending_room >= 0 && g_pending_room < room_count) {
                load_room(objects, object_count, rooms[g_pending_room], g_pending_room, instances);
                accumulator = 0.0;
                break;
            }

            for (Instance& inst : instances) {
                int spr = (int)(double)inst.var("sprite_index");
                if (spr < 0 || spr >= g_sprite_count) continue;
                const KwikSprite& s = g_sprites[spr];
                if (s.frame_count <= 1) continue;
                double fps = s.speed_type == 0 ? s.speed : s.speed * game_fps;
                double adv = fps * (double)inst.var("image_speed") * step_time;
                double idx = (double)inst.var("image_index") + adv;
                idx = idx - s.frame_count * std::floor(idx / s.frame_count);
                inst.var("image_index") = Value(idx);
            }
        }

        render_begin_frame();
        if (g_current_room >= 0) {
            const RoomDef& cur = g_room_defs[g_current_room];
            for (int i = 0; i < cur.background_count; ++i) {
                const RoomBg& bg = cur.backgrounds[i];
                if (bg.sprite_index >= 0)
                    draw_sprite(Value(bg.sprite_index), Value(0), Value((double)bg.x), Value((double)bg.y));
            }
        }
        draw_order.resize(instances.size());
        for (size_t i = 0; i < instances.size(); ++i) draw_order[i] = i;
        std::stable_sort(draw_order.begin(), draw_order.end(), [&](size_t a, size_t b) {
            return (double)instances[a].var("depth") > (double)instances[b].var("depth");
        });
        for (size_t oi : draw_order) {
            Instance& inst = instances[oi];
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
