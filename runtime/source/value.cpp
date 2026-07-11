#include "gml_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace gml {

Value::operator double() const {
    switch (type) {
        case REAL: return num;
        case STR: return std::atof(str.c_str());
        case OBJ: return obj ? (double)obj->id : -4.0;
        case FN: return -1.0;
        default: return 0.0;
    }
}

Value::operator std::string() const {
    switch (type) {
        case STR: return str;
        case REAL: {
            if (std::isfinite(num) && std::floor(num) == num && std::fabs(num) < 1e15)
                return std::to_string((long long)num);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f", num);
            return buf;
        }
        case UNDEF: return "undefined";
        case ARR: {
            std::string out = "[ ";
            if (arr) {
                for (size_t i = 0; i < arr->items.size(); ++i) {
                    if (i) out += ",";
                    out += (std::string)arr->items[i];
                }
            }
            out += " ]";
            return out;
        }
        case OBJ: return "struct";
        case FN: return std::string("function ") + fn_name;
    }
    return "";
}

Value gml_add(const Value& a, const Value& b) {
    if (a.type == Value::STR || b.type == Value::STR) {
        if (a.type == Value::STR && b.type == Value::STR) return Value(a.str + b.str);
        if (a.type == Value::STR) return Value(a.str + (std::string)b);
        return Value((std::string)a + b.str);
    }
    return Value((double)a + (double)b);
}
Value gml_sub(const Value& a, const Value& b) { return Value((double)a - (double)b); }
Value gml_mul(const Value& a, const Value& b) {
    if (b.type == Value::STR && a.type == Value::REAL) {
        std::string out;
        long long n = (long long)a.num;
        for (long long i = 0; i < n; ++i) out += b.str;
        return Value(out);
    }
    return Value((double)a * (double)b);
}
Value gml_div(const Value& a, const Value& b) {
    double d = (double)b;
    return Value(d == 0.0 ? 0.0 : (double)a / d);
}
Value gml_intdiv(const Value& a, const Value& b) {
    long long d = (long long)(double)b;
    if (d == 0) return Value(0.0);
    return Value((double)((long long)(double)a / d));
}
Value gml_mod(const Value& a, const Value& b) {
    double d = (double)b;
    return Value(d == 0.0 ? 0.0 : std::fmod((double)a, d));
}
static long long to_i64(const Value& v) { return (long long)(double)v; }
Value gml_band(const Value& a, const Value& b) { return Value((double)(to_i64(a) & to_i64(b))); }
Value gml_bor(const Value& a, const Value& b) { return Value((double)(to_i64(a) | to_i64(b))); }
Value gml_bxor(const Value& a, const Value& b) { return Value((double)(to_i64(a) ^ to_i64(b))); }
Value gml_shl(const Value& a, const Value& b) { return Value((double)(to_i64(a) << to_i64(b))); }
Value gml_shr(const Value& a, const Value& b) { return Value((double)(to_i64(a) >> to_i64(b))); }
Value gml_neg(const Value& a) { return Value(-(double)a); }
Value gml_not(const Value& a) { return Value(gml_truthy(a) ? 0.0 : 1.0); }
Value gml_bnot(const Value& a) { return Value((double)(~to_i64(a))); }

static const double kGmlEpsilon = 0.00001;

static int num_cmp(const Value& a, const Value& b) {
    double diff = (double)a - (double)b;
    if (std::fabs(diff) <= kGmlEpsilon) return 0;
    return diff < 0 ? -1 : 1;
}

Value gml_lt(const Value& a, const Value& b) {
    if (a.type == Value::STR && b.type == Value::STR) return Value(a.str < b.str);
    return Value(num_cmp(a, b) < 0);
}
Value gml_le(const Value& a, const Value& b) {
    if (a.type == Value::STR && b.type == Value::STR) return Value(a.str <= b.str);
    return Value(num_cmp(a, b) <= 0);
}
Value gml_eq(const Value& a, const Value& b) {
    if (a.type == Value::UNDEF || b.type == Value::UNDEF)
        return Value(a.type == b.type);
    if (a.type == Value::STR || b.type == Value::STR) {
        if (a.type != b.type) return Value(false);
        return Value(a.str == b.str);
    }
    if (a.type == Value::ARR || b.type == Value::ARR)
        return Value(a.type == b.type && a.arr == b.arr);
    if (a.type == Value::OBJ && b.type == Value::OBJ) return Value(a.obj == b.obj);
    return Value(num_cmp(a, b) == 0);
}
Value gml_ne(const Value& a, const Value& b) { return Value(!gml_truthy(gml_eq(a, b))); }
Value gml_ge(const Value& a, const Value& b) {
    if (a.type == Value::STR && b.type == Value::STR) return Value(a.str >= b.str);
    return Value(num_cmp(a, b) >= 0);
}
Value gml_gt(const Value& a, const Value& b) {
    if (a.type == Value::STR && b.type == Value::STR) return Value(a.str > b.str);
    return Value(num_cmp(a, b) > 0);
}

bool gml_truthy(const Value& a) {
    switch (a.type) {
        case Value::REAL: return a.num >= 0.5;
        case Value::STR: return !a.str.empty();
        case Value::UNDEF: return false;
        default: return true;
    }
}

static KwikStrMap<Value> g_globals;

Value& global_var(const std::string& name) { return g_globals[name]; }

Value& global_var(const char* name) {
    auto it = g_globals.find(KWIK_STR_KEY(name));
    if (it != g_globals.end()) return it->second;
    return g_globals.emplace(name, Value()).first->second;
}

static int64_t g_array_owner = 0;

void kwik_setowner(const Value& v) { g_array_owner = (int64_t)(double)v; }

static void ensure_array_slot(Value& slot) {
    if (slot.type != Value::ARR || !slot.arr) {
        slot = Value();
        slot.type = Value::ARR;
        slot.arr = std::make_shared<GmlArray>();
        slot.arr->owner = g_array_owner;
    } else if (slot.arr->owner != g_array_owner && slot.arr.use_count() > 2) {
        auto copy = std::make_shared<GmlArray>();
        copy->items = slot.arr->items;
        copy->owner = g_array_owner;
        slot.arr = copy;
    } else {
        slot.arr->owner = g_array_owner;
    }
}

Value kwik_array_elem(const Value& slot, int idx) {
    if (slot.type != Value::ARR || !slot.arr || idx < 0 ||
        (size_t)idx >= slot.arr->items.size())
        return Value();
    return slot.arr->items[idx];
}

void kwik_array_store(Value& slot, int idx, const Value& v) {
    if (idx < 0) return;
    ensure_array_slot(slot);
    if ((size_t)idx >= slot.arr->items.size()) slot.arr->items.resize(idx + 1);
    slot.arr->items[idx] = v;
}

Value kwik_array_wslot(Value& slot, int idx) {
    if (idx < 0) return Value();
    ensure_array_slot(slot);
    if ((size_t)idx >= slot.arr->items.size()) slot.arr->items.resize(idx + 1);
    Value& cell = slot.arr->items[idx];
    if (cell.type != Value::ARR || !cell.arr) {
        cell.type = Value::ARR;
        cell.num = 0;
        cell.arr = std::make_shared<GmlArray>();
        cell.arr->owner = g_array_owner;
    }
    Value out;
    out.type = Value::ARR;
    out.arr = cell.arr;
    return out;
}

Value kwik_pushaf(const Value& arrref, const Value& idx) {
    return kwik_array_elem(arrref, (int)(double)idx);
}

void kwik_popaf(const Value& arrref, const Value& idx, const Value& v) {
    int i = (int)(double)idx;
    if (arrref.type != Value::ARR || !arrref.arr || i < 0) return;
    if ((size_t)i >= arrref.arr->items.size()) arrref.arr->items.resize(i + 1);
    arrref.arr->items[i] = v;
}

Value kwik_pushac(const Value& arrref, const Value& idx) {
    int i = (int)(double)idx;
    if (arrref.type != Value::ARR || !arrref.arr || i < 0) return Value();
    if ((size_t)i >= arrref.arr->items.size()) arrref.arr->items.resize(i + 1);
    Value& cell = arrref.arr->items[i];
    if (cell.type != Value::ARR || !cell.arr) {
        cell.type = Value::ARR;
        cell.num = 0;
        cell.arr = std::make_shared<GmlArray>();
        cell.arr->owner = g_array_owner;
    }
    Value out;
    out.type = Value::ARR;
    out.arr = cell.arr;
    return out;
}

Value kwik_new_array(const Value* args, int argc) {
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    v.arr->owner = g_array_owner;
    for (int i = 0; i < argc; ++i) v.arr->items.push_back(args[i]);
    return v;
}

static std::unordered_map<void*, std::shared_ptr<Instance>> g_statics_registry;

std::shared_ptr<Instance> kwik_make_statics(ScriptFn fn) {
    auto& slot = g_statics_registry[(void*)fn];
    if (!slot) slot = std::make_shared<Instance>();
    return slot;
}

void kwik_copy_static_from(Instance* st, const Value& parent) {
    if (!st || parent.type != Value::FN || !parent.fn) return;
    auto it = g_statics_registry.find((void*)parent.fn);
    if (it == g_statics_registry.end() || !it->second) return;
    for (const auto& kv : it->second->vars) st->vars[kv.first] = kv.second;
}

Value kwik_make_fnref(ScriptFn fn, const char* name) {
    Value v;
    v.type = Value::FN;
    v.fn = fn;
    v.fn_name = name;
    return v;
}

Value kwik_arg_get(const Value* args, int argc, const Value& idx) {
    int i = (int)(double)idx;
    if (i < 0 || i >= argc) return Value();
    return args[i];
}

Value kwik_missing(Instance* self, const char* name) {
    (void)self;
    static std::set<std::string> warned;
    if (warned.insert(name).second)
        std::fprintf(stderr, "[kwik] missing builtin: %s\n", name);
    return Value();
}

}
