#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gml {

struct Instance;
struct GmlArray;
struct Value;

using ScriptFn = Value (*)(Instance* self, const Value* args, int argc);

struct Value {
    enum Type : uint8_t { UNDEF, REAL, STR, ARR, OBJ, FN };
    Type type = UNDEF;
    double num = 0.0;
    std::string str;
    std::shared_ptr<GmlArray> arr;
    std::shared_ptr<Instance> obj;
    ScriptFn fn = nullptr;
    int fn_bind = -1;
    const char* fn_name = "";

    Value() {}
    Value(double d) : type(REAL), num(d) {}
    Value(int i) : type(REAL), num(i) {}
    Value(long long i) : type(REAL), num(static_cast<double>(i)) {}
    Value(unsigned int i) : type(REAL), num(i) {}
    Value(unsigned long long i) : type(REAL), num(static_cast<double>(i)) {}
    Value(bool b) : type(REAL), num(b ? 1.0 : 0.0) {}
    Value(const char* s) : type(STR), str(s) {}
    Value(const std::string& s) : type(STR), str(s) {}

    operator double() const;
    operator std::string() const;
    bool is_string() const { return type == STR; }
};

struct GmlArray {
    std::vector<Value> items;
    int64_t owner = 0;
};

struct Instance : std::enable_shared_from_this<Instance> {
    double x = 0.0, y = 0.0;
    double xprevious = 0.0, yprevious = 0.0, xstart = 0.0, ystart = 0.0;
    int id = 0;
    int object_index = -1;
    bool is_struct = false;
    bool dead = false;
    bool active = true;
    bool persistent = false;
    bool visible = true;
    double depth = 0.0;
    double m_speed = 0.0, m_dir = 0.0, m_hs = 0.0, m_vs = 0.0;
    std::unordered_map<std::string, Value> vars;

    Value& var(const std::string& n) { return vars[n]; }
    bool has(const std::string& n) const { return vars.count(n) != 0; }
};

struct CollisionHandler {
    int other_object;
    ScriptFn fn;
};

struct KeyHandler {
    int key;
    ScriptFn fn;
};

struct ObjectDef {
    const char* name = "";
    int sprite_index = -1;
    int parent_index = -100;
    int mask_index = -1;
    int persistent = 0;
    int visible = 1;
    double depth = 0.0;
    ScriptFn pre_create = nullptr, create = nullptr, destroy = nullptr, cleanup = nullptr;
    ScriptFn step = nullptr, step_begin = nullptr, step_end = nullptr;
    ScriptFn draw = nullptr, draw_gui = nullptr, draw_begin = nullptr, draw_end = nullptr;
    ScriptFn draw_gui_begin = nullptr, draw_gui_end = nullptr, draw_pre = nullptr, draw_post = nullptr;
    ScriptFn alarm[12] = {};
    ScriptFn room_start = nullptr, room_end = nullptr, anim_end = nullptr, game_start = nullptr;
    ScriptFn draw_resize = nullptr;
    ScriptFn async_save_load = nullptr, async_system = nullptr, async_web = nullptr;
    ScriptFn outside_room = nullptr, path_ended = nullptr;
    ScriptFn user[16] = {};
    const CollisionHandler* collisions = nullptr;
    int collision_count = 0;
    const KeyHandler* keypress = nullptr;
    int keypress_count = 0;
    const KeyHandler* keyrelease = nullptr;
    int keyrelease_count = 0;
    const KeyHandler* keyboard = nullptr;
    int keyboard_count = 0;
    const KeyHandler* mouse = nullptr;
    int mouse_count = 0;
};

struct KwikSprite {
    int first_frame;
    int frame_count;
    int origin_x;
    int origin_y;
    double speed;
    int speed_type;
    int bbox_left, bbox_top, bbox_right, bbox_bottom;
    int width, height;
    int sep_masks;
    int mask_blob;
    const char* name;
};

struct KwikFont {
    int atlas_image;
    int glyph_start;
    int glyph_count;
    int size;
    const char* name;
};

struct KwikGlyph {
    int ch;
    int x, y, w, h;
    int shift, offset;
};

struct KwikSound {
    const char* name;
    const char* file;
    float volume;
    float pitch;
    int blob;
};

struct InstanceInit {
    int object_index;
    double x, y;
    int id;
    double scale_x, scale_y;
    int image_index;
    double angle;
    double depth;
    ScriptFn creation_code;
};

struct RoomLayerDef {
    const char* name;
    int id;
    int type;
    double depth;
    int x, y;
    int visible;
    int sprite;
    int htiled, vtiled, stretch;
    unsigned int color;
    int tile_first, tile_count;
};

struct RoomTile {
    int x, y;
    int sprite;
    int src_x, src_y, w, h;
    double depth;
    double scale_x, scale_y;
    unsigned int color;
};

struct RoomDef {
    const char* name;
    int width, height;
    unsigned int bg_color;
    int view_x, view_y, view_w, view_h;
    int view_border_x, view_border_y;
    int view_speed_x, view_speed_y;
    int view_object;
    int speed;
    int persistent;
    ScriptFn creation_code;
    const InstanceInit* instances;
    int instance_count;
    const RoomLayerDef* layers;
    int layer_count;
    const RoomTile* tiles;
    int tile_count;
};

struct ScriptEntry {
    const char* name;
    ScriptFn fn;
};

struct GameTables {
    ObjectDef* objects;
    int object_count;
    const RoomDef* rooms;
    int room_count;
    const ScriptFn* global_init;
    int global_init_count;
    const ScriptEntry* scripts;
    int script_count;
    const char* game_dir;
    const char* assets_path;
    const char* game_name;
    const char* save_id;
    int window_w;
    int window_h;
    int game_fps;
};

extern const KwikSprite* g_sprites;
extern int g_sprite_count;
extern int g_image_count;
extern const KwikFont* g_fonts;
extern int g_font_count;
extern const KwikGlyph* g_glyphs;
extern int g_glyph_count;
extern const KwikSound* g_sound_table;
extern int g_sound_count;

Value gml_add(const Value& a, const Value& b);
Value gml_sub(const Value& a, const Value& b);
Value gml_mul(const Value& a, const Value& b);
Value gml_div(const Value& a, const Value& b);
Value gml_intdiv(const Value& a, const Value& b);
Value gml_mod(const Value& a, const Value& b);
Value gml_band(const Value& a, const Value& b);
Value gml_bor(const Value& a, const Value& b);
Value gml_bxor(const Value& a, const Value& b);
Value gml_shl(const Value& a, const Value& b);
Value gml_shr(const Value& a, const Value& b);
Value gml_neg(const Value& a);
Value gml_not(const Value& a);
Value gml_bnot(const Value& a);
Value gml_lt(const Value& a, const Value& b);
Value gml_le(const Value& a, const Value& b);
Value gml_eq(const Value& a, const Value& b);
Value gml_ne(const Value& a, const Value& b);
Value gml_ge(const Value& a, const Value& b);
Value gml_gt(const Value& a, const Value& b);
bool gml_truthy(const Value& a);

Value& global_var(const std::string& name);
Value kwik_scope_get(Instance* self, int spec, const char* name);
void kwik_scope_set(Instance* self, int spec, const char* name, const Value& v);
Value kwik_inst_get(Instance* self, const Value& who, const char* name);
void kwik_inst_set(Instance* self, const Value& who, const char* name, const Value& v);
Value kwik_array_get(Instance* self, int spec, const char* name, const Value& idx);
Value kwik_array_get_at(Instance* self, const Value& who, const char* name, const Value& idx);
void kwik_array_set(Instance* self, int spec, const char* name, const Value& idx, const Value& v);
void kwik_array_set_at(Instance* self, const Value& who, const char* name, const Value& idx, const Value& v);
Value kwik_array_wref(Instance* self, int spec, const char* name, const Value& idx);
Value kwik_array_wref_at(Instance* self, const Value& who, const char* name, const Value& idx);
Value kwik_array_elem(const Value& slot, int idx);
void kwik_array_store(Value& slot, int idx, const Value& v);
Value kwik_array_wslot(Value& slot, int idx);
Value kwik_pushaf(const Value& arrref, const Value& idx);
void kwik_popaf(const Value& arrref, const Value& idx, const Value& v);
Value kwik_pushac(const Value& arrref, const Value& idx);
void kwik_setowner(const Value& v);
Value kwik_builtin_get(Instance* self, const char* name);
void kwik_builtin_set(Instance* self, const char* name, const Value& v);
Value kwik_arg_get(const Value* args, int argc, const Value& idx);

Value kwik_this(Instance* self);
Value kwik_other(Instance* self);
Value kwik_new_array(const Value* args, int argc);
Value kwik_new_object(Instance* self, const Value* args, int argc);
Value kwik_make_fnref(ScriptFn fn, const char* name);
Value kwik_call_method(Instance* self, const Value& fnval, const Value& target, const Value* args, int argc);
Value kwik_call_value(Instance* self, const Value& fnval, const Value* args, int argc);

void kwik_env_push(Instance* self, const Value& target);
Instance* kwik_env_first();
Instance* kwik_env_next();
Instance* kwik_env_pop();

Instance* kwik_instance_by_id(int id);
Instance* kwik_first_instance(int obj_or_id);
Instance* kwik_resolve_target(Instance* self, const Value& who);
bool kwik_obj_is_a(int obj, int ancestor);
Value kwik_create_instance(int obj_index, double x, double y, double depth, bool use_depth);
void kwik_destroy_instance(Instance* inst, bool run_event);
std::vector<Instance*> kwik_instances_matching(Instance* self, int who);
void kwik_fire_event(Instance* inst, int slot_kind, int sub);
double kwik_room_speed();
int kwik_current_room();
void kwik_room_goto(int index);
extern int g_room_count_rt;
extern const RoomDef* g_room_defs_rt;
extern ObjectDef* g_objects_rt;
extern int g_object_count_rt;
extern std::string g_game_dir;
extern std::string g_assets_path;

const unsigned char* kwik_sound_blob(int blob_index, unsigned int& size, int& type);
void kwik_audio_update();
void kwik_audio_shutdown();

int kwik_font_for_asset(int font_asset);
int kwik_font_add_sprite(int spr, const std::string& mapping, bool prop, int sep);
void kwik_set_font_rt(int rt_font);
int kwik_get_font_rt();
double kwik_string_width(const std::string& s);
double kwik_string_height(const std::string& s);
void kwik_draw_text_rt(double x, double y, const std::string& text, double xs, double ys, double angle);
void kwik_draw_text_ext_rt(double x, double y, const std::string& text, double sep, double wrapw,
                           double xs, double ys, double angle);
void kwik_draw_sprite_general(int spr, int sub, double x, double y, double xs, double ys,
                              double angle, unsigned int blend, double alpha);
void kwik_draw_sprite_part(int spr, int sub, double left, double top, double w, double h,
                           double x, double y, double xs, double ys, unsigned int blend, double alpha);
void kwik_draw_sprite_stretched(int spr, int sub, double x, double y, double w, double h,
                                unsigned int blend, double alpha);
void kwik_draw_sprite_tiled(int spr, int sub, double x, double y, double xs, double ys,
                            unsigned int blend, double alpha);
void draw_self_instance(Instance* inst);
bool kwik_sprite_size(int spr, int& w, int& h);

Value kwik_missing(Instance* self, const char* name);

void kwik_set_program_args(int argc, char** argv);

int run_game(const GameTables& tables);

#define GMLFN(nm) Value nm(Instance* self, const Value* args, int argc)

GMLFN(abs); GMLFN(angle_difference); GMLFN(arccos); GMLFN(arcsin); GMLFN(arctan); GMLFN(arctan2);
GMLFN(ceil); GMLFN(choose); GMLFN(clamp); GMLFN(cos); GMLFN(sin); GMLFN(tan); GMLFN(exp);
GMLFN(floor); GMLFN(frac); GMLFN(lengthdir_x); GMLFN(lengthdir_y); GMLFN(ln); GMLFN(log2);
GMLFN(log10); GMLFN(max); GMLFN(min); GMLFN(point_direction); GMLFN(point_distance);
GMLFN(power); GMLFN(random); GMLFN(random_range); GMLFN(irandom); GMLFN(irandom_range);
GMLFN(random_set_seed); GMLFN(randomize); GMLFN(round); GMLFN(sign); GMLFN(sqr); GMLFN(sqrt);
GMLFN(dsin); GMLFN(dcos); GMLFN(dtan); GMLFN(darcsin); GMLFN(darccos); GMLFN(darctan);

GMLFN(chr); GMLFN(ord); GMLFN(real); GMLFN(string); GMLFN(string_byte_length);
GMLFN(string_char_at); GMLFN(string_copy); GMLFN(string_delete); GMLFN(string_digits);
GMLFN(string_hash_to_newline); GMLFN(string_insert); GMLFN(string_length); GMLFN(string_letters);
GMLFN(string_lower); GMLFN(string_pos); GMLFN(string_repeat); GMLFN(string_replace);
GMLFN(string_replace_all); GMLFN(string_split); GMLFN(string_upper); GMLFN(string_format);
GMLFN(string_width); GMLFN(string_height);

GMLFN(is_string); GMLFN(is_real); GMLFN(is_array); GMLFN(is_undefined); GMLFN(is_bool);
GMLFN(is_struct); GMLFN(is_method); GMLFN(typeof_fn); GMLFN(bool_fn);
GMLFN(variable_global_exists); GMLFN(variable_instance_exists); GMLFN(variable_struct_set);
GMLFN(variable_struct_get); GMLFN(variable_struct_exists);

GMLFN(array_create); GMLFN(array_length); GMLFN(array_length_1d); GMLFN(array_push);
GMLFN(array_pop); GMLFN(array_resize); GMLFN(array_copy); GMLFN(array_delete); GMLFN(array_insert);

GMLFN(ds_exists); GMLFN(ds_list_add); GMLFN(ds_list_create); GMLFN(ds_list_destroy);
GMLFN(ds_list_find_value); GMLFN(ds_list_read); GMLFN(ds_list_size); GMLFN(ds_list_write);
GMLFN(ds_list_clear); GMLFN(ds_list_delete_fn); GMLFN(ds_map_add); GMLFN(ds_map_create);
GMLFN(ds_map_delete); GMLFN(ds_map_destroy); GMLFN(ds_map_exists); GMLFN(ds_map_find_first);
GMLFN(ds_map_find_next); GMLFN(ds_map_find_value); GMLFN(ds_map_keys_to_array); GMLFN(ds_map_set);
GMLFN(ds_map_set_post); GMLFN(ds_map_size); GMLFN(json_decode); GMLFN(json_encode);
GMLFN(json_parse); GMLFN(json_stringify);

GMLFN(ini_open); GMLFN(ini_open_from_string); GMLFN(ini_close); GMLFN(ini_read_real);
GMLFN(ini_read_string); GMLFN(ini_write_real); GMLFN(ini_write_string);
GMLFN(file_exists); GMLFN(file_copy); GMLFN(file_delete); GMLFN(file_text_open_read);
GMLFN(file_text_open_write); GMLFN(file_text_close); GMLFN(file_text_eof);
GMLFN(file_text_read_real); GMLFN(file_text_read_string); GMLFN(file_text_readln);
GMLFN(file_text_write_real); GMLFN(file_text_write_string); GMLFN(file_text_writeln);

GMLFN(buffer_create); GMLFN(buffer_delete); GMLFN(buffer_get_size); GMLFN(buffer_load);
GMLFN(buffer_load_async); GMLFN(buffer_read); GMLFN(buffer_write); GMLFN(buffer_save_async);
GMLFN(buffer_async_group_begin); GMLFN(buffer_async_group_end); GMLFN(buffer_async_group_option);

GMLFN(instance_exists); GMLFN(instance_find); GMLFN(instance_number); GMLFN(instance_create_depth);
GMLFN(instance_create_layer); GMLFN(instance_destroy); GMLFN(instance_activate_all);
GMLFN(instance_activate_object); GMLFN(instance_deactivate_all); GMLFN(instance_deactivate_object);
GMLFN(place_meeting); GMLFN(place_free); GMLFN(position_meeting); GMLFN(instance_place);
GMLFN(instance_position); GMLFN(instance_nearest); GMLFN(collision_line); GMLFN(collision_point);
GMLFN(collision_rectangle); GMLFN(collision_circle); GMLFN(distance_to_object); GMLFN(distance_to_point);
GMLFN(point_in_rectangle); GMLFN(move_towards_point); GMLFN(motion_set); GMLFN(motion_add);

GMLFN(event_user); GMLFN(event_perform); GMLFN(event_inherited_fn);
GMLFN(object_get_sprite); GMLFN(object_get_name); GMLFN(object_get_parent);
GMLFN(object_is_ancestor); GMLFN(script_execute); GMLFN(method); GMLFN(asset_get_index);

GMLFN(room_goto); GMLFN(room_goto_next); GMLFN(room_goto_previous); GMLFN(room_next);
GMLFN(room_previous); GMLFN(room_restart); GMLFN(room_get_name); GMLFN(room_exists);
GMLFN(game_end); GMLFN(game_restart); GMLFN(game_change);

GMLFN(sprite_exists); GMLFN(sprite_get_height); GMLFN(sprite_get_width); GMLFN(sprite_get_name);
GMLFN(sprite_get_number); GMLFN(sprite_get_xoffset); GMLFN(sprite_get_yoffset);
GMLFN(sprite_create_from_surface); GMLFN(sprite_delete); GMLFN(font_add_sprite_ext);
GMLFN(path_start); GMLFN(path_end);

GMLFN(draw_arrow); GMLFN(draw_circle); GMLFN(draw_circle_color); GMLFN(draw_circle_colour);
GMLFN(draw_ellipse); GMLFN(draw_ellipse_color); GMLFN(draw_ellipse_colour); GMLFN(draw_get_alpha);
GMLFN(draw_get_color); GMLFN(draw_line); GMLFN(draw_line_color); GMLFN(draw_line_colour);
GMLFN(draw_line_width); GMLFN(draw_line_width_color); GMLFN(draw_line_width_colour);
GMLFN(draw_path); GMLFN(draw_point); GMLFN(draw_point_color); GMLFN(draw_point_colour);
GMLFN(draw_rectangle); GMLFN(draw_rectangle_color); GMLFN(draw_rectangle_colour);
GMLFN(draw_roundrect); GMLFN(draw_roundrect_color); GMLFN(draw_roundrect_color_ext);
GMLFN(draw_roundrect_colour); GMLFN(draw_roundrect_colour_ext); GMLFN(draw_roundrect_ext);
GMLFN(draw_self); GMLFN(draw_set_alpha); GMLFN(draw_set_color); GMLFN(draw_set_colour);
GMLFN(draw_set_font); GMLFN(draw_set_halign); GMLFN(draw_set_valign); GMLFN(draw_sprite);
GMLFN(draw_sprite_ext); GMLFN(draw_sprite_part); GMLFN(draw_sprite_part_ext);
GMLFN(draw_sprite_stretched); GMLFN(draw_sprite_tiled_ext); GMLFN(draw_surface_ext);
GMLFN(draw_text); GMLFN(draw_text_color); GMLFN(draw_text_colour); GMLFN(draw_text_transformed);
GMLFN(draw_triangle); GMLFN(draw_triangle_color); GMLFN(draw_triangle_colour);
GMLFN(make_color_hsv); GMLFN(make_color_rgb); GMLFN(merge_color); GMLFN(color_get_red);
GMLFN(color_get_green); GMLFN(color_get_blue);

GMLFN(gpu_set_blendenable); GMLFN(gpu_set_blendmode); GMLFN(gpu_set_fog); GMLFN(gpu_set_texfilter);
GMLFN(surface_get_width); GMLFN(surface_get_height); GMLFN(texture_is_ready);
GMLFN(texture_prefetch); GMLFN(texturegroup_get_textures); GMLFN(application_surface_draw_enable);
GMLFN(application_surface_enable); GMLFN(vertex_create_buffer); GMLFN(vertex_format_add_colour);
GMLFN(vertex_format_add_normal); GMLFN(vertex_format_add_position_3d);
GMLFN(vertex_format_add_textcoord); GMLFN(vertex_format_begin); GMLFN(vertex_format_end);

GMLFN(layer_background_alpha); GMLFN(layer_background_blend); GMLFN(layer_background_change);
GMLFN(layer_background_create); GMLFN(layer_background_exists); GMLFN(layer_background_get_alpha);
GMLFN(layer_background_get_blend); GMLFN(layer_background_get_htiled);
GMLFN(layer_background_get_index); GMLFN(layer_background_get_sprite);
GMLFN(layer_background_get_stretch); GMLFN(layer_background_get_vtiled);
GMLFN(layer_background_get_xscale); GMLFN(layer_background_get_yscale);
GMLFN(layer_background_htiled); GMLFN(layer_background_stretch); GMLFN(layer_background_visible);
GMLFN(layer_background_vtiled); GMLFN(layer_background_xscale); GMLFN(layer_background_yscale);
GMLFN(layer_create); GMLFN(layer_depth); GMLFN(layer_destroy); GMLFN(layer_force_draw_depth);
GMLFN(layer_get_all); GMLFN(layer_get_all_elements); GMLFN(layer_get_depth);
GMLFN(layer_get_element_type); GMLFN(layer_get_hspeed); GMLFN(layer_get_name);
GMLFN(layer_get_visible); GMLFN(layer_get_vspeed); GMLFN(layer_get_x); GMLFN(layer_get_y);
GMLFN(layer_hspeed); GMLFN(layer_set_visible); GMLFN(layer_sprite_destroy);
GMLFN(layer_sprite_get_sprite); GMLFN(layer_tile_alpha); GMLFN(layer_tile_get_x);
GMLFN(layer_tile_get_y); GMLFN(layer_tile_visible); GMLFN(layer_tile_x); GMLFN(layer_tile_y);
GMLFN(layer_vspeed); GMLFN(layer_x); GMLFN(layer_y);

GMLFN(camera_create); GMLFN(camera_get_view_angle); GMLFN(camera_get_view_border_x);
GMLFN(camera_get_view_border_y); GMLFN(camera_get_view_height); GMLFN(camera_get_view_speed_x);
GMLFN(camera_get_view_speed_y); GMLFN(camera_get_view_target); GMLFN(camera_get_view_width);
GMLFN(camera_get_view_x); GMLFN(camera_get_view_y); GMLFN(camera_set_view_angle);
GMLFN(camera_set_view_border); GMLFN(camera_set_view_pos); GMLFN(camera_set_view_size);
GMLFN(camera_set_view_speed); GMLFN(camera_set_view_target); GMLFN(view_get_camera);
GMLFN(view_get_hport); GMLFN(view_get_surface_id); GMLFN(view_get_visible); GMLFN(view_get_wport);
GMLFN(view_get_xport); GMLFN(view_get_yport); GMLFN(view_set_camera); GMLFN(view_set_hport);
GMLFN(view_set_surface_id); GMLFN(view_set_visible); GMLFN(view_set_wport); GMLFN(view_set_xport);
GMLFN(view_set_yport);

GMLFN(window_center); GMLFN(window_enable_borderless_fullscreen); GMLFN(window_get_fullscreen);
GMLFN(window_get_height); GMLFN(window_get_width); GMLFN(window_set_caption);
GMLFN(window_set_cursor); GMLFN(window_set_fullscreen); GMLFN(window_set_size);
GMLFN(display_get_width); GMLFN(display_get_height); GMLFN(display_get_gui_width);
GMLFN(display_get_gui_height);

GMLFN(keyboard_check); GMLFN(keyboard_check_pressed); GMLFN(keyboard_check_released);
GMLFN(keyboard_key_press); GMLFN(keyboard_key_release);
GMLFN(mouse_check_button); GMLFN(mouse_check_button_pressed);
GMLFN(gamepad_axis_value); GMLFN(gamepad_button_check); GMLFN(gamepad_button_check_pressed);
GMLFN(gamepad_get_description); GMLFN(gamepad_get_device_count); GMLFN(gamepad_get_guid);
GMLFN(gamepad_is_connected); GMLFN(gamepad_test_mapping);

GMLFN(audio_create_stream); GMLFN(audio_destroy_stream); GMLFN(audio_group_is_loaded);
GMLFN(audio_group_load); GMLFN(audio_group_set_gain); GMLFN(audio_is_playing);
GMLFN(audio_pause_all); GMLFN(audio_pause_sound); GMLFN(audio_play_sound); GMLFN(audio_resume_all);
GMLFN(audio_resume_sound); GMLFN(audio_set_master_gain); GMLFN(audio_sound_gain);
GMLFN(audio_sound_get_track_position); GMLFN(audio_sound_pitch);
GMLFN(audio_sound_set_track_position); GMLFN(audio_stop_all); GMLFN(audio_stop_sound);

GMLFN(show_debug_message); GMLFN(show_error); GMLFN(show_message);
GMLFN(lerp); GMLFN(median); GMLFN(degtorad); GMLFN(radtodeg); GMLFN(randomise);
GMLFN(game_get_speed); GMLFN(array_get); GMLFN(array_length_2d); GMLFN(array_height_2d);
GMLFN(make_colour_rgb); GMLFN(make_colour_hsv); GMLFN(merge_colour);
GMLFN(variable_global_set); GMLFN(variable_instance_get); GMLFN(variable_instance_set);
GMLFN(variable_instance_get_names); GMLFN(event_inherited); GMLFN(get_string);
GMLFN(clipboard_set_text); GMLFN(keyboard_check_direct); GMLFN(buffer_md5);
GMLFN(buffer_get_surface); GMLFN(screen_save);
GMLFN(draw_clear); GMLFN(draw_clear_alpha); GMLFN(draw_get_font); GMLFN(draw_get_halign);
GMLFN(draw_get_valign); GMLFN(draw_healthbar); GMLFN(draw_primitive_begin);
GMLFN(draw_primitive_begin_texture); GMLFN(draw_primitive_end); GMLFN(draw_vertex);
GMLFN(draw_vertex_texture_color); GMLFN(draw_sprite_general); GMLFN(draw_sprite_pos);
GMLFN(draw_sprite_stretched_ext); GMLFN(draw_sprite_tiled); GMLFN(draw_surface);
GMLFN(draw_text_ext); GMLFN(draw_text_ext_transformed); GMLFN(draw_tilemap);
GMLFN(surface_create); GMLFN(surface_exists); GMLFN(surface_free); GMLFN(surface_set_target);
GMLFN(surface_reset_target); GMLFN(surface_get_texture); GMLFN(surface_getpixel_ext);
GMLFN(shader_set); GMLFN(shader_reset); GMLFN(shader_get_uniform);
GMLFN(shader_set_uniform_f); GMLFN(shader_get_sampler_index); GMLFN(texture_set_stage);
GMLFN(texture_get_texel_width); GMLFN(texture_get_texel_height);
GMLFN(gpu_set_alphatestenable); GMLFN(gpu_set_alphatestref); GMLFN(gpu_set_blendmode_ext);
GMLFN(gpu_set_colorwriteenable);
GMLFN(ds_list_find_index); GMLFN(ds_list_shuffle); GMLFN(ds_map_add_list); GMLFN(ds_map_clear);
GMLFN(ds_priority_add); GMLFN(ds_priority_clear); GMLFN(ds_priority_copy);
GMLFN(ds_priority_create); GMLFN(ds_priority_delete_min); GMLFN(ds_priority_empty);
GMLFN(ds_queue_create);
GMLFN(path_add); GMLFN(path_add_point); GMLFN(path_delete); GMLFN(path_exists);
GMLFN(path_get_x); GMLFN(path_get_y); GMLFN(path_set_closed); GMLFN(path_set_kind);
GMLFN(path_set_precision);
GMLFN(sprite_add); GMLFN(sprite_duplicate); GMLFN(sprite_get_bbox_left);
GMLFN(sprite_get_bbox_right); GMLFN(sprite_get_bbox_top); GMLFN(sprite_get_bbox_bottom);
GMLFN(sprite_get_texture); GMLFN(sprite_get_uvs); GMLFN(sprite_set_bbox);
GMLFN(sprite_set_offset);
GMLFN(audio_is_paused); GMLFN(audio_sound_get_gain); GMLFN(audio_sound_get_pitch);
GMLFN(audio_sound_length);
GMLFN(layer_exists); GMLFN(layer_get_id); GMLFN(layer_get_id_at_depth);
GMLFN(layer_background_get_id); GMLFN(layer_script_begin); GMLFN(layer_script_end);
GMLFN(layer_sprite_change); GMLFN(layer_sprite_get_id); GMLFN(layer_sprite_get_index);
GMLFN(layer_sprite_get_speed); GMLFN(layer_sprite_get_x); GMLFN(layer_sprite_get_xscale);
GMLFN(layer_sprite_get_y); GMLFN(layer_sprite_get_yscale); GMLFN(layer_sprite_speed);
GMLFN(layer_sprite_x); GMLFN(layer_sprite_y); GMLFN(layer_tilemap_get_id);
GMLFN(tilemap_get_x); GMLFN(tilemap_x); GMLFN(vertex_format_add_texcoord);
GMLFN(date_current_datetime); GMLFN(current_time_fn); GMLFN(get_timer);
GMLFN(environment_get_variable); GMLFN(os_get_info); GMLFN(os_get_language); GMLFN(os_is_paused);
GMLFN(parameter_count); GMLFN(parameter_string);
GMLFN(psn_get_trophy_unlock_state); GMLFN(psn_init_np_libs); GMLFN(psn_init_trophy);
GMLFN(psn_load_modules); GMLFN(psn_np_commerce_dialog_tick); GMLFN(psn_post_uds_event);
GMLFN(psn_tick); GMLFN(psn_unlock_trophy);
GMLFN(switch_accounts_is_user_open); GMLFN(switch_accounts_open_user);
GMLFN(switch_accounts_select_account); GMLFN(switch_controller_set_supported_styles);
GMLFN(switch_controller_support_get_selected_id); GMLFN(switch_controller_support_set_defaults);
GMLFN(switch_controller_support_set_singleplayer_only); GMLFN(switch_controller_support_show);
GMLFN(switch_language_get_desired_language); GMLFN(switch_save_data_commit);
GMLFN(switch_save_data_mount); GMLFN(switch_show_store);

}
