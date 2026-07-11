#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif

#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <random>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}
static unsigned int C(const Value* args, int argc, int i, unsigned int dflt = 0xFFFFFF) {
    return i < argc ? (unsigned int)(long long)(double)args[i] : dflt;
}

GMLFN(lerp) {
    (void)self;
    double a = A(args, argc, 0), b = A(args, argc, 1), t = A(args, argc, 2);
    return Value(a + (b - a) * t);
}
GMLFN(median) {
    (void)self;
    if (argc == 0) return Value(0.0);
    std::vector<double> v;
    for (int i = 0; i < argc; ++i) v.push_back((double)args[i]);
    std::sort(v.begin(), v.end());
    return Value(v[v.size() / 2]);
}
GMLFN(degtorad) { (void)self; return Value(A(args, argc, 0) * M_PI / 180.0); }
GMLFN(radtodeg) { (void)self; return Value(A(args, argc, 0) * 180.0 / M_PI); }
GMLFN(randomise) { return randomize(self, args, argc); }
GMLFN(game_get_speed) { (void)self; (void)args; (void)argc; return Value(kwik_room_speed()); }
GMLFN(make_colour_rgb) { return make_color_rgb(self, args, argc); }
GMLFN(make_colour_hsv) { return make_color_hsv(self, args, argc); }
GMLFN(merge_colour) { return merge_color(self, args, argc); }
GMLFN(keyboard_check_direct) { return keyboard_check(self, args, argc); }
GMLFN(get_string) { (void)self; return Value(argc > 1 ? (std::string)args[1] : ""); }
GMLFN(clipboard_set_text) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_add_texcoord) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(array_get) {
    (void)self;
    if (argc < 2) return Value();
    return kwik_array_elem(args[0], (int)(double)args[1]);
}
GMLFN(array_length_2d) {
    (void)self;
    if (argc < 2 || args[0].type != Value::ARR || !args[0].arr) return Value(0.0);
    Value row = kwik_array_elem(args[0], (int)(double)args[1]);
    if (row.type != Value::ARR || !row.arr) return Value(0.0);
    return Value((double)row.arr->items.size());
}
GMLFN(array_height_2d) {
    (void)self;
    if (argc < 1 || args[0].type != Value::ARR || !args[0].arr) return Value(0.0);
    return Value((double)args[0].arr->items.size());
}

GMLFN(variable_global_set) {
    (void)self;
    if (argc >= 2) global_var((std::string)args[0]) = args[1];
    return Value();
}
GMLFN(variable_global_get) {
    (void)self;
    if (argc < 1) return Value();
    return global_var((std::string)args[0]);
}
GMLFN(variable_instance_get) {
    if (argc < 2) return Value();
    Instance* t = kwik_resolve_target(self, args[0]);
    if (!t) return Value();
    return kwik_inst_get(self, args[0], ((std::string)args[1]).c_str());
}
GMLFN(variable_instance_set) {
    if (argc < 3) return Value();
    kwik_inst_set(self, args[0], ((std::string)args[1]).c_str(), args[2]);
    return Value();
}
GMLFN(variable_instance_get_names) {
    if (argc < 1) return kwik_new_array(nullptr, 0);
    Instance* t = kwik_resolve_target(self, args[0]);
    Value out = kwik_new_array(nullptr, 0);
    if (t)
        for (auto& kv : t->vars) out.arr->items.push_back(Value(kv.first));
    return out;
}

GMLFN(path_add) { (void)self; (void)args; (void)argc; return Value((double)kwik_path_new()); }
GMLFN(path_add_point) {
    (void)self;
    if (argc >= 3) kwik_path_add_point((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2));
    return Value();
}
GMLFN(path_delete) {
    (void)self;
    if (argc >= 1) kwik_path_clear((int)A(args, argc, 0));
    return Value();
}
GMLFN(path_exists) {
    (void)self;
    return Value(argc >= 1 && kwik_path_exists((int)A(args, argc, 0)));
}
GMLFN(path_get_x) {
    (void)self;
    double x = 0, y = 0;
    if (argc >= 2) kwik_path_xy((int)A(args, argc, 0), A(args, argc, 1), x, y);
    return Value(x);
}
GMLFN(path_get_y) {
    (void)self;
    double x = 0, y = 0;
    if (argc >= 2) kwik_path_xy((int)A(args, argc, 0), A(args, argc, 1), x, y);
    return Value(y);
}
GMLFN(path_set_closed) {
    (void)self;
    if (argc >= 2) kwik_path_set_closed((int)A(args, argc, 0), gml_truthy(args[1]));
    return Value();
}
GMLFN(path_set_kind) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(path_set_precision) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(surface_create) {
    (void)self;
    return Value((double)render_surface_create((int)A(args, argc, 0, 1), (int)A(args, argc, 1, 1)));
}
GMLFN(surface_exists) {
    (void)self;
    if (argc < 1 || args[0].type == Value::UNDEF) return Value(0.0);
    int id = (int)(double)args[0];
    return Value(id >= 0 && render_surface_exists(id));
}
GMLFN(surface_free) {
    (void)self;
    if (argc >= 1) render_surface_free((int)A(args, argc, 0));
    return Value();
}
GMLFN(surface_set_target) {
    (void)self;
    return Value(argc >= 1 && render_surface_set_target((int)A(args, argc, 0)));
}
GMLFN(surface_reset_target) {
    (void)self; (void)args; (void)argc;
    render_surface_reset_target();
    return Value();
}
GMLFN(surface_get_texture) {
    (void)self;
    return Value(argc >= 1 ? (double)((int)A(args, argc, 0) + 700000) : -1.0);
}
GMLFN(surface_getpixel_ext) {
    (void)self;
    if (argc < 3) return Value(0.0);
    unsigned char px[4] = {0, 0, 0, 0};
    render_surface_getpixel((int)A(args, argc, 0), (int)A(args, argc, 1), (int)A(args, argc, 2), px);
    return Value((double)((unsigned)px[0] | ((unsigned)px[1] << 8) | ((unsigned)px[2] << 16) |
                          ((unsigned)px[3] << 24)));
}
GMLFN(screen_save) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(draw_surface) {
    (void)self;
    if (argc < 3) return Value();
    int id = (int)A(args, argc, 0);
    unsigned int tex = render_surface_texture(id);
    if (!tex) return Value();
    render_draw_quad(tex, A(args, argc, 1), A(args, argc, 2), render_surface_width(id),
                     render_surface_height(id), 0, 0, 1, 1, 0, 0.f, 1.f, 1.f, 0.f, 0xFFFFFF, 1.0);
    return Value();
}

GMLFN(draw_clear) {
    (void)self;
    render_surface_clear(C(args, argc, 0), 1.0);
    return Value();
}
GMLFN(draw_clear_alpha) {
    (void)self;
    render_surface_clear(C(args, argc, 0), A(args, argc, 1));
    return Value();
}
GMLFN(draw_get_font) {
    (void)self; (void)args; (void)argc;
    return Value((double)kwik_get_font_rt() + 10000);
}
GMLFN(draw_get_halign) { (void)self; (void)args; (void)argc; return Value((double)render_get_halign()); }
GMLFN(draw_get_valign) { (void)self; (void)args; (void)argc; return Value((double)render_get_valign()); }

GMLFN(draw_healthbar) {
    (void)self;
    if (argc < 11) return Value();
    double x1 = A(args, argc, 0), y1 = A(args, argc, 1), x2 = A(args, argc, 2),
           y2 = A(args, argc, 3);
    double amount = A(args, argc, 4) / 100.0;
    unsigned int backcol = C(args, argc, 5);
    unsigned int mincol = C(args, argc, 6), maxcol = C(args, argc, 7);
    bool showback = gml_truthy(args[9]);
    if (amount < 0) amount = 0;
    if (amount > 1) amount = 1;
    if (showback)
        render_draw_rectangle_color(x1, y1, x2, y2, backcol, backcol, backcol, backcol, false);
    unsigned int col = amount > 0.5 ? maxcol : mincol;
    render_draw_rectangle_color(x1, y1, x1 + (x2 - x1) * amount, y2, col, col, col, col, false);
    return Value();
}

static bool g_prim_textured = false;
GMLFN(draw_primitive_begin) {
    (void)self;
    g_prim_textured = false;
    render_primitive_begin((int)A(args, argc, 0, 4), 0);
    return Value();
}
GMLFN(draw_primitive_begin_texture) {
    (void)self;
    int texref = (int)A(args, argc, 1, -1);
    unsigned int tex = 0;
    int w, h;
    if (texref >= 700000) {
        tex = render_surface_texture(texref - 700000);
    } else if (texref >= 0) {
        tex = kwik_image_texture(texref, w, h);
    }
    g_prim_textured = tex != 0;
    render_primitive_begin((int)A(args, argc, 0, 4), tex);
    return Value();
}
GMLFN(draw_primitive_end) {
    (void)self; (void)args; (void)argc;
    render_primitive_end();
    return Value();
}
GMLFN(draw_vertex) {
    (void)self;
    render_primitive_vertex(A(args, argc, 0), A(args, argc, 1), 0, 0, render_get_color(),
                            render_get_alpha(), false);
    return Value();
}
GMLFN(draw_vertex_texture_color) {
    (void)self;
    render_primitive_vertex(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                            C(args, argc, 4), A(args, argc, 5, 1), g_prim_textured);
    return Value();
}
GMLFN(draw_vertex_texture_colour) { return draw_vertex_texture_color(self, args, argc); }
GMLFN(draw_vertex_color) {
    (void)self;
    render_primitive_vertex(A(args, argc, 0), A(args, argc, 1), 0, 0, C(args, argc, 2),
                            A(args, argc, 3, 1), false);
    return Value();
}
GMLFN(draw_vertex_colour) { return draw_vertex_color(self, args, argc); }
GMLFN(draw_vertex_texture) {
    (void)self;
    render_primitive_vertex(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                            render_get_color(), render_get_alpha(), g_prim_textured);
    return Value();
}

GMLFN(draw_sprite_general) {
    (void)self;
    if (argc < 16) return Value();
    kwik_draw_sprite_part((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                          A(args, argc, 3), A(args, argc, 4), A(args, argc, 5), A(args, argc, 6),
                          A(args, argc, 7), A(args, argc, 8, 1), A(args, argc, 9, 1),
                          C(args, argc, 11), A(args, argc, 15, 1));
    return Value();
}
GMLFN(draw_sprite_pos) {
    (void)self;
    if (argc < 10) return Value();
    int image = kwik_sprite_frame_image((int)A(args, argc, 0), (int)A(args, argc, 1));
    int w = 0, h = 0;
    unsigned int tex = kwik_image_texture(image, w, h);
    if (!tex) return Value();
    double alpha = A(args, argc, 10, 1);
    render_primitive_begin(6, tex);
    render_primitive_vertex(A(args, argc, 2), A(args, argc, 3), 0, 0, 0xFFFFFF, alpha, true);
    render_primitive_vertex(A(args, argc, 4), A(args, argc, 5), 1, 0, 0xFFFFFF, alpha, true);
    render_primitive_vertex(A(args, argc, 6), A(args, argc, 7), 1, 1, 0xFFFFFF, alpha, true);
    render_primitive_vertex(A(args, argc, 8), A(args, argc, 9), 0, 1, 0xFFFFFF, alpha, true);
    render_primitive_end();
    return Value();
}
GMLFN(draw_sprite_stretched_ext) {
    (void)self;
    if (argc < 8) return Value();
    kwik_draw_sprite_stretched((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                               A(args, argc, 3), A(args, argc, 4), A(args, argc, 5),
                               C(args, argc, 6), A(args, argc, 7, 1));
    return Value();
}
GMLFN(draw_sprite_tiled) {
    (void)self;
    if (argc < 4) return Value();
    kwik_draw_sprite_tiled((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                           A(args, argc, 3), 1, 1, 0xFFFFFF, render_get_alpha());
    return Value();
}
GMLFN(draw_tilemap) {
    (void)self;
    int id = (int)A(args, argc, 0, -1);
    if (id >= 900000) id -= 900000;
    RtLayer* l = kwik_layer_by_id(id);
    if (!l) return Value();
    kwik_render_tilemap(*l, A(args, argc, 1), A(args, argc, 2));
    return Value();
}

GMLFN(draw_text_ext) {
    (void)self;
    if (argc < 5) return Value();
    kwik_draw_text_ext_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2],
                          A(args, argc, 3, -1), A(args, argc, 4, -1), 1, 1, 0);
    return Value();
}
GMLFN(draw_text_ext_transformed) {
    (void)self;
    if (argc < 8) return Value();
    kwik_draw_text_ext_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2],
                          A(args, argc, 3, -1), A(args, argc, 4, -1), A(args, argc, 5, 1),
                          A(args, argc, 6, 1), A(args, argc, 7));
    return Value();
}

GMLFN(shader_set) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_reset) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_get_uniform) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(shader_set_uniform_f) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_get_sampler_index) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(texture_set_stage) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(texture_get_texel_width) { (void)self; (void)args; (void)argc; return Value(1.0 / 2048.0); }
GMLFN(texture_get_texel_height) { (void)self; (void)args; (void)argc; return Value(1.0 / 2048.0); }
GMLFN(gpu_set_alphatestenable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_alphatestref) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_blendmode_ext) {
    (void)self;
    g_gpu_blend_src = (int)A(args, argc, 0, 2);
    g_gpu_blend_dst = (int)A(args, argc, 1, 6);
    render_set_blendmode_ext(g_gpu_blend_src, g_gpu_blend_dst);
    return Value();
}
GMLFN(gpu_set_colorwriteenable) {
    (void)self;
    if (argc == 1 && args[0].type == Value::ARR && args[0].arr) {
        for (int i = 0; i < 4 && i < (int)args[0].arr->items.size(); ++i)
            g_gpu_colorwrite[i] = gml_truthy(args[0].arr->items[i]) ? 1 : 0;
    } else {
        for (int i = 0; i < 4 && i < argc; ++i) g_gpu_colorwrite[i] = gml_truthy(args[i]) ? 1 : 0;
    }
    render_set_colorwrite(g_gpu_colorwrite[0], g_gpu_colorwrite[1], g_gpu_colorwrite[2],
                          g_gpu_colorwrite[3]);
    return Value();
}

GMLFN(sprite_get_bbox_left) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->bbox_left : 0.0);
}
GMLFN(sprite_get_bbox_right) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->bbox_right : 0.0);
}
GMLFN(sprite_get_bbox_top) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->bbox_top : 0.0);
}
GMLFN(sprite_get_bbox_bottom) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->bbox_bottom : 0.0);
}
GMLFN(sprite_set_bbox) {
    (void)self;
    if (argc >= 5)
        kwik_sprite_override_bbox((int)A(args, argc, 0), (int)A(args, argc, 1),
                                  (int)A(args, argc, 2), (int)A(args, argc, 3),
                                  (int)A(args, argc, 4));
    return Value();
}
GMLFN(sprite_set_offset) {
    (void)self;
    if (argc >= 3)
        kwik_sprite_override_offset((int)A(args, argc, 0), (int)A(args, argc, 1),
                                    (int)A(args, argc, 2));
    return Value();
}
GMLFN(sprite_get_texture) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    return Value((double)kwik_sprite_frame_image((int)A(args, argc, 0), (int)A(args, argc, 1)));
}
GMLFN(sprite_get_uvs) {
    (void)self; (void)args; (void)argc;
    Value out = kwik_new_array(nullptr, 0);
    double uvs[8] = {0, 0, 1, 1, 0, 0, 1, 1};
    for (double u : uvs) out.arr->items.push_back(Value(u));
    return out;
}
GMLFN(sprite_add) {
    (void)self;
    if (argc < 5) return Value(-1.0);
    return Value((double)kwik_sprite_add_file((std::string)args[0], (int)A(args, argc, 1, 1),
                                              (int)A(args, argc, 3), (int)A(args, argc, 4)));
}
GMLFN(sprite_duplicate) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0));
    if (!s) return Value(-1.0);
    return Value((double)kwik_register_dynamic_sprite(*s));
}

GMLFN(audio_is_paused) {
    (void)self;
    return Value(argc >= 1 && kwik_voice_paused((int)A(args, argc, 0)));
}
GMLFN(audio_sound_get_gain) {
    (void)self;
    return Value(argc >= 1 ? kwik_voice_gain((int)A(args, argc, 0)) : 1.0);
}
GMLFN(audio_sound_get_pitch) {
    (void)self;
    return Value(argc >= 1 ? kwik_voice_pitch((int)A(args, argc, 0)) : 1.0);
}
GMLFN(audio_sound_length) {
    (void)self;
    return Value(argc >= 1 ? kwik_sound_length_seconds((int)A(args, argc, 0)) : 0.0);
}

GMLFN(layer_exists) {
    (void)self;
    return Value(argc >= 1 && kwik_layer_by_id((int)A(args, argc, 0)) != nullptr);
}
GMLFN(layer_get_id) {
    (void)self;
    std::string name = argc >= 1 ? (std::string)args[0] : "";
    for (auto& l : g_rt_layers)
        if (l.name == name) return Value((double)l.id);
    return Value(-1.0);
}
GMLFN(layer_get_id_at_depth) {
    (void)self;
    double depth = A(args, argc, 0);
    for (auto& l : g_rt_layers)
        if (l.depth == depth) return Value((double)l.id);
    return Value(-1.0);
}
GMLFN(layer_background_get_id) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    RtLayer* l = kwik_layer_by_id((int)(double)args[0]);
    return Value(l ? (double)l->id : -1.0);
}
GMLFN(layer_script_begin) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_script_end) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_change) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_get_id) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(layer_sprite_get_index) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(layer_sprite_get_speed) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_sprite_get_x) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_sprite_get_xscale) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(layer_sprite_get_y) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_sprite_get_yscale) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(layer_sprite_get_angle) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_sprite_get_alpha) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(layer_sprite_get_blend) { (void)self; (void)args; (void)argc; return Value(16777215.0); }
GMLFN(layer_sprite_speed) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_x) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_y) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tilemap_get_id) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    RtLayer* l = kwik_layer_by_id((int)(double)args[0]);
    if (!l || l->grid_blob < 0) return Value(-1.0);
    return Value((double)(900000 + l->id));
}
GMLFN(tilemap_get_x) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(tilemap_x) { (void)self; (void)args; (void)argc; return Value(); }

struct DsPriority {
    std::multimap<double, Value> data;
    bool alive = true;
};
static std::vector<DsPriority> g_priorities;

static DsPriority* prio_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_priorities.size() || !g_priorities[i].alive) return nullptr;
    return &g_priorities[i];
}

GMLFN(ds_priority_create) {
    (void)self; (void)args; (void)argc;
    g_priorities.emplace_back();
    return Value((double)(g_priorities.size() - 1));
}
GMLFN(ds_priority_add) {
    (void)self;
    DsPriority* p = argc > 2 ? prio_of(args[0]) : nullptr;
    if (p) p->data.insert({(double)args[2], args[1]});
    return Value();
}
GMLFN(ds_priority_clear) {
    (void)self;
    DsPriority* p = argc > 0 ? prio_of(args[0]) : nullptr;
    if (p) p->data.clear();
    return Value();
}
GMLFN(ds_priority_copy) {
    (void)self;
    DsPriority* dst = argc > 0 ? prio_of(args[0]) : nullptr;
    DsPriority* src = argc > 1 ? prio_of(args[1]) : nullptr;
    if (dst && src) dst->data = src->data;
    return Value();
}
GMLFN(ds_priority_delete_min) {
    (void)self;
    DsPriority* p = argc > 0 ? prio_of(args[0]) : nullptr;
    if (!p || p->data.empty()) return Value();
    Value v = p->data.begin()->second;
    p->data.erase(p->data.begin());
    return v;
}
GMLFN(ds_priority_empty) {
    (void)self;
    DsPriority* p = argc > 0 ? prio_of(args[0]) : nullptr;
    return Value(!p || p->data.empty());
}
GMLFN(ds_priority_delete_max) {
    (void)self;
    DsPriority* p = argc > 0 ? prio_of(args[0]) : nullptr;
    if (!p || p->data.empty()) return Value();
    auto it = std::prev(p->data.end());
    Value v = it->second;
    p->data.erase(it);
    return v;
}
GMLFN(ds_priority_destroy) {
    (void)self;
    DsPriority* p = argc > 0 ? prio_of(args[0]) : nullptr;
    if (p) { p->alive = false; p->data.clear(); }
    return Value();
}
GMLFN(ds_queue_create) {
    (void)self; (void)args; (void)argc;
    return Value(-1.0);
}

}
