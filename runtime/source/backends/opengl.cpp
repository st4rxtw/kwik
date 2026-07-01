#include "render.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

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
    glfwPollEvents();
}

void render_draw_text(double x, double y, const std::string& text) {
    if (!g_font_tex) return;

    glBindTexture(GL_TEXTURE_2D, g_font_tex);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    float px = static_cast<float>(x);
    float py = static_cast<float>(y) + kFontPixelHeight;

    glBegin(GL_QUADS);
    for (unsigned char c : text) {
        if (c < kFirstChar || c >= kFirstChar + kCharCount) continue;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_baked, kAtlasW, kAtlasH, c - kFirstChar, &px, &py, &q, 1);
        glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
        glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
        glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
        glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
    }
    glEnd();
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
