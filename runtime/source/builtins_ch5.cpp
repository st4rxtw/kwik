#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#ifdef KWIK_USE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

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

static void queue_video_event(Instance* self, const char* type) {
    Value map = ds_map_create(self, nullptr, 0);
    Value entry[3] = {map, Value("type"), Value(type)};
    ds_map_add(self, entry, 3);
    kwik_queue_async(ASYNC_WEB_EV, (int)(double)map);
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

static double tri_sign(double px, double py, double ax, double ay, double bx, double by) {
    return (px - bx) * (ay - by) - (ax - bx) * (py - by);
}
static bool point_in_triangle(double px, double py, double x1, double y1, double x2, double y2,
                              double x3, double y3) {
    double d1 = tri_sign(px, py, x1, y1, x2, y2);
    double d2 = tri_sign(px, py, x2, y2, x3, y3);
    double d3 = tri_sign(px, py, x3, y3, x1, y1);
    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}
static bool segments_intersect(double ax, double ay, double bx, double by, double cx, double cy,
                               double dx, double dy) {
    auto cross = [](double ox, double oy, double ax_, double ay_, double bx_, double by_) {
        return (ax_ - ox) * (by_ - oy) - (ay_ - oy) * (bx_ - ox);
    };
    double d1 = cross(cx, cy, dx, dy, ax, ay);
    double d2 = cross(cx, cy, dx, dy, bx, by);
    double d3 = cross(ax, ay, bx, by, cx, cy);
    double d4 = cross(ax, ay, bx, by, dx, dy);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
GMLFN(rectangle_in_triangle) {
    (void)self;
    double rx1 = A(args, argc, 0), ry1 = A(args, argc, 1), rx2 = A(args, argc, 2),
           ry2 = A(args, argc, 3);
    double x1 = A(args, argc, 4), y1 = A(args, argc, 5);
    double x2 = A(args, argc, 6), y2 = A(args, argc, 7);
    double x3 = A(args, argc, 8), y3 = A(args, argc, 9);
    double cxs[4] = {rx1, rx2, rx2, rx1};
    double cys[4] = {ry1, ry1, ry2, ry2};
    int inside_count = 0;
    for (int i = 0; i < 4; ++i)
        if (point_in_triangle(cxs[i], cys[i], x1, y1, x2, y2, x3, y3)) ++inside_count;
    if (inside_count == 4) return Value(1.0);
    if (inside_count > 0) return Value(2.0);
    if (point_in_triangle(x1, y1, rx1, ry1, rx2, ry1, rx2, ry2) ||
        point_in_triangle(x1, y1, rx1, ry1, rx2, ry2, rx1, ry2) ||
        (x1 >= rx1 && x1 <= rx2 && y1 >= ry1 && y1 <= ry2) ||
        (x2 >= rx1 && x2 <= rx2 && y2 >= ry1 && y2 <= ry2) ||
        (x3 >= rx1 && x3 <= rx2 && y3 >= ry1 && y3 <= ry2))
        return Value(2.0);
    double trix[3] = {x1, x2, x3}, triy[3] = {y1, y2, y3};
    for (int i = 0; i < 3; ++i) {
        double ex1 = trix[i], ey1 = triy[i];
        double ex2 = trix[(i + 1) % 3], ey2 = triy[(i + 1) % 3];
        for (int j = 0; j < 4; ++j) {
            double sx1 = cxs[j], sy1 = cys[j];
            double sx2 = cxs[(j + 1) % 4], sy2 = cys[(j + 1) % 4];
            if (segments_intersect(ex1, ey1, ex2, ey2, sx1, sy1, sx2, sy2)) return Value(2.0);
        }
    }
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

GMLFN(string_ends_with) {
    (void)self;
    std::string str = S(args, argc, 0), suffix = S(args, argc, 1);
    if (suffix.size() > str.size()) return Value(0.0);
    return Value(str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0 ? 1.0 : 0.0);
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

static double g_date_timezone = 0.0;
GMLFN(date_get_timezone) { (void)self; (void)args; (void)argc; return Value(g_date_timezone); }
GMLFN(date_set_timezone) {
    (void)self;
    if (argc >= 1) g_date_timezone = (double)args[0];
    return Value();
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
GMLFN(gpu_get_alphatestref) { (void)self; (void)args; (void)argc; return Value(g_gpu_alphatest_ref); }
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
static int g_gpu_tex_filter = 0;
static int g_gpu_texrepeat = 0;

GMLFN(gpu_get_tex_filter) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_tex_filter); }
GMLFN(gpu_get_texfilter) { return gpu_get_tex_filter(self, args, argc); }
GMLFN(gpu_get_texfilter_ext) { return gpu_get_tex_filter(self, args, argc); }
GMLFN(gpu_get_texrepeat) { (void)self; (void)args; (void)argc; return Value((double)g_gpu_texrepeat); }
GMLFN(gpu_get_depth) { (void)self; (void)args; (void)argc; return Value(render_get_depth()); }
GMLFN(gpu_get_ztestenable) { (void)self; (void)args; (void)argc; return Value(render_get_ztest() ? 1.0 : 0.0); }
GMLFN(gpu_get_zfunc) { (void)self; (void)args; (void)argc; return Value((double)render_get_zfunc()); }
GMLFN(gpu_get_zwriteenable) { (void)self; (void)args; (void)argc; return Value(render_get_zwrite() ? 1.0 : 0.0); }
GMLFN(gpu_get_cullmode) { (void)self; (void)args; (void)argc; return Value((double)render_get_cullmode()); }
GMLFN(gpu_set_blendmode_ext_sepalpha) {
    (void)self;
    g_gpu_blend_src = (int)A(args, argc, 0, 2);
    g_gpu_blend_dst = (int)A(args, argc, 1, 6);
    render_set_blendmode_sepalpha(g_gpu_blend_src, g_gpu_blend_dst, (int)A(args, argc, 2, 2),
                                  (int)A(args, argc, 3, 6));
    return Value();
}
GMLFN(gpu_set_colourwriteenable) { return gpu_set_colorwriteenable(self, args, argc); }
GMLFN(gpu_set_depth) {
    (void)self;
    render_set_depth(A(args, argc, 0));
    return Value();
}
GMLFN(gpu_set_cullmode) {
    (void)self;
    render_set_cullmode((int)A(args, argc, 0));
    return Value();
}
GMLFN(gpu_set_tex_filter) {
    (void)self;
    g_gpu_tex_filter = argc > 0 && gml_truthy(args[0]) ? 1 : 0;
    return Value();
}
GMLFN(gpu_set_tex_repeat) {
    (void)self;
    g_gpu_texrepeat = argc > 0 && gml_truthy(args[0]) ? 1 : 0;
    render_set_texture_repeat(0, g_gpu_texrepeat != 0);
    return Value();
}
GMLFN(gpu_set_texrepeat) { return gpu_set_tex_repeat(self, args, argc); }
GMLFN(gpu_set_texrepeat_ext) {
    (void)self;
    int sampler = (int)A(args, argc, 0, 0);
    bool repeat = argc > 1 && gml_truthy(args[1]);
    render_set_texture_repeat(sampler, repeat);
    if (sampler == 0) g_gpu_texrepeat = repeat ? 1 : 0;
    return Value();
}
GMLFN(gpu_set_ztestenable) {
    (void)self;
    render_set_ztest(argc > 0 && gml_truthy(args[0]));
    return Value();
}
GMLFN(gpu_set_zfunc) {
    (void)self;
    render_set_zfunc((int)A(args, argc, 0, 4));
    return Value();
}
GMLFN(gpu_set_zwriteenable) {
    (void)self;
    render_set_zwrite(argc > 0 && gml_truthy(args[0]));
    return Value();
}
GMLFN(draw_set_lighting) { return d3d_set_lighting(self, args, argc); }
GMLFN(draw_enable_drawevent) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(draw_flush) { (void)self; (void)args; (void)argc; return Value(); }

static Value mat_value(const double m[16]) {
    Value out = mk_array();
    for (int i = 0; i < 16; ++i) out.arr->items.push_back(Value(m[i]));
    return out;
}
static void mat_identity(double m[16]) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0;
    m[0] = m[5] = m[10] = m[15] = 1.0;
}
static void mat_read(const Value& v, double m[16]) {
    mat_identity(m);
    if (v.type != Value::ARR || !v.arr) return;
    for (int i = 0; i < 16 && i < (int)v.arr->items.size(); ++i)
        m[i] = (double)v.arr->items[i];
}
static void mat_copy(double dst[16], const double src[16]) {
    for (int i = 0; i < 16; ++i) dst[i] = src[i];
}
static void mat_mul_raw(const double a[16], const double b[16], double m[16]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
            m[r * 4 + c] = sum;
        }
}
static void mat_translation(double x, double y, double z, double m[16]) {
    mat_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}
static void mat_scaling(double x, double y, double z, double m[16]) {
    mat_identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
}
static void mat_rotation_axis(double x, double y, double z, double deg, double m[16]) {
    double len = std::sqrt(x * x + y * y + z * z);
    if (len <= 0.0) {
        mat_identity(m);
        return;
    }
    x /= len;
    y /= len;
    z /= len;
    double a = deg * 3.14159265358979323846 / 180.0;
    double c = std::cos(a), s = std::sin(a), t = 1.0 - c;
    m[0] = t * x * x + c;     m[1] = t * x * y - s * z; m[2] = t * x * z + s * y; m[3] = 0;
    m[4] = t * x * y + s * z; m[5] = t * y * y + c;     m[6] = t * y * z - s * x; m[7] = 0;
    m[8] = t * x * z - s * y; m[9] = t * y * z + s * x; m[10] = t * z * z + c;    m[11] = 0;
    m[12] = 0;                m[13] = 0;                m[14] = 0;                m[15] = 1;
}
static double g_matrix_store[3][16] = {
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
};

static void set_world_matrix(const double m[16]) {
    mat_copy(g_matrix_store[2], m);
    render_set_matrix(2, g_matrix_store[2]);
}
static void add_world_matrix(const double m[16]) {
    double out[16];
    mat_mul_raw(g_matrix_store[2], m, out);
    set_world_matrix(out);
}

GMLFN(matrix_build_identity) {
    (void)self; (void)args; (void)argc;
    static const double id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    return mat_value(id);
}
GMLFN(matrix_build) {
    (void)self;
    double x = A(args, argc, 0), y = A(args, argc, 1), z = A(args, argc, 2);
    double xr = -A(args, argc, 3) * 3.14159265358979323846 / 180.0;
    double yr = -A(args, argc, 4) * 3.14159265358979323846 / 180.0;
    double zr = -A(args, argc, 5) * 3.14159265358979323846 / 180.0;
    double xs = A(args, argc, 6, 1), ys = A(args, argc, 7, 1), zs = A(args, argc, 8, 1);
    double sinp = std::sin(xr), cosp = std::cos(xr);
    double sinh = std::sin(yr), cosh = std::cos(yr);
    double sinr = std::sin(zr), cosr = std::cos(zr);
    double sinrsinp = -sinr * -sinp;
    double cosrsinp = cosr * -sinp;
    double m[16] = {
        ((cosr * cosh) + (sinrsinp * -sinh)) * xs,
        ((sinr * cosh) + (cosrsinp * -sinh)) * ys,
        (cosp * -sinh) * zs,
        0,
        (-sinr * cosp) * xs,
        (cosr * cosp) * ys,
        sinp * zs,
        0,
        ((cosr * sinh) + (sinrsinp * cosh)) * xs,
        ((sinr * sinh) + (cosrsinp * cosh)) * ys,
        (cosp * cosh) * zs,
        0,
        x,
        y,
        z,
        1,
    };
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
    render_get_matrix(which, g_matrix_store[which]);
    return mat_value(g_matrix_store[which]);
}
GMLFN(matrix_set) {
    (void)self;
    int which = (int)A(args, argc, 0);
    if (which < 0 || which > 2) return Value();
    if (argc >= 2) {
        mat_read(args[1], g_matrix_store[which]);
        render_set_matrix(which, g_matrix_store[which]);
    }
    return Value();
}
GMLFN(matrix_multiply) {
    (void)self;
    double a[16], b[16], m[16];
    mat_read(argc >= 1 ? args[0] : Value(), a);
    mat_read(argc >= 2 ? args[1] : Value(), b);
    mat_mul_raw(a, b, m);
    return mat_value(m);
}

GMLFN(matrix_transform_vertex) {
    (void)self;
    double m[16];
    mat_read(argc >= 1 ? args[0] : Value(), m);
    double x = A(args, argc, 1), y = A(args, argc, 2), z = A(args, argc, 3);
    double w = A(args, argc, 4, 1.0);
    Value out = mk_array();
    out.arr->items.push_back(Value(m[0] * x + m[4] * y + m[8] * z + m[12] * w));
    out.arr->items.push_back(Value(m[1] * x + m[5] * y + m[9] * z + m[13] * w));
    out.arr->items.push_back(Value(m[2] * x + m[6] * y + m[10] * z + m[14] * w));
    if (argc >= 5)
        out.arr->items.push_back(Value(m[3] * x + m[7] * y + m[11] * z + m[15] * w));
    return out;
}

static bool mat_inverse_raw(const double m[16], double inv[16]) {
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0) return false;
    det = 1.0 / det;
    for (int i = 0; i < 16; ++i) inv[i] *= det;
    return true;
}

GMLFN(matrix_inverse) {
    (void)self;
    double m[16], inv[16];
    mat_read(argc >= 1 ? args[0] : Value(), m);
    if (!mat_inverse_raw(m, inv)) return Value();
    return mat_value(inv);
}

static std::vector<std::array<double, 16>> g_matrix_stack;

static void ensure_matrix_stack() {
    if (!g_matrix_stack.empty()) return;
    std::array<double, 16> id{};
    mat_identity(id.data());
    g_matrix_stack.push_back(id);
}

GMLFN(matrix_stack_clear) {
    (void)self; (void)args; (void)argc;
    g_matrix_stack.clear();
    ensure_matrix_stack();
    return Value();
}
GMLFN(matrix_stack_is_empty) {
    (void)self; (void)args; (void)argc;
    ensure_matrix_stack();
    return Value(g_matrix_stack.size() <= 1 ? 1.0 : 0.0);
}
GMLFN(matrix_stack_push) {
    (void)self;
    ensure_matrix_stack();
    if (g_matrix_stack.size() >= 51) return Value();
    std::array<double, 16> top = g_matrix_stack.back();
    if (argc > 0) {
        double m[16], out[16];
        mat_read(args[0], m);
        mat_mul_raw(m, top.data(), out);
        for (int i = 0; i < 16; ++i) top[i] = out[i];
    }
    g_matrix_stack.push_back(top);
    return Value();
}
GMLFN(matrix_stack_pop) {
    (void)self; (void)args; (void)argc;
    ensure_matrix_stack();
    if (g_matrix_stack.size() > 1) g_matrix_stack.pop_back();
    return Value();
}
GMLFN(matrix_stack_set) {
    (void)self;
    ensure_matrix_stack();
    if (argc > 0) mat_read(args[0], g_matrix_stack.back().data());
    return Value();
}
GMLFN(matrix_stack_top) {
    (void)self; (void)args; (void)argc;
    ensure_matrix_stack();
    return mat_value(g_matrix_stack.back().data());
}

static bool g_d3d_lighting = false;
static unsigned int g_d3d_ambient = 0xFFFFFFFFu;
struct D3dLight {
    bool enabled = false;
    int type = 0;
    double data[4] = {0, 0, 0, 0};
    unsigned int color = 0xFFFFFFu;
};
static D3dLight g_d3d_lights[8];
static bool g_d3d_perspective = false;

GMLFN(draw_set_color_write_enable) {
    return gpu_set_colorwriteenable(self, args, argc);
}
GMLFN(draw_set_colour_write_enable) { return draw_set_color_write_enable(self, args, argc); }
GMLFN(d3d_set_depth) {
    (void)self;
    render_set_depth(std::max(-16000.0, std::min(16000.0, A(args, argc, 0))));
    return Value();
}
GMLFN(d3d_set_perspective) {
    (void)self;
    g_d3d_perspective = argc > 0 && gml_truthy(args[0]);
    return Value();
}
GMLFN(d3d_set_lighting) {
    (void)self;
    g_d3d_lighting = argc > 0 && gml_truthy(args[0]);
    return Value();
}
GMLFN(d3d_get_lighting) { (void)self; (void)args; (void)argc; return Value(g_d3d_lighting); }
GMLFN(draw_get_lighting) { return d3d_get_lighting(self, args, argc); }
GMLFN(d3d_light_define_ambient) {
    (void)self;
    g_d3d_ambient = C(args, argc, 0, 0xFFFFFF);
    return Value();
}
GMLFN(draw_light_define_ambient) { return d3d_light_define_ambient(self, args, argc); }
GMLFN(d3d_light_get_ambient) {
    (void)self; (void)args; (void)argc;
    return Value((double)g_d3d_ambient);
}
GMLFN(d3d_start) {
    (void)self; (void)args; (void)argc;
    render_set_ztest(true);
    render_set_zwrite(true);
    return Value();
}
GMLFN(d3d_end) {
    (void)self; (void)args; (void)argc;
    render_set_ztest(false);
    render_set_zwrite(false);
    mat_identity(g_matrix_store[2]);
    render_set_matrix(2, g_matrix_store[2]);
    return Value();
}
GMLFN(d3d_set_hidden) {
    (void)self;
    bool enabled = argc > 0 && gml_truthy(args[0]);
    render_set_ztest(enabled);
    render_set_zwrite(enabled);
    return Value();
}
GMLFN(d3d_light_define_direction) {
    (void)self;
    int i = (int)A(args, argc, 0);
    if (i < 0 || i >= 8) return Value();
    double x = A(args, argc, 1), y = A(args, argc, 2), z = A(args, argc, 3);
    double len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0) { x /= len; y /= len; z /= len; }
    g_d3d_lights[i].type = 1;
    g_d3d_lights[i].data[0] = x;
    g_d3d_lights[i].data[1] = y;
    g_d3d_lights[i].data[2] = z;
    g_d3d_lights[i].data[3] = 0.0;
    g_d3d_lights[i].color = C(args, argc, 4, 0xFFFFFF);
    return Value();
}
GMLFN(draw_light_define_direction) { return d3d_light_define_direction(self, args, argc); }
GMLFN(d3d_light_define_point) {
    (void)self;
    int i = (int)A(args, argc, 0);
    if (i < 0 || i >= 8) return Value();
    g_d3d_lights[i].type = 2;
    g_d3d_lights[i].data[0] = A(args, argc, 1);
    g_d3d_lights[i].data[1] = A(args, argc, 2);
    g_d3d_lights[i].data[2] = A(args, argc, 3);
    g_d3d_lights[i].data[3] = A(args, argc, 4);
    g_d3d_lights[i].color = C(args, argc, 5, 0xFFFFFF);
    return Value();
}
GMLFN(draw_light_define_point) { return d3d_light_define_point(self, args, argc); }
GMLFN(d3d_light_enable) {
    (void)self;
    int i = (int)A(args, argc, 0);
    if (i >= 0 && i < 8) g_d3d_lights[i].enabled = argc > 1 && gml_truthy(args[1]);
    return Value();
}
GMLFN(draw_light_enable) { return d3d_light_enable(self, args, argc); }
GMLFN(d3d_light_get) {
    (void)self;
    int i = (int)A(args, argc, 0);
    if (i < 0 || i >= 8) return Value();
    Value out = mk_array();
    out.arr->items.push_back(Value(g_d3d_lights[i].enabled));
    out.arr->items.push_back(Value((double)g_d3d_lights[i].type));
    for (double v : g_d3d_lights[i].data) out.arr->items.push_back(Value(v));
    out.arr->items.push_back(Value((double)g_d3d_lights[i].color));
    return out;
}
GMLFN(draw_light_get) { return d3d_light_get(self, args, argc); }
GMLFN(d3d_set_fog) {
    (void)self;
    render_set_fog(argc > 0 && gml_truthy(args[0]), C(args, argc, 1, 0));
    return Value();
}
GMLFN(d3d_set_projection_ortho) {
    (void)self;
    double x = A(args, argc, 0), y = A(args, argc, 1);
    double w = A(args, argc, 2, render_gui_width()), h = A(args, argc, 3, render_gui_height());
    double angle = A(args, argc, 4);
    Value view_args[9] = {
        Value(x + w * 0.5), Value(y + h * 0.5), Value(-w),
        Value(x + w * 0.5), Value(y + h * 0.5), Value(0.0),
        Value(std::sin(-angle * 3.14159265358979323846 / 180.0)),
        Value(std::cos(-angle * 3.14159265358979323846 / 180.0)), Value(0.0)
    };
    Value proj_args[4] = {Value(w), Value(-h), Value(1.0), Value(32000.0)};
    mat_read(matrix_build_lookat(self, view_args, 9), g_matrix_store[0]);
    mat_read(matrix_build_projection_ortho(self, proj_args, 4), g_matrix_store[1]);
    render_set_matrix(0, g_matrix_store[0]);
    render_set_matrix(1, g_matrix_store[1]);
    return Value();
}
GMLFN(d3d_set_projection) {
    (void)self;
    mat_read(matrix_build_lookat(self, args, argc), g_matrix_store[0]);
    render_set_matrix(0, g_matrix_store[0]);
    return Value();
}
GMLFN(d3d_set_projection_ext) {
    (void)self;
    mat_read(matrix_build_lookat(self, args, argc), g_matrix_store[0]);
    render_set_matrix(0, g_matrix_store[0]);
    if (argc >= 13) {
        Value p_args[4] = {args[9], args[10], args[11], args[12]};
        mat_read(matrix_build_projection_perspective_fov(self, p_args, 4), g_matrix_store[1]);
        render_set_matrix(1, g_matrix_store[1]);
    }
    return Value();
}
GMLFN(d3d_set_projection_perspective) {
    (void)self;
    double w = A(args, argc, 2, render_gui_width());
    double h = A(args, argc, 3, render_gui_height());
    double angle = A(args, argc, 4, 45.0);
    Value proj_args[4] = {Value(angle), Value(h != 0.0 ? w / h : 1.0), Value(1.0), Value(32000.0)};
    mat_read(matrix_build_projection_perspective_fov(self, proj_args, 4), g_matrix_store[1]);
    render_set_matrix(1, g_matrix_store[1]);
    return Value();
}
GMLFN(d3d_transform_set_identity) {
    (void)self; (void)args; (void)argc;
    mat_identity(g_matrix_store[2]);
    render_set_matrix(2, g_matrix_store[2]);
    return Value();
}
GMLFN(d3d_transform_set_translation) {
    double m[16];
    mat_translation(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_set_scaling) {
    double m[16];
    mat_scaling(A(args, argc, 0, 1), A(args, argc, 1, 1), A(args, argc, 2, 1), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_set_rotation_x) {
    double m[16];
    mat_rotation_axis(1, 0, 0, A(args, argc, 0), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_set_rotation_y) {
    double m[16];
    mat_rotation_axis(0, 1, 0, A(args, argc, 0), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_set_rotation_z) {
    double m[16];
    mat_rotation_axis(0, 0, 1, A(args, argc, 0), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_set_rotation_axis) {
    double m[16];
    mat_rotation_axis(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), m);
    set_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_translation) {
    double m[16];
    mat_translation(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_scaling) {
    double m[16];
    mat_scaling(A(args, argc, 0, 1), A(args, argc, 1, 1), A(args, argc, 2, 1), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_rotation_x) {
    double m[16];
    mat_rotation_axis(1, 0, 0, A(args, argc, 0), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_rotation_y) {
    double m[16];
    mat_rotation_axis(0, 1, 0, A(args, argc, 0), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_rotation_z) {
    double m[16];
    mat_rotation_axis(0, 0, 1, A(args, argc, 0), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_add_rotation_axis) {
    double m[16];
    mat_rotation_axis(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), m);
    add_world_matrix(m);
    return Value();
}
GMLFN(d3d_transform_stack_clear) { return matrix_stack_clear(self, args, argc); }
GMLFN(d3d_transform_stack_push) {
    Value world = mat_value(g_matrix_store[2]);
    matrix_stack_push(self, &world, 1);
    return Value(1.0);
}
GMLFN(d3d_transform_stack_pop) {
    matrix_stack_pop(self, nullptr, 0);
    double top[16];
    mat_read(matrix_stack_top(self, nullptr, 0), top);
    set_world_matrix(top);
    return Value(1.0);
}
GMLFN(d3d_transform_stack_top) {
    double top[16];
    mat_read(matrix_stack_top(self, nullptr, 0), top);
    set_world_matrix(top);
    return Value(1.0);
}
GMLFN(d3d_transform_stack_discard) {
    matrix_stack_pop(self, nullptr, 0);
    return Value(1.0);
}
GMLFN(d3d_transform_stack_empty) { return matrix_stack_is_empty(self, args, argc); }
GMLFN(d3d_primitive_begin) { return draw_primitive_begin(self, args, argc); }
GMLFN(d3d_primitive_begin_texture) { return draw_primitive_begin_texture(self, args, argc); }
GMLFN(d3d_primitive_end) { return draw_primitive_end(self, args, argc); }
GMLFN(d3d_vertex) {
    (void)self;
    render_primitive_vertex_3d(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), 0, 0,
                               render_get_color(), render_get_alpha(), false);
    return Value();
}
GMLFN(d3d_vertex_color) {
    (void)self;
    render_primitive_vertex_3d(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), 0, 0,
                               C(args, argc, 3), A(args, argc, 4, 1), false);
    return Value();
}
GMLFN(d3d_vertex_colour) { return d3d_vertex_color(self, args, argc); }
GMLFN(d3d_vertex_texture) {
    (void)self;
    render_primitive_vertex_3d(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2),
                               A(args, argc, 3), A(args, argc, 4), render_get_color(),
                               render_get_alpha(), true);
    return Value();
}
GMLFN(d3d_vertex_texture_color) {
    (void)self;
    render_primitive_vertex_3d(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2),
                               A(args, argc, 3), A(args, argc, 4), C(args, argc, 5),
                               A(args, argc, 6, 1), true);
    return Value();
}
GMLFN(d3d_vertex_texture_colour) { return d3d_vertex_texture_color(self, args, argc); }

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

struct VideoState {
    bool open = false;
    bool loop = false;
    bool paused = false;
    bool end_queued = false;
    unsigned long long start_frame = 0;
    double volume = 1.0;
    double position_ms = 0.0;
    double duration_ms = 0.0;
    double start_time_ms = 0.0;
    double focus_pause_ms = 0.0;
    int surface = -1;
    int width = 0;
    int height = 0;
    bool have_frame = false;
    bool focus_paused = false;
#ifdef KWIK_USE_FFMPEG
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int stream = -1;
    double fps = 30.0;
    double last_frame_ms = -1.0;
    bool decoder_eof = false;
    bool using_ffmpeg = false;
    int audio_handle = -1;
    std::vector<unsigned char> rgba;
#endif
};

static VideoState g_video;

#ifdef KWIK_USE_FFMPEG
static double rational_to_double(AVRational r, double fallback = 0.0) {
    return r.num != 0 && r.den != 0 ? av_q2d(r) : fallback;
}

static void video_free_decoder() {
    if (g_video.audio_handle >= 0) {
        kwik_audio_stop_handle(g_video.audio_handle);
        g_video.audio_handle = -1;
    }
    if (g_video.packet) {
        av_packet_free(&g_video.packet);
        g_video.packet = nullptr;
    }
    if (g_video.frame) {
        av_frame_free(&g_video.frame);
        g_video.frame = nullptr;
    }
    if (g_video.sws) {
        sws_freeContext(g_video.sws);
        g_video.sws = nullptr;
    }
    if (g_video.codec) {
        avcodec_free_context(&g_video.codec);
        g_video.codec = nullptr;
    }
    if (g_video.format) {
        avformat_close_input(&g_video.format);
        g_video.format = nullptr;
    }
    g_video.stream = -1;
    g_video.using_ffmpeg = false;
    g_video.decoder_eof = false;
    g_video.last_frame_ms = -1.0;
    g_video.rgba.clear();
}

static bool video_upload_current_frame() {
    if (!g_video.codec || !g_video.frame) return false;
    int w = g_video.codec->width;
    int h = g_video.codec->height;
    if (w <= 0 || h <= 0) return false;

    g_video.sws = sws_getCachedContext(g_video.sws, w, h, g_video.codec->pix_fmt, w, h,
                                       AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!g_video.sws) return false;

    g_video.rgba.resize((size_t)w * h * 4);
    uint8_t* dst[4] = {g_video.rgba.data(), nullptr, nullptr, nullptr};
    int dst_stride[4] = {w * 4, 0, 0, 0};
    sws_scale(g_video.sws, g_video.frame->data, g_video.frame->linesize, 0, h, dst, dst_stride);

    if (g_video.surface < 0 || g_video.width != w || g_video.height != h) {
        if (g_video.surface >= 0) render_surface_free(g_video.surface);
        g_video.surface = render_surface_create(w, h);
        if (g_video.surface < 0) return false;
        g_video.width = w;
        g_video.height = h;
    }

    unsigned int tex = render_upload_texture(g_video.rgba.data(), w, h);
    if (!tex) return false;
    if (render_surface_set_target(g_video.surface)) {
        render_surface_clear(0, 0.0);
        render_draw_quad(tex, 0, 0, w, h, 0, 0, 1, 1, 0, 0.f, 0.f, 1.f, 1.f, 0xFFFFFF, 1.0);
        render_surface_reset_target();
    }
    render_free_texture(tex);
    g_video.have_frame = true;

    int64_t pts = g_video.frame->best_effort_timestamp;
    if (pts != AV_NOPTS_VALUE) {
        g_video.last_frame_ms =
            pts * rational_to_double(g_video.format->streams[g_video.stream]->time_base) * 1000.0;
    } else {
        double step = 1000.0 / std::max(1.0, g_video.fps);
        g_video.last_frame_ms = g_video.last_frame_ms < 0.0 ? 0.0 : g_video.last_frame_ms + step;
    }
    return true;
}

static bool video_decode_one_frame() {
    if (!g_video.using_ffmpeg || !g_video.codec) return false;
    while (true) {
        int rr = avcodec_receive_frame(g_video.codec, g_video.frame);
        if (rr == 0) return video_upload_current_frame();
        if (rr == AVERROR_EOF) {
            g_video.decoder_eof = true;
            return false;
        }
        if (rr != AVERROR(EAGAIN)) return false;

        if (g_video.decoder_eof) return false;
        int pr = av_read_frame(g_video.format, g_video.packet);
        if (pr < 0) {
            avcodec_send_packet(g_video.codec, nullptr);
            g_video.decoder_eof = true;
            continue;
        }
        if (g_video.packet->stream_index == g_video.stream)
            avcodec_send_packet(g_video.codec, g_video.packet);
        av_packet_unref(g_video.packet);
    }
}

static void video_seek_decoder(double ms) {
    if (!g_video.using_ffmpeg || g_video.stream < 0) return;
    AVStream* st = g_video.format->streams[g_video.stream];
    int64_t ts = (int64_t)((ms / 1000.0) / rational_to_double(st->time_base, 1.0));
    av_seek_frame(g_video.format, g_video.stream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(g_video.codec);
    g_video.decoder_eof = false;
    g_video.last_frame_ms = -1.0;
    g_video.have_frame = false;
}

static bool video_open_decoder(const std::string& path) {
    video_free_decoder();
    if (avformat_open_input(&g_video.format, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(g_video.format, nullptr) < 0) {
        video_free_decoder();
        return false;
    }
    int si = av_find_best_stream(g_video.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (si < 0) {
        video_free_decoder();
        return false;
    }
    g_video.stream = si;
    AVStream* st = g_video.format->streams[si];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        video_free_decoder();
        return false;
    }
    g_video.codec = avcodec_alloc_context3(dec);
    if (!g_video.codec || avcodec_parameters_to_context(g_video.codec, st->codecpar) < 0 ||
        avcodec_open2(g_video.codec, dec, nullptr) < 0) {
        video_free_decoder();
        return false;
    }
    g_video.frame = av_frame_alloc();
    g_video.packet = av_packet_alloc();
    if (!g_video.frame || !g_video.packet) {
        video_free_decoder();
        return false;
    }

    double fps = rational_to_double(st->avg_frame_rate);
    if (fps <= 0.0) fps = rational_to_double(st->r_frame_rate);
    g_video.fps = fps > 0.0 ? fps : 30.0;
    if (st->duration != AV_NOPTS_VALUE)
        g_video.duration_ms = st->duration * rational_to_double(st->time_base) * 1000.0;
    else if (g_video.format->duration != AV_NOPTS_VALUE)
        g_video.duration_ms = g_video.format->duration / 1000.0;
    else
        g_video.duration_ms = 0.0;
    g_video.using_ffmpeg = true;
    return true;
}

static int video_decode_audio_file(const std::string& path, bool loop, float volume) {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int handle = -1;
    std::vector<unsigned char> pcm_bytes;

    auto cleanup = [&]() {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (swr) swr_free(&swr);
        if (ctx) avcodec_free_context(&ctx);
        if (fmt) avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        cleanup();
        return -1;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        cleanup();
        return -1;
    }
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) {
        cleanup();
        return -1;
    }
    AVStream* st = fmt->streams[si];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        cleanup();
        return -1;
    }
    ctx = avcodec_alloc_context3(dec);
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 ||
        avcodec_open2(ctx, dec, nullptr) < 0) {
        cleanup();
        return -1;
    }
    if (ctx->ch_layout.nb_channels <= 0)
        av_channel_layout_default(&ctx->ch_layout, ctx->ch_layout.nb_channels > 0 ?
                                  ctx->ch_layout.nb_channels : 2);

    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, ctx->ch_layout.nb_channels > 0 ?
                              ctx->ch_layout.nb_channels : 2);
    int in_rate = ctx->sample_rate > 0 ? ctx->sample_rate : 48000;
    int out_rate = in_rate;
    if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, out_rate,
                            &ctx->ch_layout, ctx->sample_fmt, in_rate, 0, nullptr) < 0 ||
        swr_init(swr) < 0) {
        av_channel_layout_uninit(&out_layout);
        cleanup();
        return -1;
    }
    int out_channels = out_layout.nb_channels;
    av_channel_layout_uninit(&out_layout);

    frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !pkt) {
        cleanup();
        return -1;
    }

    auto receive_frames = [&]() {
        while (true) {
            int rr = avcodec_receive_frame(ctx, frame);
            if (rr == AVERROR(EAGAIN) || rr == AVERROR_EOF) return rr;
            if (rr < 0) return rr;
            int dst_samples = (int)av_rescale_rnd(swr_get_delay(swr, in_rate) +
                                                  frame->nb_samples, out_rate, in_rate,
                                                  AV_ROUND_UP);
            std::vector<unsigned char> tmp((size_t)dst_samples * out_channels * sizeof(short));
            uint8_t* dst[1] = {tmp.data()};
            int converted = swr_convert(swr, dst, dst_samples,
                                        (const uint8_t**)frame->extended_data, frame->nb_samples);
            if (converted < 0) return converted;
            size_t bytes = (size_t)converted * out_channels * sizeof(short);
            pcm_bytes.insert(pcm_bytes.end(), tmp.begin(), tmp.begin() + bytes);
            av_frame_unref(frame);
        }
    };

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == si) {
            int sr = avcodec_send_packet(ctx, pkt);
            av_packet_unref(pkt);
            if (sr < 0) break;
            int rr = receive_frames();
            if (rr < 0 && rr != AVERROR(EAGAIN) && rr != AVERROR_EOF) break;
        } else {
            av_packet_unref(pkt);
        }
    }
    avcodec_send_packet(ctx, nullptr);
    receive_frames();

    int delay_samples = (int)swr_get_delay(swr, in_rate);
    if (delay_samples > 0) {
        std::vector<unsigned char> tmp((size_t)delay_samples * out_channels * sizeof(short));
        uint8_t* dst[1] = {tmp.data()};
        int converted = swr_convert(swr, dst, delay_samples, nullptr, 0);
        if (converted > 0) {
            size_t bytes = (size_t)converted * out_channels * sizeof(short);
            pcm_bytes.insert(pcm_bytes.end(), tmp.begin(), tmp.begin() + bytes);
        }
    }

    if (!pcm_bytes.empty()) {
        short* pcm = (short*)std::malloc(pcm_bytes.size());
        if (pcm) {
            std::memcpy(pcm, pcm_bytes.data(), pcm_bytes.size());
            unsigned long long frames = pcm_bytes.size() / (sizeof(short) * out_channels);
            handle = kwik_audio_play_pcm(pcm, (unsigned)out_channels, (unsigned)out_rate, frames,
                                         loop, volume);
        }
    }

    cleanup();
    return handle;
}
#endif

static void video_reset_common() {
    if (g_video.surface >= 0) {
        render_surface_free(g_video.surface);
        g_video.surface = -1;
    }
    g_video.open = false;
    g_video.paused = false;
    g_video.end_queued = false;
    g_video.position_ms = 0.0;
    g_video.duration_ms = 0.0;
    g_video.start_time_ms = 0.0;
    g_video.focus_pause_ms = 0.0;
    g_video.width = 0;
    g_video.height = 0;
    g_video.have_frame = false;
    g_video.focus_paused = false;
}

static void video_queue_end_once(Instance* self) {
    if (!g_video.end_queued) {
        g_video.end_queued = true;
        queue_video_event(self, "video_end");
    }
}

GMLFN(video_open) {
    std::string path = argc > 0 ? kwik_resolve_read(S(args, argc, 0)) : std::string();
#ifdef KWIK_USE_FFMPEG
    video_free_decoder();
#endif
    video_reset_common();
    g_video.open = true;
    g_video.paused = false;
    g_video.start_frame = g_frame_counter;
    g_video.start_time_ms = render_time_ms();
    g_video.position_ms = 0.0;
#ifdef KWIK_USE_FFMPEG
    if (!path.empty() && !video_open_decoder(path)) {
        std::fprintf(stderr, "[kwik] video_open failed for '%s'; using compatibility timer\n",
                     path.c_str());
    }
    if (g_video.using_ffmpeg)
        g_video.audio_handle = video_decode_audio_file(path, g_video.loop, (float)g_video.volume);
#else
    (void)path;
#endif
    queue_video_event(self, "video_start");
    return Value(1.0);
}
GMLFN(video_close) {
    (void)self; (void)args; (void)argc;
#ifdef KWIK_USE_FFMPEG
    video_free_decoder();
#endif
    video_reset_common();
    return Value();
}
GMLFN(video_get_status) {
    (void)self; (void)args; (void)argc;
    if (!g_video.open) return Value(0.0);
    return Value(g_video.paused || g_video.focus_paused ? 3.0 : 2.0);
}
GMLFN(video_draw) {
    (void)args; (void)argc;
    if (g_video.open && !g_video.paused && !g_video.focus_paused) {
#ifdef KWIK_USE_FFMPEG
        if (g_video.using_ffmpeg) {
            g_video.position_ms = std::max(0.0, render_time_ms() - g_video.start_time_ms);
            double lead_ms = 1000.0 / std::max(1.0, g_video.fps);
            while (!g_video.decoder_eof &&
                   (!g_video.have_frame || g_video.last_frame_ms < g_video.position_ms + lead_ms)) {
                if (!video_decode_one_frame()) break;
            }
            if (g_video.decoder_eof) {
                if (g_video.loop) {
                    video_seek_decoder(0.0);
                    g_video.start_time_ms = render_time_ms();
                    g_video.position_ms = 0.0;
                    g_video.end_queued = false;
                } else {
                    video_queue_end_once(self);
                }
            }
        } else
#endif
        {
        unsigned long long elapsed = g_frame_counter - g_video.start_frame;
        g_video.position_ms = elapsed * (1000.0 / std::max(1.0, g_room_speed_v));
        if (!g_video.loop && elapsed >= 8 && !g_video.end_queued) {
            video_queue_end_once(self);
        } else if (g_video.loop && elapsed >= 8) {
            g_video.start_frame = g_frame_counter;
            g_video.position_ms = 0.0;
        }
        }
    }
    Value out = mk_array();
    out.arr->items.push_back(Value(g_video.open && g_video.have_frame ? 0.0 : -1.0));
    out.arr->items.push_back(Value((double)g_video.surface));
    out.arr->items.push_back(Value(-1.0));
    return out;
}
GMLFN(video_set_volume) {
    (void)self;
    g_video.volume = std::clamp(A(args, argc, 0, 1.0), 0.0, 1.0);
#ifdef KWIK_USE_FFMPEG
    if (g_video.audio_handle >= 0)
        kwik_audio_set_handle_volume(g_video.audio_handle, (float)g_video.volume);
#endif
    return Value();
}
GMLFN(video_pause) {
    (void)self; (void)args; (void)argc;
    g_video.paused = true;
#ifdef KWIK_USE_FFMPEG
    if (g_video.audio_handle >= 0) kwik_audio_pause_handle(g_video.audio_handle);
#endif
    return Value();
}
GMLFN(video_resume) {
    (void)self; (void)args; (void)argc;
    g_video.paused = false;
    g_video.start_time_ms = render_time_ms() - g_video.position_ms;
#ifdef KWIK_USE_FFMPEG
    if (!g_video.focus_paused && g_video.audio_handle >= 0)
        kwik_audio_resume_handle(g_video.audio_handle);
#endif
    return Value();
}
GMLFN(video_enable_loop) {
    (void)self;
    g_video.loop = gml_truthy(argc > 0 ? args[0] : Value(0.0));
#ifdef KWIK_USE_FFMPEG
    if (g_video.audio_handle >= 0) kwik_audio_set_handle_looping(g_video.audio_handle, g_video.loop);
#endif
    return Value();
}
GMLFN(video_seek_to) {
    (void)self;
    g_video.position_ms = A(args, argc, 0, 0.0);
    g_video.start_frame = g_frame_counter;
    g_video.start_time_ms = render_time_ms() - g_video.position_ms;
#ifdef KWIK_USE_FFMPEG
    video_seek_decoder(g_video.position_ms);
    if (g_video.audio_handle >= 0) kwik_audio_seek_handle(g_video.audio_handle, g_video.position_ms / 1000.0);
#endif
    return Value();
}
GMLFN(video_get_duration) { (void)self; (void)args; (void)argc; return Value(g_video.duration_ms); }
GMLFN(video_get_position) { (void)self; (void)args; (void)argc; return Value(g_video.position_ms); }
GMLFN(video_get_format) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(video_is_looping) { (void)self; (void)args; (void)argc; return Value(g_video.loop ? 1.0 : 0.0); }

void kwik_video_focus_pause(bool paused) {
    if (!g_video.open || g_video.focus_paused == paused) return;
    g_video.focus_paused = paused;
    if (paused) {
        g_video.focus_pause_ms = render_time_ms();
#ifdef KWIK_USE_FFMPEG
        if (g_video.audio_handle >= 0) kwik_audio_pause_handle(g_video.audio_handle);
#endif
    } else {
        double now = render_time_ms();
        if (g_video.focus_pause_ms > 0.0)
            g_video.start_time_ms += now - g_video.focus_pause_ms;
        g_video.focus_pause_ms = 0.0;
#ifdef KWIK_USE_FFMPEG
        if (!g_video.paused && g_video.audio_handle >= 0)
            kwik_audio_resume_handle(g_video.audio_handle);
#endif
    }
}

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
GMLFN(tile_set_empty) {
    (void)self;
    uint32_t cell = (uint32_t)(long long)A(args, argc, 0);
    return Value((double)(cell & ~0x0007FFFFu));
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

static Camera* camera_arg(const Value* args, int argc) {
    int i = argc > 0 ? (int)(double)args[0] : 0;
    if (i < 0 || (size_t)i >= g_cameras.size()) return nullptr;
    return &g_cameras[i];
}

GMLFN(camera_get_view_mat) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c) return mat_value(c->view_mat);
    double id[16];
    mat_identity(id);
    return mat_value(id);
}
GMLFN(camera_get_proj_mat) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c) return mat_value(c->proj_mat);
    double id[16];
    mat_identity(id);
    return mat_value(id);
}
GMLFN(camera_set_view_mat) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c && argc >= 2) {
        mat_read(args[1], c->view_mat);
        if ((int)(double)args[0] == g_view_camera[0]) render_set_matrix(0, c->view_mat);
    }
    return Value();
}
GMLFN(camera_set_proj_mat) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c && argc >= 2) {
        mat_read(args[1], c->proj_mat);
        if ((int)(double)args[0] == g_view_camera[0]) render_set_matrix(1, c->proj_mat);
    }
    return Value();
}
GMLFN(camera_apply) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c) {
        render_set_matrix(0, c->view_mat);
        render_set_matrix(1, c->proj_mat);
    }
    return Value();
}
GMLFN(camera_destroy) {
    (void)self;
    Camera* c = camera_arg(args, argc);
    if (c) c->in_use = false;
    return Value();
}

}
