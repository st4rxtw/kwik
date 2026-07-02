#include "gml_runtime.h"
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace gml {

struct LoadedImage {
    unsigned int tex = 0;
    int w = 0;
    int h = 0;
    bool tried = false;
    bool ok = false;
};

static std::vector<uint8_t> g_assets;
static bool g_assets_tried = false;
static std::vector<LoadedImage> g_images;

static uint32_t rd32(size_t o) {
    if (o + 4 > g_assets.size()) return 0;
    return g_assets[o] | (g_assets[o + 1] << 8) | (g_assets[o + 2] << 16) |
           ((uint32_t)g_assets[o + 3] << 24);
}

static void ensure_assets() {
    if (g_assets_tried) return;
    g_assets_tried = true;
    const char* paths[] = {"Assets.dat", "./Assets.dat"};
    for (const char* p : paths) {
        std::FILE* f = std::fopen(p, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            g_assets.resize(n);
            size_t got = std::fread(g_assets.data(), 1, n, f);
            g_assets.resize(got);
        }
        std::fclose(f);
        break;
    }
    g_images.resize(g_image_count);
}

static LoadedImage& load_image(int index) {
    ensure_assets();
    static LoadedImage dummy;
    if (index < 0 || index >= g_image_count) return dummy;
    LoadedImage& img = g_images[index];
    if (img.tried) return img;
    img.tried = true;

    size_t off = rd32((size_t)g_image_count * 2 + (size_t)index * 4);
    if (off == 0 || off + 16 > g_assets.size()) return img;
    uint32_t png_size = rd32(off + 12);
    if (off + 16 + png_size > g_assets.size()) return img;

    int w, h, ch;
    unsigned char* pixels = stbi_load_from_memory(&g_assets[off + 16], png_size, &w, &h, &ch, 4);
    if (!pixels) return img;
    img.tex = render_upload_texture(pixels, w, h);
    img.w = w;
    img.h = h;
    img.ok = true;
    stbi_image_free(pixels);
    return img;
}

static void draw_sprite_full(int spr, int sub, double x, double y, double xscale, double yscale,
                             double angle, double alpha) {
    if (spr < 0 || spr >= g_sprite_count) return;
    const KwikSprite& s = g_sprites[spr];
    if (s.frame_count <= 0) return;
    int frame = s.first_frame + ((sub % s.frame_count) + s.frame_count) % s.frame_count;
    LoadedImage& img = load_image(frame);
    if (!img.ok) return;
    render_draw_sprite(img.tex, x, y, img.w, img.h, s.origin_x, s.origin_y, xscale, yscale, angle,
                       alpha);
}

Value draw_sprite(const Value& sprite, const Value& subimg, const Value& x, const Value& y) {
    draw_sprite_full((int)(double)sprite, (int)(double)subimg, (double)x, (double)y, 1.0, 1.0, 0.0,
                     1.0);
    return Value();
}

static int g_current_font = -1;

void kwik_set_font(int font_id) {
    g_current_font = (font_id >= 0 && font_id < g_font_count) ? font_id : -1;
}

static const KwikGlyph* find_glyph(const KwikFont& f, int ch) {
    for (int i = 0; i < f.glyph_count; ++i) {
        const KwikGlyph& g = g_glyphs[f.glyph_start + i];
        if (g.ch == ch) return &g;
    }
    return nullptr;
}

static double line_width(const KwikFont& f, const std::string& line, size_t start, size_t end) {
    double w = 0;
    for (size_t i = start; i < end; ++i) {
        const KwikGlyph* g = find_glyph(f, (unsigned char)line[i]);
        if (g) w += g->shift;
    }
    return w;
}

bool kwik_draw_text_custom(double x, double y, const std::string& text) {
    if (g_current_font < 0) return false;
    const KwikFont& f = g_fonts[g_current_font];
    LoadedImage& atlas = load_image(f.atlas_image);
    if (!atlas.ok) return false;

    std::vector<std::pair<size_t, size_t>> lines;
    size_t ls = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            size_t le = i;
            if (le > ls && text[le - 1] == '\r') le--;
            lines.push_back({ls, le});
            ls = i + 1;
        }
    }

    int halign = render_get_halign(), valign = render_get_valign();
    double line_h = f.size > 0 ? f.size : 16;
    double total_h = line_h * lines.size();
    double base_y = y;
    if (valign == 1) base_y -= total_h * 0.5;
    else if (valign == 2) base_y -= total_h;

    for (size_t li = 0; li < lines.size(); ++li) {
        double penx = x;
        double w = line_width(f, text, lines[li].first, lines[li].second);
        if (halign == 1) penx -= w * 0.5;
        else if (halign == 2) penx -= w;
        double liney = base_y + li * line_h;
        for (size_t i = lines[li].first; i < lines[li].second; ++i) {
            const KwikGlyph* g = find_glyph(f, (unsigned char)text[i]);
            if (!g) continue;
            if (g->w > 0 && g->h > 0) {
                float u0 = (float)g->x / atlas.w, v0 = (float)g->y / atlas.h;
                float u1 = (float)(g->x + g->w) / atlas.w, v1 = (float)(g->y + g->h) / atlas.h;
                render_draw_glyph(atlas.tex, penx + g->offset, liney, g->w, g->h, u0, v0, u1, v1);
            }
            penx += g->shift;
        }
    }
    return true;
}

Value draw_self(Instance& self) {
    int spr = (int)(double)self.var("sprite_index");
    int sub = (int)(double)self.var("image_index");
    double xs = self.var("image_xscale");
    double ys = self.var("image_yscale");
    double ang = self.var("image_angle");
    double alpha = self.var("image_alpha");
    draw_sprite_full(spr, sub, self.x, self.y, xs, ys, ang, alpha);
    return Value();
}

}
