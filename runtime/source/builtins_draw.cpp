#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <cmath>
#include <cstdio>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}
static unsigned int C(const Value* args, int argc, int i, unsigned int dflt = 0xFFFFFF) {
    return i < argc ? (unsigned int)(long long)(double)args[i] : dflt;
}

GMLFN(draw_set_color) { (void)self; render_set_color(C(args, argc, 0)); return Value(); }
GMLFN(draw_set_colour) { return draw_set_color(self, args, argc); }
GMLFN(draw_set_alpha) { (void)self; render_set_alpha(A(args, argc, 0, 1)); return Value(); }
GMLFN(draw_get_color) { (void)self; (void)args; (void)argc; return Value((double)render_get_color()); }
GMLFN(draw_get_alpha) { (void)self; (void)args; (void)argc; return Value(render_get_alpha()); }
GMLFN(draw_set_halign) { (void)self; render_set_halign((int)A(args, argc, 0)); return Value(); }
GMLFN(draw_set_valign) { (void)self; render_set_valign((int)A(args, argc, 0)); return Value(); }

GMLFN(draw_set_font) {
    (void)self;
    if (argc < 1) {
        kwik_set_font_rt(-1);
        return Value();
    }
    if (args[0].type == Value::REAL && args[0].num >= 10000)
        kwik_set_font_rt((int)args[0].num - 10000);
    else
        kwik_set_font_rt(kwik_font_for_asset((int)A(args, argc, 0)));
    return Value();
}

GMLFN(font_add_sprite_ext) {
    (void)self;
    if (argc < 4) return Value(-1.0);
    int rt = kwik_font_add_sprite((int)(double)args[0], (std::string)args[1], gml_truthy(args[2]),
                                  (int)(double)args[3]);
    return Value(rt < 0 ? -1.0 : (double)(rt + 10000));
}

GMLFN(string_width) {
    (void)self;
    return Value(kwik_string_width(argc > 0 ? (std::string)args[0] : ""));
}
GMLFN(string_height) {
    (void)self;
    return Value(kwik_string_height(argc > 0 ? (std::string)args[0] : ""));
}

GMLFN(draw_text) {
    (void)self;
    if (argc < 3) return Value();
    kwik_draw_text_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2], 1, 1, 0);
    return Value();
}

GMLFN(draw_text_transformed) {
    (void)self;
    if (argc < 6) return Value();
    kwik_draw_text_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2], A(args, argc, 3, 1),
                      A(args, argc, 4, 1), A(args, argc, 5));
    return Value();
}

GMLFN(draw_text_color) {
    (void)self;
    if (argc < 3) return Value();
    unsigned int saved = render_get_color();
    double sa = render_get_alpha();
    if (argc >= 4) render_set_color(C(args, argc, 3));
    if (argc >= 8) render_set_alpha(A(args, argc, 7, 1));
    kwik_draw_text_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2], 1, 1, 0);
    render_set_color(saved);
    render_set_alpha(sa);
    return Value();
}
GMLFN(draw_text_colour) { return draw_text_color(self, args, argc); }

GMLFN(draw_text_transformed_color) {
    (void)self;
    if (argc < 6) return Value();
    unsigned int saved = render_get_color();
    double sa = render_get_alpha();
    if (argc >= 7) render_set_color(C(args, argc, 6));
    if (argc >= 11) render_set_alpha(A(args, argc, 10, 1));
    kwik_draw_text_rt(A(args, argc, 0), A(args, argc, 1), (std::string)args[2], A(args, argc, 3, 1),
                      A(args, argc, 4, 1), A(args, argc, 5));
    render_set_color(saved);
    render_set_alpha(sa);
    return Value();
}
GMLFN(draw_text_transformed_colour) { return draw_text_transformed_color(self, args, argc); }

GMLFN(draw_self) {
    (void)args; (void)argc;
    draw_self_instance(self);
    return Value();
}

GMLFN(draw_sprite) {
    (void)self;
    if (argc < 4) return Value();
    kwik_draw_sprite_general((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                             A(args, argc, 3), 1, 1, 0, 0xFFFFFF, render_get_alpha());
    return Value();
}

GMLFN(draw_sprite_ext) {
    (void)self;
    if (argc < 9) return Value();
    kwik_draw_sprite_general((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                             A(args, argc, 3), A(args, argc, 4, 1), A(args, argc, 5, 1),
                             A(args, argc, 6), C(args, argc, 7), A(args, argc, 8, 1));
    return Value();
}

GMLFN(draw_sprite_part) {
    (void)self;
    if (argc < 8) return Value();
    kwik_draw_sprite_part((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                          A(args, argc, 3), A(args, argc, 4), A(args, argc, 5), A(args, argc, 6),
                          A(args, argc, 7), 1, 1, 0xFFFFFF, render_get_alpha());
    return Value();
}

GMLFN(draw_sprite_part_ext) {
    (void)self;
    if (argc < 12) return Value();
    kwik_draw_sprite_part((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                          A(args, argc, 3), A(args, argc, 4), A(args, argc, 5), A(args, argc, 6),
                          A(args, argc, 7), A(args, argc, 8, 1), A(args, argc, 9, 1),
                          C(args, argc, 10), A(args, argc, 11, 1));
    return Value();
}

GMLFN(draw_sprite_stretched) {
    (void)self;
    if (argc < 6) return Value();
    kwik_draw_sprite_stretched((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                               A(args, argc, 3), A(args, argc, 4), A(args, argc, 5), 0xFFFFFF,
                               render_get_alpha());
    return Value();
}

GMLFN(draw_sprite_tiled_ext) {
    (void)self;
    if (argc < 8) return Value();
    kwik_draw_sprite_tiled((int)A(args, argc, 0), (int)A(args, argc, 1), A(args, argc, 2),
                           A(args, argc, 3), A(args, argc, 4, 1), A(args, argc, 5, 1),
                           C(args, argc, 6), A(args, argc, 7, 1));
    return Value();
}

GMLFN(draw_surface_ext) {
    (void)self;
    if (argc < 8) return Value();
    int id = (int)A(args, argc, 0);
    unsigned int tex = render_surface_texture(id);
    if (!tex) return Value();
    double x = A(args, argc, 1), y = A(args, argc, 2);
    double xs = A(args, argc, 3, 1), ys = A(args, argc, 4, 1);
    double rot = A(args, argc, 5);
    unsigned int col = C(args, argc, 6);
    double alpha = A(args, argc, 7, 1);
    render_draw_quad(tex, x, y, render_surface_width(id), render_surface_height(id), 0, 0, xs, ys,
                     rot, 0.f, 1.f, 1.f, 0.f, col, alpha);
    return Value();
}

GMLFN(draw_rectangle) {
    (void)self;
    if (argc < 5) return Value();
    render_draw_rectangle(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                          gml_truthy(args[4]));
    return Value();
}
GMLFN(draw_rectangle_color) {
    (void)self;
    if (argc < 9) return Value();
    render_draw_rectangle_color(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2),
                                A(args, argc, 3), C(args, argc, 4), C(args, argc, 5),
                                C(args, argc, 6), C(args, argc, 7), gml_truthy(args[8]));
    return Value();
}
GMLFN(draw_rectangle_colour) { return draw_rectangle_color(self, args, argc); }

GMLFN(draw_line) {
    (void)self;
    if (argc < 4) return Value();
    unsigned int c = render_get_color();
    render_draw_line(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), 1, c, c);
    return Value();
}
GMLFN(draw_line_color) {
    (void)self;
    if (argc < 6) return Value();
    render_draw_line(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), 1,
                     C(args, argc, 4), C(args, argc, 5));
    return Value();
}
GMLFN(draw_line_colour) { return draw_line_color(self, args, argc); }
GMLFN(draw_line_width) {
    (void)self;
    if (argc < 5) return Value();
    unsigned int c = render_get_color();
    render_draw_line(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                     A(args, argc, 4, 1), c, c);
    return Value();
}
GMLFN(draw_line_width_color) {
    (void)self;
    if (argc < 7) return Value();
    render_draw_line(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                     A(args, argc, 4, 1), C(args, argc, 5), C(args, argc, 6));
    return Value();
}
GMLFN(draw_line_width_colour) { return draw_line_width_color(self, args, argc); }

GMLFN(draw_set_circle_precision) {
    (void)self; (void)args; (void)argc;
    return Value();
}

GMLFN(draw_circle) {
    (void)self;
    if (argc < 4) return Value();
    double x = A(args, argc, 0), y = A(args, argc, 1), r = A(args, argc, 2);
    unsigned int c = render_get_color();
    render_draw_ellipse(x - r, y - r, x + r, y + r, c, c, gml_truthy(args[3]));
    return Value();
}
GMLFN(draw_circle_color) {
    (void)self;
    if (argc < 6) return Value();
    double x = A(args, argc, 0), y = A(args, argc, 1), r = A(args, argc, 2);
    render_draw_ellipse(x - r, y - r, x + r, y + r, C(args, argc, 3), C(args, argc, 4),
                        gml_truthy(args[5]));
    return Value();
}
GMLFN(draw_circle_colour) { return draw_circle_color(self, args, argc); }

GMLFN(draw_ellipse) {
    (void)self;
    if (argc < 5) return Value();
    unsigned int c = render_get_color();
    render_draw_ellipse(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), c,
                        c, gml_truthy(args[4]));
    return Value();
}
GMLFN(draw_ellipse_color) {
    (void)self;
    if (argc < 7) return Value();
    render_draw_ellipse(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                        C(args, argc, 4), C(args, argc, 5), gml_truthy(args[6]));
    return Value();
}
GMLFN(draw_ellipse_colour) { return draw_ellipse_color(self, args, argc); }

GMLFN(draw_triangle) {
    (void)self;
    if (argc < 7) return Value();
    unsigned int c = render_get_color();
    render_draw_triangle(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                         A(args, argc, 4), A(args, argc, 5), c, c, c, gml_truthy(args[6]));
    return Value();
}
GMLFN(draw_triangle_color) {
    (void)self;
    if (argc < 10) return Value();
    render_draw_triangle(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                         A(args, argc, 4), A(args, argc, 5), C(args, argc, 6), C(args, argc, 7),
                         C(args, argc, 8), gml_truthy(args[9]));
    return Value();
}
GMLFN(draw_triangle_colour) { return draw_triangle_color(self, args, argc); }

GMLFN(draw_point) {
    (void)self;
    if (argc < 2) return Value();
    render_draw_point(A(args, argc, 0), A(args, argc, 1), render_get_color());
    return Value();
}
GMLFN(draw_point_color) {
    (void)self;
    if (argc < 3) return Value();
    render_draw_point(A(args, argc, 0), A(args, argc, 1), C(args, argc, 2));
    return Value();
}
GMLFN(draw_point_colour) { return draw_point_color(self, args, argc); }

GMLFN(draw_roundrect) {
    (void)self;
    if (argc < 5) return Value();
    render_draw_rectangle(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                          gml_truthy(args[4]));
    return Value();
}
GMLFN(draw_roundrect_ext) {
    (void)self;
    if (argc < 7) return Value();
    render_draw_rectangle(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3),
                          gml_truthy(args[6]));
    return Value();
}
GMLFN(draw_roundrect_color) {
    (void)self;
    if (argc < 7) return Value();
    render_draw_rectangle_color(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2),
                                A(args, argc, 3), C(args, argc, 4), C(args, argc, 4),
                                C(args, argc, 5), C(args, argc, 5), gml_truthy(args[6]));
    return Value();
}
GMLFN(draw_roundrect_colour) { return draw_roundrect_color(self, args, argc); }
GMLFN(draw_roundrect_color_ext) {
    (void)self;
    if (argc < 9) return Value();
    render_draw_rectangle_color(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2),
                                A(args, argc, 3), C(args, argc, 6), C(args, argc, 6),
                                C(args, argc, 7), C(args, argc, 7), gml_truthy(args[8]));
    return Value();
}
GMLFN(draw_roundrect_colour_ext) { return draw_roundrect_color_ext(self, args, argc); }

GMLFN(draw_arrow) {
    (void)self;
    if (argc < 5) return Value();
    unsigned int c = render_get_color();
    render_draw_line(A(args, argc, 0), A(args, argc, 1), A(args, argc, 2), A(args, argc, 3), 1, c, c);
    return Value();
}
GMLFN(draw_path) { (void)args; (void)argc; return kwik_missing(self, "draw_path"); }

GMLFN(gpu_set_blendenable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_blendmode) {
    (void)self;
    g_gpu_blendmode = (int)A(args, argc, 0);
    render_set_blendmode(g_gpu_blendmode);
    return Value();
}
GMLFN(gpu_set_fog) {
    (void)self;
    render_set_fog(argc > 0 && gml_truthy(args[0]), C(args, argc, 1, 0));
    return Value();
}
GMLFN(gpu_set_texfilter) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gpu_set_texfilter_ext) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(surface_get_width) {
    (void)self;
    return Value((double)render_surface_width((int)A(args, argc, 0)));
}
GMLFN(surface_get_height) {
    (void)self;
    return Value((double)render_surface_height((int)A(args, argc, 0)));
}
GMLFN(texture_is_ready) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(texture_prefetch) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(sprite_prefetch) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(texture_flush) {
    (void)self; (void)args; (void)argc;
    kwik_flush_textures();
    return Value();
}
GMLFN(ps4_touchpad_mouse_enable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(mouse_wheel_up) { (void)self; (void)args; (void)argc; return Value(render_wheel_delta() > 0); }
GMLFN(mouse_wheel_down) { (void)self; (void)args; (void)argc; return Value(render_wheel_delta() < 0); }
GMLFN(texturegroup_get_textures) {
    (void)self; (void)args; (void)argc;
    return kwik_new_array(nullptr, 0);
}
GMLFN(application_surface_draw_enable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(application_surface_enable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_create_buffer) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(vertex_format_add_colour) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_add_normal) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_add_position_3d) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_add_textcoord) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_begin) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(vertex_format_end) { (void)self; (void)args; (void)argc; return Value(-1.0); }

GMLFN(sprite_exists) {
    (void)self;
    return Value(kwik_sprite_at((int)A(args, argc, 0, -1)) != nullptr);
}
GMLFN(sprite_get_width) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->width : 0.0);
}
GMLFN(sprite_get_height) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->height : 0.0);
}
GMLFN(sprite_get_number) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->frame_count : 0.0);
}
GMLFN(sprite_get_name) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s && s->name ? s->name : "<undefined>");
}
GMLFN(sprite_get_xoffset) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->origin_x : 0.0);
}
GMLFN(sprite_get_yoffset) {
    (void)self;
    const KwikSprite* s = kwik_sprite_at((int)A(args, argc, 0, -1));
    return Value(s ? (double)s->origin_y : 0.0);
}
GMLFN(sprite_create_from_surface) {
    (void)self;
    if (argc < 5) return Value(-1.0);
    int id = (int)A(args, argc, 0);
    int sw = render_surface_width(id), sh = render_surface_height(id);
    if (sw <= 0 || sh <= 0) return Value(-1.0);
    int x = (int)A(args, argc, 1), y = (int)A(args, argc, 2);
    int w = (int)A(args, argc, 3), h = (int)A(args, argc, 4);
    int xorig = (int)A(args, argc, 7), yorig = (int)A(args, argc, 8);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return Value(-1.0);
    unsigned int tex = render_texture_from_surface(id, x, y, w, h);
    if (tex) {
        render_set_colorwrite(g_gpu_colorwrite[0] != 0, g_gpu_colorwrite[1] != 0,
                              g_gpu_colorwrite[2] != 0, g_gpu_colorwrite[3] != 0);
    } else {
        std::vector<unsigned char> pixels((size_t)w * h * 4);
        if (!render_surface_snapshot(id, x, y, w, h, pixels.data())) return Value(-1.0);
        if (id == 0)
            for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        tex = render_upload_texture(pixels.data(), w, h);
    }
    int img = kwik_register_dynamic_image(tex, w, h);
    KwikSprite s{};
    s.first_frame = img;
    s.frame_count = 1;
    s.origin_x = xorig;
    s.origin_y = yorig;
    s.speed = 1;
    s.speed_type = 1;
    s.bbox_left = 0;
    s.bbox_top = 0;
    s.bbox_right = w - 1;
    s.bbox_bottom = h - 1;
    s.width = w;
    s.height = h;
    s.name = "dyn_sprite";
    return Value((double)kwik_register_dynamic_sprite(s));
}
GMLFN(sprite_add_from_surface) {
    (void)self;
    if (argc < 6) return Value(0.0);
    int spr = (int)A(args, argc, 0);
    int id = (int)A(args, argc, 1);
    int sw = render_surface_width(id), sh = render_surface_height(id);
    if (sw <= 0 || sh <= 0) return Value(0.0);
    int x = (int)A(args, argc, 2), y = (int)A(args, argc, 3);
    int w = (int)A(args, argc, 4), h = (int)A(args, argc, 5);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return Value(0.0);
    unsigned int tex = render_texture_from_surface(id, x, y, w, h);
    if (tex) {
        render_set_colorwrite(g_gpu_colorwrite[0] != 0, g_gpu_colorwrite[1] != 0,
                              g_gpu_colorwrite[2] != 0, g_gpu_colorwrite[3] != 0);
    } else {
        std::vector<unsigned char> pixels((size_t)w * h * 4);
        if (!render_surface_snapshot(id, x, y, w, h, pixels.data())) return Value(0.0);
        tex = render_upload_texture(pixels.data(), w, h);
    }
    return Value(kwik_sprite_append_frame(spr, tex, w, h) ? 1.0 : 0.0);
}
GMLFN(sprite_delete) { (void)self; (void)args; (void)argc; return Value(1.0); }

GMLFN(window_set_size) {
    (void)self;
    render_set_window_size((int)A(args, argc, 0, 640), (int)A(args, argc, 1, 480));
    return Value();
}
GMLFN(window_center) { (void)self; (void)args; (void)argc; render_center_window(); return Value(); }
GMLFN(window_get_width) { (void)self; (void)args; (void)argc; return Value((double)render_window_width()); }
GMLFN(window_get_height) { (void)self; (void)args; (void)argc; return Value((double)render_window_height()); }
GMLFN(window_set_fullscreen) {
    (void)self;
    render_set_fullscreen(argc > 0 && gml_truthy(args[0]));
    return Value();
}
GMLFN(window_get_fullscreen) { (void)self; (void)args; (void)argc; return Value(render_get_fullscreen()); }
GMLFN(window_enable_borderless_fullscreen) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(window_set_caption) {
    (void)self;
    if (argc > 0) render_set_title(((std::string)args[0]).c_str());
    return Value();
}
GMLFN(window_set_cursor) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(display_get_width) { (void)self; (void)args; (void)argc; return Value((double)render_display_width()); }
GMLFN(display_get_height) { (void)self; (void)args; (void)argc; return Value((double)render_display_height()); }
GMLFN(display_get_gui_width) { (void)self; (void)args; (void)argc; return Value((double)render_gui_width()); }
GMLFN(display_get_gui_height) { (void)self; (void)args; (void)argc; return Value((double)render_gui_height()); }

static RtLayer* L(const Value* args, int argc, int i) {
    if (i >= argc) return nullptr;
    return kwik_layer_by_id((int)(double)args[i]);
}

GMLFN(layer_get_all) {
    (void)self; (void)args; (void)argc;
    Value out = kwik_new_array(nullptr, 0);
    for (auto& l : g_rt_layers) out.arr->items.push_back(Value((double)l.id));
    return out;
}
GMLFN(layer_get_name) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->name : "");
}
GMLFN(layer_get_depth) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->depth : 0.0);
}
GMLFN(layer_depth) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->depth = (double)args[1];
    return Value();
}
GMLFN(layer_get_x) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->x : 0.0);
}
GMLFN(layer_get_y) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->y : 0.0);
}
GMLFN(layer_x) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->x = (double)args[1];
    return Value();
}
GMLFN(layer_y) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->y = (double)args[1];
    return Value();
}
GMLFN(layer_get_hspeed) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->hspeed : 0.0);
}
GMLFN(layer_get_vspeed) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->vspeed : 0.0);
}
GMLFN(layer_hspeed) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->hspeed = (double)args[1];
    return Value();
}
GMLFN(layer_vspeed) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->vspeed = (double)args[1];
    return Value();
}
GMLFN(layer_get_visible) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->visible : 0.0);
}
GMLFN(layer_set_visible) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->visible = gml_truthy(args[1]);
    return Value();
}
GMLFN(layer_create) {
    (void)self;
    double depth = A(args, argc, 0);
    std::string name = argc > 1 ? (std::string)args[1] : "";
    return Value((double)kwik_layer_create(depth, name));
}
GMLFN(layer_destroy) {
    (void)self;
    if (argc < 1) return Value();
    int id = (int)A(args, argc, 0);
    for (size_t i = 0; i < g_rt_layers.size(); ++i)
        if (g_rt_layers[i].id == id) {
            g_rt_layers.erase(g_rt_layers.begin() + i);
            break;
        }
    return Value();
}
GMLFN(layer_force_draw_depth) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_get_all_elements) {
    (void)self;
    Value out = kwik_new_array(nullptr, 0);
    RtLayer* l = L(args, argc, 0);
    if (l && l->type == 1) out.arr->items.push_back(Value((double)l->id));
    if (l && l->type == 4) out.arr->items.push_back(Value((double)(900000 + l->id)));
    return out;
}
GMLFN(layer_get_element_type) {
    (void)self;
    if (argc < 1) return Value(-1.0);
    int id = (int)(double)args[0];
    if (id >= 900000) {
        RtLayer* l = kwik_layer_by_id(id - 900000);
        return Value(l && l->type == 4 ? 5.0 : -1.0);
    }
    RtLayer* l = kwik_layer_by_id(id);
    if (!l) return Value(-1.0);
    if (l->type == 1) return Value(1.0);
    if (l->type == 4) return Value(5.0);
    return Value(-1.0);
}
GMLFN(layer_background_get_index) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)l->id : -1.0);
}
GMLFN(layer_background_create) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (!l) return Value(-1.0);
    l->type = 1;
    l->sprite = argc > 1 ? (int)(double)args[1] : -1;
    l->color = 0xFFFFFFFF;
    return Value((double)l->id);
}
GMLFN(layer_background_exists) {
    (void)self;
    if (argc < 2) return Value(0.0);
    RtLayer* l = L(args, argc, 0);
    return Value(l && l->id == (int)(double)args[1] && l->type == 1);
}
GMLFN(layer_background_get_sprite) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)l->sprite : -1.0);
}
GMLFN(layer_background_change) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->sprite = (int)(double)args[1];
    return Value();
}
GMLFN(layer_background_sprite) { return layer_background_change(self, args, argc); }
GMLFN(layer_background_visible) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->el_visible = gml_truthy(args[1]);
    return Value();
}
GMLFN(layer_background_get_alpha) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->alpha : 1.0);
}
GMLFN(layer_background_alpha) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->alpha = (double)args[1];
    return Value();
}
GMLFN(layer_background_get_blend) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)(l->color & 0xFFFFFF) : 16777215.0);
}
GMLFN(layer_background_blend) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1)
        l->color = (l->color & 0xFF000000) | ((unsigned int)(double)args[1] & 0xFFFFFF);
    return Value();
}
GMLFN(layer_background_get_htiled) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)l->htiled : 0.0);
}
GMLFN(layer_background_htiled) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->htiled = gml_truthy(args[1]) ? 1 : 0;
    return Value();
}
GMLFN(layer_background_get_vtiled) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)l->vtiled : 0.0);
}
GMLFN(layer_background_vtiled) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->vtiled = gml_truthy(args[1]) ? 1 : 0;
    return Value();
}
GMLFN(layer_background_get_stretch) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? (double)l->stretch : 0.0);
}
GMLFN(layer_background_stretch) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->stretch = gml_truthy(args[1]) ? 1 : 0;
    return Value();
}
GMLFN(layer_background_get_xscale) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->xscale : 1.0);
}
GMLFN(layer_background_xscale) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->xscale = (double)args[1];
    return Value();
}
GMLFN(layer_background_get_yscale) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    return Value(l ? l->yscale : 1.0);
}
GMLFN(layer_background_yscale) {
    (void)self;
    RtLayer* l = L(args, argc, 0);
    if (l && argc > 1) l->yscale = (double)args[1];
    return Value();
}
GMLFN(layer_sprite_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_sprite_get_sprite) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(layer_tile_alpha) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tile_get_x) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_tile_get_y) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(layer_tile_visible) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tile_x) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tile_y) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tile_destroy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(layer_tile_get_xscale) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(layer_tile_get_yscale) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(layer_tile_get_region) {
    (void)self; (void)args; (void)argc;
    Value out;
    out.type = Value::ARR;
    out.arr = std::make_shared<GmlArray>();
    out.arr->items.assign(4, Value(0.0));
    return out;
}

}
