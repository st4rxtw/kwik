#include "render.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace gml {

static const int kAtlasW = 512;
static const int kAtlasH = 512;
static const float kFontPixelHeight = 24.0f;
static const int kFirstChar = 32;
static const int kCharCount = 96;

static const char* kFontPaths[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
};

static GLFWwindow* g_window = nullptr;
static GLuint g_font_tex = 0;
static stbtt_bakedchar g_baked[kCharCount];
static int g_win_w = 0;
static int g_win_h = 0;
static float g_ascent = kFontPixelHeight;

static float g_color_r = 1.0f;
static float g_color_g = 1.0f;
static float g_color_b = 1.0f;
static float g_alpha = 1.0f;
static int g_halign = 0;
static int g_valign = 0;

static bool g_keys_now[512] = {false};
static bool g_keys_prev[512] = {false};

static int gml_vk_to_glfw(int vk) {
    switch (vk) {
        case 37: return GLFW_KEY_LEFT;
        case 38: return GLFW_KEY_UP;
        case 39: return GLFW_KEY_RIGHT;
        case 40: return GLFW_KEY_DOWN;
        case 32: return GLFW_KEY_SPACE;
        case 13: return GLFW_KEY_ENTER;
        case 27: return GLFW_KEY_ESCAPE;
        case 16: return GLFW_KEY_LEFT_SHIFT;
        case 17: return GLFW_KEY_LEFT_CONTROL;
        case 18: return GLFW_KEY_LEFT_ALT;
        case 8: return GLFW_KEY_BACKSPACE;
        default:
            if (vk >= 'A' && vk <= 'Z') return GLFW_KEY_A + (vk - 'A');
            if (vk >= '0' && vk <= '9') return GLFW_KEY_0 + (vk - '0');
            return -1;
    }
}

static std::vector<unsigned char> read_font() {
    for (const char* path : kFontPaths) {
        std::FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> buf(n > 0 ? static_cast<size_t>(n) : 0);
        if (!buf.empty()) {
            size_t got = std::fread(buf.data(), 1, buf.size(), f);
            buf.resize(got);
        }
        std::fclose(f);
        if (!buf.empty()) return buf;
    }
    return {};
}

static bool build_font_atlas() {
    std::vector<unsigned char> font = read_font();
    if (font.empty()) {
        std::fprintf(stderr, "kwik: no usable font found\n");
        return false;
    }

    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, font.data(), 0)) {
        float scale = stbtt_ScaleForPixelHeight(&info, kFontPixelHeight);
        int ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
        g_ascent = ascent * scale;
    }

    std::vector<unsigned char> bitmap(kAtlasW * kAtlasH);
    int r = stbtt_BakeFontBitmap(font.data(), 0, kFontPixelHeight, bitmap.data(), kAtlasW, kAtlasH,
                                 kFirstChar, kCharCount, g_baked);
    if (r == 0) {
        std::fprintf(stderr, "kwik: font atlas did not fit\n");
        return false;
    }

    glGenTextures(1, &g_font_tex);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kAtlasW, kAtlasH, 0, GL_ALPHA, GL_UNSIGNED_BYTE,
                 bitmap.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return true;
}

static float text_width(const std::string& text) {
    float w = 0.0f;
    for (unsigned char c : text) {
        if (c < kFirstChar || c >= kFirstChar + kCharCount) continue;
        w += g_baked[c - kFirstChar].xadvance;
    }
    return w;
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

    float r = static_cast<float>(bg_color & 0xFF) / 255.0f;
    float g = static_cast<float>((bg_color >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((bg_color >> 16) & 0xFF) / 255.0f;
    glClearColor(r, g, b, 1.0f);

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    return build_font_atlas();
}

bool render_should_close() {
    return g_window == nullptr || glfwWindowShouldClose(g_window);
}

void render_begin_frame() {
    int fbw = g_win_w;
    int fbh = g_win_h;
    glfwGetFramebufferSize(g_window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, g_win_w, g_win_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClear(GL_COLOR_BUFFER_BIT);
}

void render_end_frame() {
    glfwSwapBuffers(g_window);
    for (int i = 0; i < 512; ++i) g_keys_prev[i] = g_keys_now[i];
    glfwPollEvents();
    for (int i = 0; i < 512; ++i) {
        int gk = gml_vk_to_glfw(i);
        g_keys_now[i] = gk >= 0 && glfwGetKey(g_window, gk) == GLFW_PRESS;
    }
}

bool render_key_down(int gml_vk) {
    if (gml_vk < 0 || gml_vk >= 512) return false;
    return g_keys_now[gml_vk];
}

bool render_key_pressed(int gml_vk) {
    if (gml_vk < 0 || gml_vk >= 512) return false;
    return g_keys_now[gml_vk] && !g_keys_prev[gml_vk];
}

void render_set_color(unsigned int bgr) {
    g_color_r = static_cast<float>(bgr & 0xFF) / 255.0f;
    g_color_g = static_cast<float>((bgr >> 8) & 0xFF) / 255.0f;
    g_color_b = static_cast<float>((bgr >> 16) & 0xFF) / 255.0f;
}

void render_set_alpha(double alpha) {
    g_alpha = static_cast<float>(alpha);
}

void render_set_halign(int align) {
    g_halign = align;
}

void render_set_valign(int align) {
    g_valign = align;
}

static void draw_line(const std::string& line, float px, float py) {
    for (unsigned char c : line) {
        if (c < kFirstChar || c >= kFirstChar + kCharCount) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_baked, kAtlasW, kAtlasH, c - kFirstChar, &px, &py, &q, 1);
        glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
    }
}

void render_draw_text(double x, double y, const std::string& text) {
    if (!g_font_tex) return;

    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    lines.push_back(cur);

    float line_height = kFontPixelHeight;
    float total_height = line_height * static_cast<float>(lines.size());

    float base_y = static_cast<float>(y) + g_ascent;
    if (g_valign == 1)
        base_y -= total_height * 0.5f;
    else if (g_valign == 2)
        base_y -= total_height;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glColor4f(g_color_r, g_color_g, g_color_b, g_alpha);

    glBegin(GL_QUADS);
    for (size_t i = 0; i < lines.size(); ++i) {
        float px = static_cast<float>(x);
        if (g_halign == 1)
            px -= text_width(lines[i]) * 0.5f;
        else if (g_halign == 2)
            px -= text_width(lines[i]);
        draw_line(lines[i], px, base_y + static_cast<float>(i) * line_height);
    }
    glEnd();
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

void render_draw_sprite(unsigned int tex, double x, double y, double w, double h,
                        double origin_x, double origin_y, double xscale, double yscale,
                        double angle_deg, double alpha) {
    double rad = angle_deg * 3.14159265358979323846 / 180.0;
    double c = std::cos(rad), s = std::sin(rad);
    double lx[4] = {0.0, w, w, 0.0};
    double ly[4] = {0.0, 0.0, h, h};
    float vx[4], vy[4];
    for (int i = 0; i < 4; ++i) {
        double px = (lx[i] - origin_x) * xscale;
        double py = (ly[i] - origin_y) * yscale;
        vx[i] = (float)(x + px * c + py * s);
        vy[i] = (float)(y - px * s + py * c);
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1.0f, 1.0f, 1.0f, (float)alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(vx[0], vy[0]);
    glTexCoord2f(1, 0); glVertex2f(vx[1], vy[1]);
    glTexCoord2f(1, 1); glVertex2f(vx[2], vy[2]);
    glTexCoord2f(0, 1); glVertex2f(vx[3], vy[3]);
    glEnd();
}

void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(g_color_r, g_color_g, g_color_b, g_alpha);
    float x0 = (float)dx, y0 = (float)dy, x1 = (float)(dx + dw), y1 = (float)(dy + dh);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1, y1);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1);
    glEnd();
}

int render_get_halign() { return g_halign; }
int render_get_valign() { return g_valign; }

void render_draw_rectangle(double x1, double y1, double x2, double y2, bool outline) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(g_color_r, g_color_g, g_color_b, g_alpha);

    glBegin(outline ? GL_LINE_LOOP : GL_QUADS);
    glVertex2f(static_cast<float>(x1), static_cast<float>(y1));
    glVertex2f(static_cast<float>(x2), static_cast<float>(y1));
    glVertex2f(static_cast<float>(x2), static_cast<float>(y2));
    glVertex2f(static_cast<float>(x1), static_cast<float>(y2));
    glEnd();

    glEnable(GL_TEXTURE_2D);
}

int render_gui_width() {
    return g_win_w;
}

int render_gui_height() {
    return g_win_h;
}

void render_set_window_size(int width, int height) {
    g_win_w = width;
    g_win_h = height;
    if (g_window) glfwSetWindowSize(g_window, width, height);
}

void render_shutdown() {
    if (g_font_tex) {
        glDeleteTextures(1, &g_font_tex);
        g_font_tex = 0;
    }
    if (g_window) {
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    glfwTerminate();
}

}
