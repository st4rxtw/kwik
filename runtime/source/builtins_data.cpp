#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}
static std::string S(const Value* args, int argc, int i) {
    return i < argc ? (std::string)args[i] : std::string();
}

struct DsKey {
    bool is_str;
    double num;
    std::string str;
    bool operator<(const DsKey& o) const {
        if (is_str != o.is_str) return is_str < o.is_str;
        if (is_str) return str < o.str;
        return num < o.num;
    }
};

static DsKey key_of(const Value& v) {
    DsKey k;
    k.is_str = v.type == Value::STR;
    if (k.is_str) k.str = v.str;
    else k.num = (double)v;
    return k;
}
static Value key_value(const DsKey& k) {
    return k.is_str ? Value(k.str) : Value(k.num);
}

struct DsMap {
    std::map<DsKey, Value> data;
    bool alive = true;
};
struct DsList {
    std::vector<Value> data;
    bool alive = true;
};

static std::vector<DsMap> g_maps;
static std::vector<DsList> g_lists;

static DsMap* map_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_maps.size() || !g_maps[i].alive) return nullptr;
    return &g_maps[i];
}
static DsList* list_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_lists.size() || !g_lists[i].alive) return nullptr;
    return &g_lists[i];
}

GMLFN(ds_map_create) {
    (void)self; (void)args; (void)argc;
    g_maps.emplace_back();
    return Value((double)(g_maps.size() - 1));
}
GMLFN(ds_map_destroy) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    if (m) { m->alive = false; m->data.clear(); }
    return Value();
}
GMLFN(ds_map_set) {
    (void)self;
    DsMap* m = argc > 2 ? map_of(args[0]) : nullptr;
    if (m) m->data[key_of(args[1])] = args[2];
    return Value();
}
GMLFN(ds_map_set_post) { return ds_map_set(self, args, argc); }
GMLFN(ds_map_add) {
    (void)self;
    DsMap* m = argc > 2 ? map_of(args[0]) : nullptr;
    if (!m) return Value(0.0);
    if (m->data.count(key_of(args[1]))) return Value(0.0);
    m->data[key_of(args[1])] = args[2];
    return Value(1.0);
}
GMLFN(ds_map_find_value) {
    (void)self;
    DsMap* m = argc > 1 ? map_of(args[0]) : nullptr;
    if (!m) return Value();
    auto it = m->data.find(key_of(args[1]));
    if (it == m->data.end()) return Value();
    return it->second;
}
GMLFN(ds_map_exists) {
    (void)self;
    DsMap* m = argc > 1 ? map_of(args[0]) : nullptr;
    return Value(m && m->data.count(key_of(args[1])) != 0);
}
GMLFN(ds_map_delete) {
    (void)self;
    DsMap* m = argc > 1 ? map_of(args[0]) : nullptr;
    if (m) m->data.erase(key_of(args[1]));
    return Value();
}
GMLFN(ds_map_size) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    return Value(m ? (double)m->data.size() : 0.0);
}
GMLFN(ds_map_empty) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    return Value(!m || m->data.empty());
}
GMLFN(ds_map_find_first) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    if (!m || m->data.empty()) return Value();
    return key_value(m->data.begin()->first);
}
GMLFN(ds_map_find_next) {
    (void)self;
    DsMap* m = argc > 1 ? map_of(args[0]) : nullptr;
    if (!m) return Value();
    auto it = m->data.upper_bound(key_of(args[1]));
    if (it == m->data.end()) return Value();
    return key_value(it->first);
}
GMLFN(ds_map_keys_to_array) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    Value out = argc > 1 && args[1].type == Value::ARR ? args[1] : kwik_new_array(nullptr, 0);
    if (m)
        for (auto& kv : m->data) out.arr->items.push_back(key_value(kv.first));
    return out;
}

GMLFN(ds_list_create) {
    (void)self; (void)args; (void)argc;
    g_lists.emplace_back();
    return Value((double)(g_lists.size() - 1));
}
GMLFN(ds_list_destroy) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (l) { l->alive = false; l->data.clear(); }
    return Value();
}
GMLFN(ds_list_add) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (l)
        for (int i = 1; i < argc; ++i) l->data.push_back(args[i]);
    return Value();
}
GMLFN(ds_list_size) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    return Value(l ? (double)l->data.size() : 0.0);
}
GMLFN(ds_list_empty) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    return Value(!l || l->data.empty());
}
GMLFN(ds_list_find_value) {
    (void)self;
    DsList* l = argc > 1 ? list_of(args[0]) : nullptr;
    if (!l) return Value();
    int i = (int)(double)args[1];
    if (i < 0 || (size_t)i >= l->data.size()) return Value();
    return l->data[i];
}
GMLFN(ds_list_clear) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (l) l->data.clear();
    return Value();
}
GMLFN(ds_list_delete_fn) {
    (void)self;
    DsList* l = argc > 1 ? list_of(args[0]) : nullptr;
    if (l) {
        int i = (int)(double)args[1];
        if (i >= 0 && (size_t)i < l->data.size()) l->data.erase(l->data.begin() + i);
    }
    return Value();
}
GMLFN(ds_list_find_index) {
    (void)self;
    DsList* l = argc > 1 ? list_of(args[0]) : nullptr;
    if (!l) return Value(-1.0);
    for (size_t i = 0; i < l->data.size(); ++i)
        if (gml_truthy(gml_eq(l->data[i], args[1]))) return Value((double)i);
    return Value(-1.0);
}
GMLFN(ds_list_shuffle) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (l) {
        for (size_t i = l->data.size(); i > 1; --i) {
            size_t j = (size_t)(gml_random01() * i);
            if (j >= i) j = i - 1;
            std::swap(l->data[i - 1], l->data[j]);
        }
    }
    return Value();
}
GMLFN(ds_map_clear) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    if (m) m->data.clear();
    return Value();
}
GMLFN(ds_map_add_list) { return ds_map_set(self, args, argc); }
static void hex_append(std::string& out, const void* p, size_t n) {
    static const char* hx = "0123456789ABCDEF";
    const unsigned char* b = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i) {
        out.push_back(hx[b[i] >> 4]);
        out.push_back(hx[b[i] & 15]);
    }
}
static bool hex_read(const std::string& in, size_t& pos, void* p, size_t n) {
    if (pos + n * 2 > in.size()) return false;
    unsigned char* b = (unsigned char*)p;
    for (size_t i = 0; i < n; ++i) {
        auto nib = [&](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hi = nib(in[pos]), lo = nib(in[pos + 1]);
        if (hi < 0 || lo < 0) return false;
        b[i] = (unsigned char)((hi << 4) | lo);
        pos += 2;
    }
    return true;
}

GMLFN(ds_list_write) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (!l) return Value("");
    std::string out = "4B57494B";
    uint32_t count = (uint32_t)l->data.size();
    hex_append(out, &count, 4);
    for (const Value& v : l->data) {
        if (v.type == Value::STR) {
            uint8_t tag = 1;
            uint32_t len = (uint32_t)v.str.size();
            hex_append(out, &tag, 1);
            hex_append(out, &len, 4);
            hex_append(out, v.str.data(), v.str.size());
        } else {
            uint8_t tag = 0;
            double d = v.num;
            hex_append(out, &tag, 1);
            hex_append(out, &d, 8);
        }
    }
    return Value(out);
}
GMLFN(ds_list_read) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (!l || argc < 2) return Value();
    std::string in = (std::string)args[1];
    size_t pos = 0;
    uint32_t magic = 0;
    if (!hex_read(in, pos, &magic, 4) || magic != 0x4B49574Bu) return Value();
    uint32_t count = 0;
    if (!hex_read(in, pos, &count, 4) || count > 1000000) return Value();
    l->data.clear();
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t tag = 0;
        if (!hex_read(in, pos, &tag, 1)) return Value();
        if (tag == 1) {
            uint32_t len = 0;
            if (!hex_read(in, pos, &len, 4) || len > 10000000) return Value();
            std::string s(len, '\0');
            if (!hex_read(in, pos, s.data(), len)) return Value();
            l->data.push_back(Value(s));
        } else {
            double d = 0;
            if (!hex_read(in, pos, &d, 8)) return Value();
            l->data.push_back(Value(d));
        }
    }
    return Value();
}
GMLFN(ds_exists) {
    (void)self;
    if (argc < 2) return Value(0.0);
    int type = (int)(double)args[1];
    if (type == 1) return Value(map_of(args[0]) != nullptr);
    if (type == 2) return Value(list_of(args[0]) != nullptr);
    return Value(0.0);
}

struct JsonParser {
    const char* p;
    const char* end;

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    bool parse_string(std::string& out) {
        if (p >= end || *p != '"') return false;
        ++p;
        out.clear();
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                switch (*p) {
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (p + 4 < end) {
                            unsigned int cp = 0;
                            for (int i = 1; i <= 4; ++i) {
                                char c = p[i];
                                cp <<= 4;
                                if (c >= '0' && c <= '9') cp |= c - '0';
                                else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
                                else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
                            }
                            p += 4;
                            if (cp < 0x80) {
                                out.push_back((char)cp);
                            } else if (cp < 0x800) {
                                out.push_back((char)(0xC0 | (cp >> 6)));
                                out.push_back((char)(0x80 | (cp & 0x3F)));
                            } else {
                                out.push_back((char)(0xE0 | (cp >> 12)));
                                out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                out.push_back((char)(0x80 | (cp & 0x3F)));
                            }
                        }
                        break;
                    }
                    default: out.push_back(*p); break;
                }
                ++p;
            } else {
                out.push_back(*p);
                ++p;
            }
        }
        if (p < end) ++p;
        return true;
    }

    bool parse_ds(Value& out) {
        skip_ws();
        if (p >= end) return false;
        if (*p == '{') {
            ++p;
            g_maps.emplace_back();
            int id = (int)g_maps.size() - 1;
            skip_ws();
            if (p < end && *p == '}') { ++p; out = Value((double)id); return true; }
            while (p < end) {
                skip_ws();
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws();
                if (p >= end || *p != ':') return false;
                ++p;
                Value v;
                if (!parse_ds(v)) return false;
                g_maps[id].data[key_of(Value(key))] = v;
                skip_ws();
                if (p < end && *p == ',') { ++p; continue; }
                break;
            }
            skip_ws();
            if (p < end && *p == '}') ++p;
            out = Value((double)id);
            return true;
        }
        if (*p == '[') {
            ++p;
            g_lists.emplace_back();
            int id = (int)g_lists.size() - 1;
            skip_ws();
            if (p < end && *p == ']') { ++p; out = Value((double)id); return true; }
            while (p < end) {
                Value v;
                if (!parse_ds(v)) return false;
                g_lists[id].data.push_back(v);
                skip_ws();
                if (p < end && *p == ',') { ++p; continue; }
                break;
            }
            skip_ws();
            if (p < end && *p == ']') ++p;
            out = Value((double)id);
            return true;
        }
        if (*p == '"') {
            std::string s;
            if (!parse_string(s)) return false;
            out = Value(s);
            return true;
        }
        if (!std::strncmp(p, "true", 4)) { p += 4; out = Value(1.0); return true; }
        if (!std::strncmp(p, "false", 5)) { p += 5; out = Value(0.0); return true; }
        if (!std::strncmp(p, "null", 4)) { p += 4; out = Value(); return true; }
        char* endp = nullptr;
        double d = std::strtod(p, &endp);
        if (endp == p) return false;
        p = endp;
        out = Value(d);
        return true;
    }
};

GMLFN(json_decode) {
    (void)self;
    std::string src = S(args, argc, 0);
    JsonParser jp{src.c_str(), src.c_str() + src.size()};
    Value out;
    jp.skip_ws();
    if (jp.p < jp.end && *jp.p == '[') {
        Value lst;
        if (!jp.parse_ds(lst)) return Value(-1.0);
        g_maps.emplace_back();
        int id = (int)g_maps.size() - 1;
        g_maps[id].data[key_of(Value("default"))] = lst;
        return Value((double)id);
    }
    if (!jp.parse_ds(out)) return Value(-1.0);
    return out;
}

static void json_encode_value(const Value& v, std::string& out);

static void json_encode_string(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
}

static void json_encode_value(const Value& v, std::string& out) {
    if (v.type == Value::STR) {
        json_encode_string(v.str, out);
    } else {
        char buf[40];
        double d = (double)v;
        if (d == (long long)d)
            std::snprintf(buf, sizeof(buf), "%lld", (long long)d);
        else
            std::snprintf(buf, sizeof(buf), "%g", d);
        out += buf;
    }
}

GMLFN(json_encode) {
    (void)self;
    DsMap* m = argc > 0 ? map_of(args[0]) : nullptr;
    if (!m) return Value("{}");
    std::string out = "{ ";
    bool first = true;
    for (auto& kv : m->data) {
        if (!first) out += ", ";
        first = false;
        json_encode_string(kv.first.is_str ? kv.first.str : (std::string)Value(kv.first.num), out);
        out += ": ";
        json_encode_value(kv.second, out);
    }
    out += " }";
    return Value(out);
}

static bool json_parse_struct(JsonParser& jp, Instance* self, Value& out);

GMLFN(json_parse) {
    (void)self;
    std::string src = S(args, argc, 0);
    JsonParser jp{src.c_str(), src.c_str() + src.size()};
    Value out;
    if (!json_parse_struct(jp, self, out)) return Value();
    return out;
}

static bool json_parse_struct(JsonParser& jp, Instance* self, Value& out) {
    jp.skip_ws();
    if (jp.p >= jp.end) return false;
    if (*jp.p == '{') {
        ++jp.p;
        Value obj = kwik_new_object(self, nullptr, 0);
        jp.skip_ws();
        if (jp.p < jp.end && *jp.p == '}') { ++jp.p; out = obj; return true; }
        while (jp.p < jp.end) {
            jp.skip_ws();
            std::string key;
            if (!jp.parse_string(key)) return false;
            jp.skip_ws();
            if (jp.p >= jp.end || *jp.p != ':') return false;
            ++jp.p;
            Value v;
            if (!json_parse_struct(jp, self, v)) return false;
            if (obj.obj) obj.obj->var(key) = v;
            jp.skip_ws();
            if (jp.p < jp.end && *jp.p == ',') { ++jp.p; continue; }
            break;
        }
        jp.skip_ws();
        if (jp.p < jp.end && *jp.p == '}') ++jp.p;
        out = obj;
        return true;
    }
    if (*jp.p == '[') {
        ++jp.p;
        Value arr = kwik_new_array(nullptr, 0);
        jp.skip_ws();
        if (jp.p < jp.end && *jp.p == ']') { ++jp.p; out = arr; return true; }
        while (jp.p < jp.end) {
            Value v;
            if (!json_parse_struct(jp, self, v)) return false;
            arr.arr->items.push_back(v);
            jp.skip_ws();
            if (jp.p < jp.end && *jp.p == ',') { ++jp.p; continue; }
            break;
        }
        jp.skip_ws();
        if (jp.p < jp.end && *jp.p == ']') ++jp.p;
        out = arr;
        return true;
    }
    return jp.parse_ds(out);
}

GMLFN(json_stringify) { (void)args; (void)argc; return kwik_missing(self, "json_stringify"); }

struct IniSection {
    std::map<std::string, std::string> entries;
};
static std::map<std::string, IniSection> g_ini;
static std::string g_ini_file;
static bool g_ini_open = false;
static bool g_ini_dirty = false;
static bool g_ini_from_string = false;

static void ini_parse(const std::string& text) {
    g_ini.clear();
    std::string section;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t st = line.find_first_not_of(" \t");
        if (st == std::string::npos) continue;
        line = line.substr(st);
        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            section = e == std::string::npos ? line.substr(1) : line.substr(1, e - 1);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        if (!v.empty() && v.front() == '"' && v.back() == '"' && v.size() >= 2)
            v = v.substr(1, v.size() - 2);
        g_ini[section].entries[k] = v;
    }
}

GMLFN(ini_open) {
    (void)self;
    g_ini_file = kwik_save_path(S(args, argc, 0));
    g_ini.clear();
    g_ini_open = true;
    g_ini_dirty = false;
    g_ini_from_string = false;
    std::FILE* f = std::fopen(kwik_resolve_read(S(args, argc, 0)).c_str(), "rb");
    if (f) {
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        std::fclose(f);
        ini_parse(text);
    }
    return Value();
}

GMLFN(ini_open_from_string) {
    (void)self;
    g_ini_file.clear();
    g_ini.clear();
    g_ini_open = true;
    g_ini_dirty = false;
    g_ini_from_string = true;
    ini_parse(S(args, argc, 0));
    return Value();
}

GMLFN(ini_close) {
    (void)self; (void)args; (void)argc;
    std::string out;
    for (auto& sec : g_ini) {
        out += "[" + sec.first + "]\n";
        for (auto& kv : sec.second.entries) out += kv.first + "=\"" + kv.second + "\"\n";
    }
    if (g_ini_open && g_ini_dirty && !g_ini_from_string && !g_ini_file.empty()) {
        std::FILE* f = std::fopen(g_ini_file.c_str(), "wb");
        if (f) {
            std::fwrite(out.data(), 1, out.size(), f);
            std::fclose(f);
        }
    }
    g_ini_open = false;
    g_ini.clear();
    return Value(out);
}

GMLFN(ini_read_real) {
    (void)self;
    auto s = g_ini.find(S(args, argc, 0));
    if (s != g_ini.end()) {
        auto k = s->second.entries.find(S(args, argc, 1));
        if (k != s->second.entries.end()) return Value(std::atof(k->second.c_str()));
    }
    return Value(A(args, argc, 2));
}

GMLFN(ini_read_string) {
    (void)self;
    auto s = g_ini.find(S(args, argc, 0));
    if (s != g_ini.end()) {
        auto k = s->second.entries.find(S(args, argc, 1));
        if (k != s->second.entries.end()) return Value(k->second);
    }
    return Value(S(args, argc, 2));
}

GMLFN(ini_write_real) {
    (void)self;
    char buf[40];
    double d = A(args, argc, 2);
    if (d == (long long)d)
        std::snprintf(buf, sizeof(buf), "%lld", (long long)d);
    else
        std::snprintf(buf, sizeof(buf), "%g", d);
    g_ini[S(args, argc, 0)].entries[S(args, argc, 1)] = buf;
    g_ini_dirty = true;
    return Value();
}

GMLFN(ini_write_string) {
    (void)self;
    g_ini[S(args, argc, 0)].entries[S(args, argc, 1)] = S(args, argc, 2);
    g_ini_dirty = true;
    return Value();
}

GMLFN(ini_section_exists) {
    (void)self;
    return Value(g_ini.find(S(args, argc, 0)) != g_ini.end());
}
GMLFN(ini_key_exists) {
    (void)self;
    auto s = g_ini.find(S(args, argc, 0));
    if (s == g_ini.end()) return Value(0.0);
    return Value(s->second.entries.find(S(args, argc, 1)) != s->second.entries.end());
}
GMLFN(ini_section_delete) {
    (void)self;
    g_ini.erase(S(args, argc, 0));
    g_ini_dirty = true;
    return Value();
}
GMLFN(ini_key_delete) {
    (void)self;
    auto s = g_ini.find(S(args, argc, 0));
    if (s != g_ini.end()) {
        s->second.entries.erase(S(args, argc, 1));
        g_ini_dirty = true;
    }
    return Value();
}

GMLFN(file_exists) {
    (void)self;
    std::FILE* f = std::fopen(kwik_resolve_read(S(args, argc, 0)).c_str(), "rb");
    if (f) { std::fclose(f); return Value(1.0); }
    return Value(0.0);
}

GMLFN(directory_exists) {
    (void)self;
    std::error_code ec;
    return Value(std::filesystem::is_directory(kwik_resolve_read(S(args, argc, 0)), ec));
}

GMLFN(directory_create) {
    (void)self;
    std::error_code ec;
    std::filesystem::create_directories(kwik_save_path(S(args, argc, 0)), ec);
    return Value();
}

GMLFN(file_delete) {
    (void)self;
    return Value(std::remove(kwik_resolve_read(S(args, argc, 0)).c_str()) == 0);
}

GMLFN(file_copy) {
    (void)self;
    std::FILE* in = std::fopen(kwik_resolve_read(S(args, argc, 0)).c_str(), "rb");
    if (!in) return Value(0.0);
    std::FILE* out = std::fopen(kwik_save_path(S(args, argc, 1)).c_str(), "wb");
    if (!out) { std::fclose(in); return Value(0.0); }
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) std::fwrite(buf, 1, n, out);
    std::fclose(in);
    std::fclose(out);
    return Value(1.0);
}

struct TextFile {
    std::FILE* f = nullptr;
    bool write = false;
    bool alive = false;
};
static std::vector<TextFile> g_text_files;

GMLFN(file_text_open_read) {
    (void)self;
    std::FILE* f = std::fopen(kwik_resolve_read(S(args, argc, 0)).c_str(), "rb");
    if (!f) return Value(-1.0);
    g_text_files.push_back({f, false, true});
    return Value((double)(g_text_files.size() - 1));
}
GMLFN(file_text_open_write) {
    (void)self;
    std::FILE* f = std::fopen(kwik_save_path(S(args, argc, 0)).c_str(), "wb");
    if (!f) return Value(-1.0);
    g_text_files.push_back({f, true, true});
    return Value((double)(g_text_files.size() - 1));
}
static TextFile* tf_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_text_files.size() || !g_text_files[i].alive) return nullptr;
    return &g_text_files[i];
}
GMLFN(file_text_close) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (tf) {
        std::fclose(tf->f);
        tf->f = nullptr;
        tf->alive = false;
    }
    return Value();
}
GMLFN(file_text_eof) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (!tf) return Value(1.0);
    int c = std::fgetc(tf->f);
    if (c == EOF) return Value(1.0);
    std::ungetc(c, tf->f);
    return Value(0.0);
}
static std::string tf_read_line_peek(TextFile* tf) {
    std::string line;
    long pos = std::ftell(tf->f);
    int c;
    while ((c = std::fgetc(tf->f)) != EOF && c != '\n') {
        if (c != '\r') line.push_back((char)c);
    }
    std::fseek(tf->f, pos, SEEK_SET);
    return line;
}
GMLFN(file_text_read_string) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (!tf) return Value("");
    std::string line = tf_read_line_peek(tf);
    for (size_t i = 0; i < line.size(); ++i) std::fgetc(tf->f);
    return Value(line);
}
GMLFN(file_text_read_real) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (!tf) return Value(0.0);
    double d = 0;
    if (std::fscanf(tf->f, "%lf", &d) != 1) d = 0;
    return Value(d);
}
GMLFN(file_text_readln) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (!tf) return Value("");
    std::string line;
    int c;
    while ((c = std::fgetc(tf->f)) != EOF && c != '\n')
        if (c != '\r') line.push_back((char)c);
    return Value(line);
}
GMLFN(file_text_write_string) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (tf && argc > 1) {
        std::string s = (std::string)args[1];
        std::fwrite(s.data(), 1, s.size(), tf->f);
    }
    return Value();
}
GMLFN(file_text_write_real) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (tf && argc > 1) {
        double d = (double)args[1];
        if (d == (long long)d)
            std::fprintf(tf->f, "%lld", (long long)d);
        else
            std::fprintf(tf->f, "%g", d);
    }
    return Value();
}
GMLFN(file_text_writeln) {
    (void)self;
    TextFile* tf = argc > 0 ? tf_of(args[0]) : nullptr;
    if (tf) std::fputc('\n', tf->f);
    return Value();
}

struct Buffer {
    std::vector<unsigned char> data;
    size_t pos = 0;
    bool alive = false;
};
static std::vector<Buffer> g_buffers;

static Buffer* buf_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_buffers.size() || !g_buffers[i].alive) return nullptr;
    return &g_buffers[i];
}

GMLFN(buffer_create) {
    (void)self;
    Buffer b;
    b.alive = true;
    b.data.resize((size_t)A(args, argc, 0, 0));
    g_buffers.push_back(std::move(b));
    return Value((double)(g_buffers.size() - 1));
}
GMLFN(buffer_delete) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    if (b) { b->alive = false; b->data.clear(); }
    return Value();
}
GMLFN(buffer_seek) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    if (!b) return Value();
    int base = (int)A(args, argc, 1);
    long long off = (long long)A(args, argc, 2);
    long long p;
    if (base == 1) p = (long long)b->pos + off;
    else if (base == 2) p = (long long)b->data.size() + off;
    else p = off;
    if (p < 0) p = 0;
    b->pos = (size_t)p;
    return Value();
}
GMLFN(buffer_get_size) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    return Value(b ? (double)b->data.size() : 0.0);
}
GMLFN(buffer_load) {
    (void)self;
    std::FILE* f = std::fopen(kwik_resolve_read(S(args, argc, 0)).c_str(), "rb");
    if (!f) return Value(-1.0);
    Buffer b;
    b.alive = true;
    char tmp[8192];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
        b.data.insert(b.data.end(), tmp, tmp + n);
    std::fclose(f);
    g_buffers.push_back(std::move(b));
    return Value((double)(g_buffers.size() - 1));
}
GMLFN(buffer_read) {
    (void)self;
    Buffer* b = argc > 1 ? buf_of(args[0]) : nullptr;
    if (!b) return Value();
    int type = (int)(double)args[1];
    auto rd = [&](int n) {
        long long v = 0;
        for (int i = 0; i < n && b->pos < b->data.size(); ++i) v |= (long long)b->data[b->pos++] << (8 * i);
        return v;
    };
    switch (type) {
        case 1: return Value((double)(unsigned char)rd(1));
        case 2: return Value((double)(signed char)rd(1));
        case 3: return Value((double)(unsigned short)rd(2));
        case 4: return Value((double)(short)rd(2));
        case 5: return Value((double)(unsigned int)rd(4));
        case 6: return Value((double)(int)rd(4));
        case 7: rd(2); return Value(0.0);
        case 8: {
            union { int i; float f; } u;
            u.i = (int)rd(4);
            return Value((double)u.f);
        }
        case 9: {
            union { long long i; double d; } u;
            u.i = rd(8);
            return Value(u.d);
        }
        case 10: return Value((double)(rd(1) != 0));
        case 11: {
            std::string s;
            while (b->pos < b->data.size() && b->data[b->pos] != 0) s.push_back((char)b->data[b->pos++]);
            if (b->pos < b->data.size()) b->pos++;
            return Value(s);
        }
        case 12: return Value((double)rd(8));
        case 13: {
            std::string s;
            while (b->pos < b->data.size()) s.push_back((char)b->data[b->pos++]);
            return Value(s);
        }
        default: return Value((double)(unsigned char)rd(1));
    }
}
GMLFN(buffer_write) {
    (void)self;
    Buffer* b = argc > 2 ? buf_of(args[0]) : nullptr;
    if (!b) return Value();
    int type = (int)(double)args[1];
    auto wr = [&](long long v, int n) {
        for (int i = 0; i < n; ++i) {
            if (b->pos >= b->data.size()) b->data.resize(b->pos + 1);
            b->data[b->pos++] = (unsigned char)(v >> (8 * i));
        }
    };
    switch (type) {
        case 1: case 2: case 10: wr((long long)(double)args[2], 1); break;
        case 3: case 4: wr((long long)(double)args[2], 2); break;
        case 5: case 6: wr((long long)(double)args[2], 4); break;
        case 12: wr((long long)(double)args[2], 8); break;
        case 8: {
            union { int i; float f; } u;
            u.f = (float)(double)args[2];
            wr(u.i, 4);
            break;
        }
        case 9: {
            union { long long i; double d; } u;
            u.d = (double)args[2];
            wr(u.i, 8);
            break;
        }
        case 11: {
            std::string s = (std::string)args[2];
            for (char c : s) wr((unsigned char)c, 1);
            wr(0, 1);
            break;
        }
        case 13: {
            std::string s = (std::string)args[2];
            for (char c : s) wr((unsigned char)c, 1);
            break;
        }
        default: wr((long long)(double)args[2], 1); break;
    }
    return Value();
}
static int g_next_async_id = 5000;
static bool g_async_group_active = false;
static bool g_async_group_ok = true;

static void queue_save_load_event(int id, bool ok) {
    g_maps.emplace_back();
    int map = (int)g_maps.size() - 1;
    g_maps[map].data[key_of(Value("id"))] = Value((double)id);
    g_maps[map].data[key_of(Value("status"))] = Value(ok ? 1.0 : 0.0);
    kwik_queue_async(ASYNC_SAVE_LOAD_EV, map);
}

GMLFN(buffer_save_async) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    bool ok = false;
    if (b && argc > 1) {
        size_t off = argc > 2 ? (size_t)(double)args[2] : 0;
        size_t sz = argc > 3 && (double)args[3] >= 0 ? (size_t)(double)args[3]
                                                     : b->data.size();
        if (off > b->data.size()) off = b->data.size();
        if (off + sz > b->data.size()) sz = b->data.size() - off;
        std::FILE* f = std::fopen(kwik_save_path(S(args, argc, 1)).c_str(), "wb");
        if (f) {
            std::fwrite(b->data.data() + off, 1, sz, f);
            std::fclose(f);
            ok = true;
        }
    }
    int id = g_next_async_id++;
    if (g_async_group_active)
        g_async_group_ok = g_async_group_ok && ok;
    else
        queue_save_load_event(id, ok);
    return Value((double)id);
}

GMLFN(buffer_load_async) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    bool ok = false;
    if (b && argc > 1) {
        std::FILE* f = std::fopen(kwik_resolve_read(S(args, argc, 1)).c_str(), "rb");
        if (f) {
            size_t off = argc > 2 ? (size_t)(double)args[2] : 0;
            std::vector<unsigned char> bytes;
            char tmp[8192];
            size_t n;
            while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
                bytes.insert(bytes.end(), tmp, tmp + n);
            std::fclose(f);
            if (b->data.size() < off + bytes.size()) b->data.resize(off + bytes.size());
            std::memcpy(b->data.data() + off, bytes.data(), bytes.size());
            b->pos = 0;
            ok = true;
        }
    }
    int id = g_next_async_id++;
    if (g_async_group_active)
        g_async_group_ok = g_async_group_ok && ok;
    else
        queue_save_load_event(id, ok);
    return Value((double)id);
}

GMLFN(buffer_async_group_begin) {
    (void)self; (void)args; (void)argc;
    g_async_group_active = true;
    g_async_group_ok = true;
    return Value();
}
GMLFN(buffer_async_group_end) {
    (void)self; (void)args; (void)argc;
    int id = g_next_async_id++;
    if (g_async_group_active) {
        queue_save_load_event(id, g_async_group_ok);
        g_async_group_active = false;
    }
    return Value((double)id);
}
GMLFN(buffer_async_group_option) { (void)self; (void)args; (void)argc; return Value(); }

static void md5_compute(const unsigned char* data, size_t len, unsigned char out[16]) {
    static const unsigned int K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    unsigned int h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;
    size_t padded = ((len + 8) / 64 + 1) * 64;
    std::vector<unsigned char> msg(padded, 0);
    std::memcpy(msg.data(), data, len);
    msg[len] = 0x80;
    unsigned long long bits = (unsigned long long)len * 8;
    for (int i = 0; i < 8; ++i) msg[padded - 8 + i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < padded; off += 64) {
        unsigned int w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = (unsigned)msg[off + i * 4] | ((unsigned)msg[off + i * 4 + 1] << 8) |
                   ((unsigned)msg[off + i * 4 + 2] << 16) | ((unsigned)msg[off + i * 4 + 3] << 24);
        unsigned int a = h0, b = h1, c = h2, d = h3;
        for (int i = 0; i < 64; ++i) {
            unsigned int f, g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            unsigned int tmp = d;
            d = c;
            c = b;
            unsigned int x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d;
    }
    unsigned int hs[4] = {h0, h1, h2, h3};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = (unsigned char)(hs[i] >> (8 * j));
}

GMLFN(buffer_md5) {
    (void)self;
    Buffer* b = argc > 0 ? buf_of(args[0]) : nullptr;
    if (!b) return Value("");
    size_t off = argc > 1 ? (size_t)(double)args[1] : 0;
    size_t sz = argc > 2 && (double)args[2] >= 0 ? (size_t)(double)args[2] : b->data.size();
    if (off > b->data.size()) off = b->data.size();
    if (off + sz > b->data.size()) sz = b->data.size() - off;
    unsigned char digest[16];
    md5_compute(b->data.data() + off, sz, digest);
    char hex[33];
    for (int i = 0; i < 16; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return Value(std::string(hex, 32));
}


bool kwik_ds_list_push(int list, const Value& v) {
    if (list < 0 || (size_t)list >= g_lists.size() || !g_lists[list].alive) return false;
    g_lists[list].data.push_back(v);
    return true;
}

GMLFN(ds_list_insert) {
    (void)self;
    DsList* l = argc > 2 ? list_of(args[0]) : nullptr;
    if (l) {
        int i = (int)(double)args[1];
        if (i < 0) i = 0;
        if ((size_t)i > l->data.size()) i = (int)l->data.size();
        l->data.insert(l->data.begin() + i, args[2]);
    }
    return Value();
}
GMLFN(ds_list_replace) {
    (void)self;
    DsList* l = argc > 2 ? list_of(args[0]) : nullptr;
    if (l) {
        int i = (int)(double)args[1];
        if (i >= 0 && (size_t)i < l->data.size()) l->data[i] = args[2];
    }
    return Value();
}
GMLFN(ds_list_set) {
    (void)self;
    DsList* l = argc > 2 ? list_of(args[0]) : nullptr;
    if (l) {
        int i = (int)(double)args[1];
        if (i >= 0) {
            if ((size_t)i >= l->data.size()) l->data.resize(i + 1);
            l->data[i] = args[2];
        }
    }
    return Value();
}
GMLFN(ds_list_sort) {
    (void)self;
    DsList* l = argc > 0 ? list_of(args[0]) : nullptr;
    if (!l) return Value();
    bool asc = argc < 2 || gml_truthy(args[1]);
    std::stable_sort(l->data.begin(), l->data.end(), [asc](const Value& a, const Value& b) {
        bool lt;
        if (a.type == Value::STR || b.type == Value::STR)
            lt = (std::string)a < (std::string)b;
        else
            lt = (double)a < (double)b;
        return asc ? lt : !lt;
    });
    return Value();
}

struct DsGrid {
    int w = 0, h = 0;
    std::vector<Value> data;
    bool alive = false;
};
static std::vector<DsGrid> g_grids;
static DsGrid* grid_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_grids.size() || !g_grids[i].alive) return nullptr;
    return &g_grids[i];
}

GMLFN(ds_grid_create) {
    (void)self;
    DsGrid g;
    g.alive = true;
    g.w = (int)A(args, argc, 0);
    g.h = (int)A(args, argc, 1);
    if (g.w < 0) g.w = 0;
    if (g.h < 0) g.h = 0;
    g.data.assign((size_t)g.w * g.h, Value(0.0));
    g_grids.push_back(std::move(g));
    return Value((double)(g_grids.size() - 1));
}
GMLFN(ds_grid_destroy) {
    (void)self;
    int i = argc > 0 ? (int)(double)args[0] : -1;
    if (i >= 0 && (size_t)i < g_grids.size()) {
        g_grids[i].alive = false;
        g_grids[i].data.clear();
    }
    return Value();
}
GMLFN(ds_grid_width) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    return Value(g ? (double)g->w : 0.0);
}
GMLFN(ds_grid_height) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    return Value(g ? (double)g->h : 0.0);
}
GMLFN(ds_grid_clear) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (g) {
        Value v = argc > 1 ? args[1] : Value(0.0);
        for (auto& c : g->data) c = v;
    }
    return Value();
}
GMLFN(ds_grid_get) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (!g) return Value();
    int x = (int)A(args, argc, 1), y = (int)A(args, argc, 2);
    if (x < 0 || x >= g->w || y < 0 || y >= g->h) return Value(0.0);
    return g->data[(size_t)y * g->w + x];
}
GMLFN(ds_grid_set) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (!g) return Value();
    int x = (int)A(args, argc, 1), y = (int)A(args, argc, 2);
    if (x < 0 || x >= g->w || y < 0 || y >= g->h) return Value();
    g->data[(size_t)y * g->w + x] = argc > 3 ? args[3] : Value(0.0);
    return Value();
}
GMLFN(ds_grid_add) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (!g) return Value();
    int x = (int)A(args, argc, 1), y = (int)A(args, argc, 2);
    if (x < 0 || x >= g->w || y < 0 || y >= g->h) return Value();
    Value& cell = g->data[(size_t)y * g->w + x];
    cell = gml_add(cell, argc > 3 ? args[3] : Value(0.0));
    return Value();
}
GMLFN(ds_grid_value_x) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (!g) return Value(-1.0);
    int x1 = (int)A(args, argc, 1), y1 = (int)A(args, argc, 2);
    int x2 = (int)A(args, argc, 3), y2 = (int)A(args, argc, 4);
    Value val = argc > 5 ? args[5] : Value(0.0);
    x1 = std::max(x1, 0); y1 = std::max(y1, 0);
    x2 = std::min(x2, g->w - 1); y2 = std::min(y2, g->h - 1);
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
            if ((double)g->data[(size_t)y * g->w + x] == (double)val) return Value((double)x);
    return Value(-1.0);
}
GMLFN(ds_grid_value_y) {
    (void)self;
    DsGrid* g = argc > 0 ? grid_of(args[0]) : nullptr;
    if (!g) return Value(-1.0);
    int x1 = (int)A(args, argc, 1), y1 = (int)A(args, argc, 2);
    int x2 = (int)A(args, argc, 3), y2 = (int)A(args, argc, 4);
    Value val = argc > 5 ? args[5] : Value(0.0);
    x1 = std::max(x1, 0); y1 = std::max(y1, 0);
    x2 = std::min(x2, g->w - 1); y2 = std::min(y2, g->h - 1);
    for (int y = y1; y <= y2; ++y)
        for (int x = x1; x <= x2; ++x)
            if ((double)g->data[(size_t)y * g->w + x] == (double)val) return Value((double)y);
    return Value(-1.0);
}

static std::vector<DsList> g_stacks;
static DsList* stack_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_stacks.size() || !g_stacks[i].alive) return nullptr;
    return &g_stacks[i];
}
GMLFN(ds_stack_create) {
    (void)self; (void)args; (void)argc;
    g_stacks.emplace_back();
    g_stacks.back().alive = true;
    return Value((double)(g_stacks.size() - 1));
}
GMLFN(ds_stack_destroy) {
    (void)self;
    int i = argc > 0 ? (int)(double)args[0] : -1;
    if (i >= 0 && (size_t)i < g_stacks.size()) {
        g_stacks[i].alive = false;
        g_stacks[i].data.clear();
    }
    return Value();
}
GMLFN(ds_stack_clear) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    if (s) s->data.clear();
    return Value();
}
GMLFN(ds_stack_copy) {
    (void)self;
    DsList* dst = argc > 0 ? stack_of(args[0]) : nullptr;
    DsList* src = argc > 1 ? stack_of(args[1]) : nullptr;
    if (dst && src) dst->data = src->data;
    return Value();
}
GMLFN(ds_stack_size) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    return Value(s ? (double)s->data.size() : 0.0);
}
GMLFN(ds_stack_empty) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    return Value(!s || s->data.empty());
}
GMLFN(ds_stack_push) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    if (s) for (int i = 1; i < argc; ++i) s->data.push_back(args[i]);
    return Value();
}
GMLFN(ds_stack_pop) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    if (!s || s->data.empty()) return Value();
    Value v = s->data.back();
    s->data.pop_back();
    return v;
}
GMLFN(ds_stack_top) {
    (void)self;
    DsList* s = argc > 0 ? stack_of(args[0]) : nullptr;
    if (!s || s->data.empty()) return Value();
    return s->data.back();
}

static std::vector<DsList> g_queues;
static DsList* queue_of(const Value& v) {
    int i = (int)(double)v;
    if (i < 0 || (size_t)i >= g_queues.size() || !g_queues[i].alive) return nullptr;
    return &g_queues[i];
}
GMLFN(ds_queue_create) {
    (void)self; (void)args; (void)argc;
    g_queues.emplace_back();
    g_queues.back().alive = true;
    return Value((double)(g_queues.size() - 1));
}
GMLFN(ds_queue_destroy) {
    (void)self;
    int i = argc > 0 ? (int)(double)args[0] : -1;
    if (i >= 0 && (size_t)i < g_queues.size()) {
        g_queues[i].alive = false;
        g_queues[i].data.clear();
    }
    return Value();
}
GMLFN(ds_queue_clear) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    if (q) q->data.clear();
    return Value();
}
GMLFN(ds_queue_copy) {
    (void)self;
    DsList* dst = argc > 0 ? queue_of(args[0]) : nullptr;
    DsList* src = argc > 1 ? queue_of(args[1]) : nullptr;
    if (dst && src) dst->data = src->data;
    return Value();
}
GMLFN(ds_queue_size) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    return Value(q ? (double)q->data.size() : 0.0);
}
GMLFN(ds_queue_empty) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    return Value(!q || q->data.empty());
}
GMLFN(ds_queue_enqueue) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    if (q) for (int i = 1; i < argc; ++i) q->data.push_back(args[i]);
    return Value();
}
GMLFN(ds_queue_dequeue) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    if (!q || q->data.empty()) return Value();
    Value v = q->data.front();
    q->data.erase(q->data.begin());
    return v;
}
GMLFN(ds_queue_head) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    if (!q || q->data.empty()) return Value();
    return q->data.front();
}
GMLFN(ds_queue_tail) {
    (void)self;
    DsList* q = argc > 0 ? queue_of(args[0]) : nullptr;
    if (!q || q->data.empty()) return Value();
    return q->data.back();
}

GMLFN(buffer_exists) {
    (void)self;
    return Value(argc > 0 && buf_of(args[0]) != nullptr);
}
GMLFN(buffer_get_surface) {
    (void)self;
    Buffer* b = argc > 1 ? buf_of(args[0]) : nullptr;
    if (!b) return Value();
    int surf = (int)A(args, argc, 1);
    int w = render_surface_width(surf), h = render_surface_height(surf);
    if (w <= 0 || h <= 0) return Value();
    size_t off = (size_t)A(args, argc, 2);
    size_t need = (size_t)w * h * 4;
    if (b->data.size() < off + need) b->data.resize(off + need);
    render_surface_snapshot(surf, 0, 0, w, h, b->data.data() + off);
    return Value();
}
GMLFN(buffer_set_surface) {
    (void)self;
    Buffer* b = argc > 1 ? buf_of(args[0]) : nullptr;
    if (!b) return Value();
    int surf = (int)A(args, argc, 1);
    int w = render_surface_width(surf), h = render_surface_height(surf);
    if (w <= 0 || h <= 0) return Value();
    size_t off = (size_t)A(args, argc, 2);
    size_t need = (size_t)w * h * 4;
    if (b->data.size() < off + need) return Value();
    unsigned int tex = render_upload_texture(b->data.data() + off, w, h);
    if (!tex) return Value();
    if (render_surface_set_target(surf)) {
        render_surface_clear(0, 0.0);
        render_draw_quad(tex, 0, 0, w, h, 0, 0, 1, 1, 0, 0.f, 0.f, 1.f, 1.f, 0xFFFFFF, 1.0);
        render_surface_reset_target();
    }
    return Value();
}

}
