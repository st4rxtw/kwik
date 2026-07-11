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
    bool script_controlled = false;
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

enum AsyncKind { ASYNC_SAVE_LOAD_EV = 0, ASYNC_SYSTEM_EV = 1, ASYNC_WEB_EV = 2 };
void kwik_queue_async(int kind, int map_id);
extern int g_async_load_map;

const KwikSprite* kwik_sprite_at(int idx);
int kwik_sprite_total();
int kwik_register_dynamic_sprite(const KwikSprite& s);
int kwik_register_dynamic_image(unsigned int tex, int w, int h);

struct MaskSet {
    int count = 0;
    int w = 0;
    int h = 0;
    int rowbytes = 0;
    const unsigned char* data = nullptr;
};
const MaskSet* kwik_sprite_masks(int spr);

struct RtLayer {
    std::string name;
    int id = 0;
    int type = 1;
    double depth = 0;
    double x = 0, y = 0;
    double hspeed = 0, vspeed = 0;
    bool visible = true;
    bool el_visible = true;
    int sprite = -1;
    int htiled = 0, vtiled = 0, stretch = 0;
    unsigned int color = 0xFFFFFFFF;
    double alpha = 1.0;
    double xscale = 1.0, yscale = 1.0;
    int tile_first = 0, tile_count = 0;
    int tileset = -1, grid_w = 0, grid_h = 0, grid_blob = -1;
};

const uint32_t* kwik_tilemap_grid(int blob, int cells);
uint32_t* kwik_tilemap_grid_mut(int blob, int cells);
uint32_t kwik_tileset_frame_index(const KwikTileset& ts, uint32_t idx);
void kwik_render_tilemap(const RtLayer& tm, double ox, double oy);
void kwik_render_tilemap_clipped(const RtLayer& tm, double ox, double oy, double cx0, double cy0,
                                 double cx1, double cy1);
void kwik_draw_image_part(int image, double sx, double sy, double sw, double sh, double dx,
                          double dy, double xs, double ys, unsigned int blend, double alpha);
void kwik_draw_image_part_rot(int image, double sx, double sy, double sw, double sh, double dx,
                              double dy, double ox, double oy, double xs, double ys, double angle,
                              unsigned int blend, double alpha);
bool kwik_ds_list_push(int list, const Value& v);
extern int g_gpu_blendmode;
extern int g_gpu_blend_src;
extern int g_gpu_blend_dst;
extern int g_gpu_colorwrite[4];
extern int g_gpu_alphatest;

extern std::vector<RtLayer> g_rt_layers;
RtLayer* kwik_layer_by_id(int id);
int kwik_layer_create(double depth, const std::string& name);

extern std::string g_save_dir;
std::string kwik_save_path(const std::string& rel);
std::string kwik_resolve_read(const std::string& rel);

struct RtPath;
RtPath* kwik_path_by_id(int id);
int kwik_path_new();
void kwik_path_xy(int id, double pos, double& ox, double& oy);
double kwik_path_length(int id);
bool kwik_path_closed(int id);
void kwik_path_add_point(int id, double x, double y);
void kwik_path_set_closed(int id, bool closed);
void kwik_path_clear(int id);
bool kwik_path_exists(int id);

int kwik_sprite_frame_image(int spr, int sub);
void kwik_sprite_override_bbox(int spr, int l, int t, int r, int b);
void kwik_sprite_override_offset(int spr, int ox, int oy);
int kwik_sprite_add_file(const std::string& path, int imgnum, int xorig, int yorig);
double kwik_sound_length_seconds(int what);
double kwik_voice_gain(int what);
double kwik_voice_pitch(int what);
bool kwik_voice_paused(int what);
unsigned int kwik_image_texture(int image, int& w, int& h);
void kwik_flush_textures();

}
