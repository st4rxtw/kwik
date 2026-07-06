#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}
static unsigned int C(const Value* args, int argc, int i, unsigned int dflt = 0xFFFFFF) {
    return i < argc ? (unsigned int)(long long)(double)args[i] : dflt;
}
static std::string S(const Value* args, int argc, int i) {
    return i < argc ? (std::string)args[i] : std::string();
}

static Value mk_array() {
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    return v;
}

GMLFN(mean) {
    (void)self;
    if (argc <= 0) return Value(0.0);
    double sum = 0;
    for (int i = 0; i < argc; ++i) sum += (double)args[i];
    return Value(sum / argc);
}

GMLFN(dot_product) {
    (void)self;
    return Value(A(args, argc, 0) * A(args, argc, 2) + A(args, argc, 1) * A(args, argc, 3));
}

GMLFN(point_distance_3d) {
    (void)self;
    double dx = A(args, argc, 3) - A(args, argc, 0);
    double dy = A(args, argc, 4) - A(args, argc, 1);
    double dz = A(args, argc, 5) - A(args, argc, 2);
    return Value(std::sqrt(dx * dx + dy * dy + dz * dz));
}

GMLFN(rectangle_in_rectangle) {
    (void)self;
    double sx1 = A(args, argc, 0), sy1 = A(args, argc, 1), sx2 = A(args, argc, 2),
           sy2 = A(args, argc, 3);
    double dx1 = A(args, argc, 4), dy1 = A(args, argc, 5), dx2 = A(args, argc, 6),
           dy2 = A(args, argc, 7);
    if (sx1 >= dx1 && sy1 >= dy1 && sx2 <= dx2 && sy2 <= dy2) return Value(1.0);
    if (sx1 <= dx2 && sx2 >= dx1 && sy1 <= dy2 && sy2 >= dy1) return Value(2.0);
    return Value(0.0);
}

GMLFN(is_numeric) {
    (void)self;
    return Value(argc > 0 && args[0].type == Value::REAL ? 1.0 : 0.0);
}

GMLFN(get_integer) {
    (void)self;
    return Value(A(args, argc, 1));
}

GMLFN(array_set) {
    (void)self;
    if (argc < 3) return Value();
    Value slot = args[0];
    kwik_array_store(slot, (int)A(args, argc, 1), args[2]);
    return Value();
}

GMLFN(array_sort) {
    if (argc < 1 || args[0].type != Value::ARR || !args[0].arr) return Value();
    auto& items = args[0].arr->items;
    if (argc >= 2 && args[1].type == Value::FN && args[1].fn) {
        Value cmp = args[1];
        std::stable_sort(items.begin(), items.end(), [&](const Value& a, const Value& b) {
            Value cargs[2] = {a, b};
            return (double)kwik_call_value(self, cmp, cargs, 2) < 0.0;
        });
        return Value();
    }
    bool asc = argc < 2 || gml_truthy(args[1]);
    std::stable_sort(items.begin(), items.end(), [&](const Value& a, const Value& b) {
        bool lt;
        if (a.type == Value::STR && b.type == Value::STR)
            lt = a.str < b.str;
        else
            lt = (double)a < (double)b;
        return asc ? lt : !lt;
    });
    return Value();
}

GMLFN(variable_struct_get_names) {
    (void)self;
    Value out = mk_array();
    if (argc >= 1 && args[0].type == Value::OBJ && args[0].obj)
        for (const auto& kv : args[0].obj->vars) out.arr->items.push_back(Value(kv.first));
    return out;
}

GMLFN(string_count) {
    (void)self;
    std::string sub = S(args, argc, 0), str = S(args, argc, 1);
    if (sub.empty()) return Value(0.0);
    int n = 0;
    size_t pos = 0;
    while ((pos = str.find(sub, pos)) != std::string::npos) {
        ++n;
        pos += sub.size();
    }
    return Value((double)n);
}

GMLFN(string_starts_with) {
    (void)self;
    std::string str = S(args, argc, 0), prefix = S(args, argc, 1);
    return Value(str.rfind(prefix, 0) == 0 ? 1.0 : 0.0);
}

GMLFN(string_trim_start) {
    (void)self;
    std::string str = S(args, argc, 0);
    size_t i = 0;
    while (i < str.size() && (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n'))
        ++i;
    return Value(str.substr(i));
}

static bool gm_datetime_tm(double v, std::tm& out) {
    time_t t = (time_t)((v - 25569.0) * 86400.0);
    std::tm* g = gmtime(&t);
    if (!g) return false;
    out = *g;
    return true;
}

GMLFN(date_get_year) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)(t.tm_year + 1900) : 0.0);
}
GMLFN(date_get_month) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)(t.tm_mon + 1) : 0.0);
}
GMLFN(date_get_day) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)t.tm_mday : 0.0);
}
GMLFN(date_get_hour) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)t.tm_hour : 0.0);
}
GMLFN(date_get_minute) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)t.tm_min : 0.0);
}
GMLFN(date_get_second) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)t.tm_sec : 0.0);
}
GMLFN(date_get_weekday) {
    (void)self;
    std::tm t{};
    return Value(gm_datetime_tm(A(args, argc, 0), t) ? (double)t.tm_wday : 0.0);
}

static void rgb_of(unsigned int c, double& r, double& g, double& b) {
    r = (double)(c & 0xFF);
    g = (double)((c >> 8) & 0xFF);
    b = (double)((c >> 16) & 0xFF);
}

GMLFN(color_get_hue) {
    (void)self;
    double r, g, b;
    rgb_of(C(args, argc, 0), r, g, b);
    double mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    if (d <= 0) return Value(0.0);
    double h;
    if (mx == r)
        h = std::fmod((g - b) / d, 6.0);
    else if (mx == g)
        h = (b - r) / d + 2.0;
    else
        h = (r - g) / d + 4.0;
    h *= 60.0;
    if (h < 0) h += 360.0;
    return Value(h / 360.0 * 255.0);
}
GMLFN(color_get_saturation) {
    (void)self;
    double r, g, b;
    rgb_of(C(args, argc, 0), r, g, b);
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    if (mx <= 0) return Value(0.0);
    return Value((mx - mn) / mx * 255.0);
}
GMLFN(color_get_value) {
    (void)self;
    double r, g, b;
    rgb_of(C(args, argc, 0), r, g, b);
    return Value(std::max({r, g, b}));
}
GMLFN(colour_get_hue) { return color_get_hue(self, args, argc); }
GMLFN(colour_get_saturation) { return color_get_saturation(self, args, argc); }
GMLFN(colour_get_value) { return color_get_value(self, args, argc); }
GMLFN(colour_get_red) { return color_get_red(self, args, argc); }
GMLFN(colour_get_green) { return color_get_green(self, args, argc); }
GMLFN(colour_get_blue) { return color_get_blue(self, args, argc); }
GMLFN(draw_get_colour) { return draw_get_color(self, args, argc); }

static void surf_quad(int id, double sx, double sy, double sw, double sh, double dx, double dy,
                      double xs, double ys, double angle, unsigned int blend, double alpha) {
    unsigned int tex = render_surface_texture(id);
    if (!tex) return;
    double tw = render_surface_width(id), th = render_surface_height(id);
    if (tw <= 0 || th <= 0) return;
    float u0 = (float)(sx / tw), u1 = (float)((sx + sw) / tw);
    float v0 = (float)(1.0 - sy / th), v1 = (float)(1.0 - (sy + sh) / th);
    render_draw_quad(tex, dx, dy, sw, sh, 0, 0, xs, ys, angle, u0, v0, u1, v1, blend, alpha);
}

GMLFN(draw_surface_part) {
    (void)self;
    surf_quad((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
              A(args, argc, 4), A(args, argc, 5), A(args, argc, 6), 1, 1, 0, 0xFFFFFF, 1.0);
    return Value();
}
GMLFN(draw_surface_part_ext) {
    (void)self;
    surf_quad((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
              A(args, argc, 4), A(args, argc, 5), A(args, argc, 6), A(args, argc, 7, 1),
              A(args, argc, 8, 1), 0, C(args, argc, 9), A(args, argc, 10, 1));
    return Value();
}
GMLFN(draw_surface_stretched) {
    (void)self;
    int id = (int)A(args, argc, 0);
    double sw = render_surface_width(id), sh = render_surface_height(id);
    if (sw <= 0 || sh <= 0) return Value();
    surf_quad(id, 0, 0, sw, sh, A(args, argc, 1), A(args, argc, 2), A(args, argc, 3) / sw,
              A(args, argc, 4) / sh, 0, 0xFFFFFF, 1.0);
    return Value();
}
GMLFN(draw_surface_stretched_ext) {
    (void)self;
    int id = (int)A(args, argc, 0);
    double sw = render_surface_width(id), sh = render_surface_height(id);
    if (sw <= 0 || sh <= 0) return Value();
    surf_quad(id, 0, 0, sw, sh, A(args, argc, 1), A(args, argc, 2), A(args, argc, 3) / sw,
              A(args, argc, 4) / sh, 0, C(args, argc, 5), A(args, argc, 6, 1));
    return Value();
}
static void surf_tiled(int id, double x, double y, double xs, double ys, unsigned int blend,
                       double alpha) {
    double sw = render_surface_width(id) * xs, sh = render_surface_height(id) * ys;
    if (sw <= 0 || sh <= 0) return;
    const Camera& c = g_cameras[g_view_camera[0]];
    double startx = x - std::ceil((x - c.x) / sw) * sw;
    double starty = y - std::ceil((y - c.y) / sh) * sh;
    for (double dy = starty; dy < c.y + c.h; dy += sh)
        for (double dx = startx; dx < c.x + c.w; dx += sw)
            surf_quad(id, 0, 0, render_surface_width(id), render_surface_height(id), dx, dy, xs,
                      ys, 0, blend, alpha);
}
GMLFN(draw_surface_tiled) {
    (void)self;
    surf_tiled((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), 1, 1, 0xFFFFFF, 1.0);
    return Value();
}
GMLFN(draw_surface_tiled_ext) {
    (void)self;
    surf_tiled((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3, 1),
               A(args, argc, 4, 1), C(args, argc, 5), A(args, argc, 6, 1));
    return Value();
}
GMLFN(draw_surface_general) {
    (void)self;
    surf_quad((int)A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
              A(args, argc, 4), A(args, argc, 5), A(args, argc, 6), A(args, argc, 7, 1),
              A(args, argc, 8, 1), A(args, argc, 9), C(args, argc, 10), A(args, argc, 14, 1));
    return Value();
}

GMLFN(surface_copy) {
    (void)self;
    int dest = (int)A(args, argc, 0);
    int src = (int)A(args, argc, 3);
    if (!render_surface_exists(dest) || !render_surface_exists(src)) return Value();
    if (!render_surface_set_target(dest)) return Value();
    surf_quad(src, 0, 0, render_surface_width(src), render_surface_height(src), A(args, argc, 1),
              A(args, argc, 2), 1, 1, 0, 0xFFFFFF, 1.0);
    render_surface_reset_target();
    return Value();
}
GMLFN(surface_copy_part) {
    (void)self;
    int dest = (int)A(args, argc, 0);
    int src = (int)A(args, argc, 3);
    if (!render_surface_exists(dest) || !render_surface_exists(src)) return Value();
    if (!render_surface_set_target(dest)) return Value();
    surf_quad(src, A(args, argc, 4), A(args, argc, 5), A(args, argc, 6), A(args, argc, 7),
              A(args, argc, 1), A(args, argc, 2), 1, 1, 0, 0xFFFFFF, 1.0);
    render_surface_reset_target();
    return Value();
}
GMLFN(surface_resize) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(surface_save) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(surface_save_part) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(surface_depth_disable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(surface_get_depth_disable) { (void)self; (void)args; (void)argc; return Value(1.0); }

GMLFN(draw_tile) {
    (void)self;
    int ts_idx = (int)A(args, argc, 0);
    if (ts_idx < 0 || ts_idx >= g_tileset_count) return Value();
    const KwikTileset& ts = g_tilesets[ts_idx];
    if (ts.image < 0 || ts.columns <= 0) return Value();
    uint32_t cell = (uint32_t)(long long)A(args, argc, 1);
    uint32_t idx = cell & 0x0007FFFF;
    idx = kwik_tileset_frame_index(ts, idx);
    int strideX = ts.tile_w + 2 * ts.border_x;
    int strideY = ts.tile_h + 2 * ts.border_y;
    int col = idx % ts.columns;
    int row = idx / ts.columns;
    bool mirror = cell & 0x10000000;
    bool flip = cell & 0x20000000;
    bool rot = cell & 0x40000000;
    if (rot) {
        kwik_draw_image_part_rot(ts.image, col * strideX + ts.border_x,
                                 row * strideY + ts.border_y, ts.tile_w, ts.tile_h,
                                 A(args, argc, 3) + ts.tile_w / 2.0,
                                 A(args, argc, 4) + ts.tile_h / 2.0, ts.tile_w / 2.0,
                                 ts.tile_h / 2.0, mirror ? -1.0 : 1.0, flip ? -1.0 : 1.0, -90.0,
                                 0xFFFFFF, render_get_alpha());
    } else {
        double dx = A(args, argc, 3) + (mirror ? ts.tile_w : 0);
        double dy = A(args, argc, 4) + (flip ? ts.tile_h : 0);
        kwik_draw_image_part(ts.image, col * strideX + ts.border_x, row * strideY + ts.border_y,
                             ts.tile_w, ts.tile_h, dx, dy, mirror ? -1.0 : 1.0, flip ? -1.0 : 1.0,
                             0xFFFFFF, render_get_alpha());
    }
    return Value();
}

GMLFN(draw_text_ext_transformed_color) {
    (void)self;
    if (argc < 8) return Value();
    unsigned int saved_c = render_get_color();
    double saved_a = render_get_alpha();
    render_set_color(C(args, argc, 8));
    render_set_alpha(A(args, argc, 12, 1));
    kwik_draw_text_ext_rt(A(args, argc, 0), A(args, argc, 1), S(args, argc, 2), A(args, argc, 3),
                          A(args, argc, 4), A(args, argc, 5, 1), A(args, argc, 6, 1),
                          A(args, argc, 7));
    render_set_color(saved_c);
    render_set_alpha(saved_a);
    return Value();
}
GMLFN(draw_text_ext_transformed_colour) {
    return draw_text_ext_transformed_color(self, args, argc);
}
GMLFN(draw_text_ext_color) {
    (void)self;
    if (argc < 5) return Value();
    unsigned int saved_c = render_get_color();
    double saved_a = render_get_alpha();
    render_set_color(C(args, argc, 5));
    render_set_alpha(A(args, argc, 9, 1));
    kwik_draw_text_ext_rt(A(args, argc, 0), A(args, argc, 1), S(args, argc, 2), A(args, argc, 3),
                          A(args, argc, 4), 1, 1, 0);
    render_set_color(saved_c);
    render_set_alpha(saved_a);
    return Value();
}
GMLFN(draw_text_ext_colour) { return draw_text_ext_color(self, args, argc); }

GMLFN(gpu_get_blendmode) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_blendmode); }
GMLFN(gpu_get_blendenable) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(gpu_get_blendmode_src) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_blend_src); }
GMLFN(gpu_get_blendmode_dest) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_blend_dst); }
GMLFN(gpu_get_alphatestenable) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_alphatest); }
GMLFN(gpu_get_colorwriteenable) {
    (void)self; (void)args; (void)argc;
    Value out = mk_array();
    for (int i = 0; i < 4; ++i) out.arr->items.push_back(Value((double)g_gpu_colorwrite[i]));
    return out;
}
GMLFN(gpu_get_colourwriteenable) { return gpu_get_colorwriteenable(self, args, argc); }
GMLFN(gpu_get_blendmode_ext) {
    (void)self; (void)args; (void)argc;
    Value out = mk_array();
    out.arr->items.push_back(Value((double)g_gpu_blend_src));
    out.arr->items.push_back(Value((double)g_gpu_blend_dst));
    return out;
}
GMLFN(gpu_get_tex_filter) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gpu_get_texrepeat) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gpu_get_ztestenable) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gpu_get_zwriteenable) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gpu_get_cullmode) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gpu_set_blendmode_ext_sepalpha) {
    (void)self;
    g_gpu_blend_src = (int)A(args, argc, 0, 2);
    g_gpu_blend_dst = (int)A(args, argc, 1, 6);
    render_set_blendmode_sepalpha(g_gpu_blend_src, g_gpu_blend_dst, (int)A(args, argc, 2, 2),
                                  (int)A(args, argc, 3, 6));
    return Value();
}
GMLFN(gpu_set_colourwriteenable) { return gpu_set_colorwriteenable(self, args, argc); }
GMLFN(gpu_set_cullmode) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_tex_filter) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_tex_repeat) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_texrepeat) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_ztestenable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_zwriteenable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(draw_set_lighting) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(draw_enable_drawevent) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(draw_flush) { (void)self; (void)args; (void)argc; return Value(); }

static Value mat_value(const double m[16]) {
    Value out = mk_array();
    for (int i = 0; i < 16; ++i) out.arr->items.push_back(Value(m[i]));
    return out;
}
static void mat_read(const Value& v, double m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0 : 0.0;
    if (v.type != Value::ARR || !v.arr) return;
    for (int i = 0; i < 16 && i < (int)v.arr->items.size(); ++i)
        m[i] = (double)v.arr->items[i];
}
static double g_matrix_store[3][16] = {
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
};

GMLFN(matrix_build_identity) {
    (void)self; (void)args; (void)argc;
    static const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return mat_value(id);
}
GMLFN(matrix_build) {
    (void)self;
    double x = A(args, argc, 0), y = A(args, argc, 1), z = A(args, argc, 2);
    double xr = A(args, argc, 3) * 3.14159265358979323846 / 180.0;
    double yr = A(args, argc, 4) * 3.14159265358979323846 / 180.0;
    double zr = A(args, argc, 5) * 3.14159265358979323846 / 180.0;
    double xs = A(args, argc, 6, 1), ys = A(args, argc, 7, 1), zs = A(args, argc, 8, 1);
    double cx = std::cos(xr), sx = std::sin(xr);
    double cy = std::cos(yr), sy = std::sin(yr);
    double cz = std::cos(zr), sz = std::sin(zr);
    double r00 = cy * cz, r01 = cy * sz, r02 = -sy;
    double r10 = sx * sy * cz - cx * sz, r11 = sx * sy * sz + cx * cz, r12 = sx * cy;
    double r20 = cx * sy * cz + sx * sz, r21 = cx * sy * sz - sx * cz, r22 = cx * cy;
    double m[16] = {r00 * xs, r01 * xs, r02 * xs, 0, r10 * ys, r11 * ys, r12 * ys, 0,
                    r20 * zs, r21 * zs, r22 * zs, 0, x,        y,        z,        1};
    return mat_value(m);
}
GMLFN(matrix_build_lookat) {
    (void)self;
    double fx = A(args, argc, 0), fy = A(args, argc, 1), fz = A(args, argc, 2);
    double tx = A(args, argc, 3), ty = A(args, argc, 4), tz = A(args, argc, 5);
    double ux = A(args, argc, 6), uy = A(args, argc, 7), uz = A(args, argc, 8);
    double zx = tx - fx, zy = ty - fy, zz = tz - fz;
    double zl = std::sqrt(zx * zx + zy * zy + zz * zz);
    if (zl > 0) { zx /= zl; zy /= zl; zz /= zl; }
    double xx = uy * zz - uz * zy, xy = uz * zx - ux * zz, xz = ux * zy - uy * zx;
    double xl = std::sqrt(xx * xx + xy * xy + xz * xz);
    if (xl > 0) { xx /= xl; xy /= xl; xz /= xl; }
    double yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
    double m[16] = {xx, yx, zx, 0, xy, yy, zy, 0, xz, yz, zz, 0,
                    -(xx * fx + xy * fy + xz * fz), -(yx * fx + yy * fy + yz * fz),
                    -(zx * fx + zy * fy + zz * fz), 1};
    return mat_value(m);
}
GMLFN(matrix_build_projection_ortho) {
    (void)self;
    double w = A(args, argc, 0), h = A(args, argc, 1);
    double zn = A(args, argc, 2), zf = A(args, argc, 3);
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (zf == zn) zf = zn + 1;
    double m[16] = {2.0 / w, 0, 0, 0, 0, 2.0 / h, 0, 0, 0, 0, 1.0 / (zf - zn), 0,
                    0, 0, zn / (zn - zf), 1};
    return mat_value(m);
}
GMLFN(matrix_build_projection_perspective) {
    (void)self;
    double w = A(args, argc, 0), h = A(args, argc, 1);
    double zn = A(args, argc, 2), zf = A(args, argc, 3);
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (zf == zn) zf = zn + 1;
    double m[16] = {2.0 * zn / w, 0, 0, 0, 0, 2.0 * zn / h, 0, 0,
                    0, 0, zf / (zf - zn), 1, 0, 0, zn * zf / (zn - zf), 0};
    return mat_value(m);
}
GMLFN(matrix_build_projection_perspective_fov) {
    (void)self;
    double fov = A(args, argc, 0) * 3.14159265358979323846 / 180.0;
    double aspect = A(args, argc, 1);
    double zn = A(args, argc, 2), zf = A(args, argc, 3);
    double ys = 1.0 / std::tan(fov / 2.0);
    double xs = aspect != 0 ? ys / aspect : ys;
    if (zf == zn) zf = zn + 1;
    double m[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1,
                    0, 0, zn * zf / (zn - zf), 0};
    return mat_value(m);
}
GMLFN(matrix_get) {
    (void)self;
    int which = (int)A(args, argc, 0);
    if (which < 0 || which > 2) which = 0;
    return mat_value(g_matrix_store[which]);
}
GMLFN(matrix_set) {
    (void)self;
    int which = (int)A(args, argc, 0);
    if (which < 0 || which > 2) return Value();
    if (argc >= 2) mat_read(args[1], g_matrix_store[which]);
    return Value();
}
GMLFN(matrix_multiply) {
    (void)self;
    double a[16], b[16], m[16];
    mat_read(argc >= 1 ? args[0] : Value(), a);
    mat_read(argc >= 2 ? args[1] : Value(), b);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
            m[c * 4 + r] = sum;
        }
    return mat_value(m);
}

GMLFN(shader_current) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(shader_is_compiled) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(shader_set_uniform_f_array) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_set_uniform_i) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_set_uniform_matrix) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_set_uniform_matrix_array) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_replace_simple_init_raw) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_replace_simple_raw) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(shader_replace_simple_sync) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(texture_get_uvs) {
    (void)self; (void)args; (void)argc;
    Value out = mk_array();
    out.arr->items.push_back(Value(0.0));
    out.arr->items.push_back(Value(0.0));
    out.arr->items.push_back(Value(1.0));
    out.arr->items.push_back(Value(1.0));
    return out;
}
GMLFN(texture_get_width) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(texture_get_height) { (void)self; (void)args; (void)argc; return Value(1.0); }

GMLFN(vertex_begin) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_end) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_position) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_position_3d) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_color) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_colour) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_normal) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_texcoord) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_submit) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_delete_buffer) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_freeze) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_create_buffer_ext) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(vertex_format_add_position) { (void)self; (void)args; (void)argc; return Value(); }

static int g_next_part_id = 1;
GMLFN(part_system_create) { (void)self; (void)args; (void)argc; return Value((double)g_next_part_id++); }
GMLFN(part_system_create_layer) { (void)self; (void)args; (void)argc; return Value((double)g_next_part_id++); }
GMLFN(part_system_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_exists) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(part_system_clear) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_depth) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_position) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_automatic_update) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_automatic_draw) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_update) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_system_drawit) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_create) { (void)self; (void)args; (void)argc; return Value((double)g_next_part_id++); }
GMLFN(part_type_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_exists) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(part_type_clear) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_shape) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_sprite) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_size) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_scale) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_speed) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_direction) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_gravity) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_orientation) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_color1) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_color2) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_color3) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_colour1) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_colour2) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_colour3) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_alpha1) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_alpha2) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_alpha3) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_life) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_type_blend) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_particles_create) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_particles_create_color) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_particles_create_colour) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_particles_clear) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_particles_count) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(part_emitter_create) { (void)self; (void)args; (void)argc; return Value((double)g_next_part_id++); }
GMLFN(part_emitter_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_emitter_region) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_emitter_burst) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_emitter_stream) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(part_emitter_destroy_all) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(video_open) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_close) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_get_status) { (void)self; (void)args; (void)argc; return Value(3.0); }
GMLFN(video_draw) {
    (void)self; (void)args; (void)argc;
    Value out = mk_array();
    out.arr->items.push_back(Value(-1.0));
    out.arr->items.push_back(Value(-1.0));
    return out;
}
GMLFN(video_set_volume) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_pause) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_resume) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_enable_loop) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_seek_to) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(video_get_duration) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(video_get_position) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(video_get_format) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(video_is_looping) { (void)self; (void)args; (void)argc; return Value(0.0); }

GMLFN(gif_open) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(gif_add_surface) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gif_add_image) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gif_save) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gif_save_buffer) { (void)self; (void)args; (void)argc; return Value(-1.0); }

GMLFN(audio_exists) {
    (void)self;
    int idx = (int)A(args, argc, 0, -1);
    return Value(idx >= 0 && idx < g_sound_count ? 1.0 : 0.0);
}
GMLFN(audio_get_name) {
    (void)self;
    int idx = (int)A(args, argc, 0, -1);
    if (idx >= 0 && idx < g_sound_count) return Value(g_sound_table[idx].name);
    return Value("<undefined>");
}
GMLFN(audio_play_sound_on) {
    if (argc < 2) return Value(-1.0);
    Value a[3] = {args[1], Value(0.0), argc > 2 ? args[2] : Value(0.0)};
    return audio_play_sound(self, a, 3);
}
GMLFN(audio_play_sound_at) {
    if (argc < 1) return Value(-1.0);
    Value a[3] = {args[0], Value(0.0), argc > 7 ? args[7] : Value(0.0)};
    return audio_play_sound(self, a, 3);
}
static int g_next_emitter_id = 1;
GMLFN(audio_emitter_create) { (void)self; (void)args; (void)argc; return Value((double)g_next_emitter_id++); }
GMLFN(audio_emitter_exists) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(audio_emitter_free) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_emitter_position) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_emitter_gain) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_emitter_pitch) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_emitter_falloff) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_emitter_velocity) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_falloff_set_model) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_listener_position) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_listener_orientation) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_listener_velocity) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(audio_channel_num) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(animcurve_get) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(animcurve_exists) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(animcurve_get_channel) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(animcurve_get_channel_index) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(animcurve_channel_evaluate) { (void)self; (void)args; (void)argc; return Value(0.0); }

GMLFN(layer_background_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_background_index) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_background_speed) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_element_move) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_instance_get_instance) {
    (void)self;
    return argc >= 1 ? args[0] : Value(-4.0);
}
GMLFN(layer_shader) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_alpha) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_blend) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_angle) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_xscale) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_yscale) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_index) { (void)self; (void)args; (void)argc; return Value(); }
static int g_next_layer_sprite_id = 800000;
GMLFN(layer_sprite_create) { (void)self; (void)args; (void)argc; return Value((double)g_next_layer_sprite_id++); }
GMLFN(layer_sprite_exists) { (void)self; (void)args; (void)argc; return Value(0.0); }

static RtLayer* tm_layer(const Value* args, int argc, int i) {
    int id = (int)A(args, argc, i, -1);
    if (id >= 900000) id -= 900000;
    return kwik_layer_by_id(id);
}
static const KwikTileset* tm_tileset(const RtLayer* l) {
    if (!l || l->tileset < 0 || l->tileset >= g_tileset_count) return nullptr;
    return &g_tilesets[l->tileset];
}

GMLFN(tilemap_get) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    if (!l || l->grid_blob < 0) return Value(-1.0);
    int cx = (int)A(args, argc, 1), cy = (int)A(args, argc, 2);
    if (cx < 0 || cy < 0 || cx >= l->grid_w || cy >= l->grid_h) return Value(-1.0);
    const uint32_t* grid = kwik_tilemap_grid(l->grid_blob, l->grid_w * l->grid_h);
    if (!grid) return Value(-1.0);
    return Value((double)grid[cy * l->grid_w + cx]);
}
GMLFN(tilemap_set) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    if (!l || l->grid_blob < 0) return Value(0.0);
    int cx = (int)A(args, argc, 2), cy = (int)A(args, argc, 3);
    if (cx < 0 || cy < 0 || cx >= l->grid_w || cy >= l->grid_h) return Value(0.0);
    uint32_t* grid = kwik_tilemap_grid_mut(l->grid_blob, l->grid_w * l->grid_h);
    if (!grid) return Value(0.0);
    grid[cy * l->grid_w + cx] = (uint32_t)(long long)A(args, argc, 1);
    return Value(1.0);
}
GMLFN(tilemap_get_at_pixel) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    const KwikTileset* ts = tm_tileset(l);
    if (!l || !ts || l->grid_blob < 0 || ts->tile_w <= 0 || ts->tile_h <= 0) return Value(-1.0);
    int cx = (int)std::floor((A(args, argc, 1) - l->x) / ts->tile_w);
    int cy = (int)std::floor((A(args, argc, 2) - l->y) / ts->tile_h);
    if (cx < 0 || cy < 0 || cx >= l->grid_w || cy >= l->grid_h) return Value(-1.0);
    const uint32_t* grid = kwik_tilemap_grid(l->grid_blob, l->grid_w * l->grid_h);
    if (!grid) return Value(-1.0);
    return Value((double)grid[cy * l->grid_w + cx]);
}
GMLFN(tilemap_set_at_pixel) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    const KwikTileset* ts = tm_tileset(l);
    if (!l || !ts || l->grid_blob < 0 || ts->tile_w <= 0 || ts->tile_h <= 0) return Value(0.0);
    int cx = (int)std::floor((A(args, argc, 2) - l->x) / ts->tile_w);
    int cy = (int)std::floor((A(args, argc, 3) - l->y) / ts->tile_h);
    if (cx < 0 || cy < 0 || cx >= l->grid_w || cy >= l->grid_h) return Value(0.0);
    uint32_t* grid = kwik_tilemap_grid_mut(l->grid_blob, l->grid_w * l->grid_h);
    if (!grid) return Value(0.0);
    grid[cy * l->grid_w + cx] = (uint32_t)(long long)A(args, argc, 1);
    return Value(1.0);
}
GMLFN(tilemap_get_cell_x_at_pixel) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    const KwikTileset* ts = tm_tileset(l);
    if (!l || !ts || ts->tile_w <= 0) return Value(-1.0);
    return Value(std::floor((A(args, argc, 1) - l->x) / ts->tile_w));
}
GMLFN(tilemap_get_cell_y_at_pixel) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    const KwikTileset* ts = tm_tileset(l);
    if (!l || !ts || ts->tile_h <= 0) return Value(-1.0);
    return Value(std::floor((A(args, argc, 2) - l->y) / ts->tile_h));
}
GMLFN(tilemap_get_width) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    return Value(l ? (double)l->grid_w : 0.0);
}
GMLFN(tilemap_get_height) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    return Value(l ? (double)l->grid_h : 0.0);
}
GMLFN(tilemap_get_tileset) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    return Value(l ? (double)l->tileset : -1.0);
}
GMLFN(tilemap_get_tile_width) {
    (void)self;
    const KwikTileset* ts = tm_tileset(tm_layer(args, argc, 0));
    return Value(ts ? (double)ts->tile_w : 0.0);
}
GMLFN(tilemap_get_tile_height) {
    (void)self;
    const KwikTileset* ts = tm_tileset(tm_layer(args, argc, 0));
    return Value(ts ? (double)ts->tile_h : 0.0);
}
GMLFN(tilemap_get_frame) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(tilemap_tileset) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    if (l) l->tileset = (int)A(args, argc, 1, -1);
    return Value();
}
GMLFN(tilemap_clear) {
    (void)self;
    RtLayer* l = tm_layer(args, argc, 0);
    if (!l || l->grid_blob < 0) return Value();
    uint32_t* grid = kwik_tilemap_grid_mut(l->grid_blob, l->grid_w * l->grid_h);
    if (!grid) return Value();
    uint32_t v = (uint32_t)(long long)A(args, argc, 1);
    for (int i = 0; i < l->grid_w * l->grid_h; ++i) grid[i] = v;
    return Value();
}

GMLFN(tile_get_index) {
    (void)self;
    return Value((double)((uint32_t)(long long)A(args, argc, 0) & 0x0007FFFF));
}
GMLFN(tile_get_mirror) {
    (void)self;
    return Value(((uint32_t)(long long)A(args, argc, 0) & 0x10000000) ? 1.0 : 0.0);
}
GMLFN(tile_get_flip) {
    (void)self;
    return Value(((uint32_t)(long long)A(args, argc, 0) & 0x20000000) ? 1.0 : 0.0);
}
GMLFN(tile_get_rotate) {
    (void)self;
    return Value(((uint32_t)(long long)A(args, argc, 0) & 0x40000000) ? 1.0 : 0.0);
}
GMLFN(tile_set_index) {
    (void)self;
    uint32_t cell = (uint32_t)(long long)A(args, argc, 0);
    uint32_t idx = (uint32_t)(long long)A(args, argc, 1) & 0x0007FFFF;
    return Value((double)((cell & ~0x0007FFFFu) | idx));
}
GMLFN(tile_set_mirror) {
    (void)self;
    uint32_t cell = (uint32_t)(long long)A(args, argc, 0);
    return Value((double)(gml_truthy(argc > 1 ? args[1] : Value()) ? (cell | 0x10000000u)
                                                                   : (cell & ~0x10000000u)));
}
GMLFN(tile_set_flip) {
    (void)self;
    uint32_t cell = (uint32_t)(long long)A(args, argc, 0);
    return Value((double)(gml_truthy(argc > 1 ? args[1] : Value()) ? (cell | 0x20000000u)
                                                                   : (cell & ~0x20000000u)));
}
GMLFN(tile_set_rotate) {
    (void)self;
    uint32_t cell = (uint32_t)(long long)A(args, argc, 0);
    return Value((double)(gml_truthy(argc > 1 ? args[1] : Value()) ? (cell | 0x40000000u)
                                                                   : (cell & ~0x40000000u)));
}

GMLFN(sprite_get_info) {
    int idx = (int)A(args, argc, 0, -1);
    Value out = kwik_new_object(self, nullptr, 0);
    if (idx < 0 || idx >= g_sprite_count || !out.obj) return out;
    const KwikSprite& s = g_sprites[idx];
    auto& v = out.obj->vars;
    v["width"] = Value((double)s.width);
    v["height"] = Value((double)s.height);
    v["xoffset"] = Value((double)s.origin_x);
    v["yoffset"] = Value((double)s.origin_y);
    v["bbox_left"] = Value((double)s.bbox_left);
    v["bbox_top"] = Value((double)s.bbox_top);
    v["bbox_right"] = Value((double)s.bbox_right);
    v["bbox_bottom"] = Value((double)s.bbox_bottom);
    v["num_subimages"] = Value((double)s.frame_count);
    v["frame_speed"] = Value(s.speed);
    v["frame_type"] = Value((double)s.speed_type);
    v["name"] = Value(s.name);
    return out;
}
GMLFN(sprite_get_speed_type) {
    (void)self;
    int idx = (int)A(args, argc, 0, -1);
    if (idx < 0 || idx >= g_sprite_count) return Value(0.0);
    return Value((double)g_sprites[idx].speed_type);
}
GMLFN(sprite_get_speed) {
    (void)self;
    int idx = (int)A(args, argc, 0, -1);
    if (idx < 0 || idx >= g_sprite_count) return Value(0.0);
    return Value(g_sprites[idx].speed);
}
GMLFN(sprite_set_bbox_mode) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(sprite_set_speed) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(sprite_collision_mask) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(camera_get_view_mat) {
    (void)self; (void)args; (void)argc;
    static const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return mat_value(id);
}
GMLFN(camera_get_proj_mat) {
    (void)self; (void)args; (void)argc;
    static const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return mat_value(id);
}
GMLFN(camera_set_view_mat) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(camera_set_proj_mat) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(camera_apply) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(camera_destroy) { (void)self; (void)args; (void)argc; return Value(); }

}
