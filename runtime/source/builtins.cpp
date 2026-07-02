#include "gml_runtime.h"
#include "render.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace gml {

Value::operator double() const {
    return is_string ? std::atof(str.c_str()) : num;
}

Value::operator std::string() const {
    if (is_string) return str;
    if (std::floor(num) == num && std::isfinite(num)) {
        return std::to_string(static_cast<long long>(num));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", num);
    return buf;
}

Value gml_add(const Value& a, const Value& b) {
    if (a.is_string && b.is_string) return Value(a.str + b.str);
    return Value(static_cast<double>(a) + static_cast<double>(b));
}
Value gml_sub(const Value& a, const Value& b) { return Value(static_cast<double>(a) - static_cast<double>(b)); }
Value gml_mul(const Value& a, const Value& b) { return Value(static_cast<double>(a) * static_cast<double>(b)); }
Value gml_div(const Value& a, const Value& b) {
    double d = static_cast<double>(b);
    return Value(d == 0.0 ? 0.0 : static_cast<double>(a) / d);
}
Value gml_intdiv(const Value& a, const Value& b) {
    long long d = static_cast<long long>(static_cast<double>(b));
    return Value(d == 0 ? 0.0 : static_cast<double>(static_cast<long long>(static_cast<double>(a)) / d));
}
Value gml_mod(const Value& a, const Value& b) {
    double d = static_cast<double>(b);
    return Value(d == 0.0 ? 0.0 : std::fmod(static_cast<double>(a), d));
}
Value gml_and(const Value& a, const Value& b) {
    return Value(static_cast<double>(static_cast<long long>(static_cast<double>(a)) &
                                     static_cast<long long>(static_cast<double>(b))));
}
Value gml_or(const Value& a, const Value& b) {
    return Value(static_cast<double>(static_cast<long long>(static_cast<double>(a)) |
                                     static_cast<long long>(static_cast<double>(b))));
}
Value gml_xor(const Value& a, const Value& b) {
    return Value(static_cast<double>(static_cast<long long>(static_cast<double>(a)) ^
                                     static_cast<long long>(static_cast<double>(b))));
}
Value gml_shl(const Value& a, const Value& b) {
    return Value(static_cast<double>(static_cast<long long>(static_cast<double>(a))
                                     << static_cast<long long>(static_cast<double>(b))));
}
Value gml_shr(const Value& a, const Value& b) {
    return Value(static_cast<double>(static_cast<long long>(static_cast<double>(a)) >>
                                     static_cast<long long>(static_cast<double>(b))));
}
Value gml_neg(const Value& a) { return Value(-static_cast<double>(a)); }
Value gml_not(const Value& a) { return Value(gml_truthy(a) ? 0.0 : 1.0); }

static bool both_strings(const Value& a, const Value& b) { return a.is_string && b.is_string; }

Value gml_lt(const Value& a, const Value& b) { return Value(static_cast<double>(a) < static_cast<double>(b)); }
Value gml_le(const Value& a, const Value& b) { return Value(static_cast<double>(a) <= static_cast<double>(b)); }
Value gml_eq(const Value& a, const Value& b) {
    if (both_strings(a, b)) return Value(a.str == b.str);
    return Value(static_cast<double>(a) == static_cast<double>(b));
}
Value gml_ne(const Value& a, const Value& b) {
    if (both_strings(a, b)) return Value(a.str != b.str);
    return Value(static_cast<double>(a) != static_cast<double>(b));
}
Value gml_ge(const Value& a, const Value& b) { return Value(static_cast<double>(a) >= static_cast<double>(b)); }
Value gml_gt(const Value& a, const Value& b) { return Value(static_cast<double>(a) > static_cast<double>(b)); }

bool gml_truthy(const Value& a) { return static_cast<double>(a) >= 0.5; }

Value& global_var(const std::string& name) {
    static std::unordered_map<std::string, Value> globals;
    return globals[name];
}

Value& builtin_var(const std::string& name) {
    static std::unordered_map<std::string, Value> builtins;
    return builtins[name];
}

Value string(const Value& v) {
    return Value(static_cast<std::string>(v));
}

Value show_debug_message(const Value& v) {
    std::fprintf(stderr, "%s\n", static_cast<std::string>(v).c_str());
    return Value();
}

Value camera_set_view_pos(const Value&, const Value& x, const Value& y) {
    render_set_view_pos(static_cast<double>(x), static_cast<double>(y));
    return Value();
}
Value camera_get_view_width(const Value&) { return Value(render_view_width()); }
Value camera_get_view_height(const Value&) { return Value(render_view_height()); }

Value room_goto(const Value& index) {
    kwik_room_goto((int)(double)index);
    return Value();
}
Value room_goto_next() {
    kwik_room_goto_next();
    return Value();
}

Value draw_set_font(const Value& font) {
    kwik_set_font((int)(double)font);
    return Value();
}

Value draw_set_halign(const Value& align) {
    render_set_halign(static_cast<int>(static_cast<double>(align)));
    return Value();
}
Value draw_set_valign(const Value& align) {
    render_set_valign(static_cast<int>(static_cast<double>(align)));
    return Value();
}
Value draw_set_color(const Value& color) {
    render_set_color(static_cast<unsigned int>(static_cast<double>(color)));
    return Value();
}
Value draw_set_alpha(const Value& alpha) {
    render_set_alpha(static_cast<double>(alpha));
    return Value();
}
Value draw_text(const Value& x, const Value& y, const Value& text) {
    double dx = x, dy = y;
    std::string s = text;
    if (!kwik_draw_text_custom(dx, dy, s))
        render_draw_text(dx, dy, s);
    return Value();
}
Value draw_rectangle(const Value& x1, const Value& y1, const Value& x2, const Value& y2,
                     const Value& outline) {
    render_draw_rectangle(static_cast<double>(x1), static_cast<double>(y1), static_cast<double>(x2),
                          static_cast<double>(y2), static_cast<double>(outline) != 0.0);
    return Value();
}

Value event_inherited() { return Value(); }

Value audio_play_sound(const Value&, const Value&, const Value&) { return Value(); }
Value audio_stop_sound(const Value&) { return Value(); }

Value clamp(const Value& v, const Value& lo, const Value& hi) {
    double x = v, a = lo, b = hi;
    if (x < a) x = a;
    if (x > b) x = b;
    return Value(x);
}
Value round(const Value& v) { return Value(std::nearbyint(static_cast<double>(v))); }

Value keyboard_check(const Value& key) {
    return Value(render_key_down(static_cast<int>(static_cast<double>(key))) ? 1.0 : 0.0);
}
Value keyboard_check_pressed(const Value& key) {
    return Value(render_key_pressed(static_cast<int>(static_cast<double>(key))) ? 1.0 : 0.0);
}

Value file_exists(const Value& path) {
    std::FILE* f = std::fopen(static_cast<std::string>(path).c_str(), "rb");
    if (f) { std::fclose(f); return Value(1.0); }
    return Value(0.0);
}

Value display_get_gui_width() { return Value(static_cast<double>(render_gui_width())); }
Value display_get_gui_height() { return Value(static_cast<double>(render_gui_height())); }

Value window_set_size(const Value& w, const Value& h) {
    render_set_window_size(static_cast<int>(static_cast<double>(w)),
                           static_cast<int>(static_cast<double>(h)));
    return Value();
}

}
