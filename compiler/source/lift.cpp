#include "lift.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "assets.h"
#include "disasm.h"

namespace kwik {

static std::string sanitize(const std::string& s) {
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            out.push_back(c);
        else
            out.push_back('_');
    }
    if (out.empty() || (out[0] >= '0' && out[0] <= '9')) out = "_" + out;
    return out;
}

static std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\n') { out += "\\n"; continue; }
        if (c == '\t') { out += "\\t"; continue; }
        if (c == '\r') { out += "\\r"; continue; }
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

static std::string builtin_call_name(const std::string& raw) {
    if (raw == "typeof") return "typeof_fn";
    if (raw == "bool") return "bool_fn";
    if (raw == "ds_list_delete") return "ds_list_delete_fn";
    return sanitize(raw);
}

static int width_of_type(uint8_t t) {
    switch (t) {
        case 0x0: case 0x3: return 8;
        case 0x5: case 0x6: return 16;
        default: return 4;
    }
}

struct Slot {
    uint8_t width = 16;
    bool is_const = false;
    int32_t cval = 0;
};

struct StackState {
    std::vector<Slot> s;
    bool set = false;
};

struct LiftCtx {
    const GameData& gd;
    const CodeEntry& e;
    std::vector<Instruction> instrs;
    std::unordered_map<uint32_t, size_t> idx_of;
    std::vector<StackState> before;
    std::vector<bool> reachable;
    std::set<uint32_t> labels;
    std::set<std::string> locals;
    int max_depth = 0;
    int warn_count = 0;
    bool needs_exit_label = false;
    bool uses_statics = false;

    LiftCtx(const GameData& g, const CodeEntry& entry) : gd(g), e(entry) {}
};

static std::string S(int i) { return "__s[" + std::to_string(i) + "]"; }

static bool is_argument_n(const std::string& name, int& n) {
    if (name.compare(0, 8, "argument") != 0 || name.size() <= 8 || name.size() > 10) return false;
    for (size_t i = 8; i < name.size(); ++i)
        if (name[i] < '0' || name[i] > '9') return false;
    n = std::atoi(name.c_str() + 8);
    return n >= 0 && n < 16;
}

static std::string read_var_expr(LiftCtx& ctx, int spec, const std::string& name) {
    int argn;
    if (name == "argument_count") return "Value((double)argc)";
    if (is_argument_n(name, argn)) return "__a[" + std::to_string(argn) + "]";
    if (spec == -7) {
        ctx.locals.insert(name);
        return "loc_" + sanitize(name);
    }
    if (spec == -1) {
        if (name == "x") return "Value(self->x)";
        if (name == "y") return "Value(self->y)";
    }
    if (spec == -16) {
        ctx.uses_statics = true;
        return "__statics->var(" + quote(name) + ")";
    }
    return "kwik_scope_get(self, " + std::to_string(spec) + ", " + quote(name) + ")";
}

static void emit_write_var(LiftCtx& ctx, std::ostream* out, int spec, const std::string& name,
                           const std::string& val) {
    if (spec == -16) ctx.uses_statics = true;
    if (!out) {
        int argn;
        if (!is_argument_n(name, argn) && name != "argument_count" && spec == -7)
            ctx.locals.insert(name);
        return;
    }
    int argn;
    if (is_argument_n(name, argn)) {
        *out << "    __a[" << argn << "] = " << val << ";\n";
        return;
    }
    if (spec == -7) {
        ctx.locals.insert(name);
        *out << "    loc_" << sanitize(name) << " = " << val << ";\n";
        return;
    }
    if (spec == -1 && name == "x") {
        *out << "    self->x = (double)" << val << ";\n";
        return;
    }
    if (spec == -1 && name == "y") {
        *out << "    self->y = (double)" << val << ";\n";
        return;
    }
    if (spec == -16) {
        *out << "    __statics->var(" << quote(name) << ") = " << val << ";\n";
        return;
    }
    *out << "    kwik_scope_set(self, " << spec << ", " << quote(name) << ", " << val << ");\n";
}

static std::string binop_helper(uint8_t op) {
    switch (op) {
        case 0x08: return "gml_mul";
        case 0x09: return "gml_div";
        case 0x0A: return "gml_intdiv";
        case 0x0B: return "gml_mod";
        case 0x0C: return "gml_add";
        case 0x0D: return "gml_sub";
        case 0x0E: return "gml_band";
        case 0x0F: return "gml_bor";
        case 0x10: return "gml_bxor";
        case 0x13: return "gml_shl";
        case 0x14: return "gml_shr";
        default: return "gml_add";
    }
}

static std::string cmp_helper(uint8_t kind) {
    switch (kind) {
        case 1: return "gml_lt";
        case 2: return "gml_le";
        case 3: return "gml_eq";
        case 4: return "gml_ne";
        case 5: return "gml_ge";
        case 6: return "gml_gt";
        default: return "gml_eq";
    }
}

static int bytes_to_slots(LiftCtx& ctx, const std::vector<Slot>& s, int from_top_base, int bytes) {
    int total = 0;
    int n = 0;
    int idx = (int)s.size() - 1 - from_top_base;
    while (total < bytes && idx >= 0) {
        total += s[idx].width;
        --idx;
        ++n;
    }
    if (total != bytes && ctx.warn_count++ < 8)
        std::fprintf(stderr, "[lift] %s: byte/slot mismatch (%d wanted, %d got)\n",
                     ctx.e.name.c_str(), bytes, total);
    return n;
}

static void warn(LiftCtx& ctx, uint32_t addr, const char* msg) {
    if (ctx.warn_count++ < 12)
        std::fprintf(stderr, "[lift] %s@0x%x: %s\n", ctx.e.name.c_str(), addr, msg);
}

struct Succ {
    size_t idx;
    bool is_branch;
};

static void exec_instr(LiftCtx& ctx, size_t i, StackState& st, std::ostream* out,
                       std::vector<Succ>& succ, bool& falls) {
    const Instruction& in = ctx.instrs[i];
    const GameData& gd = ctx.gd;
    falls = true;
    char lbl[32];

    auto push = [&](uint8_t w, bool c = false, int32_t v = 0) {
        st.s.push_back({w, c, v});
        if ((int)st.s.size() > ctx.max_depth) ctx.max_depth = (int)st.s.size();
    };
    auto pop = [&](int n = 1) {
        for (int k = 0; k < n && !st.s.empty(); ++k) st.s.pop_back();
    };
    auto d = [&]() { return (int)st.s.size(); };
    auto slot = [&](int from_top) -> Slot {
        int idx = (int)st.s.size() - 1 - from_top;
        if (idx < 0) return Slot{};
        return st.s[idx];
    };
    auto add_target = [&](uint32_t addr) {
        auto it = ctx.idx_of.find(addr);
        if (it != ctx.idx_of.end()) {
            succ.push_back({it->second, true});
            ctx.labels.insert(addr);
        } else {
            ctx.needs_exit_label = true;
        }
    };
    auto label_of = [&](uint32_t addr) {
        if (!ctx.idx_of.count(addr)) return std::string("L_exit");
        std::snprintf(lbl, sizeof(lbl), "L_%x", addr);
        return std::string(lbl);
    };

    switch (in.opcode) {
        case 0x84:
            if (out) *out << "    " << S(d()) << " = " << in.operand << ";\n";
            push(4, true, in.operand);
            break;

        case 0xC0: case 0xC1: case 0xC2: case 0xC3: {
            if (in.type1 == 0x5) {
                VarRef vr = gd.var_at(in.address);
                std::string name = vr.name.empty() ? "__unknown" : vr.name;
                uint8_t reftype = (in.extra >> 24) & 0xF8;
                if (reftype == 0xA0) {
                    if (in.operand == -9) {
                        if (out)
                            *out << "    " << S(d() - 1) << " = kwik_inst_get(self, " << S(d() - 1)
                                 << ", " << quote(name) << ");\n";
                        pop(1);
                        push(16);
                    } else {
                        if (out)
                            *out << "    " << S(d()) << " = "
                                 << read_var_expr(ctx, in.operand, name) << ";\n";
                        else
                            read_var_expr(ctx, in.operand, name);
                        push(16);
                    }
                } else if (reftype == 0x00 || reftype == 0x10 || reftype == 0x90) {
                    bool wref = reftype == 0x90;
                    Slot spec = slot(1);
                    if (spec.is_const && spec.cval == -9) {
                        std::string fn = wref ? "kwik_array_wref_at" : "kwik_array_get_at";
                        if (out)
                            *out << "    " << S(d() - 3) << " = " << fn << "(self, " << S(d() - 3)
                                 << ", " << quote(name) << ", " << S(d() - 1) << ");\n";
                        pop(3);
                        push(16);
                    } else if (spec.is_const && spec.cval == -7) {
                        ctx.locals.insert(name);
                        std::string loc = "loc_" + sanitize(name);
                        if (out) {
                            if (wref)
                                *out << "    " << S(d() - 2) << " = kwik_array_wslot(" << loc
                                     << ", (int)(double)" << S(d() - 1) << ");\n";
                            else
                                *out << "    " << S(d() - 2) << " = kwik_array_elem(" << loc
                                     << ", (int)(double)" << S(d() - 1) << ");\n";
                        }
                        pop(2);
                        push(16);
                    } else if (name == "argument") {
                        if (out)
                            *out << "    " << S(d() - 2) << " = kwik_arg_get(__a, __an, "
                                 << S(d() - 1) << ");\n";
                        pop(2);
                        push(16);
                    } else {
                        std::string specexpr = spec.is_const ? std::to_string(spec.cval)
                                                             : "(int)(double)" + S(d() - 2);
                        std::string fn = wref ? "kwik_array_wref" : "kwik_array_get";
                        if (out)
                            *out << "    " << S(d() - 2) << " = " << fn << "(self, " << specexpr
                                 << ", " << quote(name) << ", " << S(d() - 1) << ");\n";
                        pop(2);
                        push(16);
                    }
                } else if (reftype == 0x80) {
                    Slot spec = slot(0);
                    if (spec.is_const && spec.cval == -9) {
                        if (out)
                            *out << "    " << S(d() - 2) << " = kwik_inst_get(self, " << S(d() - 2)
                                 << ", " << quote(name) << ");\n";
                        pop(2);
                        push(16);
                    } else {
                        if (out)
                            *out << "    " << S(d() - 1) << " = kwik_inst_get(self, " << S(d() - 1)
                                 << ", " << quote(name) << ");\n";
                        pop(1);
                        push(16);
                    }
                } else if (reftype == 0xE0) {
                    if (out)
                        *out << "    " << S(d()) << " = kwik_inst_get(self, Value("
                             << (100000 + (int)in.operand) << ".0), " << quote(name) << ");\n";
                    push(16);
                } else {
                    warn(ctx, in.address, "unknown var reftype on push");
                    push(16);
                }
                break;
            }
            std::string rhs;
            bool is_c = false;
            int32_t cv = 0;
            if (in.type1 == 0x6 && in.extra < gd.strings().size()) {
                rhs = quote(gd.strings()[in.extra]);
            } else if (in.type1 == 0xF) {
                rhs = std::to_string(in.operand);
                is_c = true;
                cv = in.operand;
            } else if (in.type1 == 0x0) {
                uint64_t bits = (uint64_t)gd.u32(in.address + 4) |
                                ((uint64_t)gd.u32(in.address + 8) << 32);
                double dv;
                std::memcpy(&dv, &bits, 8);
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.17g", dv);
                rhs = buf;
            } else if (in.type1 == 0x3) {
                uint64_t bits = (uint64_t)gd.u32(in.address + 4) |
                                ((uint64_t)gd.u32(in.address + 8) << 32);
                rhs = "Value(" + std::to_string((int64_t)bits) + "LL)";
            } else if (in.type1 == 0x2) {
                std::string fn = gd.function_at_call(in.address + 4);
                if (!fn.empty()) {
                    std::string plain =
                        fn.rfind("gml_Script_", 0) == 0 ? fn.substr(11) : fn;
                    rhs = "kwik_make_fnref(&" + sanitize(fn) + ", " + quote(plain) + ")";
                } else {
                    rhs = std::to_string((int32_t)in.extra);
                    is_c = true;
                    cv = (int32_t)in.extra;
                }
            } else {
                rhs = std::to_string((int32_t)in.extra);
                is_c = true;
                cv = (int32_t)in.extra;
            }
            if (out) *out << "    " << S(d()) << " = " << rhs << ";\n";
            push((uint8_t)width_of_type(in.type1), is_c, cv);
            break;
        }

        case 0x45: {
            VarRef vr = gd.var_at(in.address);
            std::string name = vr.name.empty() ? "__unknown" : vr.name;
            uint8_t reftype = (in.extra >> 24) & 0xF8;
            if (reftype == 0xA0) {
                if (in.operand == -9) {
                    if (out)
                        *out << "    kwik_inst_set(self, " << S(d() - 2) << ", " << quote(name)
                             << ", " << S(d() - 1) << ");\n";
                    pop(2);
                } else {
                    emit_write_var(ctx, out, in.operand, name, S(d() - 1));
                    pop(1);
                }
            } else if (reftype == 0x00) {
                if (in.type1 == 0x5) {
                    Slot spec = slot(1);
                    if (spec.is_const && spec.cval == -9) {
                        if (out)
                            *out << "    kwik_array_set_at(self, " << S(d() - 3) << ", "
                                 << quote(name) << ", " << S(d() - 1) << ", " << S(d() - 4)
                                 << ");\n";
                        pop(4);
                    } else if (spec.is_const && spec.cval == -7) {
                        ctx.locals.insert(name);
                        if (out)
                            *out << "    kwik_array_store(loc_" << sanitize(name)
                                 << ", (int)(double)" << S(d() - 1) << ", " << S(d() - 3)
                                 << ");\n";
                        pop(3);
                    } else {
                        std::string specexpr = spec.is_const ? std::to_string(spec.cval)
                                                             : "(int)(double)" + S(d() - 2);
                        if (out)
                            *out << "    kwik_array_set(self, " << specexpr << ", " << quote(name)
                                 << ", " << S(d() - 1) << ", " << S(d() - 3) << ");\n";
                        pop(3);
                    }
                } else {
                    Slot spec = slot(2);
                    if (spec.is_const && spec.cval == -9) {
                        if (out)
                            *out << "    kwik_array_set_at(self, " << S(d() - 4) << ", "
                                 << quote(name) << ", " << S(d() - 2) << ", " << S(d() - 1)
                                 << ");\n";
                        pop(4);
                    } else if (spec.is_const && spec.cval == -7) {
                        ctx.locals.insert(name);
                        if (out)
                            *out << "    kwik_array_store(loc_" << sanitize(name)
                                 << ", (int)(double)" << S(d() - 2) << ", " << S(d() - 1)
                                 << ");\n";
                        pop(3);
                    } else {
                        std::string specexpr = spec.is_const ? std::to_string(spec.cval)
                                                             : "(int)(double)" + S(d() - 3);
                        if (out)
                            *out << "    kwik_array_set(self, " << specexpr << ", " << quote(name)
                                 << ", " << S(d() - 2) << ", " << S(d() - 1) << ");\n";
                        pop(3);
                    }
                }
            } else if (reftype == 0x80) {
                if (in.type1 == 0x5) {
                    Slot spec = slot(0);
                    if (spec.is_const && spec.cval == -9) {
                        if (out)
                            *out << "    kwik_inst_set(self, " << S(d() - 2) << ", " << quote(name)
                                 << ", " << S(d() - 3) << ");\n";
                        pop(3);
                    } else {
                        if (out)
                            *out << "    kwik_inst_set(self, " << S(d() - 1) << ", " << quote(name)
                                 << ", " << S(d() - 2) << ");\n";
                        pop(2);
                    }
                } else {
                    Slot spec = slot(1);
                    if (spec.is_const && spec.cval == -9) {
                        if (out)
                            *out << "    kwik_inst_set(self, " << S(d() - 3) << ", " << quote(name)
                                 << ", " << S(d() - 1) << ");\n";
                        pop(3);
                    } else {
                        if (out)
                            *out << "    kwik_inst_set(self, " << S(d() - 2) << ", " << quote(name)
                                 << ", " << S(d() - 1) << ");\n";
                        pop(2);
                    }
                }
            } else if (reftype == 0xE0) {
                if (out)
                    *out << "    kwik_inst_set(self, Value(" << (100000 + (int)in.operand)
                         << ".0), " << quote(name) << ", " << S(d() - 1) << ");\n";
                pop(1);
            } else {
                warn(ctx, in.address, "unknown var reftype on pop");
                pop(1);
            }
            break;
        }

        case 0x86: {
            uint16_t operand = (uint16_t)in.operand;
            int type_size = width_of_type(in.type1);
            if (operand & 0x8000) {
                int top_units = operand & 0x7FF;
                int bottom_units = (operand >> 11) & 0xF;
                int top_slots = bytes_to_slots(ctx, st.s, 0, top_units * type_size);
                int bottom_slots = bytes_to_slots(ctx, st.s, top_slots, bottom_units * type_size);
                int total = top_slots + bottom_slots;
                int base = d() - total;
                if (base < 0) {
                    warn(ctx, in.address, "dupswap underflow");
                    break;
                }
                if (out) {
                    *out << "    {\n";
                    for (int k = 0; k < top_slots; ++k)
                        *out << "    Value __t" << k << " = " << S(base + bottom_slots + k)
                             << ";\n";
                    for (int k = bottom_slots - 1; k >= 0; --k)
                        *out << "    " << S(base + top_slots + k) << " = " << S(base + k) << ";\n";
                    for (int k = 0; k < top_slots; ++k)
                        *out << "    " << S(base + k) << " = __t" << k << ";\n";
                    *out << "    }\n";
                }
                std::vector<Slot> topg(st.s.end() - top_slots, st.s.end());
                std::vector<Slot> botg(st.s.end() - total, st.s.end() - top_slots);
                for (int k = 0; k < total; ++k) st.s.pop_back();
                for (auto& sl : topg) st.s.push_back(sl);
                for (auto& sl : botg) st.s.push_back(sl);
            } else {
                int bytes = ((operand & 0x7FFF) + 1) * type_size;
                int n = bytes_to_slots(ctx, st.s, 0, bytes);
                int base = d() - n;
                if (base < 0) {
                    warn(ctx, in.address, "dup underflow");
                    break;
                }
                for (int k = 0; k < n; ++k) {
                    if (out) *out << "    " << S(d()) << " = " << S(base + k) << ";\n";
                    Slot copy = st.s[base + k];
                    st.s.push_back(copy);
                    if ((int)st.s.size() > ctx.max_depth) ctx.max_depth = (int)st.s.size();
                }
            }
            break;
        }

        case 0x07: {
            if (in.type2 == 0x4) {
                if (out)
                    *out << "    " << S(d() - 1) << " = Value(gml_truthy(" << S(d() - 1)
                         << ") ? 1.0 : 0.0);\n";
            } else if ((in.type2 == 0x2 || in.type2 == 0x3) &&
                       (in.type1 == 0x0 || in.type1 == 0x5 || in.type1 == 0x1)) {
                if (out)
                    *out << "    if (" << S(d() - 1) << ".type == Value::REAL) " << S(d() - 1)
                         << ".num = (double)(long long)llround(" << S(d() - 1) << ".num);\n";
            }
            if (!st.s.empty()) {
                st.s.back().width = (uint8_t)width_of_type(in.type2);
            }
            break;
        }

        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        case 0x0E: case 0x0F: case 0x10: case 0x13: case 0x14: {
            int b = d() - 2;
            if (b < 0) b = 0;
            if (out)
                *out << "    " << S(b) << " = " << binop_helper(in.opcode) << "(" << S(b) << ", "
                     << S(b + 1) << ");\n";
            uint8_t w = (uint8_t)std::max(width_of_type(in.type1), width_of_type(in.type2));
            pop(2);
            push(w);
            break;
        }

        case 0x15: {
            int b = d() - 2;
            if (b < 0) b = 0;
            if (out)
                *out << "    " << S(b) << " = " << cmp_helper(in.cmp_kind) << "(" << S(b) << ", "
                     << S(b + 1) << ");\n";
            pop(2);
            push(4);
            break;
        }

        case 0x11:
            if (out) *out << "    " << S(d() - 1) << " = gml_neg(" << S(d() - 1) << ");\n";
            if (!st.s.empty()) {
                st.s.back().is_const = false;
                st.s.back().width = (uint8_t)width_of_type(in.type1);
            }
            break;
        case 0x12:
            if (out) {
                if (in.type1 == 0x4)
                    *out << "    " << S(d() - 1) << " = gml_not(" << S(d() - 1) << ");\n";
                else
                    *out << "    " << S(d() - 1) << " = gml_bnot(" << S(d() - 1) << ");\n";
            }
            if (!st.s.empty()) {
                st.s.back().is_const = false;
                st.s.back().width = (uint8_t)width_of_type(in.type1);
            }
            break;

        case 0x9E:
            pop(1);
            break;

        case 0xD9: {
            std::string fn = gd.function_at_call(in.address + 4);
            int argcN = in.operand < 0 ? 0 : in.operand;
            if (argcN > d()) argcN = d();
            int base = d() - argcN;

            auto emit_args = [&](const std::string& callexpr_prefix,
                                 const std::string& callexpr_suffix) {
                if (!out) return;
                if (argcN == 0) {
                    *out << "    " << S(base) << " = " << callexpr_prefix << "nullptr, 0"
                         << callexpr_suffix << ";\n";
                } else {
                    *out << "    { Value __c[" << argcN << "] = { ";
                    for (int a = 0; a < argcN; ++a) {
                        if (a) *out << ", ";
                        *out << S(d() - 1 - a);
                    }
                    *out << " }; " << S(base) << " = " << callexpr_prefix << "__c, " << argcN
                         << callexpr_suffix << "; }\n";
                }
            };

            if (fn == "@@This@@") {
                if (out) *out << "    " << S(base) << " = kwik_this(self);\n";
            } else if (fn == "@@Other@@") {
                if (out) *out << "    " << S(base) << " = kwik_other(self);\n";
            } else if (fn == "@@Global@@") {
                if (out) *out << "    " << S(base) << " = Value(-5.0);\n";
            } else if (fn == "@@GetInstance@@") {
                if (out && argcN >= 1 && base != d() - 1)
                    *out << "    " << S(base) << " = " << S(d() - 1) << ";\n";
            } else if (fn == "@@NullObject@@") {
                if (out) *out << "    " << S(base) << " = Value(-4.0);\n";
            } else if (fn == "@@SetStatic@@") {
                ctx.uses_statics = true;
                if (out)
                    *out << "    __static_ok = true;\n    " << S(base) << " = Value();\n";
            } else if (fn == "@@CopyStatic@@") {
                ctx.uses_statics = true;
                if (out)
                    *out << "    kwik_copy_static_from(__statics, " << S(base) << ");\n    "
                         << S(base) << " = Value();\n";
            } else if (fn == "@@try_hook@@" || fn == "@@try_unhook@@" ||
                       fn == "@@throw@@" || fn == "@@finish_catch@@" ||
                       fn == "@@finish_finally@@") {
                if (out) *out << "    " << S(base) << " = Value();\n";
            } else if (fn == "@@NewGMLArray@@") {
                emit_args("kwik_new_array(", ")");
            } else if (fn == "@@NewGMLObject@@") {
                emit_args("kwik_new_object(self, ", ")");
            } else if (fn.empty()) {
                warn(ctx, in.address, "unresolved call target");
                if (out) *out << "    " << S(base) << " = kwik_missing(self, \"?\");\n";
            } else if (fn.rfind("gml_", 0) == 0 || fn.rfind("@@", 0) == 0) {
                emit_args(sanitize(fn) + "(self, ", ")");
            } else if (int32_t sci = ctx.gd.script_code_index(fn); sci >= 0 &&
                       (size_t)sci < ctx.gd.code().size()) {
                emit_args(sanitize(ctx.gd.code()[sci].name) + "(self, ", ")");
            } else {
                emit_args(builtin_call_name(fn) + "(self, ", ")");
            }
            pop(argcN);
            push(16);
            break;
        }

        case 0x99: {
            int argcN = in.operand < 0 ? 0 : in.operand;
            int base = d() - 2 - argcN;
            if (base < 0) base = 0;
            if (out) {
                if (argcN == 0) {
                    *out << "    " << S(base) << " = kwik_call_method(self, " << S(d() - 1) << ", "
                         << S(d() - 2) << ", nullptr, 0);\n";
                } else {
                    *out << "    { Value __c[" << argcN << "] = { ";
                    for (int a = 0; a < argcN; ++a) {
                        if (a) *out << ", ";
                        *out << S(d() - 3 - a);
                    }
                    *out << " }; " << S(base) << " = kwik_call_method(self, " << S(d() - 1) << ", "
                         << S(d() - 2) << ", __c, " << argcN << "); }\n";
                }
            }
            pop(argcN + 2);
            push(16);
            break;
        }

        case 0xB6:
            if (out) *out << "    goto " << label_of(in.jump_target) << ";\n";
            add_target(in.jump_target);
            falls = false;
            break;

        case 0xB7:
            if (out)
                *out << "    if (gml_truthy(" << S(d() - 1) << ")) goto "
                     << label_of(in.jump_target) << ";\n";
            pop(1);
            add_target(in.jump_target);
            break;

        case 0xB8:
            if (out)
                *out << "    if (!gml_truthy(" << S(d() - 1) << ")) goto "
                     << label_of(in.jump_target) << ";\n";
            pop(1);
            add_target(in.jump_target);
            break;

        case 0x9C:
            if (out) *out << "    return " << S(d() - 1) << ";\n";
            pop(1);
            falls = false;
            break;

        case 0x9D:
            if (out) *out << "    return Value();\n";
            falls = false;
            break;

        case 0xBA: {
            Slot spec = slot(0);
            int consumed = (spec.is_const && spec.cval == -9) ? 2 : 1;
            if (out) {
                *out << "    kwik_env_push(self, " << S(d() - consumed) << ");\n";
                *out << "    { Instance* __w = kwik_env_first(); if (!__w) goto "
                     << label_of(in.jump_target) << "; self = __w; }\n";
            }
            pop(consumed);
            add_target(in.jump_target);
            break;
        }

        case 0xBB: {
            uint32_t word = gd.u32(in.address);
            if ((word & 0x00FFFFFF) == 0x00F00000) {
                if (out) *out << "    self = kwik_env_pop();\n";
            } else {
                if (out) {
                    *out << "    { Instance* __w = kwik_env_next(); if (__w) { self = __w; goto "
                         << label_of(in.jump_target) << "; } }\n";
                    *out << "    self = kwik_env_pop();\n";
                }
                add_target(in.jump_target);
            }
            break;
        }

        case 0xFF: {
            switch (in.operand) {
                case -1:
                    break;
                case -2:
                    if (out)
                        *out << "    " << S(d() - 2) << " = kwik_pushaf(" << S(d() - 2) << ", "
                             << S(d() - 1) << ");\n";
                    pop(2);
                    push(16);
                    break;
                case -3:
                    if (out)
                        *out << "    kwik_popaf(" << S(d() - 2) << ", " << S(d() - 1) << ", "
                             << S(d() - 3) << ");\n";
                    pop(3);
                    break;
                case -4:
                    if (out)
                        *out << "    " << S(d() - 2) << " = kwik_pushac(" << S(d() - 2) << ", "
                             << S(d() - 1) << ");\n";
                    pop(2);
                    push(16);
                    break;
                case -5:
                    if (out) *out << "    kwik_setowner(" << S(d() - 1) << ");\n";
                    pop(1);
                    break;
                case -6:
                    ctx.uses_statics = true;
                    if (out) *out << "    " << S(d()) << " = Value(__static_ok);\n";
                    push(4);
                    break;
                case -7:
                    ctx.uses_statics = true;
                    if (out) *out << "    __static_ok = true;\n";
                    break;
                case -8:
                case -9:
                    break;
                case -10:
                    if (out)
                        *out << "    " << S(d()) << " = Value(" << S(d() - 1)
                             << ".type == Value::UNDEF);\n";
                    push(4);
                    break;
                case -11: {
                    uint32_t atype = (in.extra >> 24) & 0xFF;
                    int32_t aidx = (int32_t)(in.extra & 0x00FFFFFF);
                    std::string fn = gd.function_at_call(in.address + 4);
                    if (fn.empty() && atype == 5) fn = gd.function_by_index((uint32_t)aidx);
                    if (!fn.empty()) {
                        std::string plain = fn.rfind("gml_Script_", 0) == 0 ? fn.substr(11) : fn;
                        if (out)
                            *out << "    " << S(d()) << " = kwik_make_fnref(&" << sanitize(fn)
                                 << ", " << quote(plain) << ");\n";
                    } else if (atype == 5) {
                        warn(ctx, in.address, "pushref: unknown script index");
                        if (out) *out << "    " << S(d()) << " = Value();\n";
                    } else {
                        if (out) *out << "    " << S(d()) << " = " << aidx << ";\n";
                    }
                    push(16);
                    break;
                }
                default:
                    warn(ctx, in.address, "unknown break op");
                    break;
            }
            break;
        }

        default:
            warn(ctx, in.address, "unknown opcode");
            break;
    }

    if (falls && i + 1 < ctx.instrs.size()) succ.push_back({i + 1, false});
}

std::string lift_code_entry(const GameData& gd, const CodeEntry& e) {
    LiftCtx ctx(gd, e);
    ctx.instrs = disassemble(gd, e);
    size_t n = ctx.instrs.size();
    if (n == 0) return "    return Value();\n";

    for (size_t i = 0; i < n; ++i) ctx.idx_of[ctx.instrs[i].address] = i;
    ctx.before.resize(n);
    ctx.reachable.assign(n, false);

    std::vector<size_t> work;
    ctx.before[0].set = true;
    ctx.reachable[0] = true;
    work.push_back(0);
    while (!work.empty()) {
        size_t i = work.back();
        work.pop_back();
        StackState st = ctx.before[i];
        std::vector<Succ> succ;
        bool falls;
        exec_instr(ctx, i, st, nullptr, succ, falls);
        for (const Succ& sc : succ) {
            if (sc.idx >= n) continue;
            if (!ctx.before[sc.idx].set) {
                ctx.before[sc.idx] = st;
                ctx.before[sc.idx].set = true;
                ctx.reachable[sc.idx] = true;
                work.push_back(sc.idx);
            } else if (ctx.before[sc.idx].s.size() != st.s.size()) {
                warn(ctx, ctx.instrs[sc.idx].address, "stack depth mismatch at merge");
            }
        }
    }

    std::ostringstream body;
    for (size_t i = 0; i < n; ++i) {
        if (!ctx.reachable[i]) continue;
        if (ctx.labels.count(ctx.instrs[i].address))
            body << "  L_" << std::hex << ctx.instrs[i].address << std::dec << ": ;\n";
        StackState st = ctx.before[i];
        std::vector<Succ> succ;
        bool falls;
        exec_instr(ctx, i, st, &body, succ, falls);
    }

    std::ostringstream out;
    out << "    (void)self; (void)args; (void)argc;\n";
    out << "    Value __a[17]; int __an = argc < 16 ? argc : 16;\n";
    out << "    for (int __i = 0; __i < __an; ++__i) __a[__i] = args[__i];\n";
    out << "    (void)__a; (void)__an;\n";
    for (const std::string& l : ctx.locals) out << "    Value loc_" << sanitize(l) << ";\n";
    out << "    Value __s[" << (ctx.max_depth + 4) << "]; (void)__s;\n";
    if (ctx.uses_statics)
        out << "    static std::shared_ptr<Instance> __statics_sp = kwik_make_statics(&"
            << sanitize(ctx.e.name) << ");\n"
               "    Instance* __statics = __statics_sp.get(); (void)__statics;\n"
               "    static bool __static_ok = false; (void)__static_ok;\n";
    out << body.str();
    if (ctx.needs_exit_label) out << "  L_exit: ;\n";
    out << "    return Value();\n";
    return out.str();
}

struct ObjectSlots {
    std::string pre_create, create, destroy, cleanup;
    std::string step, step_begin, step_end;
    std::string draw, draw_gui, draw_begin, draw_end, draw_gui_begin, draw_gui_end;
    std::string draw_pre, draw_post;
    std::string alarm[12];
    std::string room_start, room_end, anim_end, game_start;
    std::string draw_resize, async_save_load, async_system, async_web;
    std::string outside_room, path_ended;
    std::string user[16];
    std::vector<std::pair<std::string, std::string>> collisions;
    std::vector<std::pair<int, std::string>> keypress;
    std::vector<std::pair<int, std::string>> keyrelease;
    std::vector<std::pair<int, std::string>> keyboard;
    std::vector<std::pair<int, std::string>> mouse;
};

static int obj_index_for(const GameData& gd, const std::string& code_name, std::string* suffix_out) {
    const std::string prefix = "gml_Object_";
    if (code_name.compare(0, prefix.size(), prefix) != 0) return -1;
    std::string body = code_name.substr(prefix.size());
    int best = -1;
    size_t best_len = 0;
    for (size_t i = 0; i < gd.objects().size(); ++i) {
        const std::string& on = gd.objects()[i].name;
        if (body.size() > on.size() && body.compare(0, on.size(), on) == 0 && body[on.size()] == '_') {
            if (on.size() > best_len) {
                best_len = on.size();
                best = (int)i;
            }
        }
    }
    if (best >= 0 && suffix_out) *suffix_out = body.substr(best_len + 1);
    return best;
}

static void assign_event(const GameData& gd, const std::string& code_name,
                         std::vector<ObjectSlots>& slots) {
    std::string suffix;
    int best = obj_index_for(gd, code_name, &suffix);
    if (best < 0) return;

    ObjectSlots& s = slots[best];
    std::string fn = sanitize(code_name);

    const std::string coll = "Collision_";
    if (suffix.compare(0, coll.size(), coll) == 0) {
        s.collisions.emplace_back(suffix.substr(coll.size()), fn);
        return;
    }

    size_t us = suffix.find_last_of('_');
    if (us == std::string::npos) return;
    std::string event = suffix.substr(0, us);
    int sub = std::atoi(suffix.substr(us + 1).c_str());

    if (event == "PreCreate") s.pre_create = fn;
    else if (event == "Create") s.create = fn;
    else if (event == "Destroy") s.destroy = fn;
    else if (event == "CleanUp") s.cleanup = fn;
    else if (event == "Step" && sub == 0) s.step = fn;
    else if (event == "Step" && sub == 1) s.step_begin = fn;
    else if (event == "Step" && sub == 2) s.step_end = fn;
    else if (event == "Draw" && sub == 0) s.draw = fn;
    else if (event == "Draw" && sub == 64) s.draw_gui = fn;
    else if (event == "Draw" && sub == 72) s.draw_begin = fn;
    else if (event == "Draw" && sub == 73) s.draw_end = fn;
    else if (event == "Draw" && sub == 74) s.draw_gui_begin = fn;
    else if (event == "Draw" && sub == 75) s.draw_gui_end = fn;
    else if (event == "Draw" && sub == 76) s.draw_pre = fn;
    else if (event == "Draw" && sub == 77) s.draw_post = fn;
    else if (event == "Draw" && sub == 65) s.draw_resize = fn;
    else if (event == "Alarm" && sub >= 0 && sub < 12) s.alarm[sub] = fn;
    else if (event == "Other" && sub == 2) s.game_start = fn;
    else if (event == "Other" && sub == 4) s.room_start = fn;
    else if (event == "Other" && sub == 5) s.room_end = fn;
    else if (event == "Other" && sub == 7) s.anim_end = fn;
    else if (event == "Other" && sub == 0) s.outside_room = fn;
    else if (event == "Other" && sub == 8) s.path_ended = fn;
    else if (event == "Other" && sub == 70) s.async_web = fn;
    else if (event == "Other" && sub == 72) s.async_save_load = fn;
    else if (event == "Other" && sub == 75) s.async_system = fn;
    else if (event == "Other" && sub >= 10 && sub <= 25) s.user[sub - 10] = fn;
    else if (event == "KeyPress") s.keypress.emplace_back(sub, fn);
    else if (event == "KeyRelease") s.keyrelease.emplace_back(sub, fn);
    else if (event == "Keyboard") s.keyboard.emplace_back(sub, fn);
    else if (event == "Mouse") s.mouse.emplace_back(sub, fn);
    else
        std::fprintf(stderr, "[lift] unhandled event %s_%d on %s\n", event.c_str(), sub,
                     gd.objects()[best].name.c_str());
}

static void emit_function(std::ostream& os, const GameData& gd, const CodeEntry& e) {
    os << "Value " << sanitize(e.name) << "(Instance* self, const Value* args, int argc) {\n";
    os << lift_code_entry(gd, e);
    os << "}\n\n";
}

static void emit_object_table(std::ostream& os, const GameData& gd) {
    std::vector<ObjectSlots> slots(gd.objects().size());
    for (const auto& e : gd.code())
        assign_event(gd, e.name, slots);

    auto obj_by_name = [&](const std::string& nm) -> int {
        for (size_t j = 0; j < gd.objects().size(); ++j)
            if (gd.objects()[j].name == nm) return (int)j;
        return -1;
    };

    for (size_t i = 0; i < gd.objects().size(); ++i) {
        if (!slots[i].collisions.empty()) {
            os << "static const CollisionHandler g_coll_" << i << "[] = {\n";
            for (const auto& c : slots[i].collisions)
                os << "    { " << obj_by_name(c.first) << ", " << c.second << " },\n";
            os << "};\n";
        }
        auto emit_keys = [&](const char* tag,
                             const std::vector<std::pair<int, std::string>>& list) {
            if (list.empty()) return;
            os << "static const KeyHandler g_" << tag << "_" << i << "[] = {\n";
            for (const auto& k : list) os << "    { " << k.first << ", " << k.second << " },\n";
            os << "};\n";
        };
        emit_keys("kp", slots[i].keypress);
        emit_keys("kr", slots[i].keyrelease);
        emit_keys("kb", slots[i].keyboard);
        emit_keys("ms", slots[i].mouse);
    }

    os << "ObjectDef g_objects[" << gd.objects().size() << "];\n";
    os << "const int g_object_count = " << gd.objects().size() << ";\n\n";
    os << "void kwik_fill_objects() {\n";
    auto set = [&](std::ostream& o, const char* field, const std::string& fn) {
        if (!fn.empty()) o << " d." << field << " = " << fn << ";";
    };
    for (size_t i = 0; i < gd.objects().size(); ++i) {
        const GameObject& g = gd.objects()[i];
        const ObjectSlots& s = slots[i];
        os << "    { ObjectDef& d = g_objects[" << i << "]; d.name = " << quote(g.name)
           << "; d.sprite_index = " << g.sprite_index << "; d.parent_index = " << g.parent_index
           << "; d.mask_index = " << g.mask_index << "; d.persistent = " << (g.persistent ? 1 : 0)
           << "; d.visible = " << (g.visible ? 1 : 0) << "; d.depth = " << g.depth << ";";
        set(os, "pre_create", s.pre_create);
        set(os, "create", s.create);
        set(os, "destroy", s.destroy);
        set(os, "cleanup", s.cleanup);
        set(os, "step", s.step);
        set(os, "step_begin", s.step_begin);
        set(os, "step_end", s.step_end);
        set(os, "draw", s.draw);
        set(os, "draw_gui", s.draw_gui);
        set(os, "draw_begin", s.draw_begin);
        set(os, "draw_end", s.draw_end);
        set(os, "draw_gui_begin", s.draw_gui_begin);
        set(os, "draw_gui_end", s.draw_gui_end);
        set(os, "draw_pre", s.draw_pre);
        set(os, "draw_post", s.draw_post);
        set(os, "room_start", s.room_start);
        set(os, "room_end", s.room_end);
        set(os, "anim_end", s.anim_end);
        set(os, "game_start", s.game_start);
        set(os, "draw_resize", s.draw_resize);
        set(os, "async_save_load", s.async_save_load);
        set(os, "async_system", s.async_system);
        set(os, "async_web", s.async_web);
        set(os, "outside_room", s.outside_room);
        set(os, "path_ended", s.path_ended);
        for (int a = 0; a < 12; ++a)
            if (!s.alarm[a].empty()) os << " d.alarm[" << a << "] = " << s.alarm[a] << ";";
        for (int u = 0; u < 16; ++u)
            if (!s.user[u].empty()) os << " d.user[" << u << "] = " << s.user[u] << ";";
        if (!s.collisions.empty())
            os << " d.collisions = g_coll_" << i << "; d.collision_count = "
               << s.collisions.size() << ";";
        if (!s.keypress.empty())
            os << " d.keypress = g_kp_" << i << "; d.keypress_count = " << s.keypress.size()
               << ";";
        if (!s.keyrelease.empty())
            os << " d.keyrelease = g_kr_" << i << "; d.keyrelease_count = "
               << s.keyrelease.size() << ";";
        if (!s.keyboard.empty())
            os << " d.keyboard = g_kb_" << i << "; d.keyboard_count = " << s.keyboard.size()
               << ";";
        if (!s.mouse.empty())
            os << " d.mouse = g_ms_" << i << "; d.mouse_count = " << s.mouse.size() << ";";
        os << " }\n";
    }
    os << "}\n\n";
}

static std::string code_fn_or_null(const GameData& gd, int code_id) {
    if (code_id >= 0 && code_id < (int)gd.code().size())
        return sanitize(gd.code()[code_id].name);
    return "nullptr";
}

static void emit_room_data(std::ostream& os, const GameData& gd, const AssetExtraction& ex) {
    const auto& rooms = gd.rooms();
    for (size_t i = 0; i < rooms.size(); ++i) {
        os << "static const InstanceInit g_instances_" << i << "[] = {\n";
        for (const auto& ri : rooms[i].instances)
            os << "    { " << ri.object_index << ", " << ri.x << ", " << ri.y << ", " << ri.id
               << ", " << ri.scale_x << ", " << ri.scale_y << ", " << ri.image_index << ", "
               << ri.angle << ", " << ri.depth << ", " << code_fn_or_null(gd, ri.creation_code)
               << ", " << code_fn_or_null(gd, ri.precreate_code) << " },\n";
        os << "    { -1, 0, 0, 0, 1, 1, 0, 0, 0, nullptr, nullptr },\n";
        os << "};\n";
        os << "static const RoomLayerDef g_layers_" << i << "[] = {\n";
        for (size_t li = 0; li < rooms[i].layers.size(); ++li) {
            const auto& l = rooms[i].layers[li];
            int grid_blob = -1;
            auto it = ex.tilemap_blobs.find((int)i * 1000 + (int)li);
            if (it != ex.tilemap_blobs.end()) grid_blob = it->second;
            os << "    { " << quote(l.name) << ", " << l.id << ", " << l.type << ", " << l.depth
               << ", " << l.x << ", " << l.y << ", " << l.visible << ", " << l.sprite << ", "
               << l.htiled << ", " << l.vtiled << ", " << l.stretch << ", " << l.color << "u, "
               << l.tile_first << ", " << l.tile_count << ", " << l.tileset << ", " << l.grid_w
               << ", " << l.grid_h << ", " << grid_blob << " },\n";
        }
        os << "    { \"\", -1, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0u, 0, 0, -1, 0, 0, -1 },\n";
        os << "};\n";
        os << "static const RoomTile g_tiles_" << i << "[] = {\n";
        for (const auto& t : rooms[i].tiles)
            os << "    { " << t.x << ", " << t.y << ", " << t.sprite << ", " << t.src_x << ", "
               << t.src_y << ", " << t.w << ", " << t.h << ", " << t.depth << ", " << t.scale_x
               << ", " << t.scale_y << ", " << t.color << "u, " << t.angle << ", " << t.frame
               << ", " << t.whole << " },\n";
        os << "    { 0, 0, -1, 0, 0, 0, 0, 0, 1, 1, 0u, 0, 0, 0 },\n";
        os << "};\n";
    }
    os << "\nconst RoomDef g_rooms[] = {\n";
    for (size_t i = 0; i < rooms.size(); ++i)
        os << "    { " << quote(rooms[i].name) << ", " << rooms[i].width << ", " << rooms[i].height
           << ", " << rooms[i].bg_color << "u, " << rooms[i].view_x << ", " << rooms[i].view_y
           << ", " << rooms[i].view_w << ", " << rooms[i].view_h << ", " << rooms[i].view_border_x
           << ", " << rooms[i].view_border_y << ", " << rooms[i].view_speed_x << ", "
           << rooms[i].view_speed_y << ", " << rooms[i].view_object << ", " << rooms[i].speed
           << ", " << rooms[i].persistent << ", " << code_fn_or_null(gd, rooms[i].creation_code)
           << ", g_instances_" << i << ", " << rooms[i].instances.size() << ", g_layers_" << i
           << ", " << rooms[i].layers.size() << ", g_tiles_" << i << ", " << rooms[i].tiles.size()
           << " },\n";
    os << "};\n";
    os << "const int g_room_count = " << rooms.size() << ";\n\n";
}

bool emit_cpp(const GameData&, const std::string&) {
    std::fprintf(stderr, "single-file emit no longer supported; use --emit-dir\n");
    return false;
}

bool emit_dir(const GameData& gd, const std::string& out_dir) {
    namespace fs = std::filesystem;
    fs::path root(out_dir);
    std::error_code ec;
    fs::create_directories(root / "objects", ec);
    fs::create_directories(root / "rooms", ec);
    fs::create_directories(root / "scripts", ec);

    std::ofstream hdr(root / "generated.h", std::ios::binary);
    if (!hdr) return false;
    hdr << "#pragma once\n#include \"gml_runtime.h\"\n#include <cmath>\n\nusing gml::Value;\nusing gml::Instance;\n\n";
    for (const auto& e : gd.code())
        hdr << "Value " << sanitize(e.name)
            << "(Instance* self, const Value* args, int argc);\n";
    hdr << "\nextern gml::ObjectDef g_objects[];\n";
    hdr << "extern const int g_object_count;\n";
    hdr << "extern const gml::RoomDef g_rooms[];\n";
    hdr << "extern const int g_room_count;\n";
    hdr << "void kwik_fill_objects();\n";
    hdr.close();

    std::map<std::string, std::vector<const CodeEntry*>> by_object;
    std::vector<const CodeEntry*> rooms_code;
    std::vector<const CodeEntry*> scripts_code;

    for (const auto& e : gd.code()) {
        int idx = obj_index_for(gd, e.name, nullptr);
        if (idx >= 0)
            by_object[gd.objects()[idx].name].push_back(&e);
        else if (e.name.rfind("gml_RoomCC_", 0) == 0 || e.name.rfind("gml_Room_", 0) == 0)
            rooms_code.push_back(&e);
        else
            scripts_code.push_back(&e);
    }

    for (const char* sub : {"objects", "rooms", "scripts"})
        for (const auto& old : fs::directory_iterator(root / sub, ec))
            if (old.path().extension() == ".cpp") fs::remove(old.path(), ec);

    const size_t unit_budget = 80 * 1024;
    const size_t big_fn_threshold = 128 * 1024;

    auto emit_entry = [&](const CodeEntry& e) {
        std::ostringstream ss;
        emit_function(ss, gd, e);
        std::string s = ss.str();
        if (s.size() > big_fn_threshold) {
            s = "#if defined(__GNUC__) && !defined(__clang__)\n"
                "#pragma GCC push_options\n"
                "#pragma GCC optimize (\"O1\")\n"
                "#endif\n" +
                s +
                "#if defined(__GNUC__) && !defined(__clang__)\n"
                "#pragma GCC pop_options\n"
                "#endif\n";
        }
        return s;
    };

    auto write_split = [&](const fs::path& dir, const std::string& prefix, bool numbered,
                           const std::vector<const CodeEntry*>& entries) {
        size_t file_idx = 0;
        size_t cur = 0;
        std::ofstream f;
        auto open_next = [&]() {
            if (f.is_open()) f.close();
            std::string name = !numbered && file_idx == 0
                                   ? prefix + ".cpp"
                                   : prefix + "_" + std::to_string(file_idx) + ".cpp";
            ++file_idx;
            f.open(dir / name, std::ios::binary);
            f << "#include \"generated.h\"\n\nusing namespace gml;\n\n";
            cur = 0;
        };
        open_next();
        for (const CodeEntry* e : entries) {
            std::string s = emit_entry(*e);
            if (cur > 0 && cur + s.size() > unit_budget) open_next();
            f << s;
            cur += s.size();
        }
    };

    for (const auto& kv : by_object)
        write_split(root / "objects", sanitize(kv.first), false, kv.second);

    write_split(root / "rooms", "rooms", true, rooms_code);
    write_split(root / "scripts", "scripts", true, scripts_code);

    AssetExtraction ex;
    extract_assets(gd, out_dir, ex);

    std::ofstream data(root / "game_data.cpp", std::ios::binary);
    data << "#include \"generated.h\"\n\nusing namespace gml;\n\n";
    emit_object_table(data, gd);
    emit_room_data(data, gd, ex);
    data << "namespace gml {\n";
    if (!ex.tilesets.empty()) {
        data << "static const KwikTileset g_tilesets_data[] = {\n";
        for (const auto& t : ex.tilesets)
            data << "    { " << t.image << ", " << t.tile_w << ", " << t.tile_h << ", "
                 << t.border_x << ", " << t.border_y << ", " << t.columns << ", " << t.frames
                 << ", " << t.tile_count << ", " << t.frame_ms << ", " << t.map_blob << " },\n";
        data << "};\n";
        data << "const KwikTileset* g_tilesets = g_tilesets_data;\n";
    } else {
        data << "const KwikTileset* g_tilesets = nullptr;\n";
    }
    data << "int g_tileset_count = " << ex.tilesets.size() << ";\n";
    if (!ex.sprites.empty()) {
        data << "static const KwikSprite g_sprites_data[] = {\n";
        for (const auto& s : ex.sprites)
            data << "    { " << s.first_frame << ", " << s.frame_count << ", " << s.origin_x
                 << ", " << s.origin_y << ", " << s.speed << ", " << s.speed_type << ", "
                 << s.bbox_left << ", " << s.bbox_top << ", " << s.bbox_right << ", "
                 << s.bbox_bottom << ", " << s.width << ", " << s.height << ", " << s.sep_masks
                 << ", " << s.mask_blob << ", " << quote(s.name) << ", " << s.tile_repeat
                 << " },\n";
        data << "};\n";
        data << "const KwikSprite* g_sprites = g_sprites_data;\n";
    } else {
        data << "const KwikSprite* g_sprites = nullptr;\n";
    }
    data << "int g_sprite_count = " << ex.sprites.size() << ";\n";
    data << "int g_image_count = " << ex.image_count << ";\n";
    if (!ex.glyphs.empty()) {
        data << "static const KwikGlyph g_glyphs_data[] = {\n";
        for (const auto& g : ex.glyphs)
            data << "    { " << g.ch << ", " << g.x << ", " << g.y << ", " << g.w << ", " << g.h
                 << ", " << g.shift << ", " << g.offset << " },\n";
        data << "};\n";
        data << "const KwikGlyph* g_glyphs = g_glyphs_data;\n";
    } else {
        data << "const KwikGlyph* g_glyphs = nullptr;\n";
    }
    if (!ex.fonts.empty()) {
        data << "static const KwikFont g_fonts_data[] = {\n";
        for (const auto& f : ex.fonts)
            data << "    { " << f.atlas_image << ", " << f.glyph_start << ", " << f.glyph_count
                 << ", " << f.size << ", " << quote(f.name) << " },\n";
        data << "};\n";
        data << "const KwikFont* g_fonts = g_fonts_data;\n";
    } else {
        data << "const KwikFont* g_fonts = nullptr;\n";
    }
    data << "int g_font_count = " << ex.fonts.size() << ";\n";
    data << "int g_glyph_count = " << ex.glyphs.size() << ";\n";
    if (!ex.sounds.empty()) {
        data << "static const KwikSound g_sound_data[] = {\n";
        for (const auto& s : ex.sounds)
            data << "    { " << quote(s.name) << ", " << quote(s.file) << ", (float)" << s.volume
                 << ", (float)" << s.pitch << ", " << s.blob << " },\n";
        data << "};\n";
        data << "const KwikSound* g_sound_table = g_sound_data;\n";
    } else {
        data << "const KwikSound* g_sound_table = nullptr;\n";
    }
    data << "int g_sound_count = " << ex.sounds.size() << ";\n";
    data << "}\n\n";
    data.close();

    std::ofstream mainf(root / "main.cpp", std::ios::binary);
    mainf << "#include \"generated.h\"\n\nusing namespace gml;\n\n";
    if (!gd.global_init_ids().empty()) {
        mainf << "static const ScriptFn g_global_init[] = {\n";
        for (uint32_t id : gd.global_init_ids())
            mainf << "    " << code_fn_or_null(gd, (int)id) << ",\n";
        mainf << "};\n";
    }
    {
        mainf << "static const ScriptEntry g_script_entries[] = {\n";
        int script_count = 0;
        for (const auto& e : gd.code()) {
            if (e.name.rfind("gml_Script_", 0) == 0) {
                mainf << "    { " << quote(e.name.substr(11)) << ", " << sanitize(e.name)
                      << " },\n";
                ++script_count;
            }
        }
        mainf << "    { \"\", nullptr },\n};\n";
        mainf << "static const int g_script_entry_count = " << script_count << ";\n";
    }
    mainf << "\nint main(int argc, char** argv) {\n";
    mainf << "    kwik_set_program_args(argc, argv);\n";
    mainf << "    kwik_fill_objects();\n";
    mainf << "    GameTables t{};\n";
    mainf << "    t.objects = g_objects;\n";
    mainf << "    t.object_count = g_object_count;\n";
    mainf << "    t.rooms = g_rooms;\n";
    mainf << "    t.room_count = g_room_count;\n";
    if (!gd.global_init_ids().empty()) {
        mainf << "    t.global_init = g_global_init;\n";
        mainf << "    t.global_init_count = " << gd.global_init_ids().size() << ";\n";
    }
    mainf << "    t.scripts = g_script_entries;\n";
    mainf << "    t.script_count = g_script_entry_count;\n";
    {
        mainf << "    t.assets_path = \"Assets.dat\";\n";
    }
    mainf << "    t.game_name = " << quote(gd.display_name()) << ";\n";
    mainf << "    t.save_id = " << quote(gd.game_name()) << ";\n";
    mainf << "    t.window_w = " << gd.window_w() << ";\n";
    mainf << "    t.window_h = " << gd.window_h() << ";\n";
    mainf << "    t.game_fps = " << gd.game_fps() << ";\n";
    mainf << "    return gml::run_game(t);\n";
    mainf << "}\n";
    mainf.close();

#ifdef KWIK_SOURCE_ROOT
    const char* kwik_root = KWIK_SOURCE_ROOT;
#else
    const char* kwik_root = "";
#endif
    std::ifstream base(std::string(kwik_root) + "/Base.cmake", std::ios::binary);
    if (!base) {
        std::fprintf(stderr, "kwik: could not open %s/Base.cmake\n", kwik_root);
        return false;
    }
    std::string tmpl((std::istreambuf_iterator<char>(base)), std::istreambuf_iterator<char>());
    const std::string placeholder = "@KWIK_DIR@";
    for (size_t p = tmpl.find(placeholder); p != std::string::npos;
         p = tmpl.find(placeholder, p))
        tmpl.replace(p, placeholder.size(), kwik_root);
    std::ofstream cm(root / "CMakeLists.txt", std::ios::binary);
    cm << tmpl;
    return true;
}

}
