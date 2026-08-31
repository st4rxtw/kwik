#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#elif !defined(__vita__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace gml {

static double A(const Value* args, int argc, int i, double dflt = 0.0) {
    return i < argc ? (double)args[i] : dflt;
}
static std::string S(const Value* args, int argc, int i) {
    return i < argc ? (std::string)args[i] : std::string();
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

static int g_keyboard_map[512];
static bool g_keyboard_map_init = false;

static void ensure_keyboard_map() {
    if (g_keyboard_map_init) return;
    g_keyboard_map_init = true;
    for (int i = 0; i < 512; ++i)
        g_keyboard_map[i] = i;
}

static bool keyboard_any_mapped(int vk, bool (*fn)(int)) {
    ensure_keyboard_map();
    if (vk == 1) {
        for (int i = 2; i < 512; ++i)
            if (fn(i)) return true;
        return false;
    }
    if (vk < 0 || vk >= 512) return false;
    if (fn(vk)) return true;
    for (int key = 0; key < 512; ++key)
        if (key != vk && g_keyboard_map[key] == vk && fn(key))
            return true;
    return false;
}

bool kwik_keyboard_mapped_down(int vk) { return keyboard_any_mapped(vk, render_key_down); }
bool kwik_keyboard_mapped_pressed(int vk) { return keyboard_any_mapped(vk, render_key_pressed); }
bool kwik_keyboard_mapped_released(int vk) { return keyboard_any_mapped(vk, render_key_released); }

void kwik_keyboard_set_map(int key, int maps_to) {
    ensure_keyboard_map();
    if (key >= 0 && key < 512 && maps_to >= 0 && maps_to < 512)
        g_keyboard_map[key] = maps_to;
}

void kwik_keyboard_unset_map(int key) {
    ensure_keyboard_map();
    if (key >= 0 && key < 512)
        g_keyboard_map[key] = key;
}

GMLFN(keyboard_check) {
    (void)self;
    int vk = (int)A(args, argc, 0);
    if (autoz_down(vk)) return Value(1.0);
    return Value(kwik_keyboard_mapped_down(vk));
}
GMLFN(keyboard_check_pressed) {
    (void)self;
    int vk = (int)A(args, argc, 0);
    if (autoz_pressed(vk)) return Value(1.0);
    return Value(kwik_keyboard_mapped_pressed(vk));
}
GMLFN(keyboard_check_released) {
    (void)self;
    return Value(kwik_keyboard_mapped_released((int)A(args, argc, 0)));
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

static Value g_exception_unhandled_handler;
GMLFN(exception_unhandled_handler) {
    (void)self;
    Value prev = g_exception_unhandled_handler;
    if (argc >= 1) g_exception_unhandled_handler = args[0];
    return prev;
}

GMLFN(gamepad_is_connected) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_get_device_count) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_check) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_check_pressed) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_check_released) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_count) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_button_value) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_axis_value) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(gamepad_get_description) { (void)self; (void)args; (void)argc; return Value(""); }
GMLFN(gamepad_get_guid) { (void)self; (void)args; (void)argc; return Value("none"); }
GMLFN(gamepad_test_mapping) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gamepad_set_vibration) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(gamepad_set_axis_deadzone) { (void)self; (void)args; (void)argc; return Value(); }

GMLFN(os_get_info) { (void)self; (void)args; (void)argc; return ds_map_create(self, nullptr, 0); }
GMLFN(os_get_language) { (void)self; (void)args; (void)argc; return Value("en"); }
GMLFN(os_is_paused) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(os_check_permission) { (void)self; (void)args; (void)argc; return Value(1.0); }
GMLFN(os_request_permission) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(show_debug_overlay) { (void)self; (void)args; (void)argc; return Value(); }
GMLFN(extension_stubfunc_real) { (void)self; (void)args; (void)argc; return Value(0.0); }
GMLFN(extension_stubfunc_string) { (void)self; (void)args; (void)argc; return Value(""); }
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
GMLFN(shaders_are_supported) { (void)self; (void)args; (void)argc; return Value(1.0); }

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
GMLFN(window_has_focus) { (void)self; (void)args; (void)argc; return Value(render_has_focus()); }
GMLFN(window_get_x) { (void)self; (void)args; (void)argc; return Value((double)render_window_x()); }
GMLFN(window_get_y) { (void)self; (void)args; (void)argc; return Value((double)render_window_y()); }
GMLFN(window_set_position) {
    (void)self;
    render_set_window_position((int)A(args, argc, 0), (int)A(args, argc, 1));
    return Value();
}

GMLFN(url_open_ext) {
    (void)self;
    std::string url = S(args, argc, 0);
    if (url.empty()) return Value(0.0);
#ifdef _WIN32
    std::string target = S(args, argc, 1);
    HINSTANCE r = ShellExecuteA(nullptr, "open", url.c_str(), nullptr,
                                target.empty() ? nullptr : target.c_str(), SW_SHOWNORMAL);
    return Value((intptr_t)r > 32 ? 1.0 : 0.0);
#elif defined(__vita__)
    return Value(0.0);
#else
    pid_t pid = fork();
    if (pid < 0) return Value(0.0);
    if (pid == 0) {
        pid_t grandchild = fork();
        if (grandchild < 0) _exit(127);
        if (grandchild > 0) _exit(0);
        execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return Value(1.0);
#endif
}

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
