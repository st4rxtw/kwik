#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif

#include "gml_runtime.h"
#include "engine_internal.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}

GMLFN(abs) { (void)self; return Value(std::fabs(A(args, argc, 0))); }
GMLFN(sign) { (void)self; double v = A(args, argc, 0); return Value(v > 0 ? 1.0 : v < 0 ? -1.0 : 0.0); }
GMLFN(round) {
    (void)self;
    double v = A(args, argc, 0);
    double r = std::nearbyint(v);
    return Value(r == 0.0 ? 0.0 : r);
}
GMLFN(floor) { (void)self; return Value(std::floor(A(args, argc, 0))); }
GMLFN(ceil) { (void)self; return Value(std::ceil(A(args, argc, 0))); }
GMLFN(frac) { (void)self; double v = A(args, argc, 0); return Value(v - std::trunc(v)); }
GMLFN(sqrt) { (void)self; double v = A(args, argc, 0); return Value(v < 0 ? 0.0 : std::sqrt(v)); }
GMLFN(sqr) { (void)self; double v = A(args, argc, 0); return Value(v * v); }
GMLFN(exp) { (void)self; return Value(std::exp(A(args, argc, 0))); }
GMLFN(ln) { (void)self; return Value(std::log(std::max(1e-300, A(args, argc, 0)))); }
GMLFN(log2) { (void)self; return Value(std::log2(std::max(1e-300, A(args, argc, 0)))); }
GMLFN(log10) { (void)self; return Value(std::log10(std::max(1e-300, A(args, argc, 0)))); }
GMLFN(sin) { (void)self; return Value(std::sin(A(args, argc, 0))); }
GMLFN(cos) { (void)self; return Value(std::cos(A(args, argc, 0))); }
GMLFN(tan) { (void)self; return Value(std::tan(A(args, argc, 0))); }
GMLFN(arcsin) { (void)self; return Value(std::asin(A(args, argc, 0))); }
GMLFN(arccos) { (void)self; return Value(std::acos(A(args, argc, 0))); }
GMLFN(arctan) { (void)self; return Value(std::atan(A(args, argc, 0))); }
GMLFN(arctan2) { (void)self; return Value(std::atan2(A(args, argc, 0), A(args, argc, 1))); }
GMLFN(dsin) { (void)self; return Value(std::sin(A(args, argc, 0) * M_PI / 180.0)); }
GMLFN(dcos) { (void)self; return Value(std::cos(A(args, argc, 0) * M_PI / 180.0)); }
GMLFN(dtan) { (void)self; return Value(std::tan(A(args, argc, 0) * M_PI / 180.0)); }
GMLFN(darcsin) { (void)self; return Value(std::asin(A(args, argc, 0)) * 180.0 / M_PI); }
GMLFN(darccos) { (void)self; return Value(std::acos(A(args, argc, 0)) * 180.0 / M_PI); }
GMLFN(darctan) { (void)self; return Value(std::atan(A(args, argc, 0)) * 180.0 / M_PI); }
GMLFN(darctan2) { (void)self; return Value(std::atan2(A(args, argc, 0), A(args, argc, 1)) * 180.0 / M_PI); }
GMLFN(power) { (void)self; return Value(std::pow(A(args, argc, 0), A(args, argc, 1))); }

GMLFN(min) {
    (void)self;
    if (argc == 0) return Value(0.0);
    double best = (double)args[0];
    for (int i = 1; i < argc; ++i) best = std::min(best, (double)args[i]);
    return Value(best);
}
GMLFN(max) {
    (void)self;
    if (argc == 0) return Value(0.0);
    double best = (double)args[0];
    for (int i = 1; i < argc; ++i) best = std::max(best, (double)args[i]);
    return Value(best);
}
GMLFN(clamp) {
    (void)self;
    double v = A(args, argc, 0), lo = A(args, argc, 1), hi = A(args, argc, 2);
    return Value(v < lo ? lo : v > hi ? hi : v);
}

GMLFN(random) { (void)self; return Value(gml_random01() * A(args, argc, 0)); }
GMLFN(random_range) {
    (void)self;
    double lo = A(args, argc, 0), hi = A(args, argc, 1);
    return Value(lo + gml_random01() * (hi - lo));
}
GMLFN(irandom) { (void)self; return Value(std::floor(gml_random01() * (std::floor(A(args, argc, 0)) + 1))); }
GMLFN(irandom_range) {
    (void)self;
    double lo = std::floor(A(args, argc, 0)), hi = std::floor(A(args, argc, 1));
    if (hi < lo) std::swap(lo, hi);
    return Value(lo + std::floor(gml_random01() * (hi - lo + 1)));
}
GMLFN(random_set_seed) { (void)self; gml_random_seed((unsigned)A(args, argc, 0)); return Value(); }
GMLFN(randomize) { (void)self; (void)args; (void)argc; gml_random_seed((unsigned)now_ms()); return Value(); }
GMLFN(choose) {
    (void)self;
    if (argc == 0) return Value();
    int i = (int)(gml_random01() * argc);
    if (i >= argc) i = argc - 1;
    return args[i];
}

GMLFN(point_direction) {
    (void)self;
    double d = std::atan2(-(A(args, argc, 3) - A(args, argc, 1)),
                          A(args, argc, 2) - A(args, argc, 0)) * 180.0 / M_PI;
    return Value(std::fmod(d + 360.0, 360.0));
}
GMLFN(point_distance) {
    (void)self;
    return Value(std::hypot(A(args, argc, 2) - A(args, argc, 0), A(args, argc, 3) - A(args, argc, 1)));
}
GMLFN(lengthdir_x) { (void)self; return Value(A(args, argc, 0) * std::cos(A(args, argc, 1) * M_PI / 180.0)); }
GMLFN(lengthdir_y) { (void)self; return Value(-A(args, argc, 0) * std::sin(A(args, argc, 1) * M_PI / 180.0)); }
GMLFN(angle_difference) {
    (void)self;
    double d = std::fmod(A(args, argc, 0) - A(args, argc, 1), 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return Value(d);
}

static std::string S(const Value* args, int argc, int i) {
    return i < argc ? (std::string)args[i] : std::string();
}

GMLFN(chr) { (void)self; std::string s(1, (char)(int)A(args, argc, 0)); return Value(s); }
GMLFN(ord) {
    (void)self;
    std::string s = S(args, argc, 0);
    return Value(s.empty() ? 0.0 : (double)(unsigned char)s[0]);
}
GMLFN(real) {
    (void)self;
    if (argc < 1) return Value(0.0);
    return Value((double)args[0]);
}
GMLFN(string) {
    (void)self;
    if (argc < 1) return Value("");
    return Value((std::string)args[0]);
}
GMLFN(__string__) {
    (void)self;
    if (argc < 1) return Value("");
    std::string fmt = (std::string)args[0];
    std::string out;
    out.reserve(fmt.size());
    for (size_t i = 0; i < fmt.size();) {
        if (fmt[i] == '{') {
            size_t close = fmt.find('}', i + 1);
            if (close != std::string::npos) {
                std::string idx_str = fmt.substr(i + 1, close - i - 1);
                bool numeric = !idx_str.empty();
                for (char c : idx_str) numeric = numeric && (c >= '0' && c <= '9');
                if (numeric) {
                    int idx = std::atoi(idx_str.c_str()) + 1;
                    if (idx >= 0 && idx < argc) out += (std::string)args[idx];
                    i = close + 1;
                    continue;
                }
            }
        }
        out += fmt[i];
        ++i;
    }
    return Value(out);
}
GMLFN(string_length) { (void)self; return Value((double)S(args, argc, 0).size()); }
GMLFN(string_byte_length) { (void)self; return Value((double)S(args, argc, 0).size()); }
GMLFN(string_char_at) {
    (void)self;
    std::string s = S(args, argc, 0);
    int i = (int)A(args, argc, 1) - 1;
    if (i < 0) i = 0;
    if (i >= (int)s.size()) return Value("");
    return Value(std::string(1, s[i]));
}
GMLFN(string_copy) {
    (void)self;
    std::string s = S(args, argc, 0);
    int i = (int)A(args, argc, 1) - 1;
    int n = (int)A(args, argc, 2);
    if (i < 0) i = 0;
    if (i >= (int)s.size() || n <= 0) return Value("");
    return Value(s.substr(i, n));
}
GMLFN(string_delete) {
    (void)self;
    std::string s = S(args, argc, 0);
    int i = (int)A(args, argc, 1) - 1;
    int n = (int)A(args, argc, 2);
    if (i < 0 || i >= (int)s.size() || n <= 0) return Value(s);
    s.erase(i, n);
    return Value(s);
}
GMLFN(string_insert) {
    (void)self;
    std::string sub = S(args, argc, 0);
    std::string s = S(args, argc, 1);
    int i = (int)A(args, argc, 2) - 1;
    if (i < 0) i = 0;
    if (i > (int)s.size()) i = s.size();
    s.insert(i, sub);
    return Value(s);
}
GMLFN(string_pos) {
    (void)self;
    std::string sub = S(args, argc, 0);
    std::string s = S(args, argc, 1);
    size_t p = s.find(sub);
    return Value(p == std::string::npos ? 0.0 : (double)(p + 1));
}
GMLFN(string_replace) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string find = S(args, argc, 1);
    std::string repl = S(args, argc, 2);
    size_t p = s.find(find);
    if (p != std::string::npos) s.replace(p, find.size(), repl);
    return Value(s);
}
GMLFN(string_replace_all) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string find = S(args, argc, 1);
    std::string repl = S(args, argc, 2);
    if (find.empty()) return Value(s);
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t p = s.find(find, pos);
        if (p == std::string::npos) {
            out += s.substr(pos);
            break;
        }
        out += s.substr(pos, p - pos);
        out += repl;
        pos = p + find.size();
    }
    return Value(out);
}
GMLFN(string_lower) {
    (void)self;
    std::string s = S(args, argc, 0);
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 32;
    return Value(s);
}
GMLFN(string_upper) {
    (void)self;
    std::string s = S(args, argc, 0);
    for (char& c : s)
        if (c >= 'a' && c <= 'z') c -= 32;
    return Value(s);
}
GMLFN(string_repeat) {
    (void)self;
    std::string s = S(args, argc, 0);
    int n = (int)A(args, argc, 1);
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return Value(out);
}
GMLFN(string_digits) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string out;
    for (char c : s)
        if (c >= '0' && c <= '9') out.push_back(c);
    return Value(out);
}
GMLFN(string_letters) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string out;
    for (char c : s)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) out.push_back(c);
    return Value(out);
}
GMLFN(string_hash_to_newline) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '#') {
            out.push_back('#');
            ++i;
        } else if (s[i] == '#') {
            out.push_back('\n');
        } else {
            out.push_back(s[i]);
        }
    }
    return Value(out);
}
GMLFN(string_split) {
    (void)self;
    std::string s = S(args, argc, 0);
    std::string delim = S(args, argc, 1);
    bool remove_empty = argc > 2 && gml_truthy(args[2]);
    Value out = kwik_new_array(nullptr, 0);
    if (delim.empty()) {
        out.arr->items.push_back(Value(s));
        return out;
    }
    size_t pos = 0;
    while (true) {
        size_t p = s.find(delim, pos);
        std::string part = p == std::string::npos ? s.substr(pos) : s.substr(pos, p - pos);
        if (!remove_empty || !part.empty()) out.arr->items.push_back(Value(part));
        if (p == std::string::npos) break;
        pos = p + delim.size();
    }
    return out;
}
GMLFN(string_format) {
    (void)self;
    double v = A(args, argc, 0);
    int tot = (int)A(args, argc, 1);
    int dec = (int)A(args, argc, 2);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%*.*f", tot, dec, v);
    return Value(std::string(buf));
}

GMLFN(is_string) { (void)self; return Value(argc > 0 && args[0].type == Value::STR); }
GMLFN(is_real) { (void)self; return Value(argc > 0 && args[0].type == Value::REAL); }
GMLFN(is_array) { (void)self; return Value(argc > 0 && args[0].type == Value::ARR); }
GMLFN(is_undefined) { (void)self; return Value(argc == 0 || args[0].type == Value::UNDEF); }
GMLFN(is_bool) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(is_struct) { (void)self; return Value(argc > 0 && args[0].type == Value::OBJ); }
GMLFN(is_method) { (void)self; return Value(argc > 0 && args[0].type == Value::FN); }
GMLFN(typeof_fn) {
    (void)self;
    if (argc < 1) return Value("undefined");
    switch (args[0].type) {
        case Value::REAL: return Value("number");
        case Value::STR: return Value("string");
        case Value::ARR: return Value("array");
        case Value::OBJ: return Value("struct");
        case Value::FN: return Value("method");
        default: return Value("undefined");
    }
}
GMLFN(bool_fn) { (void)self; return Value(argc > 0 && gml_truthy(args[0])); }

GMLFN(variable_global_exists) {
    (void)self;
    if (argc < 1) return Value(0.0);
    return Value(global_var((std::string)args[0]).type != Value::UNDEF);
}
GMLFN(variable_instance_exists) {
    if (argc < 2) return Value(0.0);
    Instance* t = kwik_resolve_target(self, args[0]);
    if (!t) return Value(0.0);
    std::string n = (std::string)args[1];
    return Value(t->has(n) && t->var(n).type != Value::UNDEF);
}
GMLFN(variable_struct_set) {
    if (argc < 3) return Value();
    Instance* t = kwik_resolve_target(self, args[0]);
    if (t) t->var((std::string)args[1]) = args[2];
    return Value();
}
GMLFN(variable_struct_get) {
    if (argc < 2) return Value();
    Instance* t = kwik_resolve_target(self, args[0]);
    if (!t) return Value();
    return t->var((std::string)args[1]);
}
GMLFN(variable_struct_exists) {
    if (argc < 2) return Value(0.0);
    Instance* t = kwik_resolve_target(self, args[0]);
    return Value(t && t->has((std::string)args[1]));
}

GMLFN(array_create) {
    (void)self;
    Value out = kwik_new_array(nullptr, 0);
    int n = (int)A(args, argc, 0);
    Value fill = argc > 1 ? args[1] : Value(0.0);
    if (n > 0) out.arr->items.assign(n, fill);
    return out;
}
GMLFN(array_length) {
    (void)self;
    if (argc < 1 || args[0].type != Value::ARR || !args[0].arr) return Value(0.0);
    return Value((double)args[0].arr->items.size());
}
GMLFN(array_length_1d) { return array_length(self, args, argc); }
GMLFN(array_push) {
    (void)self;
    if (argc < 1 || args[0].type != Value::ARR || !args[0].arr) return Value();
    for (int i = 1; i < argc; ++i) args[0].arr->items.push_back(args[i]);
    return Value();
}
GMLFN(array_pop) {
    (void)self;
    if (argc < 1 || args[0].type != Value::ARR || !args[0].arr || args[0].arr->items.empty())
        return Value();
    Value v = args[0].arr->items.back();
    args[0].arr->items.pop_back();
    return v;
}
GMLFN(array_resize) {
    (void)self;
    if (argc < 2 || args[0].type != Value::ARR || !args[0].arr) return Value();
    int n = (int)(double)args[1];
    if (n >= 0) args[0].arr->items.resize(n);
    return Value();
}
GMLFN(array_copy) {
    (void)self;
    if (argc < 5 || args[0].type != Value::ARR || !args[0].arr || args[2].type != Value::ARR ||
        !args[2].arr)
        return Value();
    int di = (int)(double)args[1];
    int si = (int)(double)args[3];
    int n = (int)(double)args[4];
    auto& dst = args[0].arr->items;
    auto& src = args[2].arr->items;
    for (int i = 0; i < n; ++i) {
        if (si + i < 0 || (size_t)(si + i) >= src.size() || di + i < 0) continue;
        if ((size_t)(di + i) >= dst.size()) dst.resize(di + i + 1);
        dst[di + i] = src[si + i];
    }
    return Value();
}
GMLFN(array_delete) {
    (void)self;
    if (argc < 3 || args[0].type != Value::ARR || !args[0].arr) return Value();
    int i = (int)(double)args[1];
    int n = (int)(double)args[2];
    auto& v = args[0].arr->items;
    if (i < 0 || (size_t)i >= v.size() || n <= 0) return Value();
    v.erase(v.begin() + i, v.begin() + std::min((size_t)(i + n), v.size()));
    return Value();
}
GMLFN(array_insert) {
    (void)self;
    if (argc < 3 || args[0].type != Value::ARR || !args[0].arr) return Value();
    int i = (int)(double)args[1];
    auto& v = args[0].arr->items;
    if (i < 0) i = 0;
    if ((size_t)i > v.size()) v.resize(i);
    v.insert(v.begin() + i, args + 2, args + argc);
    return Value();
}

GMLFN(show_debug_message) {
    (void)self;
    std::fprintf(stderr, "%s\n", argc > 0 ? ((std::string)args[0]).c_str() : "");
    return Value();
}
GMLFN(show_error) {
    (void)self;
    std::fprintf(stderr, "[error] %s\n", argc > 0 ? ((std::string)args[0]).c_str() : "");
    if (argc > 1 && gml_truthy(args[1])) g_game_end_requested = true;
    return Value();
}
GMLFN(show_message) { return show_debug_message(self, args, argc); }

GMLFN(make_color_rgb) {
    (void)self;
    int r = (int)A(args, argc, 0) & 0xFF, g = (int)A(args, argc, 1) & 0xFF,
        b = (int)A(args, argc, 2) & 0xFF;
    return Value((double)(r | (g << 8) | (b << 16)));
}
GMLFN(make_color_hsv) {
    (void)self;
    double h = A(args, argc, 0) / 255.0 * 360.0;
    double s = A(args, argc, 1) / 255.0;
    double v = A(args, argc, 2) / 255.0;
    double c = v * s;
    double hh = h / 60.0;
    double x = c * (1 - std::fabs(std::fmod(hh, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    if (hh < 1) { r = c; g = x; }
    else if (hh < 2) { r = x; g = c; }
    else if (hh < 3) { g = c; b = x; }
    else if (hh < 4) { g = x; b = c; }
    else if (hh < 5) { r = x; b = c; }
    else { r = c; b = x; }
    double m = v - c;
    int ri = (int)((r + m) * 255), gi = (int)((g + m) * 255), bi = (int)((b + m) * 255);
    return Value((double)(ri | (gi << 8) | (bi << 16)));
}
GMLFN(merge_color) {
    (void)self;
    int c1 = (int)A(args, argc, 0), c2 = (int)A(args, argc, 1);
    double f = A(args, argc, 2);
    int r = (int)((c1 & 0xFF) * (1 - f) + (c2 & 0xFF) * f);
    int g = (int)(((c1 >> 8) & 0xFF) * (1 - f) + ((c2 >> 8) & 0xFF) * f);
    int b = (int)(((c1 >> 16) & 0xFF) * (1 - f) + ((c2 >> 16) & 0xFF) * f);
    return Value((double)(r | (g << 8) | (b << 16)));
}
GMLFN(color_get_red) { (void)self; return Value((double)((int)A(args, argc, 0) & 0xFF)); }
GMLFN(color_get_green) { (void)self; return Value((double)(((int)A(args, argc, 0) >> 8) & 0xFF)); }
GMLFN(color_get_blue) { (void)self; return Value((double)(((int)A(args, argc, 0) >> 16) & 0xFF)); }

}
