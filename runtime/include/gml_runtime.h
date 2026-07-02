#pragma once

#include <string>
#include <unordered_map>

namespace gml {

struct Value {
    bool is_string = false;
    double num = 0.0;
    std::string str;

    Value() {}
    Value(double d) : is_string(false), num(d) {}
    Value(int i) : is_string(false), num(static_cast<double>(i)) {}
    Value(bool b) : is_string(false), num(b ? 1.0 : 0.0) {}
    Value(const char* s) : is_string(true), str(s) {}
    Value(const std::string& s) : is_string(true), str(s) {}

    operator double() const;
    operator std::string() const;
};

Value gml_add(const Value& a, const Value& b);
Value gml_sub(const Value& a, const Value& b);
Value gml_mul(const Value& a, const Value& b);
Value gml_div(const Value& a, const Value& b);
Value gml_intdiv(const Value& a, const Value& b);
Value gml_mod(const Value& a, const Value& b);
Value gml_and(const Value& a, const Value& b);
Value gml_or(const Value& a, const Value& b);
Value gml_xor(const Value& a, const Value& b);
Value gml_shl(const Value& a, const Value& b);
Value gml_shr(const Value& a, const Value& b);
Value gml_neg(const Value& a);
Value gml_not(const Value& a);
Value gml_lt(const Value& a, const Value& b);
Value gml_le(const Value& a, const Value& b);
Value gml_eq(const Value& a, const Value& b);
Value gml_ne(const Value& a, const Value& b);
Value gml_ge(const Value& a, const Value& b);
Value gml_gt(const Value& a, const Value& b);
bool gml_truthy(const Value& a);

struct Instance {
    double x = 0.0;
    double y = 0.0;
    int id = 0;
    int object_index = 0;
    std::unordered_map<std::string, Value> dynamic_vars;

    Value& var(const std::string& name) { return dynamic_vars[name]; }
};

using EventFn = void (*)(Instance&);

struct ObjectDef {
    const char* name;
    EventFn create;
    EventFn step;
    EventFn draw;
    EventFn draw_gui;
    int sprite_index;
};

struct KwikSprite {
    int first_frame;
    int frame_count;
    int origin_x;
    int origin_y;
    double speed;
    int speed_type;
};

struct KwikFont {
    int atlas_image;
    int glyph_start;
    int glyph_count;
    int size;
};

struct KwikGlyph {
    int ch;
    int x, y, w, h;
    int shift, offset;
};

struct InstanceInit {
    int object_index;
    double x;
    double y;
    int id;
};

struct RoomDef {
    const char* name;
    int width;
    int height;
    unsigned int bg_color;
    int view_x;
    int view_y;
    int view_w;
    int view_h;
    const InstanceInit* instances;
    int instance_count;
};

extern const KwikSprite* g_sprites;
extern int g_sprite_count;
extern int g_image_count;
extern const KwikFont* g_fonts;
extern int g_font_count;
extern const KwikGlyph* g_glyphs;
extern int g_glyph_count;

void kwik_set_font(int font_id);
bool kwik_draw_text_custom(double x, double y, const std::string& text);

void kwik_room_goto(int index);
void kwik_room_goto_next();
void kwik_room_goto_previous();

Value& global_var(const std::string& name);
Value& builtin_var(const std::string& name);

Value draw_sprite(const Value& sprite, const Value& subimg, const Value& x, const Value& y);
Value draw_self(Instance& self);

Value string(const Value& v);
Value show_debug_message(const Value& v);

Value camera_set_view_pos(const Value& cam, const Value& x, const Value& y);
Value camera_get_view_width(const Value& cam);
Value camera_get_view_height(const Value& cam);

Value room_goto(const Value& index);
Value room_goto_next();

Value draw_set_font(const Value& font);
Value draw_set_halign(const Value& align);
Value draw_set_valign(const Value& align);
Value draw_set_color(const Value& color);
Value draw_set_alpha(const Value& alpha);
Value draw_text(const Value& x, const Value& y, const Value& text);
Value draw_rectangle(const Value& x1, const Value& y1, const Value& x2, const Value& y2,
                     const Value& outline);

Value event_inherited();

Value audio_play_sound(const Value& snd, const Value& priority, const Value& loop);
Value audio_stop_sound(const Value& snd);

Value clamp(const Value& v, const Value& lo, const Value& hi);
Value round(const Value& v);

Value instance_exists(const Value& obj);
Value instance_find(const Value& obj, const Value& n);
Value place_meeting(const Value& x, const Value& y, const Value& obj);

Value keyboard_check(const Value& key);
Value keyboard_check_pressed(const Value& key);

Value file_exists(const Value& path);

Value display_get_gui_width();
Value display_get_gui_height();

Value window_set_size(const Value& w, const Value& h);

int run_game(const ObjectDef* objects, int object_count, const RoomDef* rooms, int room_count);

}
