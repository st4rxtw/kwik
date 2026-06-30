#pragma once

#include <string>
#include <unordered_map>

namespace gml {

struct Instance {
    double x = 0.0;
    double y = 0.0;
    int id = 0;
    int object_index = 0;
    std::unordered_map<std::string, double> dynamic_vars;

    double& var(const std::string& name) { return dynamic_vars[name]; }
};

using EventFn = void (*)(Instance&);

struct ObjectDef {
    const char* name;
    EventFn create;
    EventFn step;
    EventFn draw;
    EventFn draw_gui;
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
    const InstanceInit* instances;
    int instance_count;
};

double& global_var(const std::string& name);

void draw_text(double x, double y, const std::string& text);

int run_game(const ObjectDef* objects, int object_count, const RoomDef& room);

}
