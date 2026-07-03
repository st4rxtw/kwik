#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <unistd.h>

namespace gml {

std::vector<std::shared_ptr<Instance>> g_instances;
Instance* g_other_ptr = nullptr;
Instance* g_dummy_instance = nullptr;

int g_current_room = -1;
int g_pending_room = -1;
double g_room_speed_v = 30.0;
static double g_default_fps = 30.0;
bool g_game_end_requested = false;
bool g_game_restart_requested = false;
bool g_room_restart_requested = false;
unsigned long long g_frame_counter = 0;

ObjectDef* g_objects_rt = nullptr;
int g_object_count_rt = 0;
const RoomDef* g_room_defs_rt = nullptr;
int g_room_count_rt = 0;
std::string g_game_dir;
std::string g_assets_path;
std::string g_save_dir;

std::string kwik_save_path(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || g_save_dir.empty()) return rel;
    return g_save_dir + "/" + rel;
}

std::string kwik_resolve_read(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || g_save_dir.empty()) return rel;
    std::string in_save = g_save_dir + "/" + rel;
    std::FILE* f = std::fopen(in_save.c_str(), "rb");
    if (f) {
        std::fclose(f);
        return in_save;
    }
    return rel;
}

std::vector<Camera> g_cameras;
std::vector<RtLayer> g_rt_layers;
static int g_next_layer_id = 1000000;

RtLayer* kwik_layer_by_id(int id) {
    for (auto& l : g_rt_layers)
        if (l.id == id) return &l;
    return nullptr;
}

int kwik_layer_create(double depth, const std::string& name) {
    RtLayer l;
    l.id = g_next_layer_id++;
    l.name = name;
    l.depth = depth;
    l.type = 1;
    l.sprite = -1;
    l.color = 0;
    g_rt_layers.push_back(l);
    return l.id;
}
int g_view_camera[8] = {0};
int g_view_visible[8] = {1, 0, 0, 0, 0, 0, 0, 0};

static int g_next_instance_id = 10000000;
static int g_next_struct_id = 20000000;
static std::unordered_map<int, std::shared_ptr<Instance>> g_structs;

static std::mt19937 g_rng(0);
double gml_random01() {
    return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng);
}
void gml_random_seed(unsigned int seed) { g_rng.seed(seed); }

double now_ms() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int room_width_cur() {
    if (g_current_room >= 0 && g_current_room < g_room_count_rt)
        return g_room_defs_rt[g_current_room].width;
    return 640;
}
int room_height_cur() {
    if (g_current_room >= 0 && g_current_room < g_room_count_rt)
        return g_room_defs_rt[g_current_room].height;
    return 480;
}

bool kwik_obj_is_a(int obj, int ancestor) {
    int guard = 0;
    while (obj >= 0 && obj < g_object_count_rt && guard++ < 128) {
        if (obj == ancestor) return true;
        obj = g_objects_rt[obj].parent_index;
    }
    return obj == ancestor;
}

static bool inst_matches(Instance* inst, int who) {
    if (inst->dead || !inst->active || inst->is_struct) return false;
    if (who == -3) return true;
    if (who >= 100000) return inst->id == who;
    return kwik_obj_is_a(inst->object_index, who);
}

Instance* kwik_instance_by_id(int id) {
    if (id >= 20000000) {
        auto it = g_structs.find(id);
        return it == g_structs.end() ? nullptr : it->second.get();
    }
    for (auto& sp : g_instances)
        if (!sp->dead && sp->id == id) return sp.get();
    return nullptr;
}

Instance* kwik_first_instance(int who) {
    for (auto& sp : g_instances)
        if (inst_matches(sp.get(), who)) return sp.get();
    return nullptr;
}

std::vector<Instance*> kwik_instances_matching(Instance* self, int who) {
    std::vector<Instance*> out;
    if (who == -1) {
        if (self) out.push_back(self);
        return out;
    }
    if (who == -2) {
        if (g_other_ptr) out.push_back(g_other_ptr);
        return out;
    }
    if (who == -4) return out;
    for (auto& sp : g_instances)
        if (inst_matches(sp.get(), who)) out.push_back(sp.get());
    return out;
}

Instance* kwik_resolve_target(Instance* self, const Value& who) {
    if (who.type == Value::OBJ) return who.obj.get();
    int w = (int)(double)who;
    if (w == -1) return self;
    if (w == -2) return g_other_ptr;
    if (w == -4) return nullptr;
    if (w >= 20000000) return kwik_instance_by_id(w);
    if (w >= 100000) return kwik_instance_by_id(w);
    return kwik_first_instance(w);
}

static void sync_from_component(Instance* i) {
    i->m_speed = std::hypot(i->m_hs, i->m_vs);
    if (i->m_hs != 0.0 || i->m_vs != 0.0)
        i->m_dir = std::fmod(std::atan2(-i->m_vs, i->m_hs) * 180.0 / M_PI + 360.0, 360.0);
}
static void sync_from_polar(Instance* i) {
    double r = i->m_dir * M_PI / 180.0;
    i->m_hs = std::cos(r) * i->m_speed;
    i->m_vs = -std::sin(r) * i->m_speed;
}

int g_async_load_map = -1;
static std::vector<std::pair<int, int>> g_async_queue;

void kwik_queue_async(int kind, int map_id) { g_async_queue.push_back({kind, map_id}); }

static int inst_sprite(Instance* inst) {
    auto it = inst->vars.find("sprite_index");
    return it == inst->vars.end() ? -1 : (int)(double)it->second;
}
static int inst_mask(Instance* inst) {
    auto it = inst->vars.find("mask_index");
    int m = it == inst->vars.end() ? -1 : (int)(double)it->second;
    return m >= 0 ? m : inst_sprite(inst);
}

struct KBox {
    double x = 0, y = 0;
    double lx0 = 0, lx1 = 0, ly0 = 0, ly1 = 0;
    double cs = 1, sn = 0;
    bool rot = false;
    bool valid = false;
};

static KBox make_box(Instance* inst, double px, double py) {
    KBox b;
    int spr = inst_mask(inst);
    const KwikSprite* s = kwik_sprite_at(spr);
    if (!s) return b;
    double xs = 1.0, ys = 1.0, ang = 0.0;
    auto ix = inst->vars.find("image_xscale");
    auto iy = inst->vars.find("image_yscale");
    auto ia = inst->vars.find("image_angle");
    if (ix != inst->vars.end()) xs = (double)ix->second;
    if (iy != inst->vars.end()) ys = (double)iy->second;
    if (ia != inst->vars.end()) ang = (double)ia->second;
    double x0 = (s->bbox_left - s->origin_x) * xs;
    double x1 = (s->bbox_right + 1 - s->origin_x) * xs;
    double y0 = (s->bbox_top - s->origin_y) * ys;
    double y1 = (s->bbox_bottom + 1 - s->origin_y) * ys;
    b.lx0 = std::min(x0, x1);
    b.lx1 = std::max(x0, x1);
    b.ly0 = std::min(y0, y1);
    b.ly1 = std::max(y0, y1);
    b.x = px;
    b.y = py;
    if (std::fabs(ang) > 0.0001) {
        double rad = ang * M_PI / 180.0;
        b.cs = std::cos(rad);
        b.sn = std::sin(rad);
        b.rot = true;
    }
    b.valid = true;
    return b;
}

static void box_corners(const KBox& b, double cx[4], double cy[4]) {
    double xs[4] = {b.lx0, b.lx1, b.lx1, b.lx0};
    double ys[4] = {b.ly0, b.ly0, b.ly1, b.ly1};
    for (int i = 0; i < 4; ++i) {
        cx[i] = b.x + b.cs * xs[i] + b.sn * ys[i];
        cy[i] = b.y - b.sn * xs[i] + b.cs * ys[i];
    }
}

static bool boxes_hit(const KBox& a, const KBox& b) {
    if (!a.valid || !b.valid) return false;
    if (!a.rot && !b.rot) {
        return a.x + a.lx0 < b.x + b.lx1 && a.x + a.lx1 > b.x + b.lx0 &&
               a.y + a.ly0 < b.y + b.ly1 && a.y + a.ly1 > b.y + b.ly0;
    }
    double ax[4], ay[4], bx[4], by[4];
    box_corners(a, ax, ay);
    box_corners(b, bx, by);
    const KBox* boxes[2] = {&a, &b};
    for (int bi = 0; bi < 2; ++bi) {
        double axes[2][2] = {{boxes[bi]->cs, -boxes[bi]->sn}, {boxes[bi]->sn, boxes[bi]->cs}};
        for (int k = 0; k < 2; ++k) {
            double amin = 1e30, amax = -1e30, bmin = 1e30, bmax = -1e30;
            for (int i = 0; i < 4; ++i) {
                double pa = ax[i] * axes[k][0] + ay[i] * axes[k][1];
                double pb = bx[i] * axes[k][0] + by[i] * axes[k][1];
                amin = std::min(amin, pa);
                amax = std::max(amax, pa);
                bmin = std::min(bmin, pb);
                bmax = std::max(bmax, pb);
            }
            if (amax <= bmin || bmax <= amin) return false;
        }
    }
    return true;
}

static bool point_in_box(const KBox& b, double px, double py) {
    if (!b.valid) return false;
    double dx = px - b.x, dy = py - b.y;
    double lx = b.cs * dx - b.sn * dy;
    double ly = b.sn * dx + b.cs * dy;
    return lx >= b.lx0 && lx < b.lx1 && ly >= b.ly0 && ly < b.ly1;
}

static const MaskSet* inst_masks(Instance* inst) {
    return kwik_sprite_masks(inst_mask(inst));
}

static bool point_in_instance(Instance* inst, double at_x, double at_y, double px, double py) {
    int spr_idx = inst_mask(inst);
    const KwikSprite* s = kwik_sprite_at(spr_idx);
    if (!s) return false;
    double xs = 1.0, ys = 1.0, ang = 0.0, img = 0.0;
    auto ix = inst->vars.find("image_xscale");
    auto iy = inst->vars.find("image_yscale");
    auto ia = inst->vars.find("image_angle");
    auto ii = inst->vars.find("image_index");
    if (ix != inst->vars.end()) xs = (double)ix->second;
    if (iy != inst->vars.end()) ys = (double)iy->second;
    if (ia != inst->vars.end()) ang = (double)ia->second;
    if (ii != inst->vars.end()) img = (double)ii->second;
    if (std::fabs(xs) < 0.0001 || std::fabs(ys) < 0.0001) return false;

    double dx = px - at_x;
    double dy = py - at_y;
    if (std::fabs(ang) > 0.0001) {
        double rad = ang * M_PI / 180.0;
        double cs = std::cos(rad), sn = std::sin(rad);
        double rx = cs * dx - sn * dy;
        double ry = sn * dx + cs * dy;
        dx = rx;
        dy = ry;
    }
    double local_x = dx / xs + s->origin_x;
    double local_y = dy / ys + s->origin_y;
    int lx = (int)local_x;
    int ly = (int)local_y;
    if (local_x < 0 || local_y < 0 || lx >= s->width || ly >= s->height) return false;

    const MaskSet* ms = kwik_sprite_masks(spr_idx);
    if (ms) {
        int frame = ((int)img % ms->count + ms->count) % ms->count;
        const unsigned char* mask = ms->data + (size_t)frame * ms->rowbytes * ms->h;
        if (lx >= ms->w || ly >= ms->h) return false;
        return (mask[ly * ms->rowbytes + (lx >> 3)] & (1 << (7 - (lx & 7)))) != 0;
    }
    return true;
}

static bool instances_hit(Instance* a, double ax, double ay, Instance* b) {
    KBox ka = make_box(a, ax, ay);
    KBox kb = make_box(b, b->x, b->y);
    if (!ka.valid || !kb.valid) return false;
    double al, at, ar, ab, bl, bt, br, bb;
    {
        double cx[4], cy[4];
        box_corners(ka, cx, cy);
        al = ar = cx[0];
        at = ab = cy[0];
        for (int i = 1; i < 4; ++i) {
            al = std::min(al, cx[i]);
            ar = std::max(ar, cx[i]);
            at = std::min(at, cy[i]);
            ab = std::max(ab, cy[i]);
        }
        box_corners(kb, cx, cy);
        bl = br = cx[0];
        bt = bb = cy[0];
        for (int i = 1; i < 4; ++i) {
            bl = std::min(bl, cx[i]);
            br = std::max(br, cx[i]);
            bt = std::min(bt, cy[i]);
            bb = std::max(bb, cy[i]);
        }
    }
    if (al >= br || bl >= ar || at >= bb || bt >= ab) return false;

    bool precise_a = inst_masks(a) != nullptr;
    bool precise_b = inst_masks(b) != nullptr;
    if (!precise_a && !precise_b) return boxes_hit(ka, kb);

    int sx = (int)std::floor(std::max(al, bl));
    int ex = (int)std::ceil(std::min(ar, br));
    int sy = (int)std::floor(std::max(at, bt));
    int ey = (int)std::ceil(std::min(ab, bb));
    for (int py = sy; py < ey; ++py) {
        for (int px = sx; px < ex; ++px) {
            double wx = px + 0.5, wy = py + 0.5;
            if (!point_in_instance(a, ax, ay, wx, wy)) continue;
            if (!point_in_instance(b, b->x, b->y, wx, wy)) continue;
            return true;
        }
    }
    return false;
}

bool inst_bbox(Instance* inst, double px, double py, double& l, double& t, double& r, double& b) {
    KBox box = make_box(inst, px, py);
    if (!box.valid) return false;
    double cx[4], cy[4];
    box_corners(box, cx, cy);
    l = r = cx[0];
    t = b = cy[0];
    for (int i = 1; i < 4; ++i) {
        l = std::min(l, cx[i]);
        r = std::max(r, cx[i]);
        t = std::min(t, cy[i]);
        b = std::max(b, cy[i]);
    }
    return true;
}

static bool boxes_overlap(double al, double at, double ar, double ab,
                          double bl, double bt, double br, double bb) {
    return al < br && ar > bl && at < bb && ab > bt;
}

Instance* collision_at(Instance* self, double px, double py, int who, bool) {
    if (!self) return nullptr;
    for (auto& sp : g_instances) {
        Instance* other = sp.get();
        if (other == self || !inst_matches(other, who)) continue;
        if (instances_hit(self, px, py, other)) return other;
    }
    return nullptr;
}

static Instance* collision_point_at(Instance* self, double px, double py, int who) {
    for (auto& sp : g_instances) {
        Instance* other = sp.get();
        if (other == self || !inst_matches(other, who)) continue;
        if (inst_masks(other)) {
            if (point_in_instance(other, other->x, other->y, px, py)) return other;
        } else {
            KBox b = make_box(other, other->x, other->y);
            if (point_in_box(b, px, py)) return other;
        }
    }
    return nullptr;
}

static Instance* collision_rect_at(Instance* self, double x1, double y1, double x2, double y2,
                                   int who) {
    KBox a;
    a.lx0 = std::min(x1, x2) ;
    a.lx1 = std::max(x1, x2);
    a.ly0 = std::min(y1, y2);
    a.ly1 = std::max(y1, y2);
    a.x = 0;
    a.y = 0;
    a.valid = true;
    for (auto& sp : g_instances) {
        Instance* other = sp.get();
        if (other == self || !inst_matches(other, who)) continue;
        KBox b = make_box(other, other->x, other->y);
        if (boxes_hit(a, b)) return other;
    }
    return nullptr;
}

static Instance* collision_line_at(Instance* self, double x1, double y1, double x2, double y2,
                                   int who) {
    int steps = (int)std::ceil(std::max(std::fabs(x2 - x1), std::fabs(y2 - y1)) / 4.0) + 1;
    for (int i = 0; i <= steps; ++i) {
        double f = steps == 0 ? 0.0 : (double)i / steps;
        Instance* hit = collision_point_at(self, x1 + (x2 - x1) * f, y1 + (y2 - y1) * f, who);
        if (hit) return hit;
    }
    return nullptr;
}

static void init_instance_vars(Instance* inst, const ObjectDef* def) {
    inst->var("image_index") = Value(0.0);
    inst->var("image_speed") = Value(1.0);
    inst->var("image_xscale") = Value(1.0);
    inst->var("image_yscale") = Value(1.0);
    inst->var("image_angle") = Value(0.0);
    inst->var("image_alpha") = Value(1.0);
    inst->var("image_blend") = Value(16777215.0);
    inst->var("gravity") = Value(0.0);
    inst->var("gravity_direction") = Value(270.0);
    inst->var("friction") = Value(0.0);
    inst->var("solid") = Value(0.0);
    inst->var("mask_index") = Value(def ? (double)def->mask_index : -1.0);
    inst->var("sprite_index") = Value(def ? (double)def->sprite_index : -1.0);
    Value alarms;
    alarms.type = Value::ARR;
    alarms.arr = std::make_shared<GmlArray>();
    alarms.arr->items.assign(12, Value(-1.0));
    inst->var("alarm") = alarms;
    if (def) {
        inst->visible = def->visible != 0;
        inst->persistent = def->persistent != 0;
        inst->depth = def->depth;
    }
}

enum EvKind {
    EVK_CREATE, EVK_DESTROY, EVK_CLEANUP, EVK_STEP, EVK_STEP_BEGIN, EVK_STEP_END,
    EVK_DRAW, EVK_DRAW_GUI, EVK_DRAW_BEGIN, EVK_DRAW_END, EVK_DRAW_GUI_BEGIN, EVK_DRAW_GUI_END,
    EVK_DRAW_PRE, EVK_DRAW_POST, EVK_ALARM, EVK_ROOM_START, EVK_ROOM_END, EVK_ANIM_END,
    EVK_GAME_START, EVK_USER, EVK_PRE_CREATE, EVK_DRAW_RESIZE, EVK_ASYNC_SAVE_LOAD,
    EVK_ASYNC_SYSTEM, EVK_ASYNC_WEB, EVK_OUTSIDE_ROOM, EVK_PATH_ENDED
};

static ScriptFn find_event(int obj, int kind, int sub, int* owner_out = nullptr) {
    int guard = 0;
    while (obj >= 0 && obj < g_object_count_rt && guard++ < 128) {
        const ObjectDef& d = g_objects_rt[obj];
        ScriptFn fn = nullptr;
        switch (kind) {
            case EVK_CREATE: fn = d.create; break;
            case EVK_PRE_CREATE: fn = d.pre_create; break;
            case EVK_DESTROY: fn = d.destroy; break;
            case EVK_CLEANUP: fn = d.cleanup; break;
            case EVK_STEP: fn = d.step; break;
            case EVK_STEP_BEGIN: fn = d.step_begin; break;
            case EVK_STEP_END: fn = d.step_end; break;
            case EVK_DRAW: fn = d.draw; break;
            case EVK_DRAW_GUI: fn = d.draw_gui; break;
            case EVK_DRAW_BEGIN: fn = d.draw_begin; break;
            case EVK_DRAW_END: fn = d.draw_end; break;
            case EVK_DRAW_GUI_BEGIN: fn = d.draw_gui_begin; break;
            case EVK_DRAW_GUI_END: fn = d.draw_gui_end; break;
            case EVK_DRAW_PRE: fn = d.draw_pre; break;
            case EVK_DRAW_POST: fn = d.draw_post; break;
            case EVK_ALARM: fn = (sub >= 0 && sub < 12) ? d.alarm[sub] : nullptr; break;
            case EVK_ROOM_START: fn = d.room_start; break;
            case EVK_ROOM_END: fn = d.room_end; break;
            case EVK_ANIM_END: fn = d.anim_end; break;
            case EVK_GAME_START: fn = d.game_start; break;
            case EVK_DRAW_RESIZE: fn = d.draw_resize; break;
            case EVK_ASYNC_SAVE_LOAD: fn = d.async_save_load; break;
            case EVK_ASYNC_SYSTEM: fn = d.async_system; break;
            case EVK_ASYNC_WEB: fn = d.async_web; break;
            case EVK_OUTSIDE_ROOM: fn = d.outside_room; break;
            case EVK_PATH_ENDED: fn = d.path_ended; break;
            case EVK_USER: fn = (sub >= 0 && sub < 16) ? d.user[sub] : nullptr; break;
        }
        if (fn) {
            if (owner_out) *owner_out = obj;
            return fn;
        }
        obj = d.parent_index;
    }
    return nullptr;
}

struct EvCtx {
    int kind;
    int sub;
    int owner;
};
static std::vector<EvCtx> g_ev_stack;

static void call_event(Instance* inst, int kind, int sub, ScriptFn fn, int owner) {
    g_ev_stack.push_back({kind, sub, owner});
    fn(inst, nullptr, 0);
    g_ev_stack.pop_back();
}

static void fire(Instance* inst, int kind, int sub) {
    if (!inst || inst->dead) return;
    static const char* trace = std::getenv("KWIK_TRACE_OBJ");
    if (trace && inst->object_index >= 0 && inst->object_index < g_object_count_rt &&
        !std::strcmp(g_objects_rt[inst->object_index].name, trace)) {
        auto gv = [&](const char* n) -> double {
            auto it = inst->vars.find(n);
            return it == inst->vars.end() ? -999 : (double)it->second;
        };
        std::fprintf(stderr,
                     "[trace] #%d %s ev=%d sub=%d active=%g frame=%g maxframe=%g target=%g spr=%g imgidx=%g imgspd=%g\n",
                     inst->id, trace, kind, sub, gv("active"), gv("frame"), gv("maxframe"),
                     gv("target"), gv("sprite_index"), gv("image_index"), gv("image_speed"));
    }
    int owner = -1;
    ScriptFn fn = find_event(inst->object_index, kind, sub, &owner);
    if (fn) call_event(inst, kind, sub, fn, owner);
}

void kwik_fire_event(Instance* inst, int kind, int sub) { fire(inst, kind, sub); }

GMLFN(event_inherited) {
    (void)args; (void)argc;
    if (!self || g_ev_stack.empty()) return Value();
    EvCtx ctx = g_ev_stack.back();
    if (ctx.owner < 0 || ctx.owner >= g_object_count_rt) return Value();
    int parent = g_objects_rt[ctx.owner].parent_index;
    if (parent < 0) return Value();
    int owner = -1;
    ScriptFn fn = find_event(parent, ctx.kind, ctx.sub, &owner);
    if (fn) call_event(self, ctx.kind, ctx.sub, fn, owner);
    return Value();
}

static void dispatch_async() {
    if (g_async_queue.empty()) return;
    std::vector<std::pair<int, int>> queue = std::move(g_async_queue);
    g_async_queue.clear();
    for (auto& ev : queue) {
        int evk = ev.first == ASYNC_SAVE_LOAD_EV ? EVK_ASYNC_SAVE_LOAD
                  : ev.first == ASYNC_SYSTEM_EV ? EVK_ASYNC_SYSTEM
                                                : EVK_ASYNC_WEB;
        g_async_load_map = ev.second;
        size_t n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (inst->dead || !inst->active) continue;
            fire(inst, evk, 0);
        }
        g_async_load_map = -1;
    }
}

double kwik_room_speed() { return g_room_speed_v; }
int kwik_current_room() { return g_current_room; }

void kwik_room_goto(int index) {
    if (index >= 0 && index < g_room_count_rt) g_pending_room = index;
}

Value kwik_create_instance(int obj_index, double x, double y, double depth, bool use_depth) {
    if (obj_index < 0 || obj_index >= g_object_count_rt) return Value(-4.0);
    auto sp = std::make_shared<Instance>();
    sp->x = x; sp->y = y;
    sp->xstart = x; sp->ystart = y;
    sp->xprevious = x; sp->yprevious = y;
    sp->id = g_next_instance_id++;
    sp->object_index = obj_index;
    init_instance_vars(sp.get(), &g_objects_rt[obj_index]);
    if (use_depth) sp->depth = depth;
    g_instances.push_back(sp);
    Instance* raw = sp.get();
    fire(raw, EVK_PRE_CREATE, 0);
    fire(raw, EVK_CREATE, 0);
    return Value((double)raw->id);
}

void kwik_destroy_instance(Instance* inst, bool run_event) {
    if (!inst || inst->dead) return;
    if (run_event) fire(inst, EVK_DESTROY, 0);
    fire(inst, EVK_CLEANUP, 0);
    inst->dead = true;
}

Value kwik_register_struct_value(std::shared_ptr<Instance> s) {
    s->is_struct = true;
    s->id = g_next_struct_id++;
    g_structs[s->id] = s;
    Value v;
    v.type = Value::OBJ;
    v.obj = s;
    return v;
}

Instance* kwik_struct_by_id(int id) {
    auto it = g_structs.find(id);
    return it == g_structs.end() ? nullptr : it->second.get();
}

Value kwik_this(Instance* self) {
    if (!self) return Value(-4.0);
    if (self->is_struct) {
        Value v;
        v.type = Value::OBJ;
        v.obj = self->shared_from_this();
        return v;
    }
    return Value((double)self->id);
}

Value kwik_other(Instance* self) {
    (void)self;
    if (!g_other_ptr) return Value(-4.0);
    if (g_other_ptr->is_struct) {
        Value v;
        v.type = Value::OBJ;
        v.obj = g_other_ptr->shared_from_this();
        return v;
    }
    return Value((double)g_other_ptr->id);
}

Value kwik_new_object(Instance* self, const Value* args, int argc) {
    auto sp = std::make_shared<Instance>();
    Value out = kwik_register_struct_value(sp);
    if (argc > 0 && args[0].type == Value::FN && args[0].fn) {
        Instance* saved_other = g_other_ptr;
        g_other_ptr = self;
        args[0].fn(sp.get(), argc > 1 ? args + 1 : nullptr, argc - 1);
        g_other_ptr = saved_other;
    }
    return out;
}

Value kwik_call_value(Instance* self, const Value& fnval, const Value* args, int argc) {
    if (fnval.type == Value::FN && fnval.fn) {
        Instance* target = self;
        if (fnval.fn_bind >= 0) {
            Instance* b = kwik_instance_by_id(fnval.fn_bind);
            if (b) target = b;
        }
        return fnval.fn(target, args, argc);
    }
    return Value();
}

Value kwik_call_method(Instance* self, const Value& fnval, const Value& target,
                       const Value* args, int argc) {
    Instance* callee = self;
    if (fnval.type == Value::FN && fnval.fn_bind >= 0) {
        Instance* b = kwik_instance_by_id(fnval.fn_bind);
        if (b) callee = b;
    } else {
        Instance* t = kwik_resolve_target(self, target);
        if (t) callee = t;
    }
    if (fnval.type == Value::FN && fnval.fn) return fnval.fn(callee, args, argc);
    return Value();
}

static Value scope_get_special(Instance* inst, const char* name, bool& handled) {
    handled = true;
    if (!std::strcmp(name, "x")) return Value(inst->x);
    if (!std::strcmp(name, "y")) return Value(inst->y);
    if (!std::strcmp(name, "id")) return kwik_this(inst);
    if (!std::strcmp(name, "object_index")) return Value((double)inst->object_index);
    if (!std::strcmp(name, "visible")) return Value(inst->visible);
    if (!std::strcmp(name, "persistent")) return Value(inst->persistent);
    if (!std::strcmp(name, "depth")) return Value(inst->depth);
    if (!std::strcmp(name, "xprevious")) return Value(inst->xprevious);
    if (!std::strcmp(name, "yprevious")) return Value(inst->yprevious);
    if (!std::strcmp(name, "xstart")) return Value(inst->xstart);
    if (!std::strcmp(name, "ystart")) return Value(inst->ystart);
    if (!std::strcmp(name, "speed")) return Value(inst->m_speed);
    if (!std::strcmp(name, "direction")) return Value(inst->m_dir);
    if (!std::strcmp(name, "hspeed")) return Value(inst->m_hs);
    if (!std::strcmp(name, "vspeed")) return Value(inst->m_vs);
    if (!std::strcmp(name, "bbox_left") || !std::strcmp(name, "bbox_right") ||
        !std::strcmp(name, "bbox_top") || !std::strcmp(name, "bbox_bottom")) {
        double l, t, r, b;
        if (!inst_bbox(inst, inst->x, inst->y, l, t, r, b)) return Value(inst->x);
        if (!std::strcmp(name, "bbox_left")) return Value(l);
        if (!std::strcmp(name, "bbox_right")) return Value(r - 1);
        if (!std::strcmp(name, "bbox_top")) return Value(t);
        return Value(b - 1);
    }
    if (!std::strcmp(name, "sprite_width") || !std::strcmp(name, "sprite_height")) {
        int spr = inst_sprite(inst);
        double xs = 1, ys = 1;
        auto ix = inst->vars.find("image_xscale");
        auto iy = inst->vars.find("image_yscale");
        if (ix != inst->vars.end()) xs = (double)ix->second;
        if (iy != inst->vars.end()) ys = (double)iy->second;
        if (const KwikSprite* sd = kwik_sprite_at(spr)) {
            if (!std::strcmp(name, "sprite_width")) return Value(sd->width * xs);
            return Value(sd->height * ys);
        }
        return Value(0.0);
    }
    if (!std::strcmp(name, "sprite_xoffset") || !std::strcmp(name, "sprite_yoffset")) {
        int spr = inst_sprite(inst);
        if (const KwikSprite* sd = kwik_sprite_at(spr)) {
            if (!std::strcmp(name, "sprite_xoffset")) return Value((double)sd->origin_x);
            return Value((double)sd->origin_y);
        }
        return Value(0.0);
    }
    if (!std::strcmp(name, "image_number")) {
        int spr = inst_sprite(inst);
        if (const KwikSprite* sd = kwik_sprite_at(spr)) return Value((double)sd->frame_count);
        return Value(0.0);
    }
    handled = false;
    return Value();
}

static bool scope_set_special(Instance* inst, const char* name, const Value& v) {
    if (!std::strcmp(name, "x")) { inst->x = (double)v; return true; }
    if (!std::strcmp(name, "y")) { inst->y = (double)v; return true; }
    if (!std::strcmp(name, "visible")) { inst->visible = gml_truthy(v); return true; }
    if (!std::strcmp(name, "persistent")) { inst->persistent = gml_truthy(v); return true; }
    if (!std::strcmp(name, "depth")) { inst->depth = (double)v; return true; }
    if (!std::strcmp(name, "xprevious")) { inst->xprevious = (double)v; return true; }
    if (!std::strcmp(name, "yprevious")) { inst->yprevious = (double)v; return true; }
    if (!std::strcmp(name, "xstart")) { inst->xstart = (double)v; return true; }
    if (!std::strcmp(name, "ystart")) { inst->ystart = (double)v; return true; }
    if (!std::strcmp(name, "speed")) { inst->m_speed = (double)v; sync_from_polar(inst); return true; }
    if (!std::strcmp(name, "direction")) { inst->m_dir = (double)v; sync_from_polar(inst); return true; }
    if (!std::strcmp(name, "hspeed")) { inst->m_hs = (double)v; sync_from_component(inst); return true; }
    if (!std::strcmp(name, "vspeed")) { inst->m_vs = (double)v; sync_from_component(inst); return true; }
    return false;
}

static Value inst_get_raw(Instance* inst, const char* name) {
    bool handled;
    Value v = scope_get_special(inst, name, handled);
    if (handled) return v;
    auto it = inst->vars.find(name);
    if (it != inst->vars.end()) return it->second;
    return kwik_builtin_get(inst, name);
}

static void inst_set_raw(Instance* inst, const char* name, const Value& v) {
    if (scope_set_special(inst, name, v)) return;
    inst->var(name) = v;
}

Value kwik_scope_get(Instance* self, int spec, const char* name) {
    switch (spec) {
        case -1: case -9: return self ? inst_get_raw(self, name) : Value();
        case -2: return g_other_ptr ? inst_get_raw(g_other_ptr, name) : Value();
        case -5: return global_var(name);
        case -6: return kwik_builtin_get(self, name);
        default: {
            if (spec >= 0) {
                Instance* t = kwik_first_instance(spec);
                return t ? inst_get_raw(t, name) : Value();
            }
            return self ? inst_get_raw(self, name) : Value();
        }
    }
}

void kwik_scope_set(Instance* self, int spec, const char* name, const Value& v) {
    switch (spec) {
        case -1: case -9: if (self) inst_set_raw(self, name, v); return;
        case -2: if (g_other_ptr) inst_set_raw(g_other_ptr, name, v); return;
        case -5: global_var(name) = v; return;
        case -6: kwik_builtin_set(self, name, v); return;
        default:
            if (spec >= 0) {
                for (auto& sp : g_instances)
                    if (inst_matches(sp.get(), spec)) inst_set_raw(sp.get(), name, v);
                return;
            }
            if (self) inst_set_raw(self, name, v);
    }
}

Value kwik_inst_get(Instance* self, const Value& who, const char* name) {
    if (who.type == Value::OBJ && who.obj) return inst_get_raw(who.obj.get(), name);
    int w = (int)(double)who;
    if (w == -5) return global_var(name);
    if (w == -6) return kwik_builtin_get(self, name);
    Instance* t = kwik_resolve_target(self, who);
    return t ? inst_get_raw(t, name) : Value();
}

void kwik_inst_set(Instance* self, const Value& who, const char* name, const Value& v) {
    if (who.type == Value::OBJ && who.obj) { inst_set_raw(who.obj.get(), name, v); return; }
    int w = (int)(double)who;
    if (w == -5) { global_var(name) = v; return; }
    if (w == -6) { kwik_builtin_set(self, name, v); return; }
    if (w >= 0 && w < 100000) {
        for (auto& sp : g_instances)
            if (inst_matches(sp.get(), w)) inst_set_raw(sp.get(), name, v);
        return;
    }
    Instance* t = kwik_resolve_target(self, who);
    if (t) inst_set_raw(t, name, v);
}

Value kwik_array_elem(const Value& slot, int idx);
void kwik_array_store(Value& slot, int idx, const Value& v);
Value kwik_array_wslot(Value& slot, int idx);

static Value* scope_slot(Instance* self, int spec, const char* name) {
    switch (spec) {
        case -1: case -9: case -6: return self ? &self->var(name) : nullptr;
        case -2: return g_other_ptr ? &g_other_ptr->var(name) : nullptr;
        case -5: return &global_var(name);
        default: {
            if (spec >= 0) {
                Instance* t = kwik_first_instance(spec);
                return t ? &t->var(name) : nullptr;
            }
            return self ? &self->var(name) : nullptr;
        }
    }
}

static Value builtin_array_get(Instance* self, const char* name, int idx, bool& handled) {
    handled = true;
    if (!std::strcmp(name, "view_camera")) return Value((double)(idx >= 0 && idx < 8 ? g_view_camera[idx] : 0));
    if (!std::strcmp(name, "view_visible")) return Value((double)(idx >= 0 && idx < 8 ? g_view_visible[idx] : 0));
    if (!std::strcmp(name, "view_xview")) { Camera& c = g_cameras[g_view_camera[0]]; return Value(c.x); }
    if (!std::strcmp(name, "view_yview")) { Camera& c = g_cameras[g_view_camera[0]]; return Value(c.y); }
    if (!std::strcmp(name, "view_wview")) { Camera& c = g_cameras[g_view_camera[0]]; return Value(c.w); }
    if (!std::strcmp(name, "view_hview")) { Camera& c = g_cameras[g_view_camera[0]]; return Value(c.h); }
    if (!std::strcmp(name, "view_xport") || !std::strcmp(name, "view_yport")) return Value(0.0);
    if (!std::strcmp(name, "view_wport")) return Value((double)render_gui_width());
    if (!std::strcmp(name, "view_hport")) return Value((double)render_gui_height());
    (void)self;
    handled = false;
    return Value();
}

static bool builtin_array_set(Instance* self, const char* name, int idx, const Value& v) {
    (void)self;
    if (!std::strcmp(name, "view_camera")) {
        if (idx >= 0 && idx < 8) g_view_camera[idx] = (int)(double)v;
        return true;
    }
    if (!std::strcmp(name, "view_visible")) {
        if (idx >= 0 && idx < 8) g_view_visible[idx] = gml_truthy(v) ? 1 : 0;
        return true;
    }
    if (!std::strcmp(name, "view_xview")) { g_cameras[g_view_camera[0]].x = (double)v; return true; }
    if (!std::strcmp(name, "view_yview")) { g_cameras[g_view_camera[0]].y = (double)v; return true; }
    if (!std::strcmp(name, "view_wview")) { g_cameras[g_view_camera[0]].w = (double)v; return true; }
    if (!std::strcmp(name, "view_hview")) { g_cameras[g_view_camera[0]].h = (double)v; return true; }
    return false;
}

Value kwik_array_get(Instance* self, int spec, const char* name, const Value& idx) {
    int i = (int)(double)idx;
    bool handled;
    Value bv = builtin_array_get(self, name, i, handled);
    if (handled) return bv;
    Value* slot = scope_slot(self, spec, name);
    if (!slot) return Value();
    return kwik_array_elem(*slot, i);
}

Value kwik_array_get_at(Instance* self, const Value& who, const char* name, const Value& idx) {
    if (who.type == Value::OBJ && who.obj)
        return kwik_array_elem(who.obj->var(name), (int)(double)idx);
    int w = (int)(double)who;
    if (w == -5 || w == -1 || w == -2 || w == -6 || w == -9)
        return kwik_array_get(self, w == -9 ? -1 : w, name, idx);
    Instance* t = kwik_resolve_target(self, who);
    if (!t) return Value();
    return kwik_array_elem(t->var(name), (int)(double)idx);
}

void kwik_array_set(Instance* self, int spec, const char* name, const Value& idx, const Value& v) {
    int i = (int)(double)idx;
    if (builtin_array_set(self, name, i, v)) return;
    Value* slot = scope_slot(self, spec, name);
    if (!slot) return;
    kwik_array_store(*slot, i, v);
}

void kwik_array_set_at(Instance* self, const Value& who, const char* name, const Value& idx,
                       const Value& v) {
    if (who.type == Value::OBJ && who.obj) {
        kwik_array_store(who.obj->var(name), (int)(double)idx, v);
        return;
    }
    int w = (int)(double)who;
    if (w == -5 || w == -1 || w == -2 || w == -6 || w == -9) {
        kwik_array_set(self, w == -9 ? -1 : w, name, idx, v);
        return;
    }
    Instance* t = kwik_resolve_target(self, who);
    if (t) kwik_array_store(t->var(name), (int)(double)idx, v);
}

Value kwik_array_wref(Instance* self, int spec, const char* name, const Value& idx) {
    Value* slot = scope_slot(self, spec, name);
    if (!slot) return Value();
    return kwik_array_wslot(*slot, (int)(double)idx);
}

Value kwik_array_wref_at(Instance* self, const Value& who, const char* name, const Value& idx) {
    if (who.type == Value::OBJ && who.obj)
        return kwik_array_wslot(who.obj->var(name), (int)(double)idx);
    Instance* t = kwik_resolve_target(self, who);
    if (!t) return Value();
    return kwik_array_wslot(t->var(name), (int)(double)idx);
}

struct EnvFrame {
    Instance* saved_self;
    Instance* saved_other;
    std::vector<Instance*> list;
    size_t index = 0;
};
static std::vector<EnvFrame> g_env_stack;

void kwik_env_push(Instance* self, const Value& target) {
    EnvFrame f;
    f.saved_self = self;
    f.saved_other = g_other_ptr;
    if (target.type == Value::OBJ && target.obj) {
        f.list.push_back(target.obj.get());
    } else {
        int w = (int)(double)target;
        if (w == -1) {
            if (self) f.list.push_back(self);
        } else if (w == -2) {
            Instance* o = !g_env_stack.empty() ? g_env_stack.back().saved_self : g_other_ptr;
            if (o) f.list.push_back(o);
        } else if (w == -4) {
        } else if (w >= 100000) {
            Instance* t = kwik_instance_by_id(w);
            if (t && t->active && !t->dead) f.list.push_back(t);
        } else if (w >= 0 || w == -3) {
            f.list = kwik_instances_matching(self, w >= 0 ? w : -3);
            if (f.list.size() > 2) std::reverse(f.list.begin(), f.list.end());
        }
    }
    g_other_ptr = self;
    g_env_stack.push_back(std::move(f));
}

Instance* kwik_env_first() {
    if (g_env_stack.empty()) return nullptr;
    EnvFrame& f = g_env_stack.back();
    f.index = 0;
    if (f.list.empty()) return nullptr;
    return f.list[0];
}

Instance* kwik_env_next() {
    if (g_env_stack.empty()) return nullptr;
    EnvFrame& f = g_env_stack.back();
    while (f.index + 1 < f.list.size()) {
        f.index++;
        Instance* n = f.list[f.index];
        if (!n->dead && n->active) return n;
    }
    return nullptr;
}

Instance* kwik_env_pop() {
    if (g_env_stack.empty()) return nullptr;
    EnvFrame f = std::move(g_env_stack.back());
    g_env_stack.pop_back();
    g_other_ptr = f.saved_other;
    return f.saved_self;
}

Value kwik_builtin_get(Instance* self, const char* name) {
    if (!std::strcmp(name, "room")) return Value((double)g_current_room);
    if (!std::strcmp(name, "room_speed")) return Value(g_room_speed_v);
    if (!std::strcmp(name, "room_width")) return Value((double)room_width_cur());
    if (!std::strcmp(name, "room_height")) return Value((double)room_height_cur());
    if (!std::strcmp(name, "room_first")) return Value(0.0);
    if (!std::strcmp(name, "room_last")) return Value((double)(g_room_count_rt - 1));
    if (!std::strcmp(name, "fps")) return Value(g_room_speed_v);
    if (!std::strcmp(name, "fps_real")) return Value(1000.0);
    if (!std::strcmp(name, "current_time")) return Value(now_ms());
    if (!std::strcmp(name, "delta_time")) return Value(1000000.0 / g_room_speed_v);
    if (!std::strcmp(name, "os_type")) return Value(0.0);
    if (!std::strcmp(name, "os_browser")) return Value(-1.0);
    if (!std::strcmp(name, "undefined")) return Value();
    if (!std::strcmp(name, "pointer_null")) return Value();
    if (!std::strcmp(name, "all")) return Value(-3.0);
    if (!std::strcmp(name, "noone")) return Value(-4.0);
    if (!std::strcmp(name, "other")) return kwik_other(self);
    if (!std::strcmp(name, "self")) return kwik_this(self);
    if (!std::strcmp(name, "instance_count")) {
        int n = 0;
        for (auto& sp : g_instances)
            if (!sp->dead && sp->active) ++n;
        return Value((double)n);
    }
    if (!std::strcmp(name, "mouse_x")) {
        Camera& c = g_cameras[g_view_camera[0]];
        return Value(c.x + render_mouse_x() * c.w / std::max(1, render_gui_width()));
    }
    if (!std::strcmp(name, "mouse_y")) {
        Camera& c = g_cameras[g_view_camera[0]];
        return Value(c.y + render_mouse_y() * c.h / std::max(1, render_gui_height()));
    }
    if (!std::strcmp(name, "working_directory")) return Value(g_game_dir + "/");
    if (!std::strcmp(name, "program_directory")) return Value(g_game_dir + "/");
    if (!std::strcmp(name, "view_current")) return Value(0.0);
    if (!std::strcmp(name, "keyboard_string")) return Value("");
    if (!std::strcmp(name, "game_save_id"))
        return Value(g_save_dir.empty() ? g_game_dir + "/" : g_save_dir + "/");
    if (!std::strcmp(name, "argument_count")) return Value(0.0);
    if (!std::strcmp(name, "view_enabled")) return Value(1.0);
    if (!std::strcmp(name, "async_load")) return Value((double)g_async_load_map);
    if (!std::strcmp(name, "application_surface")) return Value(0.0);
    if (self) {
        bool handled;
        Value v = scope_get_special(self, name, handled);
        if (handled) return v;
        if (self->has(name)) return self->var(name);
    }
    if (g_dummy_instance && g_dummy_instance->has(name)) return g_dummy_instance->var(name);
    return kwik_missing(self, (std::string("builtin var ") + name).c_str());
}

void kwik_builtin_set(Instance* self, const char* name, const Value& v) {
    if (!std::strcmp(name, "room_speed")) { g_room_speed_v = (double)v; return; }
    if (!std::strcmp(name, "room")) { kwik_room_goto((int)(double)v); return; }
    if (!std::strcmp(name, "keyboard_string")) return;
    if (self) inst_set_raw(self, name, v);
}

GMLFN(event_user) {
    (void)argc;
    int sub = argc > 0 ? (int)(double)args[0] : 0;
    fire(self, EVK_USER, sub);
    return Value();
}

GMLFN(event_perform) {
    if (argc < 2) return Value();
    int type = (int)(double)args[0];
    int sub = (int)(double)args[1];
    switch (type) {
        case 0: fire(self, EVK_CREATE, 0); break;
        case 1: fire(self, EVK_DESTROY, 0); break;
        case 2: fire(self, EVK_ALARM, sub); break;
        case 3:
            fire(self, sub == 0 ? EVK_STEP : sub == 1 ? EVK_STEP_BEGIN : EVK_STEP_END, 0);
            break;
        case 7:
            if (sub >= 10 && sub <= 25) fire(self, EVK_USER, sub - 10);
            else if (sub == 4) fire(self, EVK_ROOM_START, 0);
            else if (sub == 5) fire(self, EVK_ROOM_END, 0);
            else if (sub == 7) fire(self, EVK_ANIM_END, 0);
            break;
        case 8:
            if (sub == 64) fire(self, EVK_DRAW_GUI, 0);
            else fire(self, EVK_DRAW, 0);
            break;
        default: break;
    }
    return Value();
}

GMLFN(instance_exists) {
    (void)self; (void)argc;
    if (argc < 1) return Value(0.0);
    if (args[0].type == Value::OBJ) return Value(args[0].obj && !args[0].obj->dead);
    int w = (int)(double)args[0];
    if (w == -4) return Value(0.0);
    return Value(kwik_first_instance(w) != nullptr);
}

GMLFN(instance_find) {
    (void)self;
    if (argc < 2) return Value(-4.0);
    int who = (int)(double)args[0];
    int n = (int)(double)args[1];
    int count = 0;
    for (auto& sp : g_instances) {
        if (!inst_matches(sp.get(), who)) continue;
        if (count == n) return Value((double)sp->id);
        ++count;
    }
    return Value(-4.0);
}

GMLFN(instance_number) {
    (void)self;
    if (argc < 1) return Value(0.0);
    int who = (int)(double)args[0];
    int count = 0;
    for (auto& sp : g_instances)
        if (inst_matches(sp.get(), who)) ++count;
    return Value((double)count);
}

GMLFN(instance_create_depth) {
    (void)self;
    if (argc < 4) return Value(-4.0);
    return kwik_create_instance((int)(double)args[3], (double)args[0], (double)args[1],
                                (double)args[2], true);
}

GMLFN(instance_create_layer) {
    (void)self;
    if (argc < 4) return Value(-4.0);
    return kwik_create_instance((int)(double)args[3], (double)args[0], (double)args[1], 0.0, false);
}

GMLFN(instance_destroy) {
    if (argc >= 1) {
        bool run_ev = argc < 2 || gml_truthy(args[1]);
        if (args[0].type == Value::OBJ) {
            kwik_destroy_instance(args[0].obj.get(), run_ev);
            return Value();
        }
        int w = (int)(double)args[0];
        for (Instance* t : kwik_instances_matching(self, w)) kwik_destroy_instance(t, run_ev);
        return Value();
    }
    kwik_destroy_instance(self, true);
    return Value();
}

GMLFN(instance_activate_all) {
    (void)self; (void)args; (void)argc;
    for (auto& sp : g_instances) sp->active = true;
    return Value();
}

GMLFN(instance_activate_object) {
    (void)self;
    if (argc < 1) return Value();
    int who = (int)(double)args[0];
    for (auto& sp : g_instances) {
        if (sp->dead) continue;
        bool m = who >= 100000 ? sp->id == who : kwik_obj_is_a(sp->object_index, who);
        if (m) sp->active = true;
    }
    return Value();
}

GMLFN(instance_deactivate_all) {
    bool notme = argc > 0 && gml_truthy(args[0]);
    for (auto& sp : g_instances) {
        if (sp->dead) continue;
        if (notme && sp.get() == self) continue;
        sp->active = false;
    }
    return Value();
}

GMLFN(instance_deactivate_object) {
    (void)self;
    if (argc < 1) return Value();
    int who = (int)(double)args[0];
    for (auto& sp : g_instances) {
        if (sp->dead) continue;
        bool m = who >= 100000 ? sp->id == who : kwik_obj_is_a(sp->object_index, who);
        if (m) sp->active = false;
    }
    return Value();
}

GMLFN(place_meeting) {
    if (argc < 3) return Value(0.0);
    Instance* hit = collision_at(self, (double)args[0], (double)args[1], (int)(double)args[2], false);
    static int dbg_left = std::getenv("KWIK_DEBUG_COLL") ? 300 : 0;
    if (dbg_left > 0 && self && self->object_index >= 0 && self->object_index < g_object_count_rt &&
        !std::strcmp(g_objects_rt[self->object_index].name, "obj_heart")) {
        --dbg_left;
        std::fprintf(stderr, "[coll] heart at(%.1f,%.1f) test(%.1f,%.1f) obj=%d -> %s",
                     self->x, self->y, (double)args[0], (double)args[1], (int)(double)args[2],
                     hit ? "HIT" : "no");
        if (hit) {
            KBox hb = make_box(hit, hit->x, hit->y);
            std::fprintf(stderr, " #%d %s pos(%.1f,%.1f) lbox(%.1f..%.1f, %.1f..%.1f) rot=%d",
                         hit->id, g_objects_rt[hit->object_index].name, hit->x, hit->y, hb.lx0,
                         hb.lx1, hb.ly0, hb.ly1, (int)hb.rot);
        }
        std::fprintf(stderr, "\n");
    }
    return Value(hit != nullptr);
}

GMLFN(place_free) {
    if (argc < 2) return Value(1.0);
    return Value(collision_at(self, (double)args[0], (double)args[1], -3, false) == nullptr);
}

GMLFN(position_meeting) {
    if (argc < 3) return Value(0.0);
    return Value(collision_point_at(self, (double)args[0], (double)args[1], (int)(double)args[2]) != nullptr);
}

GMLFN(instance_place) {
    if (argc < 3) return Value(-4.0);
    Instance* hit = collision_at(self, (double)args[0], (double)args[1], (int)(double)args[2], false);
    return Value(hit ? (double)hit->id : -4.0);
}

GMLFN(instance_position) {
    if (argc < 3) return Value(-4.0);
    Instance* hit = collision_point_at(self, (double)args[0], (double)args[1], (int)(double)args[2]);
    return Value(hit ? (double)hit->id : -4.0);
}

GMLFN(instance_nearest) {
    if (argc < 3) return Value(-4.0);
    double px = (double)args[0], py = (double)args[1];
    int who = (int)(double)args[2];
    Instance* best = nullptr;
    double bd = 1e30;
    for (auto& sp : g_instances) {
        if (sp.get() == self || !inst_matches(sp.get(), who)) continue;
        double d = std::hypot(sp->x - px, sp->y - py);
        if (d < bd) { bd = d; best = sp.get(); }
    }
    return Value(best ? (double)best->id : -4.0);
}

GMLFN(collision_line) {
    if (argc < 7) return Value(-4.0);
    Instance* hit = collision_line_at(self, (double)args[0], (double)args[1], (double)args[2],
                                      (double)args[3], (int)(double)args[4]);
    return Value(hit ? (double)hit->id : -4.0);
}

GMLFN(collision_point) {
    if (argc < 5) return Value(-4.0);
    Instance* hit = collision_point_at(self, (double)args[0], (double)args[1], (int)(double)args[2]);
    return Value(hit ? (double)hit->id : -4.0);
}

GMLFN(collision_rectangle) {
    if (argc < 7) return Value(-4.0);
    Instance* hit = collision_rect_at(self, (double)args[0], (double)args[1], (double)args[2],
                                      (double)args[3], (int)(double)args[4]);
    return Value(hit ? (double)hit->id : -4.0);
}

GMLFN(collision_circle) {
    if (argc < 4) return Value(-4.0);
    double cx = (double)args[0], cy = (double)args[1], r = (double)args[2];
    int who = (int)(double)args[3];
    for (auto& sp : g_instances) {
        Instance* other = sp.get();
        if (other == self || !inst_matches(other, who)) continue;
        double l, t, rr, b;
        if (!inst_bbox(other, other->x, other->y, l, t, rr, b)) continue;
        double px = cx < l ? l : (cx > rr ? rr : cx);
        double py = cy < t ? t : (cy > b ? b : cy);
        if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r) return Value((double)other->id);
    }
    return Value(-4.0);
}

GMLFN(distance_to_object) {
    if (argc < 1 || !self) return Value(1e10);
    int who = (int)(double)args[0];
    double l, t, r, b;
    bool have_self = inst_bbox(self, self->x, self->y, l, t, r, b);
    double best = 1e10;
    for (auto& sp : g_instances) {
        Instance* other = sp.get();
        if (other == self || !inst_matches(other, who)) continue;
        double ol, ot, orr, ob;
        double d;
        if (have_self && inst_bbox(other, other->x, other->y, ol, ot, orr, ob)) {
            double dx = std::max({ol - r, l - orr, 0.0});
            double dy = std::max({ot - b, t - ob, 0.0});
            d = std::hypot(dx, dy);
        } else {
            d = std::hypot(other->x - self->x, other->y - self->y);
        }
        if (d < best) best = d;
    }
    return Value(best);
}

GMLFN(distance_to_point) {
    if (argc < 2 || !self) return Value(0.0);
    double px = (double)args[0], py = (double)args[1];
    double l, t, r, b;
    if (inst_bbox(self, self->x, self->y, l, t, r, b)) {
        double dx = std::max({l - px, px - r, 0.0});
        double dy = std::max({t - py, py - b, 0.0});
        return Value(std::hypot(dx, dy));
    }
    return Value(std::hypot(self->x - px, self->y - py));
}

GMLFN(point_in_rectangle) {
    (void)self;
    if (argc < 6) return Value(0.0);
    double px = (double)args[0], py = (double)args[1];
    return Value(px >= (double)args[2] && px <= (double)args[4] && py >= (double)args[3] &&
                 py <= (double)args[5]);
}

GMLFN(move_towards_point) {
    if (argc < 3 || !self) return Value();
    double dx = (double)args[0] - self->x, dy = (double)args[1] - self->y;
    double dist = std::hypot(dx, dy);
    double spd = (double)args[2];
    if (dist > 0.0001) {
        self->m_dir = std::fmod(std::atan2(-dy, dx) * 180.0 / M_PI + 360.0, 360.0);
        self->m_speed = spd;
        sync_from_polar(self);
    }
    return Value();
}

GMLFN(motion_set) {
    if (argc < 2 || !self) return Value();
    self->m_dir = (double)args[0];
    self->m_speed = (double)args[1];
    sync_from_polar(self);
    return Value();
}

GMLFN(motion_add) {
    if (argc < 2 || !self) return Value();
    double r = (double)args[0] * M_PI / 180.0;
    self->m_hs += std::cos(r) * (double)args[1];
    self->m_vs -= std::sin(r) * (double)args[1];
    sync_from_component(self);
    return Value();
}

GMLFN(object_get_sprite) {
    (void)self;
    int o = argc > 0 ? (int)(double)args[0] : -1;
    if (o >= 0 && o < g_object_count_rt) return Value((double)g_objects_rt[o].sprite_index);
    return Value(-1.0);
}

GMLFN(object_get_name) {
    (void)self;
    int o = argc > 0 ? (int)(double)args[0] : -1;
    if (o >= 0 && o < g_object_count_rt) return Value(g_objects_rt[o].name);
    return Value("<undefined>");
}

GMLFN(object_get_parent) {
    (void)self;
    int o = argc > 0 ? (int)(double)args[0] : -1;
    if (o >= 0 && o < g_object_count_rt) return Value((double)g_objects_rt[o].parent_index);
    return Value(-100.0);
}

GMLFN(object_is_ancestor) {
    (void)self;
    if (argc < 2) return Value(0.0);
    int child = (int)(double)args[0];
    int anc = (int)(double)args[1];
    if (child == anc) return Value(0.0);
    return Value(kwik_obj_is_a(child, anc));
}

GMLFN(room_goto) {
    (void)self;
    if (argc > 0) kwik_room_goto((int)(double)args[0]);
    return Value();
}
GMLFN(room_goto_next) {
    (void)self; (void)args; (void)argc;
    kwik_room_goto(g_current_room + 1);
    return Value();
}
GMLFN(room_goto_previous) {
    (void)self; (void)args; (void)argc;
    kwik_room_goto(g_current_room - 1);
    return Value();
}
GMLFN(room_next) {
    (void)self;
    int r = argc > 0 ? (int)(double)args[0] : g_current_room;
    return Value(r + 1 < g_room_count_rt ? (double)(r + 1) : -1.0);
}
GMLFN(room_previous) {
    (void)self;
    int r = argc > 0 ? (int)(double)args[0] : g_current_room;
    return Value(r - 1 >= 0 ? (double)(r - 1) : -1.0);
}
GMLFN(room_restart) {
    (void)self; (void)args; (void)argc;
    g_pending_room = g_current_room;
    return Value();
}
GMLFN(room_get_name) {
    (void)self;
    int r = argc > 0 ? (int)(double)args[0] : g_current_room;
    if (r >= 0 && r < g_room_count_rt) return Value(g_room_defs_rt[r].name);
    return Value("<undefined>");
}
GMLFN(room_exists) {
    (void)self;
    int r = argc > 0 ? (int)(double)args[0] : -1;
    return Value(r >= 0 && r < g_room_count_rt);
}
GMLFN(game_end) {
    (void)self; (void)args; (void)argc;
    g_game_end_requested = true;
    return Value();
}
GMLFN(game_restart) {
    (void)self; (void)args; (void)argc;
    g_game_restart_requested = true;
    return Value();
}
GMLFN(game_change) {
    (void)self; (void)args; (void)argc;
    std::fprintf(stderr, "[kwik] game_change requested (chapter switch) - ending\n");
    g_game_end_requested = true;
    return Value();
}

GMLFN(method) {
    if (argc < 2) return Value();
    Value out = args[1];
    if (out.type != Value::FN) return out;
    if (args[0].type == Value::OBJ && args[0].obj)
        out.fn_bind = args[0].obj->id;
    else {
        int w = (int)(double)args[0];
        if (w == -1 && self) out.fn_bind = self->id;
        else if (w >= 100000) out.fn_bind = w;
        else out.fn_bind = -1;
    }
    return out;
}

GMLFN(script_execute) {
    if (argc < 1) return Value();
    return kwik_call_value(self, args[0], args + 1, argc - 1);
}

static const ScriptEntry* g_script_entries = nullptr;
static int g_script_entry_count = 0;

GMLFN(asset_get_index) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    std::string n = (std::string)args[0];
    for (int i = 0; i < g_object_count_rt; ++i)
        if (n == g_objects_rt[i].name) return Value((double)i);
    for (int i = 0; i < kwik_sprite_total(); ++i) {
        const KwikSprite* sd = kwik_sprite_at(i);
        if (sd && sd->name && n == sd->name) return Value((double)i);
    }
    for (int i = 0; i < g_sound_count; ++i)
        if (g_sound_table[i].name && n == g_sound_table[i].name) return Value((double)i);
    for (int i = 0; i < g_room_count_rt; ++i)
        if (n == g_room_defs_rt[i].name) return Value((double)i);
    for (int i = 0; i < g_script_entry_count; ++i)
        if (n == g_script_entries[i].name)
            return kwik_make_fnref(g_script_entries[i].fn, g_script_entries[i].name);
    return Value(-1.0);
}

GMLFN(camera_create) {
    (void)self; (void)args; (void)argc;
    Camera c;
    c.in_use = true;
    g_cameras.push_back(c);
    return Value((double)(g_cameras.size() - 1));
}

static Camera& cam_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_cameras.size()) i = 0;
    return g_cameras[i];
}

GMLFN(camera_set_view_pos) {
    (void)self;
    if (argc < 3) return Value();
    Camera& c = cam_of(args[0]);
    c.x = (double)args[1];
    c.y = (double)args[2];
    return Value();
}
GMLFN(camera_set_view_size) {
    (void)self;
    if (argc < 3) return Value();
    Camera& c = cam_of(args[0]);
    c.w = (double)args[1];
    c.h = (double)args[2];
    return Value();
}
GMLFN(camera_set_view_target) {
    (void)self;
    if (argc < 2) return Value();
    cam_of(args[0]).target = (int)(double)args[1];
    return Value();
}
GMLFN(camera_set_view_border) {
    (void)self;
    if (argc < 3) return Value();
    Camera& c = cam_of(args[0]);
    c.border_x = (double)args[1];
    c.border_y = (double)args[2];
    return Value();
}
GMLFN(camera_set_view_speed) {
    (void)self;
    if (argc < 3) return Value();
    Camera& c = cam_of(args[0]);
    c.speed_x = (double)args[1];
    c.speed_y = (double)args[2];
    return Value();
}
GMLFN(camera_set_view_angle) {
    (void)self;
    if (argc >= 2) cam_of(args[0]).angle = (double)args[1];
    return Value();
}
GMLFN(camera_get_view_x) { (void)self; (void)argc; return Value(cam_of(args[0]).x); }
GMLFN(camera_get_view_y) { (void)self; (void)argc; return Value(cam_of(args[0]).y); }
GMLFN(camera_get_view_width) { (void)self; (void)argc; return Value(cam_of(args[0]).w); }
GMLFN(camera_get_view_height) { (void)self; (void)argc; return Value(cam_of(args[0]).h); }
GMLFN(camera_get_view_angle) { (void)self; (void)argc; return Value(cam_of(args[0]).angle); }
GMLFN(camera_get_view_border_x) { (void)self; (void)argc; return Value(cam_of(args[0]).border_x); }
GMLFN(camera_get_view_border_y) { (void)self; (void)argc; return Value(cam_of(args[0]).border_y); }
GMLFN(camera_get_view_speed_x) { (void)self; (void)argc; return Value(cam_of(args[0]).speed_x); }
GMLFN(camera_get_view_speed_y) { (void)self; (void)argc; return Value(cam_of(args[0]).speed_y); }
GMLFN(camera_get_view_target) { (void)self; (void)argc; return Value((double)cam_of(args[0]).target); }

GMLFN(view_get_camera) {
    (void)self;
    int v = argc > 0 ? (int)(double)args[0] : 0;
    return Value((double)(v >= 0 && v < 8 ? g_view_camera[v] : 0));
}
GMLFN(view_set_camera) {
    (void)self;
    if (argc >= 2) {
        int v = (int)(double)args[0];
        if (v >= 0 && v < 8) g_view_camera[v] = (int)(double)args[1];
    }
    return Value();
}
GMLFN(view_get_visible) {
    (void)self;
    int v = argc > 0 ? (int)(double)args[0] : 0;
    return Value((double)(v >= 0 && v < 8 ? g_view_visible[v] : 0));
}
GMLFN(view_set_visible) {
    (void)self;
    if (argc >= 2) {
        int v = (int)(double)args[0];
        if (v >= 0 && v < 8) g_view_visible[v] = gml_truthy(args[1]) ? 1 : 0;
    }
    return Value();
}
GMLFN(view_get_wport) { (void)self; (void)args; (void)argc; return Value((double)render_gui_width()); }
GMLFN(view_get_hport) { (void)self; (void)args; (void)argc; return Value((double)render_gui_height()); }
GMLFN(view_get_xport) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(view_get_yport) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(view_set_wport) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(view_set_hport) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(view_set_xport) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(view_set_yport) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(view_get_surface_id) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(view_set_surface_id) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(event_inherited_fn) { (void)self; (void)args; (void)argc; return Value(); }

struct RtPath {
    std::vector<std::pair<double, double>> pts;
    bool closed = false;
};
static std::vector<RtPath> g_paths;

RtPath* kwik_path_by_id(int id) {
    int i = id - 1;
    if (i < 0 || (size_t)i >= g_paths.size()) return nullptr;
    return &g_paths[i];
}

int kwik_path_new() {
    g_paths.emplace_back();
    return (int)g_paths.size();
}

static void path_point_at(const RtPath& p, double pos, double& ox, double& oy) {
    if (p.pts.empty()) {
        ox = oy = 0;
        return;
    }
    if (p.pts.size() == 1 || pos <= 0) {
        ox = p.pts[0].first;
        oy = p.pts[0].second;
        return;
    }
    size_t segs = p.pts.size() - (p.closed ? 0 : 1);
    std::vector<double> lens(segs);
    double total = 0;
    for (size_t i = 0; i < segs; ++i) {
        auto& a = p.pts[i];
        auto& b = p.pts[(i + 1) % p.pts.size()];
        lens[i] = std::hypot(b.first - a.first, b.second - a.second);
        total += lens[i];
    }
    if (total <= 0) {
        ox = p.pts[0].first;
        oy = p.pts[0].second;
        return;
    }
    double want = pos * total;
    if (want >= total) {
        auto& e = p.closed ? p.pts[0] : p.pts.back();
        ox = e.first;
        oy = e.second;
        return;
    }
    for (size_t i = 0; i < segs; ++i) {
        if (want <= lens[i] || i == segs - 1) {
            double f = lens[i] > 0 ? want / lens[i] : 0;
            if (f > 1) f = 1;
            auto& a = p.pts[i];
            auto& b = p.pts[(i + 1) % p.pts.size()];
            ox = a.first + (b.first - a.first) * f;
            oy = a.second + (b.second - a.second) * f;
            return;
        }
        want -= lens[i];
    }
    ox = p.pts.back().first;
    oy = p.pts.back().second;
}

double kwik_path_length(int id) {
    RtPath* p = kwik_path_by_id(id);
    if (!p || p->pts.size() < 2) return 0;
    double total = 0;
    size_t segs = p->pts.size() - (p->closed ? 0 : 1);
    for (size_t i = 0; i < segs; ++i) {
        auto& a = p->pts[i];
        auto& b = p->pts[(i + 1) % p->pts.size()];
        total += std::hypot(b.first - a.first, b.second - a.second);
    }
    return total;
}

void kwik_path_xy(int id, double pos, double& ox, double& oy) {
    RtPath* p = kwik_path_by_id(id);
    if (!p) {
        ox = oy = 0;
        return;
    }
    path_point_at(*p, pos, ox, oy);
}

bool kwik_path_closed(int id) {
    RtPath* p = kwik_path_by_id(id);
    return p && p->closed;
}

void kwik_path_add_point(int id, double x, double y) {
    RtPath* p = kwik_path_by_id(id);
    if (p) p->pts.push_back({x, y});
}

void kwik_path_set_closed(int id, bool closed) {
    RtPath* p = kwik_path_by_id(id);
    if (p) p->closed = closed;
}

void kwik_path_clear(int id) {
    RtPath* p = kwik_path_by_id(id);
    if (p) p->pts.clear();
}

bool kwik_path_exists(int id) { return kwik_path_by_id(id) != nullptr; }

GMLFN(path_start) {
    if (!self || argc < 3) return Value();
    int path = (int)(double)args[0];
    RtPath* p = kwik_path_by_id(path);
    if (!p || p->pts.empty()) return Value();
    double speed = (double)args[1];
    int endaction = (int)(double)args[2];
    bool absolute = argc > 3 && gml_truthy(args[3]);
    double ox = 0, oy = 0;
    if (!absolute) {
        ox = self->x - p->pts[0].first;
        oy = self->y - p->pts[0].second;
    }
    self->var("__kwik_path") = Value((double)path);
    self->var("path_position") = Value(0.0);
    self->var("path_speed") = Value(speed);
    self->var("__kwik_path_end") = Value((double)endaction);
    self->var("__kwik_path_ox") = Value(ox);
    self->var("__kwik_path_oy") = Value(oy);
    double px, py;
    kwik_path_xy(path, 0, px, py);
    self->x = px + ox;
    self->y = py + oy;
    return Value();
}

GMLFN(path_end) {
    (void)args; (void)argc;
    if (self) {
        self->vars.erase("__kwik_path");
        self->vars.erase("__kwik_path_end");
    }
    return Value();
}

static void step_path(Instance* inst) {
    auto it = inst->vars.find("__kwik_path");
    if (it == inst->vars.end()) return;
    int path = (int)(double)it->second;
    double len = kwik_path_length(path);
    if (len <= 0) return;
    double pos = (double)inst->var("path_position");
    double speed = (double)inst->var("path_speed");
    pos += speed / len;
    bool ended = pos >= 1.0;
    if (ended) {
        int endaction = (int)(double)inst->var("__kwik_path_end");
        switch (endaction) {
            case 1: pos -= 1.0; break;
            case 2: pos -= 1.0; break;
            case 3:
                pos = 1.0 - (pos - 1.0);
                inst->var("path_speed") = Value(-speed);
                break;
            default:
                pos = 1.0;
                break;
        }
    }
    if (pos < 0) pos = 0;
    inst->var("path_position") = Value(pos);
    double px, py;
    kwik_path_xy(path, pos, px, py);
    inst->x = px + (double)inst->var("__kwik_path_ox");
    inst->y = py + (double)inst->var("__kwik_path_oy");
    if (ended) {
        int endaction = (int)(double)inst->var("__kwik_path_end");
        if (endaction == 0) {
            inst->vars.erase("__kwik_path");
            inst->vars.erase("__kwik_path_end");
        }
        fire(inst, EVK_PATH_ENDED, 0);
    }
}

static void step_motion(Instance* inst) {
    inst->xprevious = inst->x;
    inst->yprevious = inst->y;
    double grav = 0, gdir = 270, fric = 0;
    auto ig = inst->vars.find("gravity");
    if (ig != inst->vars.end()) grav = (double)ig->second;
    auto igd = inst->vars.find("gravity_direction");
    if (igd != inst->vars.end()) gdir = (double)igd->second;
    auto ifr = inst->vars.find("friction");
    if (ifr != inst->vars.end()) fric = (double)ifr->second;
    if (grav != 0.0) {
        double r = gdir * M_PI / 180.0;
        inst->m_hs += std::cos(r) * grav;
        inst->m_vs -= std::sin(r) * grav;
        sync_from_component(inst);
    }
    if (fric != 0.0 && inst->m_speed != 0.0) {
        double ns = inst->m_speed > 0 ? std::max(0.0, inst->m_speed - fric)
                                      : std::min(0.0, inst->m_speed + fric);
        inst->m_speed = ns;
        sync_from_polar(inst);
    }
    if (inst->m_hs != 0.0 || inst->m_vs != 0.0) {
        inst->x += inst->m_hs;
        inst->y += inst->m_vs;
    }
}

static void run_alarms(Instance* inst) {
    auto it = inst->vars.find("alarm");
    if (it == inst->vars.end() || it->second.type != Value::ARR || !it->second.arr) return;
    auto& items = it->second.arr->items;
    for (size_t i = 0; i < items.size() && i < 12; ++i) {
        double v = (double)items[i];
        if (v > 0) {
            v -= 1;
            items[i] = Value(v);
            if (v <= 0) {
                items[i] = Value(-1.0);
                fire(inst, EVK_ALARM, (int)i);
            }
        }
    }
}

static void run_animation(Instance* inst) {
    int spr = inst_sprite(inst);
    const KwikSprite* sdef = kwik_sprite_at(spr);
    if (!sdef) return;
    const KwikSprite& s = *sdef;
    if (s.frame_count <= 0) return;
    double imgspd = 1.0;
    auto it = inst->vars.find("image_speed");
    if (it != inst->vars.end()) imgspd = (double)it->second;
    double base = s.speed_type == 0 ? s.speed / std::max(1.0, g_room_speed_v) : s.speed;
    if (base <= 0) base = 1.0;
    double adv = base * imgspd;
    if (adv == 0.0) return;
    double idx = 0;
    auto ii = inst->vars.find("image_index");
    if (ii != inst->vars.end()) idx = (double)ii->second;
    idx += adv;
    bool wrapped = false;
    if (idx >= s.frame_count) {
        wrapped = true;
        idx = std::fmod(idx, (double)s.frame_count);
    } else if (idx < 0) {
        idx = std::fmod(idx, (double)s.frame_count) + s.frame_count;
    }
    inst->var("image_index") = Value(idx);
    if (wrapped) fire(inst, EVK_ANIM_END, 0);
}

static void run_collisions() {
    size_t n = g_instances.size();
    std::vector<int> handled;
    for (size_t ai = 0; ai < n; ++ai) {
        Instance* a = g_instances[ai].get();
        if (a->dead || !a->active || a->object_index < 0) continue;
        int obj = a->object_index;
        int guard = 0;
        handled.clear();
        while (obj >= 0 && obj < g_object_count_rt && guard++ < 128) {
            const ObjectDef& od = g_objects_rt[obj];
            for (int c = 0; c < od.collision_count; ++c) {
                int target = od.collisions[c].other_object;
                if (std::find(handled.begin(), handled.end(), target) != handled.end()) continue;
                handled.push_back(target);
                ScriptFn fn = od.collisions[c].fn;
                size_t m = g_instances.size();
                for (size_t bi = 0; bi < m; ++bi) {
                    Instance* b = g_instances[bi].get();
                    if (b == a || !inst_matches(b, target)) continue;
                    if (instances_hit(a, a->x, a->y, b)) {
                        Instance* saved_other = g_other_ptr;
                        g_other_ptr = b;
                        call_event(a, EVK_STEP, 0, fn, obj);
                        g_other_ptr = saved_other;
                        if (a->dead) break;
                    }
                }
                if (a->dead) break;
            }
            if (a->dead) break;
            obj = od.parent_index;
        }
    }
}

static void sweep_dead() {
    g_instances.erase(std::remove_if(g_instances.begin(), g_instances.end(),
                                     [](const std::shared_ptr<Instance>& sp) { return sp->dead; }),
                      g_instances.end());
}

static void update_camera_follow() {
    Camera& c = g_cameras[g_view_camera[0]];
    if (c.target >= 0) {
        Instance* t = kwik_first_instance(c.target);
        if (t) {
            double left = c.x + c.border_x;
            double right = c.x + c.w - c.border_x;
            double top = c.y + c.border_y;
            double bottom = c.y + c.h - c.border_y;
            double nx = c.x, ny = c.y;
            if (t->x < left) nx = t->x - c.border_x;
            else if (t->x > right) nx = t->x - c.w + c.border_x;
            if (t->y < top) ny = t->y - c.border_y;
            else if (t->y > bottom) ny = t->y - c.h + c.border_y;
            if (c.speed_x >= 0) {
                double dx = nx - c.x;
                if (std::fabs(dx) > c.speed_x) nx = c.x + (dx > 0 ? c.speed_x : -c.speed_x);
            }
            if (c.speed_y >= 0) {
                double dy = ny - c.y;
                if (std::fabs(dy) > c.speed_y) ny = c.y + (dy > 0 ? c.speed_y : -c.speed_y);
            }
            c.x = std::max(0.0, std::min(nx, (double)room_width_cur() - c.w));
            c.y = std::max(0.0, std::min(ny, (double)room_height_cur() - c.h));
        }
    }
}

static bool g_first_room_loaded = false;

static void load_room(int index, bool clear_persistent) {
    const RoomDef& room = g_room_defs_rt[index];
    if (std::getenv("KWIK_DEBUG"))
        std::fprintf(stderr, "[kwik] room %d: %s (%d instances)\n", index, room.name,
                     room.instance_count);

    if (g_first_room_loaded) {
        size_t n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (!inst->dead) fire(inst, EVK_ROOM_END, 0);
        }
    }

    std::vector<std::shared_ptr<Instance>> kept;
    for (auto& sp : g_instances) {
        if (sp->dead) continue;
        if (!clear_persistent && sp->persistent) kept.push_back(sp);
    }
    g_instances = std::move(kept);
    size_t persist_count = g_instances.size();

    g_current_room = index;
    g_pending_room = -1;
    g_room_speed_v = room.speed > 0 ? room.speed : g_default_fps;

    Camera& cam = g_cameras[g_view_camera[0]];
    cam.x = room.view_x;
    cam.y = room.view_y;
    cam.w = room.view_w > 0 ? room.view_w : room.width;
    cam.h = room.view_h > 0 ? room.view_h : room.height;
    cam.border_x = room.view_border_x;
    cam.border_y = room.view_border_y;
    cam.speed_x = room.view_speed_x;
    cam.speed_y = room.view_speed_y;
    cam.target = room.view_object;

    render_set_room(room.width, room.height, room.bg_color);

    g_rt_layers.clear();
    for (int i = 0; i < room.layer_count; ++i) {
        const RoomLayerDef& ld = room.layers[i];
        RtLayer l;
        l.name = ld.name;
        l.id = ld.id;
        l.type = ld.type;
        l.depth = ld.depth;
        l.x = ld.x;
        l.y = ld.y;
        l.visible = ld.visible != 0;
        l.sprite = ld.sprite;
        l.htiled = ld.htiled;
        l.vtiled = ld.vtiled;
        l.stretch = ld.stretch;
        l.color = ld.color;
        l.tile_first = ld.tile_first;
        l.tile_count = ld.tile_count;
        g_rt_layers.push_back(l);
    }

    std::vector<Instance*> created;
    for (int i = 0; i < room.instance_count; ++i) {
        const InstanceInit& init = room.instances[i];
        if (init.object_index < 0 || init.object_index >= g_object_count_rt) continue;
        auto sp = std::make_shared<Instance>();
        sp->x = init.x; sp->y = init.y;
        sp->xstart = init.x; sp->ystart = init.y;
        sp->xprevious = init.x; sp->yprevious = init.y;
        sp->id = init.id > 0 ? init.id : g_next_instance_id++;
        sp->object_index = init.object_index;
        init_instance_vars(sp.get(), &g_objects_rt[init.object_index]);
        sp->var("image_xscale") = Value(init.scale_x);
        sp->var("image_yscale") = Value(init.scale_y);
        sp->var("image_angle") = Value(init.angle);
        if (init.image_index > 0) sp->var("image_index") = Value((double)init.image_index);
        sp->depth = init.depth;
        g_instances.push_back(sp);
        created.push_back(sp.get());
    }

    for (size_t i = 0; i < created.size(); ++i) fire(created[i], EVK_PRE_CREATE, 0);
    for (size_t i = 0; i < created.size(); ++i) {
        fire(created[i], EVK_CREATE, 0);
        const InstanceInit& init = room.instances[i];
        if (init.creation_code && !created[i]->dead) init.creation_code(created[i], nullptr, 0);
    }

    if (room.creation_code && g_dummy_instance) room.creation_code(g_dummy_instance, nullptr, 0);

    if (!g_first_room_loaded) {
        size_t n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (!inst->dead) fire(inst, EVK_GAME_START, 0);
        }
    }

    {
        (void)persist_count;
        size_t n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (!inst->dead) fire(inst, EVK_ROOM_START, 0);
        }
    }
    g_first_room_loaded = true;
    {
        const char* sg = std::getenv("KWIK_SETGLOBALS");
        if (sg && *sg) {
            std::string spec = sg;
            size_t pos = 0;
            while (pos < spec.size()) {
                size_t comma = spec.find(',', pos);
                if (comma == std::string::npos) comma = spec.size();
                std::string pair = spec.substr(pos, comma - pos);
                pos = comma + 1;
                size_t eq = pair.find('=');
                if (eq == std::string::npos) continue;
                std::string key = pair.substr(0, eq);
                std::string val = pair.substr(eq + 1);
                char* endp = nullptr;
                double d = std::strtod(val.c_str(), &endp);
                if (endp && *endp == 0 && !val.empty())
                    global_var(key) = Value(d);
                else
                    global_var(key) = Value(val);
            }
        }
    }
    sweep_dead();
}

static void draw_world() {
    Camera& cam = g_cameras[g_view_camera[0]];
    render_set_view(cam.x, cam.y, cam.w, cam.h);

    struct DrawItem {
        double depth;
        int type;
        long long order;
        Instance* inst;
        const RtLayer* layer;
        const RoomTile* tile;
        double tile_ox, tile_oy;
    };
    std::vector<DrawItem> items;
    const RoomDef* cur = g_current_room >= 0 ? &g_room_defs_rt[g_current_room] : nullptr;
    for (const RtLayer& l : g_rt_layers) {
        if (!l.visible) continue;
        if (l.type == 1) {
            if (l.el_visible && (l.sprite >= 0 || (l.color & 0xFF000000) != 0))
                items.push_back({l.depth, 2, l.id, nullptr, &l, nullptr, 0, 0});
        } else if (l.type == 3 && cur) {
            for (int t = 0; t < l.tile_count; ++t) {
                int ti = l.tile_first + t;
                if (ti < 0 || ti >= cur->tile_count) continue;
                items.push_back({cur->tiles[ti].depth, 0, (long long)ti, nullptr, nullptr,
                                 &cur->tiles[ti], l.x, l.y});
            }
        }
    }
    for (auto& sp : g_instances) {
        Instance* inst = sp.get();
        if (inst->dead || !inst->active || !inst->visible) continue;
        items.push_back({inst->depth, 1, (long long)inst->id, inst, nullptr, nullptr, 0, 0});
    }
    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.depth != b.depth) return a.depth > b.depth;
        if (a.type != b.type) return a.type < b.type;
        if (a.type == 0) return a.order < b.order;
        return a.order > b.order;
    });

    for (auto& sp : g_instances) {
        Instance* inst = sp.get();
        if (inst->dead || !inst->active || !inst->visible) continue;
        fire(inst, EVK_DRAW_PRE, 0);
    }
    for (auto& sp : g_instances) {
        Instance* inst = sp.get();
        if (inst->dead || !inst->active || !inst->visible) continue;
        fire(inst, EVK_DRAW_BEGIN, 0);
    }

    for (const DrawItem& it : items) {
        if (it.layer) {
            const RtLayer& bg = *it.layer;
            unsigned int blend = bg.color & 0xFFFFFF;
            double alpha = (((bg.color >> 24) & 0xFF) / 255.0) * bg.alpha;
            if (bg.sprite < 0) {
                if (alpha > 0.0) {
                    double sa = render_get_alpha();
                    render_set_alpha(alpha);
                    render_draw_rectangle_color(cam.x - 32, cam.y - 32, cam.x + cam.w + 32,
                                                cam.y + cam.h + 32, blend, blend, blend, blend,
                                                false);
                    render_set_alpha(sa);
                }
            } else if (bg.stretch) {
                kwik_draw_sprite_stretched(bg.sprite, 0, bg.x, bg.y,
                                           room_width_cur(), room_height_cur(), blend, alpha);
            } else if (bg.htiled || bg.vtiled) {
                kwik_draw_sprite_tiled(bg.sprite, 0, bg.x, bg.y, bg.xscale, bg.yscale, blend,
                                       alpha);
            } else {
                kwik_draw_sprite_general(bg.sprite, 0, bg.x, bg.y, bg.xscale, bg.yscale, 0, blend,
                                         alpha);
            }
            continue;
        }
        if (it.tile) {
            const RoomTile& t = *it.tile;
            kwik_draw_sprite_part(t.sprite, 0, t.src_x, t.src_y, t.w, t.h, t.x + it.tile_ox,
                                  t.y + it.tile_oy, t.scale_x, t.scale_y, t.color & 0xFFFFFF,
                                  ((t.color >> 24) & 0xFF) / 255.0);
            continue;
        }
        Instance* inst = it.inst;
        if (inst->dead) continue;
        int owner = -1;
        ScriptFn fn = find_event(inst->object_index, EVK_DRAW, 0, &owner);
        if (fn) call_event(inst, EVK_DRAW, 0, fn, owner);
        else draw_self_instance(inst);
    }

    for (auto& sp : g_instances) {
        Instance* inst = sp.get();
        if (inst->dead || !inst->active || !inst->visible) continue;
        fire(inst, EVK_DRAW_END, 0);
    }
    for (auto& sp : g_instances) {
        Instance* inst = sp.get();
        if (inst->dead || !inst->active || !inst->visible) continue;
        fire(inst, EVK_DRAW_POST, 0);
    }

    render_begin_gui();
    for (int kind : {EVK_DRAW_GUI_BEGIN, EVK_DRAW_GUI, EVK_DRAW_GUI_END}) {
        std::vector<DrawItem> gitems;
        for (auto& sp : g_instances) {
            Instance* inst = sp.get();
            if (inst->dead || !inst->active || !inst->visible) continue;
            if (find_event(inst->object_index, kind, 0))
                gitems.push_back(
                    {inst->depth, 1, (long long)inst->id, inst, nullptr, nullptr, 0, 0});
        }
        std::sort(gitems.begin(), gitems.end(), [](const DrawItem& a, const DrawItem& b) {
            if (a.depth != b.depth) return a.depth > b.depth;
            return a.order > b.order;
        });
        for (const DrawItem& it : gitems) {
            if (it.inst->dead) continue;
            fire(it.inst, kind, 0);
        }
    }
}

static void run_step_phase() {
    size_t n;

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        fire(inst, EVK_STEP_BEGIN, 0);
    }

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        run_alarms(inst);
    }

    {
        double wheel = render_wheel_delta();
        double mx = 0, my = 0;
        {
            Camera& c = g_cameras[g_view_camera[0]];
            mx = c.x + render_mouse_x() * c.w / std::max(1, render_gui_width());
            my = c.y + render_mouse_y() * c.h / std::max(1, render_gui_height());
        }
        n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (inst->dead || !inst->active) continue;
            int obj = inst->object_index;
            int guard = 0;
            while (obj >= 0 && obj < g_object_count_rt && guard++ < 128) {
                const ObjectDef& od = g_objects_rt[obj];
                for (int k = 0; k < od.keypress_count; ++k)
                    if (render_key_pressed(od.keypress[k].key))
                        call_event(inst, EVK_STEP, 0, od.keypress[k].fn, obj);
                if (inst->dead) break;
                for (int k = 0; k < od.keyrelease_count; ++k)
                    if (render_key_released(od.keyrelease[k].key))
                        call_event(inst, EVK_STEP, 0, od.keyrelease[k].fn, obj);
                if (inst->dead) break;
                for (int k = 0; k < od.keyboard_count; ++k)
                    if (render_key_down(od.keyboard[k].key))
                        call_event(inst, EVK_STEP, 0, od.keyboard[k].fn, obj);
                if (inst->dead) break;
                if (od.mouse_count > 0) {
                    double l, t, r, b;
                    bool over = inst_bbox(inst, inst->x, inst->y, l, t, r, b) && mx >= l &&
                                mx < r && my >= t && my < b;
                    for (int k = 0; k < od.mouse_count; ++k) {
                        int sub = od.mouse[k].key;
                        bool go = false;
                        if (sub == 60) go = wheel > 0;
                        else if (sub == 61) go = wheel < 0;
                        else if (sub == 0) go = over && render_mouse_down(0);
                        else if (sub == 1) go = over && render_mouse_down(1);
                        else if (sub == 4) go = over && render_mouse_pressed(0);
                        else if (sub == 5) go = over && render_mouse_pressed(1);
                        if (go) call_event(inst, EVK_STEP, 0, od.mouse[k].fn, obj);
                    }
                }
                if (inst->dead) break;
                obj = od.parent_index;
            }
        }
    }

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        fire(inst, EVK_STEP, 0);
    }

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        step_motion(inst);
        step_path(inst);
    }

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        if (!find_event(inst->object_index, EVK_OUTSIDE_ROOM, 0)) continue;
        double l, t, r, b;
        if (!inst_bbox(inst, inst->x, inst->y, l, t, r, b)) {
            l = r = inst->x;
            t = b = inst->y;
        }
        if (r < 0 || l > room_width_cur() || b < 0 || t > room_height_cur())
            fire(inst, EVK_OUTSIDE_ROOM, 0);
    }

    run_collisions();

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        fire(inst, EVK_STEP_END, 0);
    }

    n = g_instances.size();
    for (size_t i = 0; i < n; ++i) {
        Instance* inst = g_instances[i].get();
        if (inst->dead || !inst->active) continue;
        run_animation(inst);
    }

    dispatch_async();

    if (g_frame_counter == 20) {
        const char* warp = std::getenv("KWIK_START_ROOM");
        if (warp && *warp) {
            int idx = -1;
            if (warp[0] >= '0' && warp[0] <= '9') {
                idx = std::atoi(warp);
            } else {
                for (int i = 0; i < g_room_count_rt; ++i)
                    if (!std::strcmp(g_room_defs_rt[i].name, warp)) idx = i;
            }
            if (idx >= 0 && idx < g_room_count_rt) {
                std::fprintf(stderr, "[kwik] warping to room %d\n", idx);
                g_pending_room = idx;
            }
        }
    }

    static int last_ww = -1, last_wh = -1;
    int ww = render_window_width(), wh = render_window_height();
    if (last_ww >= 0 && (ww != last_ww || wh != last_wh)) {
        n = g_instances.size();
        for (size_t i = 0; i < n; ++i) {
            Instance* inst = g_instances[i].get();
            if (inst->dead || !inst->active) continue;
            fire(inst, EVK_DRAW_RESIZE, 0);
        }
    }
    last_ww = ww;
    last_wh = wh;

    for (auto& l : g_rt_layers) {
        l.x += l.hspeed;
        l.y += l.vspeed;
    }

    update_camera_follow();
    sweep_dead();
    ++g_frame_counter;

    static bool dbg_inst = std::getenv("KWIK_DEBUG_INST") != nullptr;
    if (dbg_inst && g_frame_counter % 90 == 0) {
        std::fprintf(stderr, "[inst] frame %llu:\n", g_frame_counter);
        for (auto& sp : g_instances) {
            Instance* in = sp.get();
            const char* nm = in->object_index >= 0 && in->object_index < g_object_count_rt
                                 ? g_objects_rt[in->object_index].name
                                 : "?";
            auto gv = [&](const char* n) -> double {
                auto it = in->vars.find(n);
                return it == in->vars.end() ? -1 : (double)it->second;
            };
            std::fprintf(stderr,
                         "  #%d %s (%.0f,%.0f) vis=%d act=%d depth=%.0f spr=%g idx=%.2f spd=%g\n",
                         in->id, nm, in->x, in->y, (int)in->visible, (int)in->active, in->depth,
                         gv("sprite_index"), gv("image_index"), gv("image_speed"));
        }
    }
}

int run_game(const GameTables& tables) {
    g_objects_rt = tables.objects;
    g_object_count_rt = tables.object_count;
    g_room_defs_rt = tables.rooms;
    g_room_count_rt = tables.room_count;
    g_script_entries = tables.scripts;
    g_script_entry_count = tables.script_count;
    g_game_dir = tables.game_dir ? tables.game_dir : "";
    g_assets_path = tables.assets_path ? tables.assets_path : "Assets.dat";
    if (tables.room_count <= 0) return 1;

    if (!g_game_dir.empty() && chdir(g_game_dir.c_str()) != 0)
        std::fprintf(stderr, "[kwik] warning: could not chdir to %s\n", g_game_dir.c_str());

    {
        std::string save_id = tables.save_id && *tables.save_id ? tables.save_id : "kwik_game";
        std::string clean;
        for (char c : save_id)
            clean.push_back((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '
                                ? c
                                : '_');
        const char* xdg = std::getenv("XDG_DATA_HOME");
        std::string root;
        if (xdg && *xdg) {
            root = xdg;
        } else {
            const char* home = std::getenv("HOME");
            root = std::string(home ? home : ".") + "/.local/share";
        }
        g_save_dir = root + "/kwik/saves/" + clean;
        std::error_code ec;
        std::filesystem::create_directories(g_save_dir, ec);
        if (ec) {
            std::fprintf(stderr, "[kwik] could not create save dir %s\n", g_save_dir.c_str());
            g_save_dir.clear();
        } else if (std::getenv("KWIK_DEBUG")) {
            std::fprintf(stderr, "[kwik] save dir: %s\n", g_save_dir.c_str());
        }
    }

    gml_random_seed((unsigned)std::random_device{}());

    Camera c0;
    c0.in_use = true;
    g_cameras.assign(1, c0);

    if (tables.game_fps > 0) {
        g_default_fps = tables.game_fps;
        g_room_speed_v = tables.game_fps;
    }

    const RoomDef& first = tables.rooms[0];
    int win_w = tables.window_w > 0 ? tables.window_w : (first.view_w > 0 ? first.view_w : first.width);
    int win_h = tables.window_h > 0 ? tables.window_h : (first.view_h > 0 ? first.view_h : first.height);
    if (!render_init(tables.game_name && *tables.game_name ? tables.game_name : first.name, win_w,
                     win_h, first.bg_color))
        return 1;

restart_game:
    g_game_restart_requested = false;
    g_first_room_loaded = false;
    g_instances.clear();
    g_structs.clear();

    {
        auto dummy = std::make_shared<Instance>();
        dummy->id = 90000000;
        dummy->object_index = -1;
        init_instance_vars(dummy.get(), nullptr);
        static std::shared_ptr<Instance> dummy_hold;
        dummy_hold = dummy;
        g_dummy_instance = dummy.get();
    }

    for (int i = 0; i < tables.global_init_count; ++i)
        if (tables.global_init[i]) tables.global_init[i](g_dummy_instance, nullptr, 0);

    load_room(0, true);

    double step_time = 1.0 / g_room_speed_v;
    double accumulator = 0.0;
    double last_t = now_ms() / 1000.0;
    static const bool ignore_focus =
        std::getenv("KWIK_AUTOZ") != nullptr || std::getenv("KWIK_NOFOCUS_PAUSE") != nullptr;
    while (!render_should_close() && !g_game_end_requested) {
        if (!ignore_focus && !render_has_focus()) {
            render_present_last();
            usleep(30000);
            last_t = now_ms() / 1000.0;
            accumulator = 0.0;
            continue;
        }
        double t = now_ms() / 1000.0;
        accumulator += t - last_t;
        last_t = t;
        if (accumulator > 0.2) accumulator = 0.2;
        step_time = 1.0 / std::max(1.0, g_room_speed_v);

        int guard = 0;
        bool stepped = false;
        while (accumulator >= step_time && guard++ < 4) {
            accumulator -= step_time;
            run_step_phase();
            stepped = true;
            kwik_audio_update();
            if (g_game_restart_requested) goto restart_game;
            if (g_game_end_requested) break;
            if (g_pending_room >= 0) {
                load_room(g_pending_room, false);
                accumulator = 0.0;
                break;
            }
            step_time = 1.0 / std::max(1.0, g_room_speed_v);
        }

        if (stepped) {
            render_begin_frame();
            draw_world();
            render_end_frame();
        } else {
            render_idle();
            double wait_s = step_time - accumulator;
            if (wait_s > 0.004) wait_s = 0.004;
            if (wait_s > 0) usleep((useconds_t)(wait_s * 1e6));
        }
    }

    kwik_audio_shutdown();
    render_shutdown();
    return 0;
}

}
