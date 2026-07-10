#include "render.h"

#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace gml {

static const int VITA_W = 960;
static const int VITA_H = 544;

static bool g_inited = false;
static int g_win_w = 640;
static int g_win_h = 480;
static int g_gui_w = 640;
static int g_gui_h = 480;
static int g_room_w = 640;
static int g_room_h = 480;
static double g_view_x = 0, g_view_y = 0, g_view_w = 640, g_view_h = 480;
static bool g_fog_on = false;
static float g_fog_rgb[4] = {0, 0, 0, 1};

static unsigned int g_color_bgr = 0xFFFFFF;
static float g_alpha = 1.0f;
static int g_halign = 0;
static int g_valign = 0;

static bool g_keys_now[512] = {false};
static bool g_keys_prev[512] = {false};
static bool g_mouse_now[3] = {false};
static bool g_mouse_prev[3] = {false};

static double g_last_time = 0.0;
static double g_dt = 0.0;

static GLuint g_fbo = 0;
static GLuint g_fbo_tex = 0;
static int g_fbo_w = 0;
static int g_fbo_h = 0;

static bool init_app_surface(int w, int h) {
    glGenFramebuffers(1, &g_fbo);
    glGenTextures(1, &g_fbo_tex);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_fbo_tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) {
        glDeleteFramebuffers(1, &g_fbo);
        glDeleteTextures(1, &g_fbo_tex);
        g_fbo = 0;
        g_fbo_tex = 0;
        return false;
    }
    g_fbo_w = w;
    g_fbo_h = h;
    return true;
}

bool render_app_surface_available() { return g_fbo != 0; }
unsigned int render_app_texture() { return g_fbo_tex; }
int render_app_width() { return g_fbo_w > 0 ? g_fbo_w : g_gui_w; }
int render_app_height() { return g_fbo_h > 0 ? g_fbo_h : g_gui_h; }

struct RtSurface {
    GLuint fbo = 0;
    GLuint tex = 0;
    int w = 0, h = 0;
    bool alive = false;
};
static std::vector<RtSurface> g_surfaces;
static std::vector<int> g_target_stack;

int render_surface_create(int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    RtSurface sf;
    glGenFramebuffers(1, &sf.fbo);
    glGenTextures(1, &sf.tex);
    glBindTexture(GL_TEXTURE_2D, sf.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, sf.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sf.tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    if (!ok) {
        glDeleteFramebuffers(1, &sf.fbo);
        glDeleteTextures(1, &sf.tex);
        return -1;
    }
    sf.w = w;
    sf.h = h;
    sf.alive = true;
    for (size_t i = 0; i < g_surfaces.size(); ++i)
        if (!g_surfaces[i].alive) {
            g_surfaces[i] = sf;
            return (int)i + 1;
        }
    g_surfaces.push_back(sf);
    return (int)g_surfaces.size();
}

static RtSurface* surf_of(int id) {
    int i = id - 1;
    if (i < 0 || (size_t)i >= g_surfaces.size() || !g_surfaces[i].alive) return nullptr;
    return &g_surfaces[i];
}

bool render_surface_exists(int id) {
    if (id == 0) return g_fbo != 0;
    return surf_of(id) != nullptr;
}

void render_surface_free(int id) {
    RtSurface* sf = surf_of(id);
    if (!sf) return;
    glDeleteFramebuffers(1, &sf->fbo);
    glDeleteTextures(1, &sf->tex);
    sf->alive = false;
}

unsigned int render_surface_texture(int id) {
    if (id == 0) return g_fbo_tex;
    RtSurface* sf = surf_of(id);
    return sf ? sf->tex : 0;
}

int render_surface_width(int id) {
    if (id == 0) return render_app_width();
    RtSurface* sf = surf_of(id);
    return sf ? sf->w : 0;
}

int render_surface_height(int id) {
    if (id == 0) return render_app_height();
    RtSurface* sf = surf_of(id);
    return sf ? sf->h : 0;
}

bool render_surface_set_target(int id) {
    RtSurface* sf = surf_of(id);
    if (!sf && id != 0) return false;
    g_target_stack.push_back(id);
    GLuint fbo = id == 0 ? g_fbo : sf->fbo;
    int w = id == 0 ? g_fbo_w : sf->w;
    int h = id == 0 ? g_fbo_h : sf->h;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, w, h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    return true;
}

void render_surface_reset_target() {
    if (g_target_stack.empty()) return;
    g_target_stack.pop_back();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    int prev = g_target_stack.empty() ? 0 : g_target_stack.back();
    if (prev == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glViewport(0, 0, g_fbo_w, g_fbo_h);
    } else {
        RtSurface* sf = surf_of(prev);
        if (sf) {
            glBindFramebuffer(GL_FRAMEBUFFER, sf->fbo);
            glViewport(0, 0, sf->w, sf->h);
        }
    }
}

bool render_surface_getpixel(int id, int x, int y, unsigned char* rgba_out) {
    RtSurface* sf = surf_of(id);
    GLuint fbo = id == 0 ? g_fbo : (sf ? sf->fbo : 0);
    int h = id == 0 ? g_fbo_h : (sf ? sf->h : 0);
    if (!fbo && id != 0) return false;
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(x, h - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba_out);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    return true;
}

void render_surface_clear(unsigned int bgr, double alpha) {
    glClearColor((bgr & 0xFF) / 255.0f, ((bgr >> 8) & 0xFF) / 255.0f,
                 ((bgr >> 16) & 0xFF) / 255.0f, (float)alpha);
    glClear(GL_COLOR_BUFFER_BIT);
}

struct Vtx {
    float x, y;
    float u, v;
    float r, g, b, a;
};

static void submit(const Vtx* v, int n, GLenum mode) {
    glVertexPointer(2, GL_FLOAT, sizeof(Vtx), &v[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vtx), &v[0].u);
    glColorPointer(4, GL_FLOAT, sizeof(Vtx), &v[0].r);
    glDrawArrays(mode, 0, n);
}

static int g_prim_kind = 0;
static GLenum g_prim_mode = GL_TRIANGLES;
static std::vector<Vtx> g_prim_verts;

void render_primitive_begin(int kind, unsigned int tex) {
    g_prim_kind = kind;
    g_prim_verts.clear();
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, tex);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    switch (kind) {
        case 1: g_prim_mode = GL_POINTS; break;
        case 2: g_prim_mode = GL_LINES; break;
        case 3: g_prim_mode = GL_LINE_STRIP; break;
        case 4: g_prim_mode = GL_TRIANGLES; break;
        case 5: g_prim_mode = GL_TRIANGLE_STRIP; break;
        case 6: g_prim_mode = GL_TRIANGLE_FAN; break;
        default: g_prim_mode = GL_TRIANGLES; break;
    }
}

void render_primitive_vertex(double x, double y, double u, double v, unsigned int color,
                             double alpha, bool textured) {
    (void)textured;
    Vtx vtx;
    vtx.x = (float)x;
    vtx.y = (float)y;
    vtx.u = (float)u;
    vtx.v = (float)v;
    vtx.r = (color & 0xFF) / 255.0f;
    vtx.g = ((color >> 8) & 0xFF) / 255.0f;
    vtx.b = ((color >> 16) & 0xFF) / 255.0f;
    vtx.a = (float)alpha;
    g_prim_verts.push_back(vtx);
}

void render_primitive_end() {
    if (g_prim_verts.size() >= 2 || (g_prim_verts.size() >= 1 && g_prim_kind == 1))
        submit(g_prim_verts.data(), (int)g_prim_verts.size(), g_prim_mode);
    g_prim_verts.clear();
    glEnable(GL_TEXTURE_2D);
}

static double g_wheel_accum = 0.0;
static double g_wheel_frame = 0.0;

bool render_surface_snapshot(int id, int x, int y, int w, int h, unsigned char* rgba_out) {
    RtSurface* sf = surf_of(id);
    GLuint fbo = id == 0 ? g_fbo : (sf ? sf->fbo : 0);
    int fh = id == 0 ? g_fbo_h : (sf ? sf->h : 0);
    if (!fbo && id != 0) return false;
    if (id == 0 && !g_fbo) return false;
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glReadPixels(x, fh - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba_out);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    for (int row = 0; row < h / 2; ++row) {
        for (int col = 0; col < w * 4; ++col) {
            unsigned char tmp = rgba_out[row * w * 4 + col];
            rgba_out[row * w * 4 + col] = rgba_out[(h - 1 - row) * w * 4 + col];
            rgba_out[(h - 1 - row) * w * 4 + col] = tmp;
        }
    }
    return true;
}

bool render_app_snapshot(int x, int y, int w, int h, unsigned char* rgba_out) {
    return render_surface_snapshot(0, x, y, w, h, rgba_out);
}

static bool pad_key_state(int vk) {
    SceCtrlData pad;
    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) return false;
    unsigned int b = pad.buttons;
    // temp until I add the input.ini shit from the 3ds port
    switch (vk) {
        case 13: return (b & (SCE_CTRL_CROSS | SCE_CTRL_START)) != 0;   // enter
        case 27: return (b & SCE_CTRL_CIRCLE) != 0;                     // escape
        case 32: return (b & SCE_CTRL_SQUARE) != 0;                     // space
        case 'Y': return (b & SCE_CTRL_TRIANGLE) != 0;
        case 'Q': return (b & SCE_CTRL_LTRIGGER) != 0;
        case 'E': return (b & SCE_CTRL_RTRIGGER) != 0;
        case 37: return (b & SCE_CTRL_LEFT) != 0 || pad.lx < 64;        // left
        case 38: return (b & SCE_CTRL_UP) != 0 || pad.ly < 64;          // up
        case 39: return (b & SCE_CTRL_RIGHT) != 0 || pad.lx > 192;      // right
        case 40: return (b & SCE_CTRL_DOWN) != 0 || pad.ly > 192;       // down
        default: return false;
    }
}

static bool key_state(int vk) {
    if (!g_inited) return false;
    return pad_key_state(vk);
}

bool render_init(const char* title, int width, int height, unsigned int bg_color) {
    (void)title;
    vglInit(0x1400000);
    vglUseVram(GL_TRUE);
    g_inited = true;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

    g_win_w = width;
    g_win_h = height;
    g_gui_w = width;
    g_gui_h = height;
    g_room_w = width;
    g_room_h = height;
    g_view_w = width;
    g_view_h = height;

    render_set_room(width, height, bg_color);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    if (!init_app_surface(width, height))
        std::fprintf(stderr, "kwik: application surface FBO unavailable\n");
    return true;
}

bool render_should_close() { return !g_inited; }

void render_begin_frame() {
    g_target_stack.clear();
    g_fog_on = false;
    if (g_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glViewport(0, 0, g_fbo_w, g_fbo_h);
    } else {
        glViewport(0, 0, VITA_W, VITA_H);
    }

    double vw = g_view_w > 0 ? g_view_w : g_room_w;
    double vh = g_view_h > 0 ? g_view_h : g_room_h;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(g_view_x, g_view_x + vw, g_view_y + vh, g_view_y, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void render_begin_gui() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, g_gui_w, g_gui_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void render_set_title(const char*) {}

void render_set_view(double x, double y, double w, double h) {
    g_view_x = x;
    g_view_y = y;
    if (w > 0) g_view_w = w;
    if (h > 0) g_view_h = h;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(g_view_x, g_view_x + g_view_w, g_view_y + g_view_h, g_view_y, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

double render_delta_time() { return g_dt; }
double render_time_ms() { return sceKernelGetProcessTimeWide() / 1000.0; }

static void blit_fbo_to_screen() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, VITA_W, VITA_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    double scale = std::min((double)VITA_W / g_fbo_w, (double)VITA_H / g_fbo_h);
    double dw = g_fbo_w * scale / VITA_W;
    double dh = g_fbo_h * scale / VITA_H;
    float x0 = (float)((1.0 - dw) * 0.5);
    float y0 = (float)((1.0 - dh) * 0.5);
    float x1 = x0 + (float)dw;
    float y1 = y0 + (float)dh;
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
    Vtx v[4] = {
        {x0, y0, 0, 1, 1, 1, 1, 1},
        {x1, y0, 1, 1, 1, 1, 1, 1},
        {x1, y1, 1, 0, 1, 1, 1, 1},
        {x0, y1, 0, 0, 1, 1, 1, 1},
    };
    submit(v, 4, GL_TRIANGLE_FAN);
    glEnable(GL_BLEND);
}

void render_present_last() {
    if (!g_inited) return;
    if (g_fbo) blit_fbo_to_screen();
    vglSwapBuffers(GL_FALSE);
}

void render_end_frame() {
    if (g_fbo) blit_fbo_to_screen();
    vglSwapBuffers(GL_FALSE);
    double now = sceKernelGetProcessTimeWide() / 1000000.0;
    g_dt = g_last_time > 0.0 ? now - g_last_time : 0.0;
    if (g_dt > 0.25) g_dt = 0.25;
    g_last_time = now;
    for (int i = 0; i < 512; ++i) g_keys_prev[i] = g_keys_now[i];
    for (int i = 0; i < 3; ++i) g_mouse_prev[i] = g_mouse_now[i];
    for (int i = 0; i < 512; ++i) g_keys_now[i] = key_state(i);
    bool any = false;
    for (int i = 2; i < 512; ++i) any = any || g_keys_now[i];
    g_keys_now[1] = any;
    g_keys_now[0] = false;
}

double render_wheel_delta() { return g_wheel_frame; }

void render_idle() {}

bool render_has_focus() { return true; }

bool render_key_down(int vk) {
    if (vk == 0) {
        for (int i = 2; i < 512; ++i)
            if (g_keys_now[i]) return false;
        return true;
    }
    if (vk < 0 || vk >= 512) return false;
    return g_keys_now[vk];
}
bool render_key_pressed(int vk) {
    if (vk < 0 || vk >= 512) return false;
    return g_keys_now[vk] && !g_keys_prev[vk];
}
bool render_key_released(int vk) {
    if (vk < 0 || vk >= 512) return false;
    return !g_keys_now[vk] && g_keys_prev[vk];
}
void render_keyboard_clear(int vk) {
    if (vk < 0) {
        for (int i = 0; i < 512; ++i) g_keys_now[i] = g_keys_prev[i] = false;
        return;
    }
    if (vk >= 512) return;
    g_keys_now[vk] = g_keys_prev[vk] = false;
}

double render_mouse_x() { return 0.0; }
double render_mouse_y() { return 0.0; }
bool render_mouse_down(int b) { return b >= 0 && b < 3 && g_mouse_now[b]; }
bool render_mouse_pressed(int b) { return b >= 0 && b < 3 && g_mouse_now[b] && !g_mouse_prev[b]; }
bool render_mouse_released(int b) { return b >= 0 && b < 3 && !g_mouse_now[b] && g_mouse_prev[b]; }

void render_set_color(unsigned int bgr) { g_color_bgr = bgr; }
unsigned int render_get_color() { return g_color_bgr; }
void render_set_alpha(double alpha) { g_alpha = static_cast<float>(alpha); }
double render_get_alpha() { return g_alpha; }
void render_set_halign(int align) { g_halign = align; }
void render_set_valign(int align) { g_valign = align; }
int render_get_halign() { return g_halign; }
int render_get_valign() { return g_valign; }

void render_set_blendmode(int mode) {
    switch (mode) {
        case 1: glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
        case 2: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR); break;
        case 3: glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR); break;
        default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    }
}

static GLenum gm_blend_factor(int f) {
    switch (f) {
        case 1: return GL_ZERO;
        case 2: return GL_ONE;
        case 3: return GL_SRC_COLOR;
        case 4: return GL_ONE_MINUS_SRC_COLOR;
        case 5: return GL_SRC_ALPHA;
        case 6: return GL_ONE_MINUS_SRC_ALPHA;
        case 7: return GL_DST_ALPHA;
        case 8: return GL_ONE_MINUS_DST_ALPHA;
        case 9: return GL_DST_COLOR;
        case 10: return GL_ONE_MINUS_DST_COLOR;
        case 11: return GL_SRC_ALPHA_SATURATE;
        default: return GL_ONE;
    }
}

void render_set_blendmode_ext(int src, int dst) {
    glBlendFunc(gm_blend_factor(src), gm_blend_factor(dst));
}

void render_set_blendmode_sepalpha(int src, int dst, int asrc, int adst) {
    glBlendFuncSeparate(gm_blend_factor(src), gm_blend_factor(dst), gm_blend_factor(asrc),
                        gm_blend_factor(adst));
}

void render_set_colorwrite(bool r, bool g, bool b, bool a) {
    glColorMask(r ? GL_TRUE : GL_FALSE, g ? GL_TRUE : GL_FALSE, b ? GL_TRUE : GL_FALSE,
                a ? GL_TRUE : GL_FALSE);
}

unsigned int render_upload_texture(const unsigned char* rgba, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

void render_set_fog(bool on, unsigned int bgr) {
    static bool disabled = std::getenv("KWIK_NO_FOG") != nullptr;
    if (disabled) return;
    g_fog_on = on;
    g_fog_rgb[0] = (bgr & 0xFF) / 255.0f;
    g_fog_rgb[1] = ((bgr >> 8) & 0xFF) / 255.0f;
    g_fog_rgb[2] = ((bgr >> 16) & 0xFF) / 255.0f;
    g_fog_rgb[3] = 1.0f;
}

static void apply_fog_env() {
    if (g_fog_on) {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_CONSTANT);
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
        glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, g_fog_rgb);
    } else {
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    }
}

void render_draw_quad(unsigned int tex, double x, double y, double dw, double dh,
                      double origin_x, double origin_y, double xscale, double yscale,
                      double angle_deg, float u0, float v0, float u1, float v1,
                      unsigned int blend_bgr, double alpha) {
    double rad = angle_deg * 3.14159265358979323846 / 180.0;
    double c = std::cos(rad), s = std::sin(rad);
    double lx[4] = {0.0, dw, dw, 0.0};
    double ly[4] = {0.0, 0.0, dh, dh};
    float vx[4], vy[4];
    for (int i = 0; i < 4; ++i) {
        double px = (lx[i] - origin_x) * xscale;
        double py = (ly[i] - origin_y) * yscale;
        vx[i] = (float)(x + px * c + py * s);
        vy[i] = (float)(y - px * s + py * c);
    }
    float r = (blend_bgr & 0xFF) / 255.0f;
    float g = ((blend_bgr >> 8) & 0xFF) / 255.0f;
    float b = ((blend_bgr >> 16) & 0xFF) / 255.0f;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    apply_fog_env();
    Vtx vtx[4] = {
        {vx[0], vy[0], u0, v0, r, g, b, (float)alpha},
        {vx[1], vy[1], u1, v0, r, g, b, (float)alpha},
        {vx[2], vy[2], u1, v1, r, g, b, (float)alpha},
        {vx[3], vy[3], u0, v1, r, g, b, (float)alpha},
    };
    submit(vtx, 4, GL_TRIANGLE_FAN);
}

void render_draw_glyph_colored(unsigned int tex, double dx, double dy, double dw, double dh,
                               float u0, float v0, float u1, float v1, unsigned int bgr,
                               double alpha) {
    float r = (bgr & 0xFF) / 255.0f;
    float g = ((bgr >> 8) & 0xFF) / 255.0f;
    float b = ((bgr >> 16) & 0xFF) / 255.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    float x0 = (float)dx, y0 = (float)dy, x1 = (float)(dx + dw), y1 = (float)(dy + dh);
    Vtx v[4] = {
        {x0, y0, u0, v0, r, g, b, (float)alpha},
        {x1, y0, u1, v0, r, g, b, (float)alpha},
        {x1, y1, u1, v1, r, g, b, (float)alpha},
        {x0, y1, u0, v1, r, g, b, (float)alpha},
    };
    submit(v, 4, GL_TRIANGLE_FAN);
}

void render_draw_glyphs_colored(unsigned int tex, const GlyphQuad* quads, int count,
                                unsigned int bgr, double alpha) {
    if (count <= 0) return;
    float r = (bgr & 0xFF) / 255.0f;
    float g = ((bgr >> 8) & 0xFF) / 255.0f;
    float b = ((bgr >> 16) & 0xFF) / 255.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    std::vector<Vtx> v;
    v.reserve((size_t)count * 6);
    for (int i = 0; i < count; ++i) {
        const GlyphQuad& q = quads[i];
        float x0 = (float)q.x, y0 = (float)q.y, x1 = (float)(q.x + q.w), y1 = (float)(q.y + q.h);
        Vtx a = {x0, y0, q.u0, q.v0, r, g, b, (float)alpha};
        Vtx bb = {x1, y0, q.u1, q.v0, r, g, b, (float)alpha};
        Vtx c = {x1, y1, q.u1, q.v1, r, g, b, (float)alpha};
        Vtx d = {x0, y1, q.u0, q.v1, r, g, b, (float)alpha};
        v.push_back(a); v.push_back(bb); v.push_back(c);
        v.push_back(a); v.push_back(c); v.push_back(d);
    }
    submit(v.data(), (int)v.size(), GL_TRIANGLES);
}

void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1) {
    render_draw_glyph_colored(tex, dx, dy, dw, dh, u0, v0, u1, v1, g_color_bgr, g_alpha);
}

static Vtx mkv(double x, double y, unsigned int bgr, float alpha) {
    Vtx v;
    v.x = (float)x;
    v.y = (float)y;
    v.u = 0;
    v.v = 0;
    v.r = (bgr & 0xFF) / 255.0f;
    v.g = ((bgr >> 8) & 0xFF) / 255.0f;
    v.b = ((bgr >> 16) & 0xFF) / 255.0f;
    v.a = alpha;
    return v;
}

void render_draw_rectangle(double x1, double y1, double x2, double y2, bool outline) {
    render_draw_rectangle_color(x1, y1, x2, y2, g_color_bgr, g_color_bgr, g_color_bgr, g_color_bgr,
                                outline);
}

void render_draw_rectangle_color(double x1, double y1, double x2, double y2, unsigned int c1,
                                 unsigned int c2, unsigned int c3, unsigned int c4, bool outline) {
    glDisable(GL_TEXTURE_2D);
    if (!outline) {
        x2 += 1;
        y2 += 1;
    }
    Vtx v[4] = {mkv(x1, y1, c1, g_alpha), mkv(x2, y1, c2, g_alpha), mkv(x2, y2, c3, g_alpha),
                mkv(x1, y2, c4, g_alpha)};
    submit(v, 4, outline ? GL_LINE_LOOP : GL_TRIANGLE_FAN);
    glEnable(GL_TEXTURE_2D);
}

void render_draw_line(double x1, double y1, double x2, double y2, double width, unsigned int c1,
                      unsigned int c2) {
    glDisable(GL_TEXTURE_2D);
    if (width <= 1.0) {
        Vtx v[2] = {mkv(x1 + 0.5, y1 + 0.5, c1, g_alpha), mkv(x2 + 0.5, y2 + 0.5, c2, g_alpha)};
        submit(v, 2, GL_LINES);
    } else {
        double dx = x2 - x1, dy = y2 - y1;
        double len = std::hypot(dx, dy);
        if (len < 1e-9) len = 1;
        double nx = -dy / len * width * 0.5, ny = dx / len * width * 0.5;
        Vtx v[4] = {mkv(x1 + nx, y1 + ny, c1, g_alpha), mkv(x1 - nx, y1 - ny, c1, g_alpha),
                    mkv(x2 - nx, y2 - ny, c2, g_alpha), mkv(x2 + nx, y2 + ny, c2, g_alpha)};
        submit(v, 4, GL_TRIANGLE_FAN);
    }
    glEnable(GL_TEXTURE_2D);
}

void render_draw_ellipse(double x1, double y1, double x2, double y2, unsigned int c1,
                         unsigned int c2, bool outline) {
    double cx = (x1 + x2) * 0.5, cy = (y1 + y2) * 0.5;
    double rx = std::fabs(x2 - x1) * 0.5, ry = std::fabs(y2 - y1) * 0.5;
    glDisable(GL_TEXTURE_2D);
    const int seg = 48;
    if (outline) {
        std::vector<Vtx> v;
        v.reserve(seg);
        for (int i = 0; i < seg; ++i) {
            double a = i * 2.0 * 3.14159265358979 / seg;
            v.push_back(mkv(cx + std::cos(a) * rx, cy + std::sin(a) * ry, c2, g_alpha));
        }
        submit(v.data(), (int)v.size(), GL_LINE_LOOP);
    } else {
        std::vector<Vtx> v;
        v.reserve(seg + 2);
        v.push_back(mkv(cx, cy, c1, g_alpha));
        for (int i = 0; i <= seg; ++i) {
            double a = i * 2.0 * 3.14159265358979 / seg;
            v.push_back(mkv(cx + std::cos(a) * rx, cy + std::sin(a) * ry, c2, g_alpha));
        }
        submit(v.data(), (int)v.size(), GL_TRIANGLE_FAN);
    }
    glEnable(GL_TEXTURE_2D);
}

void render_draw_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          unsigned int c1, unsigned int c2, unsigned int c3, bool outline) {
    glDisable(GL_TEXTURE_2D);
    Vtx v[3] = {mkv(x1, y1, c1, g_alpha), mkv(x2, y2, c2, g_alpha), mkv(x3, y3, c3, g_alpha)};
    submit(v, 3, outline ? GL_LINE_LOOP : GL_TRIANGLES);
    glEnable(GL_TEXTURE_2D);
}

void render_draw_point(double x, double y, unsigned int c) {
    glDisable(GL_TEXTURE_2D);
    Vtx v[1] = {mkv(x + 0.5, y + 0.5, c, g_alpha)};
    submit(v, 1, GL_POINTS);
    glEnable(GL_TEXTURE_2D);
}

int render_gui_width() { return g_gui_w; }
int render_gui_height() { return g_gui_h; }

void render_set_window_size(int width, int height) {
    g_win_w = width;
    g_win_h = height;
}
void render_set_fullscreen(bool) {}
bool render_get_fullscreen() { return true; }
void render_center_window() {}

int render_window_width() { return g_win_w; }
int render_window_height() { return g_win_h; }
int render_display_width() { return VITA_W; }
int render_display_height() { return VITA_H; }

void render_set_room(int width, int height, unsigned int) {
    g_room_w = width;
    g_room_h = height;
}

void render_shutdown() { g_inited = false; }

}
