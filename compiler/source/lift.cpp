#include "lift.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "disasm.h"

namespace kwik {

static bool is_builtin_var(const std::string& n) {
    static const char* names[] = {"x", "y", "xprevious", "yprevious", "xstart", "ystart",
                                  "image_index", "image_speed", "image_xscale", "image_yscale",
                                  "image_angle", "image_alpha", "image_blend", "sprite_index",
                                  "depth", "direction", "speed", "hspeed", "vspeed", "gravity",
                                  "friction", "visible", "solid", "persistent", "id", "object_index"};
    for (const char* s : names)
        if (n == s) return true;
    return false;
}

static std::string var_expr(const GameData& gd, uint32_t addr) {
    VarRef v = gd.var_at(addr);
    if (v.name.empty()) return "Value()";
    if (v.instance_type == -5) return "global_var(\"" + v.name + "\")";
    if (is_builtin_var(v.name)) return "self." + v.name;
    return "self.var(\"" + v.name + "\")";
}

static std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string lift_code_entry(const GameData& gd, const CodeEntry& e) {
    std::vector<std::string> stack;
    std::ostringstream body;

    for (const auto& in : disassemble(gd, e)) {
        switch (in.opcode) {
            case 0xC0:
            case 0xC1:
            case 0xC2:
            case 0xC3: {
                if (in.type1 == 0x6 && in.extra < gd.strings().size())
                    stack.push_back(quote(gd.strings()[in.extra]));
                else if (in.type1 == 0x5)
                    stack.push_back(var_expr(gd, in.address));
                else if (in.type1 == 0xF)
                    stack.push_back(std::to_string(in.operand));
                else
                    stack.push_back(std::to_string(static_cast<int32_t>(in.extra)));
                break;
            }
            case 0x84:
                stack.push_back(std::to_string(in.operand));
                break;
            case 0x07:
                break;
            case 0xD9: {
                std::string fn = gd.function_at_call(in.address + 4);
                if (fn.empty()) fn = "unknown_func";
                std::vector<std::string> args;
                for (int i = 0; i < in.operand && !stack.empty(); ++i) {
                    args.push_back(stack.back());
                    stack.pop_back();
                }
                std::string call = fn + "(";
                for (size_t i = 0; i < args.size(); ++i) {
                    if (i) call += ", ";
                    call += args[i];
                }
                call += ")";
                stack.push_back(call);
                break;
            }
            case 0x9E:
                if (!stack.empty()) {
                    body << "    " << stack.back() << ";\n";
                    stack.pop_back();
                }
                break;
            case 0x9C:
                if (!stack.empty()) {
                    body << "    return " << stack.back() << ";\n";
                    stack.pop_back();
                }
                break;
            case 0x9D:
                body << "    return;\n";
                break;
            default:
                break;
        }
    }
    return body.str();
}

struct ObjectSlots {
    std::string create;
    std::string step;
    std::string draw;
    std::string draw_gui;
};

static void assign_event(const GameData& gd, const std::string& code_name,
                         std::vector<ObjectSlots>& slots) {
    const std::string prefix = "gml_Object_";
    if (code_name.compare(0, prefix.size(), prefix) != 0) return;
    std::string body = code_name.substr(prefix.size());

    int best = -1;
    size_t best_len = 0;
    for (size_t i = 0; i < gd.objects().size(); ++i) {
        const std::string& on = gd.objects()[i].name;
        if (body.size() > on.size() && body.compare(0, on.size(), on) == 0 &&
            body[on.size()] == '_') {
            if (on.size() > best_len) {
                best_len = on.size();
                best = static_cast<int>(i);
            }
        }
    }
    if (best < 0) return;

    std::string suffix = body.substr(best_len + 1);
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

bool emit_cpp(const GameData& gd, const std::string& out_path) {
    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) return false;

    std::fprintf(f, "#include \"gml_runtime.h\"\n\n");
    std::fprintf(f, "using namespace gml;\n\n");

    for (const auto& e : gd.code()) {
        std::fprintf(f, "static void %s(Instance& self) {\n", e.name.c_str());
        std::fprintf(f, "%s", lift_code_entry(gd, e).c_str());
        std::fprintf(f, "}\n\n");
    }

    std::vector<ObjectSlots> slots(gd.objects().size());
    for (const auto& e : gd.code())
        assign_event(gd, e.name, slots);

    std::fprintf(f, "static const ObjectDef g_objects[] = {\n");
    for (size_t i = 0; i < gd.objects().size(); ++i) {
        const ObjectSlots& s = slots[i];
        std::fprintf(f, "    { \"%s\", %s, %s, %s, %s },\n", gd.objects()[i].name.c_str(),
                     slot_or_null(s.create).c_str(), slot_or_null(s.step).c_str(),
                     slot_or_null(s.draw).c_str(), slot_or_null(s.draw_gui).c_str());
    }
    std::fprintf(f, "};\n\n");

    const Room* room = gd.rooms().empty() ? nullptr : &gd.rooms()[0];

    std::fprintf(f, "static const InstanceInit g_instances[] = {\n");
    if (room) {
        for (const auto& ri : room->instances)
            std::fprintf(f, "    { %d, %d, %d, %d },\n", ri.object_index, ri.x, ri.y, ri.id);
    }
    std::fprintf(f, "};\n\n");

    std::fprintf(f, "static const RoomDef g_room = {\n");
    if (room) {
        std::fprintf(f, "    \"%s\", %d, %d, %uu, g_instances, %d\n", room->name.c_str(), room->width,
                     room->height, room->bg_color, static_cast<int>(room->instances.size()));
    } else {
        std::fprintf(f, "    \"\", 0, 0, 0u, g_instances, 0\n");
    }
    std::fprintf(f, "};\n\n");

    std::fprintf(f, "int main() {\n");
    std::fprintf(f, "    return run_game(g_objects, %d, g_room);\n",
                 static_cast<int>(gd.objects().size()));
    std::fprintf(f, "}\n");

    std::fclose(f);
    return true;
}

}
