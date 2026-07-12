#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}

static bool autoz_enabled() {
    static int state = -1;
    if (state < 0) state = std::getenv("KWIK_AUTOZ") ? 1 : 0;
    return state == 1;
}
static bool autoz_down(int vk) {
    if (!autoz_enabled()) return false;
    if (vk >= 37 && vk <= 40) {
        int dir = (int)((g_frame_counter / 90) % 4);
        static const int order[4] = {39, 40, 37, 38};
        return vk == order[dir];
    }
    if (vk != 90 && vk != 13 && vk != 1) return false;
    return (g_frame_counter % 16) < 8;
}
static bool autoz_pressed(int vk) {
    if (!autoz_enabled()) return false;
    if (vk != 90 && vk != 13 && vk != 1) return false;
    return (g_frame_counter % 16) == 0;
}

GMLFN(keyboard_check) {
    (void)self;
    int vk = (int)A(args, argc, 0);
    if (autoz_down(vk)) return Value(1.0);
    return Value(render_key_down(vk));
}
GMLFN(keyboard_check_pressed) {
    (void)self;
    int vk = (int)A(args, argc, 0);
    if (autoz_pressed(vk)) return Value(1.0);
    if (vk == 1) {
        for (int i = 2; i < 512; ++i)
            if (render_key_pressed(i)) return Value(1.0);
        return Value(0.0);
    }
    return Value(render_key_pressed(vk));
}
GMLFN(keyboard_check_released) {
    (void)self;
    return Value(render_key_released((int)A(args, argc, 0)));
}
GMLFN(keyboard_key_press) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(keyboard_key_release) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(mouse_check_button) {
    (void)self;
    return Value(render_mouse_down((int)A(args, argc, 0) - 1));
}
GMLFN(mouse_check_button_pressed) {
    (void)self;
    return Value(render_mouse_pressed((int)A(args, argc, 0) - 1));
}

GMLFN(gamepad_is_connected) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_get_device_count) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_check) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_check_pressed) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_axis_value) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_get_description) { (void)self; (void)args; (void)argc; return Value(""); }
GMLFN(gamepad_get_guid) { (void)self; (void)args; (void)argc; return Value("none"); }
GMLFN(gamepad_test_mapping) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gamepad_set_vibration) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gamepad_set_axis_deadzone) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(os_get_info) { (void)self; (void)args; (void)argc; return ds_map_create(self, nullptr, 0); }
GMLFN(os_get_language) { (void)self; (void)args; (void)argc; return Value("en"); }
GMLFN(os_is_paused) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(environment_get_variable) {
    (void)self;
    const char* v = argc > 0 ? std::getenv(((std::string)args[0]).c_str()) : nullptr;
    return Value(v ? v : "");
}
static int g_prog_argc = 0;
static char** g_prog_argv = nullptr;

void kwik_set_program_args(int argc, char** argv) {
    g_prog_argc = argc;
    g_prog_argv = argv;
}

GMLFN(parameter_count) {
    (void)self; (void)args; (void)argc;
    return Value((double)(g_prog_argc > 0 ? g_prog_argc - 1 : 0));
}
GMLFN(parameter_string) {
    (void)self;
    int n = argc > 0 ? (int)(double)args[0] : 0;
    if (n >= 0 && n < g_prog_argc) return Value(g_prog_argv[n]);
    return Value("");
}

GMLFN(date_current_datetime) {
    (void)self; (void)args; (void)argc;
    return Value((double)std::time(nullptr) / 86400.0 + 25569.0);
}
GMLFN(current_time_fn) { (void)self; (void)args; (void)argc; return Value(now_ms()); }
GMLFN(get_timer) { (void)self; (void)args; (void)argc; return Value(now_ms() * 1000.0); }

GMLFN(psn_get_trophy_unlock_state) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(psn_init_np_libs) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_init_trophy) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_load_modules) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_np_commerce_dialog_tick) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_post_uds_event) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_tick) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(psn_unlock_trophy) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(switch_accounts_is_user_open) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(switch_accounts_open_user) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_accounts_select_account) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_controller_set_supported_styles) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_controller_support_get_selected_id) { (void)self; (void)args; (void)argc; return Value(-1.0); }
GMLFN(switch_controller_support_set_defaults) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_controller_support_set_singleplayer_only) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_controller_support_show) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(switch_language_get_desired_language) { (void)self; (void)args; (void)argc; return Value("en"); }
GMLFN(switch_save_data_commit) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(switch_save_data_mount) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(switch_show_store) { (void)self; (void)args; (void)argc; return Value(); }


GMLFN(audio_system_is_available) { (void)self; (void)args; (void)argc; return Value(1.0); }

GMLFN(is_callable) { (void)self; return Value(argc > 0 && args[0].type == Value::FN); }
GMLFN(is_handle) {
    (void)self;
    return Value(argc > 0 && (args[0].type == Value::OBJ || args[0].type == Value::FN));
}
GMLFN(os_get_region) { (void)self; (void)args; (void)argc; return Value("US"); }
GMLFN(struct_get_from_hash) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(device_mouse_dbclick_enable) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(device_mouse_check_button) {
    (void)self;
    return Value(render_mouse_down((int)A(args, argc, 1) - 1));
}
GMLFN(device_mouse_check_button_pressed) {
    (void)self;
    return Value(render_mouse_pressed((int)A(args, argc, 1) - 1));
}
GMLFN(device_mouse_x_to_gui) { (void)self; (void)args; (void)argc; return Value(render_mouse_x()); }
GMLFN(device_mouse_y_to_gui) { (void)self; (void)args; (void)argc; return Value(render_mouse_y()); }
GMLFN(display_set_gui_size) {
    (void)self;
    render_set_gui_size((int)A(args, argc, 0), (int)A(args, argc, 1));
    return Value();
}
GMLFN(display_set_gui_maximize) { (void)self; (void)args; (void)argc; return Value(); }

static Value g_scissor_saved;

GMLFN(gpu_set_scissor) {
    (void)self;
    if (argc == 1) {
        g_scissor_saved = args[0];
        return Value();
    }
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    for (int i = 0; i < argc && i < 4; ++i) v.arr->items.push_back(args[i]);
    g_scissor_saved = v;
    return Value();
}
GMLFN(gpu_get_scissor) {
    (void)self; (void)args; (void)argc;
    if (g_scissor_saved.type == Value::ARR) return g_scissor_saved;
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    v.arr->items.push_back(Value(0.0));
    v.arr->items.push_back(Value(0.0));
    v.arr->items.push_back(Value((double)render_gui_width()));
    v.arr->items.push_back(Value((double)render_gui_height()));
    return v;
}
GMLFN(array_create_ext) {
    int n = (int)A(args, argc, 0);
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    for (int i = 0; i < n; ++i) {
        Value idx((double)i);
        v.arr->items.push_back(argc > 1 ? kwik_call_value(self, args[1], &idx, 1) : Value(0.0));
    }
    return v;
}
GMLFN(array_shuffle) {
    (void)self;
    Value v;
    v.type = Value::ARR;
    v.arr = std::make_shared<GmlArray>();
    if (argc > 0 && args[0].type == Value::ARR && args[0].arr) {
        v.arr->items = args[0].arr->items;
        for (size_t i = v.arr->items.size(); i > 1; --i) {
            size_t j = (size_t)(gml_random01() * (double)i);
            if (j >= i) j = i - 1;
            std::swap(v.arr->items[i - 1], v.arr->items[j]);
        }
    }
    return v;
}
GMLFN(room_get_info) {
    int rm = (int)A(args, argc, 0, -1);
    Value v = kwik_new_object(self, nullptr, 0);
    if (rm >= 0 && rm < g_room_count_rt && v.obj) {
        const RoomDef& r = g_room_defs_rt[rm];
        v.obj->var(std::string("name")) = Value(r.name);
        v.obj->var(std::string("width")) = Value((double)r.width);
        v.obj->var(std::string("height")) = Value((double)r.height);
        v.obj->var(std::string("persistent")) = Value((double)r.persistent);
    }
    return v;
}
}
