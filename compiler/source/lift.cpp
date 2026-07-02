#include "lift.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "assets.h"
#include "disasm.h"

namespace kwik {

static bool is_builtin_var(const std::string& n) {
    return n == "x" || n == "y";
}

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

static std::string var_ref(const GameData& gd, const Instruction& in, std::set<std::string>& locals) {
    VarRef v = gd.var_at(in.address);
    if (v.name.empty()) return "";
    if (v.instance_type == -5) return "global_var(" + quote(v.name) + ")";
    if (v.instance_type == -7) {
        locals.insert(v.name);
        return "loc_" + sanitize(v.name);
    }
    if (is_builtin_var(v.name) && v.instance_type == -1) return "self." + v.name;
    if (in.opcode == 0xC3 || v.instance_type == -6) return "builtin_var(" + quote(v.name) + ")";
    return "self.var(" + quote(v.name) + ")";
}

static bool is_branch(uint8_t op) { return op == 0xB6 || op == 0xB7 || op == 0xB8; }

static int stack_delta(const Instruction& in) {
    switch (in.opcode) {
        case 0x84: return 1;
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: return 1;
        case 0x86: return 1;
        case 0xFF: return in.operand == -11 ? 1 : 0;
        case 0x45: return -1;
        case 0x9E: return -1;
        case 0xD9: case 0x99: return 1 - in.operand;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
        case 0x0E: case 0x0F: case 0x10: case 0x13: case 0x14: return -1;
        case 0x15: return -1;
        case 0x11: case 0x12: case 0x07: return 0;
        case 0xB6: return 0;
        case 0xB7: case 0xB8: return -1;
        case 0x9C: return -1;
        case 0x9D: return 0;
        case 0xBA: return -1;
        case 0xBB: return 0;
        default: return 0;
    }
}

static std::string binop_helper(uint8_t op) {
    switch (op) {
        case 0x08: return "gml_mul";
        case 0x09: return "gml_div";
        case 0x0A: return "gml_intdiv";
        case 0x0B: return "gml_mod";
        case 0x0C: return "gml_add";
        case 0x0D: return "gml_sub";
        case 0x0E: return "gml_and";
        case 0x0F: return "gml_or";
        case 0x10: return "gml_xor";
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

std::string lift_code_entry(const GameData& gd, const CodeEntry& e) {
    std::vector<Instruction> instrs = disassemble(gd, e);
    size_t n = instrs.size();

    std::vector<int> depth_before(n, 0);
    std::set<uint32_t> targets;
    std::set<std::string> locals;
    int d = 0;
    int maxd = 1;
    for (size_t i = 0; i < n; ++i) {
        depth_before[i] = d;
        if (is_branch(instrs[i].opcode)) targets.insert(instrs[i].jump_target);
        int after = d + stack_delta(instrs[i]);
        if (after < 0) after = 0;
        if (d > maxd) maxd = d;
        if (after > maxd) maxd = after;
        d = after;
    }

    for (size_t i = 0; i < n; ++i) {
        const Instruction& in = instrs[i];
        if (in.opcode == 0xC0 || in.opcode == 0xC1 || in.opcode == 0xC2 || in.opcode == 0x45)
            var_ref(gd, in, locals);
    }

    std::ostringstream out;
    for (const std::string& l : locals)
        out << "    Value loc_" << sanitize(l) << ";\n";
    out << "    Value __s[" << (maxd + 2) << "];\n";
    out << "    (void)self; (void)__s;\n";

    auto S = [](int i) { return "__s[" + std::to_string(i) + "]"; };

    d = 0;
    char lbl[32];
    for (size_t i = 0; i < n; ++i) {
        const Instruction& in = instrs[i];
        if (targets.count(in.address)) {
            std::snprintf(lbl, sizeof(lbl), "L_%x", in.address);
            out << "  " << lbl << ": ;\n";
            d = depth_before[i];
        }

        switch (in.opcode) {
            case 0x84:
                out << "    " << S(d) << " = " << in.operand << ";\n";
                ++d;
                break;
            case 0xC0: case 0xC1: case 0xC2: case 0xC3: {
                std::string rhs;
                if (in.type1 == 0x6 && in.extra < gd.strings().size())
                    rhs = quote(gd.strings()[in.extra]);
                else if (in.type1 == 0x5) {
                    rhs = var_ref(gd, in, locals);
                    if (rhs.empty()) rhs = "Value()";
                } else if (in.type1 == 0xF)
                    rhs = std::to_string(in.operand);
                else
                    rhs = std::to_string(static_cast<int32_t>(in.extra));
                out << "    " << S(d) << " = " << rhs << ";\n";
                ++d;
                break;
            }
            case 0xFF:
                if (in.operand == -11) {
                    out << "    " << S(d) << " = " << static_cast<int32_t>(in.extra & 0x00FFFFFF) << ";\n";
                    ++d;
                }
                break;
            case 0x86:
                if (d >= 1) { out << "    " << S(d) << " = " << S(d - 1) << ";\n"; ++d; }
                break;
            case 0x07:
                break;
            case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            case 0x0E: case 0x0F: case 0x10: case 0x13: case 0x14: {
                int b = d - 2;
                if (b < 0) b = 0;
                out << "    " << S(b) << " = " << binop_helper(in.opcode) << "(" << S(b) << ", " << S(b + 1) << ");\n";
                if (d > 0) --d;
                break;
            }
            case 0x15: {
                int b = d - 2;
                if (b < 0) b = 0;
                out << "    " << S(b) << " = " << cmp_helper(in.cmp_kind) << "(" << S(b) << ", " << S(b + 1) << ");\n";
                if (d > 0) --d;
                break;
            }
            case 0x11:
                if (d >= 1) out << "    " << S(d - 1) << " = gml_neg(" << S(d - 1) << ");\n";
                break;
            case 0x12:
                if (d >= 1) out << "    " << S(d - 1) << " = gml_not(" << S(d - 1) << ");\n";
                break;
            case 0x45: {
                std::string dst = var_ref(gd, in, locals);
                if (!dst.empty() && d >= 1)
                    out << "    " << dst << " = " << S(d - 1) << ";\n";
                if (d > 0) --d;
                break;
            }
            case 0x9E:
                if (d > 0) --d;
                break;
            case 0xD9: case 0x99: {
                std::string fn = gd.function_at_call(in.address + 4);
                if (fn.empty()) fn = "unknown_func";
                int argc = in.operand;
                if (argc > d) argc = d;
                int base = d - argc;
                if (base < 0) base = 0;
                std::string call = fn + "(";
                for (int a = 0; a < argc; ++a) {
                    if (a) call += ", ";
                    call += S(d - 1 - a);
                }
                call += ")";
                out << "    " << S(base) << " = " << call << ";\n";
                d = base + 1;
                break;
            }
            case 0xB6:
                std::snprintf(lbl, sizeof(lbl), "L_%x", in.jump_target);
                out << "    goto " << lbl << ";\n";
                break;
            case 0xB7:
                std::snprintf(lbl, sizeof(lbl), "L_%x", in.jump_target);
                if (d >= 1) out << "    if (gml_truthy(" << S(d - 1) << ")) goto " << lbl << ";\n";
                if (d > 0) --d;
                break;
            case 0xB8:
                std::snprintf(lbl, sizeof(lbl), "L_%x", in.jump_target);
                if (d >= 1) out << "    if (!gml_truthy(" << S(d - 1) << ")) goto " << lbl << ";\n";
                if (d > 0) --d;
                break;
            case 0x9C:
                if (d > 0) --d;
                out << "    return;\n";
                break;
            case 0x9D:
                out << "    return;\n";
                break;
            case 0xBA:
                if (d > 0) --d;
                break;
            case 0xBB:
                break;
            default:
                break;
        }
    }

    std::set<uint32_t> placed;
    for (const auto& in : instrs) placed.insert(in.address);
    for (uint32_t t : targets) {
        if (!placed.count(t)) {
            std::snprintf(lbl, sizeof(lbl), "L_%x", t);
            out << "  " << lbl << ": ;\n";
        }
    }
    return out.str();
}

struct ObjectSlots {
    std::string create;
    std::string step;
    std::string draw;
    std::string draw_gui;
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
                best = static_cast<int>(i);
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

    size_t us = suffix.find_last_of('_');
    if (us == std::string::npos) return;
    std::string event = suffix.substr(0, us);
    int sub = std::atoi(suffix.substr(us + 1).c_str());

    ObjectSlots& s = slots[best];
    if (event == "Create" && sub == 0)
        s.create = code_name;
    else if (event == "Step" && sub == 0)
        s.step = code_name;
    else if (event == "Draw" && sub == 0)
        s.draw = code_name;
    else if (event == "Draw" && sub == 64)
        s.draw_gui = code_name;
}

static std::string slot_or_null(const std::string& fn) {
    return fn.empty() ? "nullptr" : fn;
}

static void emit_function(std::ostream& os, const GameData& gd, const CodeEntry& e) {
    os << "void " << e.name << "(Instance& self) {\n";
    os << lift_code_entry(gd, e);
    os << "}\n\n";
}

static void emit_object_table(std::ostream& os, const GameData& gd) {
    std::vector<ObjectSlots> slots(gd.objects().size());
    for (const auto& e : gd.code())
        assign_event(gd, e.name, slots);

    os << "const ObjectDef g_objects[] = {\n";
    for (size_t i = 0; i < gd.objects().size(); ++i) {
        const ObjectSlots& s = slots[i];
        os << "    { " << quote(gd.objects()[i].name) << ", " << slot_or_null(s.create) << ", "
           << slot_or_null(s.step) << ", " << slot_or_null(s.draw) << ", " << slot_or_null(s.draw_gui)
           << ", " << gd.objects()[i].sprite_index << " },\n";
    }
    os << "};\n";
    os << "const int g_object_count = " << gd.objects().size() << ";\n\n";
}

static void emit_room_data(std::ostream& os, const GameData& gd) {
    const Room* room = gd.rooms().empty() ? nullptr : &gd.rooms()[0];
    os << "static const InstanceInit g_instances[] = {\n";
    if (room)
        for (const auto& ri : room->instances)
            os << "    { " << ri.object_index << ", " << ri.x << ", " << ri.y << ", " << ri.id << " },\n";
    os << "};\n\n";

    os << "const RoomDef g_room = {\n";
    if (room)
        os << "    " << quote(room->name) << ", " << room->width << ", " << room->height << ", "
           << room->bg_color << "u, g_instances, " << room->instances.size() << "\n";
    else
        os << "    \"\", 0, 0, 0u, g_instances, 0\n";
    os << "};\n\n";
}

bool emit_cpp(const GameData& gd, const std::string& out_path) {
    std::ofstream f(out_path, std::ios::binary);
    if (!f) return false;

    f << "#include \"gml_runtime.h\"\n\n";
    f << "using namespace gml;\n\n";

    for (const auto& e : gd.code())
        emit_function(f, gd, e);

    emit_object_table(f, gd);
    emit_room_data(f, gd);
    f << "namespace gml {\n";
    f << "const KwikSprite* g_sprites = nullptr;\n";
    f << "int g_sprite_count = 0;\n";
    f << "int g_image_count = 0;\n";
    f << "const KwikGlyph* g_glyphs = nullptr;\n";
    f << "const KwikFont* g_fonts = nullptr;\n";
    f << "int g_font_count = 0;\n";
    f << "int g_glyph_count = 0;\n";
    f << "}\n\n";

    f << "int main() {\n";
    f << "    return run_game(g_objects, g_object_count, g_room);\n";
    f << "}\n";
    return true;
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
    hdr << "#pragma once\n#include \"gml_runtime.h\"\n\n";
    for (const auto& e : gd.code())
        hdr << "void " << e.name << "(gml::Instance& self);\n";
    hdr << "\nextern const gml::ObjectDef g_objects[];\n";
    hdr << "extern const int g_object_count;\n";
    hdr << "extern const gml::RoomDef g_room;\n";
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

    auto write_unit = [&](const fs::path& path, const std::vector<const CodeEntry*>& entries) {
        std::ofstream f(path, std::ios::binary);
        f << "#include \"generated.h\"\n\nusing namespace gml;\n\n";
        for (const CodeEntry* e : entries)
            emit_function(f, gd, *e);
    };

    for (const auto& kv : by_object)
        write_unit(root / "objects" / (sanitize(kv.first) + ".cpp"), kv.second);
    if (!rooms_code.empty())
        write_unit(root / "rooms" / "rooms.cpp", rooms_code);
    write_unit(root / "scripts" / "scripts.cpp", scripts_code);

    AssetExtraction ex;
    extract_assets(gd, out_dir, ex);

    std::ofstream data(root / "game_data.cpp", std::ios::binary);
    data << "#include \"generated.h\"\n\nusing namespace gml;\n\n";
    emit_object_table(data, gd);
    emit_room_data(data, gd);
    data << "namespace gml {\n";
    if (!ex.sprites.empty()) {
        data << "static const KwikSprite g_sprites_data[] = {\n";
        for (const auto& s : ex.sprites)
            data << "    { " << s.first_frame << ", " << s.frame_count << ", " << s.origin_x
                 << ", " << s.origin_y << " },\n";
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
                 << ", " << f.size << " },\n";
        data << "};\n";
        data << "const KwikFont* g_fonts = g_fonts_data;\n";
    } else {
        data << "const KwikFont* g_fonts = nullptr;\n";
    }
    data << "int g_font_count = " << ex.fonts.size() << ";\n";
    data << "int g_glyph_count = " << ex.glyphs.size() << ";\n";
    data << "}\n\n";
    data.close();

    std::ofstream mainf(root / "main.cpp", std::ios::binary);
    mainf << "#include \"generated.h\"\n\n";
    mainf << "int main() {\n";
    mainf << "    return gml::run_game(g_objects, g_object_count, g_room);\n";
    mainf << "}\n";
    mainf.close();

#ifdef KWIK_SOURCE_ROOT
    const char* kwik_root = KWIK_SOURCE_ROOT;
#else
    const char* kwik_root = "";
#endif
    std::ofstream cm(root / "CMakeLists.txt", std::ios::binary);
    cm << "cmake_minimum_required(VERSION 3.16)\n";
    cm << "project(kwik_game C CXX)\n\n";
    cm << "set(CMAKE_CXX_STANDARD 17)\n";
    cm << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";
    cm << "set(KWIK_DIR \"" << kwik_root << "\" CACHE PATH \"kwik repo root\")\n\n";
    cm << "add_subdirectory(${KWIK_DIR}/runtime ${CMAKE_BINARY_DIR}/kwik_runtime)\n\n";
    cm << "file(GLOB GAME_SOURCES CONFIGURE_DEPENDS\n";
    cm << "    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp\n";
    cm << "    ${CMAKE_CURRENT_SOURCE_DIR}/objects/*.cpp\n";
    cm << "    ${CMAKE_CURRENT_SOURCE_DIR}/rooms/*.cpp\n";
    cm << "    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.cpp)\n\n";
    cm << "add_executable(game ${GAME_SOURCES})\n";
    cm << "target_include_directories(game PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})\n";
    cm << "target_link_libraries(game PRIVATE kwik_runtime)\n";
    return true;
}

}
