#include "render.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gml {

static GLFWwindow* g_window = nullptr;
static int g_win_w = 640;
static int g_win_h = 480;
static int g_gui_w = 640;
static int g_gui_h = 480;
static int g_room_w = 640;
static int g_room_h = 480;
static double g_view_x = 0, g_view_y = 0, g_view_w = 640, g_view_h = 480;
static bool g_fullscreen = false;
static int g_saved_x = 100, g_saved_y = 100, g_saved_w = 640, g_saved_h = 480;

static float g_color_r = 1.0f, g_color_g = 1.0f, g_color_b = 1.0f;
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
    if (!glGenFramebuffers) return false;
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

bool render_app_snapshot(int x, int y, int w, int h, unsigned char* rgba_out) {
    if (!g_fbo) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    glReadPixels(x, g_fbo_h - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba_out);
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    for (int row = 0; row < h / 2; ++row) {
        for (int col = 0; col < w * 4; ++col) {
            unsigned char tmp = rgba_out[row * w * 4 + col];
            rgba_out[row * w * 4 + col] = rgba_out[(h - 1 - row) * w * 4 + col];
            rgba_out[(h - 1 - row) * w * 4 + col] = tmp;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    return true;
}

static bool key_state(int vk) {
    if (!g_window) return false;
    switch (vk) {
        case 37: return glfwGetKey(g_window, GLFW_KEY_LEFT) == GLFW_PRESS;
        case 38: return glfwGetKey(g_window, GLFW_KEY_UP) == GLFW_PRESS;
        case 39: return glfwGetKey(g_window, GLFW_KEY_RIGHT) == GLFW_PRESS;
        case 40: return glfwGetKey(g_window, GLFW_KEY_DOWN) == GLFW_PRESS;
        case 32: return glfwGetKey(g_window, GLFW_KEY_SPACE) == GLFW_PRESS;
        case 13: return glfwGetKey(g_window, GLFW_KEY_ENTER) == GLFW_PRESS ||
                        glfwGetKey(g_window, GLFW_KEY_KP_ENTER) == GLFW_PRESS;
        case 27: return glfwGetKey(g_window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
        case 16: return glfwGetKey(g_window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                        glfwGetKey(g_window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        case 17: return glfwGetKey(g_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                        glfwGetKey(g_window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        case 18: return glfwGetKey(g_window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                        glfwGetKey(g_window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        case 8: return glfwGetKey(g_window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        case 9: return glfwGetKey(g_window, GLFW_KEY_TAB) == GLFW_PRESS;
        case 46: return glfwGetKey(g_window, GLFW_KEY_DELETE) == GLFW_PRESS;
        case 45: return glfwGetKey(g_window, GLFW_KEY_INSERT) == GLFW_PRESS;
        case 36: return glfwGetKey(g_window, GLFW_KEY_HOME) == GLFW_PRESS;
        case 35: return glfwGetKey(g_window, GLFW_KEY_END) == GLFW_PRESS;
        case 33: return glfwGetKey(g_window, GLFW_KEY_PAGE_UP) == GLFW_PRESS;
        case 34: return glfwGetKey(g_window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS;
        default:
            if (vk >= 'A' && vk <= 'Z') return glfwGetKey(g_window, GLFW_KEY_A + (vk - 'A')) == GLFW_PRESS;
            if (vk >= '0' && vk <= '9') return glfwGetKey(g_window, GLFW_KEY_0 + (vk - '0')) == GLFW_PRESS;
            if (vk >= 112 && vk <= 123) return glfwGetKey(g_window, GLFW_KEY_F1 + (vk - 112)) == GLFW_PRESS;
            if (vk >= 96 && vk <= 105) return glfwGetKey(g_window, GLFW_KEY_KP_0 + (vk - 96)) == GLFW_PRESS;
            return false;
    }
}

bool render_init(const char* title, int width, int height, unsigned int bg_color) {
    if (!glfwInit()) {
        std::fprintf(stderr, "kwik: glfwInit failed\n");
        return false;
    }

    g_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!g_window) {
        std::fprintf(stderr, "kwik: window creation failed\n");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "kwik: glad load failed\n");
        return false;
    }

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
    if (!init_app_surface(width, height))
        std::fprintf(stderr, "kwik: application surface FBO unavailable\n");
    return true;
}

bool render_should_close() {
    return g_window == nullptr || glfwWindowShouldClose(g_window);
}

void render_begin_frame() {
    if (g_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        glViewport(0, 0, g_fbo_w, g_fbo_h);
    } else {
        int fbw = g_win_w, fbh = g_win_h;
        glfwGetFramebufferSize(g_window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
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

void render_set_title(const char* title) {
    if (g_window && title) glfwSetWindowTitle(g_window, title);
}

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
double render_time_ms() { return glfwGetTime() * 1000.0; }

void render_end_frame() {
    if (g_fbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int fbw = g_win_w, fbh = g_win_h;
        glfwGetFramebufferSize(g_window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 1.0, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        double scale = std::min((double)fbw / g_fbo_w, (double)fbh / g_fbo_h);
        double dw = g_fbo_w * scale / fbw;
        double dh = g_fbo_h * scale / fbh;
        float x0 = (float)((1.0 - dw) * 0.5);
        float y0 = (float)((1.0 - dh) * 0.5);
        float x1 = x0 + (float)dw;
        float y1 = y0 + (float)dh;
        glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_fbo_tex);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 1); glVertex2f(x0, y0);
        glTexCoord2f(1, 1); glVertex2f(x1, y0);
        glTexCoord2f(1, 0); glVertex2f(x1, y1);
        glTexCoord2f(0, 0); glVertex2f(x0, y1);
        glEnd();
        glEnable(GL_BLEND);
    }
    glfwSwapBuffers(g_window);
    double now = glfwGetTime();
    g_dt = g_last_time > 0.0 ? now - g_last_time : 0.0;
    if (g_dt > 0.25) g_dt = 0.25;
    g_last_time = now;
    for (int i = 0; i < 512; ++i) g_keys_prev[i] = g_keys_now[i];
    for (int i = 0; i < 3; ++i) g_mouse_prev[i] = g_mouse_now[i];
    glfwPollEvents();
    for (int i = 0; i < 512; ++i) g_keys_now[i] = key_state(i);
    bool any = false;
    for (int i = 2; i < 512; ++i) any = any || g_keys_now[i];
    g_keys_now[1] = any;
    g_keys_now[0] = false;
    for (int i = 0; i < 3; ++i)
        g_mouse_now[i] = glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT + i) == GLFW_PRESS;
}

void render_idle() { glfwPollEvents(); }

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

static void mouse_to_gui(double& gx, double& gy) {
    gx = 0;
    gy = 0;
    if (!g_window) return;
    double mx, my;
    glfwGetCursorPos(g_window, &mx, &my);
    int ww, wh;
    glfwGetWindowSize(g_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    int cw = g_fbo_w > 0 ? g_fbo_w : g_gui_w;
    int ch = g_fbo_h > 0 ? g_fbo_h : g_gui_h;
    double scale = std::min((double)ww / cw, (double)wh / ch);
    if (scale <= 0) return;
    double ox = (ww - cw * scale) * 0.5;
    double oy = (wh - ch * scale) * 0.5;
    gx = (mx - ox) / scale;
    gy = (my - oy) / scale;
}

double render_mouse_x() {
    double gx, gy;
    mouse_to_gui(gx, gy);
    return gx;
}
double render_mouse_y() {
    double gx, gy;
    mouse_to_gui(gx, gy);
    return gy;
}
bool render_mouse_down(int b) { return b >= 0 && b < 3 && g_mouse_now[b]; }
bool render_mouse_pressed(int b) { return b >= 0 && b < 3 && g_mouse_now[b] && !g_mouse_prev[b]; }

void render_set_color(unsigned int bgr) {
    g_color_bgr = bgr;
    g_color_r = static_cast<float>(bgr & 0xFF) / 255.0f;
    g_color_g = static_cast<float>((bgr >> 8) & 0xFF) / 255.0f;
    g_color_b = static_cast<float>((bgr >> 16) & 0xFF) / 255.0f;
}
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
        case 3: glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); break;
        default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    }
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
    glColor4f(r, g, b, (float)alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(vx[0], vy[0]);
    glTexCoord2f(u1, v0); glVertex2f(vx[1], vy[1]);
    glTexCoord2f(u1, v1); glVertex2f(vx[2], vy[2]);
    glTexCoord2f(u0, v1); glVertex2f(vx[3], vy[3]);
    glEnd();
}

void render_draw_glyph_colored(unsigned int tex, double dx, double dy, double dw, double dh,
                               float u0, float v0, float u1, float v1, unsigned int bgr,
                               double alpha) {
    float r = (bgr & 0xFF) / 255.0f;
    float g = ((bgr >> 8) & 0xFF) / 255.0f;
    float b = ((bgr >> 16) & 0xFF) / 255.0f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(r, g, b, (float)alpha);
    float x0 = (float)dx, y0 = (float)dy, x1 = (float)(dx + dw), y1 = (float)(dy + dh);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1, y1);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    glEnd();
}

void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1) {
    render_draw_glyph_colored(tex, dx, dy, dw, dh, u0, v0, u1, v1, g_color_bgr, g_alpha);
}

static void set_col(unsigned int bgr) {
    glColor4f((bgr & 0xFF) / 255.0f, ((bgr >> 8) & 0xFF) / 255.0f, ((bgr >> 16) & 0xFF) / 255.0f,
              g_alpha);
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
    glBegin(outline ? GL_LINE_LOOP : GL_QUADS);
    set_col(c1); glVertex2f((float)x1, (float)y1);
    set_col(c2); glVertex2f((float)x2, (float)y1);
    set_col(c3); glVertex2f((float)x2, (float)y2);
    set_col(c4); glVertex2f((float)x1, (float)y2);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void render_draw_line(double x1, double y1, double x2, double y2, double width, unsigned int c1,
                      unsigned int c2) {
    glDisable(GL_TEXTURE_2D);
    if (width <= 1.0) {
        glBegin(GL_LINES);
        set_col(c1); glVertex2f((float)(x1 + 0.5), (float)(y1 + 0.5));
        set_col(c2); glVertex2f((float)(x2 + 0.5), (float)(y2 + 0.5));
        glEnd();
    } else {
        double dx = x2 - x1, dy = y2 - y1;
        double len = std::hypot(dx, dy);
        if (len < 1e-9) len = 1;
        double nx = -dy / len * width * 0.5, ny = dx / len * width * 0.5;
        glBegin(GL_QUADS);
        set_col(c1); glVertex2f((float)(x1 + nx), (float)(y1 + ny));
        set_col(c1); glVertex2f((float)(x1 - nx), (float)(y1 - ny));
        set_col(c2); glVertex2f((float)(x2 - nx), (float)(y2 - ny));
        set_col(c2); glVertex2f((float)(x2 + nx), (float)(y2 + ny));
        glEnd();
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
        glBegin(GL_LINE_LOOP);
        set_col(c2);
        for (int i = 0; i < seg; ++i) {
            double a = i * 2.0 * 3.14159265358979 / seg;
            glVertex2f((float)(cx + std::cos(a) * rx), (float)(cy + std::sin(a) * ry));
        }
        glEnd();
    } else {
        glBegin(GL_TRIANGLE_FAN);
        set_col(c1);
        glVertex2f((float)cx, (float)cy);
        set_col(c2);
        for (int i = 0; i <= seg; ++i) {
            double a = i * 2.0 * 3.14159265358979 / seg;
            glVertex2f((float)(cx + std::cos(a) * rx), (float)(cy + std::sin(a) * ry));
        }
        glEnd();
    }
    glEnable(GL_TEXTURE_2D);
}

void render_draw_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          unsigned int c1, unsigned int c2, unsigned int c3, bool outline) {
    glDisable(GL_TEXTURE_2D);
    glBegin(outline ? GL_LINE_LOOP : GL_TRIANGLES);
    set_col(c1); glVertex2f((float)x1, (float)y1);
    set_col(c2); glVertex2f((float)x2, (float)y2);
    set_col(c3); glVertex2f((float)x3, (float)y3);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void render_draw_point(double x, double y, unsigned int c) {
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_POINTS);
    set_col(c);
    glVertex2f((float)(x + 0.5), (float)(y + 0.5));
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

int render_gui_width() { return g_gui_w; }
int render_gui_height() { return g_gui_h; }

void render_set_window_size(int width, int height) {
    g_win_w = width;
    g_win_h = height;
    if (g_window && !g_fullscreen) glfwSetWindowSize(g_window, width, height);
}

void render_set_fullscreen(bool fs) {
    if (!g_window || fs == g_fullscreen) return;
    if (fs) {
        glfwGetWindowPos(g_window, &g_saved_x, &g_saved_y);
        glfwGetWindowSize(g_window, &g_saved_w, &g_saved_h);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(g_window, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(g_window, nullptr, g_saved_x, g_saved_y, g_saved_w, g_saved_h, 0);
    }
    g_fullscreen = fs;
}

bool render_get_fullscreen() { return g_fullscreen; }

void render_center_window() {
    if (!g_window || g_fullscreen) return;
    GLFWmonitor* mon = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(mon);
    int ww, wh;
    glfwGetWindowSize(g_window, &ww, &wh);
    glfwSetWindowPos(g_window, (mode->width - ww) / 2, (mode->height - wh) / 2);
}

int render_window_width() {
    int ww = g_win_w, wh;
    if (g_window) glfwGetWindowSize(g_window, &ww, &wh);
    return ww;
}
int render_window_height() {
    int ww, wh = g_win_h;
    if (g_window) glfwGetWindowSize(g_window, &ww, &wh);
    return wh;
}
int render_display_width() {
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    return mode ? mode->width : 1920;
}
int render_display_height() {
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    return mode ? mode->height : 1080;
}

void render_set_room(int width, int height, unsigned int) {
    g_room_w = width;
    g_room_h = height;
}

void render_shutdown() {
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    glfwTerminate();
}

}
