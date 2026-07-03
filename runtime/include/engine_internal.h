#pragma once

#include "gml_runtime.h"

namespace gml {

struct Camera {
    double x = 0, y = 0, w = 640, h = 480;
    int target = -4;
    double border_x = 32, border_y = 32;
    double speed_x = -1, speed_y = -1;
    double angle = 0;
    bool in_use = false;
};

extern std::vector<Camera> g_cameras;
extern int g_view_camera[8];
extern int g_view_visible[8];

extern std::vector<std::shared_ptr<Instance>> g_instances;
extern Instance* g_other_ptr;
extern Instance* g_dummy_instance;

extern int g_current_room;
extern int g_pending_room;
extern double g_room_speed_v;
extern bool g_game_end_requested;
extern bool g_game_restart_requested;
extern unsigned long long g_frame_counter;

double gml_random01();
void gml_random_seed(unsigned int seed);

int room_width_cur();
int room_height_cur();

bool inst_bbox(Instance* inst, double px, double py, double& l, double& t, double& r, double& b);
Instance* collision_at(Instance* self, double px, double py, int who, bool precise_point);
double now_ms();

int kwik_stream_register(const std::string& path);
const std::string* kwik_stream_path(int id);

}
