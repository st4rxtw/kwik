#include "gml_runtime.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace gml {

static int g_current_room = -1;
static int g_pending_room = -1;
static int g_room_total = 0;
static const RoomDef* g_room_defs = nullptr;
static std::vector<Instance>* g_live = nullptr;

static Instance* g_current_self = nullptr;
static const ObjectDef* g_objects = nullptr;
static int g_object_count = 0;

static bool obj_is_a(int obj, int target) {
    int guard = 0;
    while (obj >= 0 && obj < g_object_count && guard++ < 64) {
        if (obj == target) return true;
        obj = g_objects[obj].parent_index;
    }
    return obj == target;
}

static bool inst_matches(const Instance& inst, int who) {
    return who >= 100000 ? (inst.id == who) : obj_is_a(inst.object_index, who);
}

static Instance* find_instance(int who) {
    if (!g_live) return nullptr;
    for (Instance& inst : *g_live) {
        if (inst_matches(inst, who))
            return &inst;
    }
    return nullptr;
}

static bool inst_bbox(const Instance& inst, double px, double py, double& l, double& t,
                      double& r, double& b) {
    int spr = (int)(double)const_cast<Instance&>(inst).var("sprite_index");
    if (spr < 0 || spr >= g_sprite_count) return false;
    const KwikSprite& s = g_sprites[spr];
    double xs = (double)const_cast<Instance&>(inst).var("image_xscale");
    double ys = (double)const_cast<Instance&>(inst).var("image_yscale");
    if (xs == 0.0) xs = 1.0;
    if (ys == 0.0) ys = 1.0;
    double x0 = px + (s.bbox_left - s.origin_x) * xs;
    double x1 = px + (s.bbox_right + 1 - s.origin_x) * xs;
    double y0 = py + (s.bbox_top - s.origin_y) * ys;
    double y1 = py + (s.bbox_bottom + 1 - s.origin_y) * ys;
    l = std::min(x0, x1); r = std::max(x0, x1);
    t = std::min(y0, y1); b = std::max(y0, y1);
    return true;
}

Value place_meeting(const Value& x, const Value& y, const Value& obj) {
    if (!g_current_self || !g_live) return Value(0.0);
    double l, t, r, b;
    if (!inst_bbox(*g_current_self, (double)x, (double)y, l, t, r, b)) return Value(0.0);
    int who = (int)(double)obj;
    for (Instance& other : *g_live) {
        if (&other == g_current_self) continue;
        if (!inst_matches(other, who)) continue;
        double ol, ot, orr, ob;
        if (!inst_bbox(other, other.x, other.y, ol, ot, orr, ob)) continue;
        if (l < orr && r > ol && t < ob && b > ot) return Value(1.0);
    }
    return Value(0.0);
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

Value instance_find(const Value& obj, const Value& n) {
    if (!g_live) return Value(-4.0);
    int who = (int)(double)obj;
    int idx = (int)(double)n;
    int count = 0;
    for (Instance& inst : *g_live) {
        if (!inst_matches(inst, who)) continue;
        if (count == idx) return Value((double)inst.id);
        ++count;
    }
    return Value(-4.0);
}

static std::vector<std::pair<int, size_t>> g_with;

static Instance* with_advance() {
    if (g_with.empty() || !g_live) return nullptr;
    auto& top = g_with.back();
    while (top.second < g_live->size()) {
        Instance& inst = (*g_live)[top.second++];
        if (inst_matches(inst, top.first)) return &inst;
    }
    g_with.pop_back();
    return nullptr;
}

Instance* kwik_with_first(const Value& target) {
    if (!g_live) return nullptr;
    g_with.push_back({(int)(double)target, 0});
    return with_advance();
}

Instance* kwik_with_next() { return with_advance(); }

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
    std::vector<Instance> kept;
    for (Instance& inst : instances) {
        if (inst.object_index >= 0 && inst.object_index < object_count &&
            objects[inst.object_index].persistent)
            kept.push_back(std::move(inst));
    }
    instances = std::move(kept);
    size_t persistent_count = instances.size();

    instances.reserve(persistent_count + room.instance_count);
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

    for (size_t k = persistent_count; k < instances.size(); ++k) {
        Instance& inst = instances[k];
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn pc = objects[inst.object_index].pre_create;
        if (pc) { g_current_self = &inst; pc(inst); }
        EventFn fn = objects[inst.object_index].create;
        if (fn) { g_current_self = &inst; fn(inst); }
        EventFn cc = room.instances[k - persistent_count].creation_code;
        if (cc) { g_current_self = &inst; cc(inst); }
    }
    for (Instance& inst : instances) {
        if (inst.object_index < 0 || inst.object_index >= object_count) continue;
        EventFn fn = objects[inst.object_index].room_start;
        if (fn) { g_current_self = &inst; fn(inst); }
    }
    g_current_self = nullptr;
}

int run_game(const ObjectDef* objects, int object_count, const RoomDef* rooms, int room_count) {
    if (room_count <= 0) return 1;
    g_room_total = room_count;
    g_room_defs = rooms;
    g_objects = objects;
    g_object_count = object_count;

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
                if (fn) { g_current_self = &inst; fn(inst); }
            }
            g_current_self = nullptr;

            for (size_t ai = 0; ai < instances.size(); ++ai) {
                Instance& a = instances[ai];
                if (a.object_index < 0 || a.object_index >= object_count) continue;
                const ObjectDef& oa = objects[a.object_index];
                if (oa.collision_count == 0) continue;
                double al, at, ar, ab;
                if (!inst_bbox(a, a.x, a.y, al, at, ar, ab)) continue;
                for (int c = 0; c < oa.collision_count; ++c) {
                    int target = oa.collisions[c].other_object;
                    EventFn fn = oa.collisions[c].fn;
                    for (Instance& b : instances) {
                        if (&b == &a || !inst_matches(b, target)) continue;
                        double bl, bt, br, bb;
                        if (!inst_bbox(b, b.x, b.y, bl, bt, br, bb)) continue;
                        if (al < br && ar > bl && at < bb && ab > bt) {
                            g_current_self = &a;
                            fn(a);
                            break;
                        }
                    }
                }
            }
            g_current_self = nullptr;

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
            g_current_self = &inst;
            if (obj.draw)
                obj.draw(inst);
            else if (obj.sprite_index >= 0)
                draw_self(inst);
        }
        render_begin_gui();
        for (Instance& inst : instances) {
            if (inst.object_index < 0 || inst.object_index >= object_count) continue;
            EventFn fn = objects[inst.object_index].draw_gui;
            if (fn) { g_current_self = &inst; fn(inst); }
        }
        g_current_self = nullptr;
        render_end_frame();
    }

    render_shutdown();
    return 0;
}

}
