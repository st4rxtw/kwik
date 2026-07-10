#include "gml_runtime.h"
#include "engine_internal.h"
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef _WIN32
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

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
    const char* path = g_assets_path.empty() ? "Assets.dat" : g_assets_path.c_str();
    std::FILE* f = std::fopen(path, "rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            g_assets.resize(n);
            size_t got = std::fread(g_assets.data(), 1, n, f);
            g_assets.resize(got);
        }
        std::fclose(f);
    } else {
        std::fprintf(stderr, "[kwik] could not open %s\n", path);
    }
    g_images.resize(g_image_count);
}

const unsigned char* kwik_sound_blob(int blob_index, unsigned int& size, int& type) {
    ensure_assets();
    size = 0;
    type = 0;
    if (blob_index < 0) return nullptr;
    size_t off = rd32((size_t)g_image_count * 2 + (size_t)(g_image_count + blob_index) * 4);
    if (off == 0 || off + 8 > g_assets.size()) return nullptr;
    type = (int)rd32(off);
    uint32_t sz = rd32(off + 4);
    if (off + 8 + sz > g_assets.size()) return nullptr;
    size = sz;
    return &g_assets[off + 8];
}

static std::vector<KwikSprite> g_dyn_sprites;
static std::vector<std::string> g_dyn_sprite_names;

static std::unordered_map<int, KwikSprite> g_sprite_overrides;

const KwikSprite* kwik_sprite_at(int idx) {
    auto ov = g_sprite_overrides.find(idx);
    if (ov != g_sprite_overrides.end()) return &ov->second;
    if (idx >= 0 && idx < g_sprite_count) return &g_sprites[idx];
    int d = idx - g_sprite_count;
    if (d >= 0 && d < (int)g_dyn_sprites.size()) return &g_dyn_sprites[d];
    return nullptr;
}

static KwikSprite* sprite_mutable(int idx) {
    int d = idx - g_sprite_count;
    if (d >= 0 && d < (int)g_dyn_sprites.size()) return &g_dyn_sprites[d];
    const KwikSprite* s = kwik_sprite_at(idx);
    if (!s) return nullptr;
    auto it = g_sprite_overrides.find(idx);
    if (it == g_sprite_overrides.end()) it = g_sprite_overrides.insert({idx, *s}).first;
    return &it->second;
}

void kwik_sprite_override_bbox(int spr, int l, int t, int r, int b) {
    KwikSprite* s = sprite_mutable(spr);
    if (!s) return;
    s->bbox_left = l;
    s->bbox_top = t;
    s->bbox_right = r;
    s->bbox_bottom = b;
}

void kwik_sprite_override_offset(int spr, int ox, int oy) {
    KwikSprite* s = sprite_mutable(spr);
    if (!s) return;
    s->origin_x = ox;
    s->origin_y = oy;
}

int kwik_sprite_total() { return g_sprite_count + (int)g_dyn_sprites.size(); }

int kwik_register_dynamic_sprite(const KwikSprite& s) {
    g_dyn_sprites.push_back(s);
    g_dyn_sprite_names.push_back(s.name ? s.name : "dyn_sprite");
    g_dyn_sprites.back().name = g_dyn_sprite_names.back().c_str();
    return g_sprite_count + (int)g_dyn_sprites.size() - 1;
}

int kwik_register_dynamic_image(unsigned int tex, int w, int h) {
    ensure_assets();
    LoadedImage img;
    img.tex = tex;
    img.w = w;
    img.h = h;
    img.tried = true;
    img.ok = tex != 0;
    g_images.push_back(img);
    return (int)g_images.size() - 1;
}

static LoadedImage& load_image(int index) {
    ensure_assets();
    static LoadedImage dummy;
    if (index < 0 || index >= (int)g_images.size()) return dummy;
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
    img.ok = img.tex != 0;
    stbi_image_free(pixels);
    if (!img.ok)
        std::fprintf(stderr, "[kwik] texture upload failed for image %d (%dx%d)\n", index, w, h);
    return img;
}

const MaskSet* kwik_sprite_masks(int spr) {
    static std::unordered_map<int, MaskSet> cache;
    auto it = cache.find(spr);
    if (it != cache.end()) return it->second.count > 0 ? &it->second : nullptr;
    MaskSet ms;
    const KwikSprite* s = kwik_sprite_at(spr);
    if (s && s->sep_masks == 1 && s->mask_blob >= 0) {
        unsigned int size = 0;
        int type = 0;
        const unsigned char* d = kwik_sound_blob(s->mask_blob, size, type);
        if (d && type == 4 && size >= 12) {
            auto r32 = [&](int o) {
                return (unsigned)d[o] | ((unsigned)d[o + 1] << 8) | ((unsigned)d[o + 2] << 16) |
                       ((unsigned)d[o + 3] << 24);
            };
            ms.count = (int)r32(0);
            ms.w = (int)r32(4);
            ms.h = (int)r32(8);
            ms.rowbytes = (ms.w + 7) / 8;
            if (ms.count > 0 && ms.w > 0 && ms.h > 0 &&
                12 + (size_t)ms.count * ms.rowbytes * ms.h <= size)
                ms.data = d + 12;
            else
                ms.count = 0;
        }
    }
    auto& slot = cache[spr];
    slot = ms;
    return slot.count > 0 ? &slot : nullptr;
}

int kwik_sprite_frame_image(int spr, int sub) {
    const KwikSprite* s = kwik_sprite_at(spr);
    if (!s || s->frame_count <= 0) return -1;
    return s->first_frame + ((sub % s->frame_count) + s->frame_count) % s->frame_count;
}

unsigned int kwik_image_texture(int image, int& w, int& h) {
    LoadedImage& img = load_image(image);
    w = img.w;
    h = img.h;
    return img.ok ? img.tex : 0;
}

void kwik_draw_image_part(int image, double sx, double sy, double sw, double sh, double dx,
                          double dy, double xs, double ys, unsigned int blend, double alpha) {
    LoadedImage& img = load_image(image);
    if (!img.ok || img.w <= 0 || img.h <= 0) return;
    float u0 = (float)(sx / img.w), v0 = (float)(sy / img.h);
    float u1 = (float)((sx + sw) / img.w), v1 = (float)((sy + sh) / img.h);
    render_draw_quad(img.tex, dx, dy, sw, sh, 0, 0, xs, ys, 0, u0, v0, u1, v1, blend, alpha);
}

uint32_t kwik_tileset_frame_index(const KwikTileset& ts, uint32_t idx) {
    if (ts.map_blob < 0 || ts.tile_count <= 0 || idx >= (uint32_t)ts.tile_count) return idx;
    int frames = ts.frames > 0 ? ts.frames : 1;
    const uint32_t* map = kwik_tilemap_grid(ts.map_blob, ts.tile_count * frames);
    if (!map) return idx;
    int fr = 0;
    if (frames > 1 && ts.frame_ms > 0)
        fr = (int)((long long)(now_ms() / ts.frame_ms) % frames);
    return map[idx * frames + fr];
}

void kwik_draw_image_part_rot(int image, double sx, double sy, double sw, double sh, double dx,
                              double dy, double ox, double oy, double xs, double ys, double angle,
                              unsigned int blend, double alpha) {
    LoadedImage& img = load_image(image);
    if (!img.ok || img.w <= 0 || img.h <= 0) return;
    float u0 = (float)(sx / img.w), v0 = (float)(sy / img.h);
    float u1 = (float)((sx + sw) / img.w), v1 = (float)((sy + sh) / img.h);
    render_draw_quad(img.tex, dx, dy, sw, sh, ox, oy, xs, ys, angle, u0, v0, u1, v1, blend,
                     alpha);
}

static std::unordered_map<int, std::vector<uint32_t>> g_tilemap_cache;

const uint32_t* kwik_tilemap_grid(int blob, int cells) {
    auto it = g_tilemap_cache.find(blob);
    if (it != g_tilemap_cache.end()) return it->second.empty() ? nullptr : it->second.data();
    std::vector<uint32_t>& g = g_tilemap_cache[blob];
    unsigned int size = 0;
    int type = 0;
    const unsigned char* d = kwik_sound_blob(blob, size, type);
    if (d && type == 6 && (int)(size / 4) >= cells) {
        g.resize(cells);
        for (int i = 0; i < cells; ++i)
            g[i] = (unsigned)d[i * 4] | ((unsigned)d[i * 4 + 1] << 8) |
                   ((unsigned)d[i * 4 + 2] << 16) | ((unsigned)d[i * 4 + 3] << 24);
    }
    return g.empty() ? nullptr : g.data();
}

uint32_t* kwik_tilemap_grid_mut(int blob, int cells) {
    kwik_tilemap_grid(blob, cells);
    auto it = g_tilemap_cache.find(blob);
    if (it == g_tilemap_cache.end() || it->second.empty()) return nullptr;
    return it->second.data();
}

int kwik_sprite_add_file(const std::string& path, int imgnum, int xorig, int yorig) {
    (void)imgnum;
    std::FILE* f = std::fopen(kwik_resolve_read(path).c_str(), "rb");
    if (!f) return -1;
    std::vector<unsigned char> bytes;
    char tmp[8192];
    size_t n;
    while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) bytes.insert(bytes.end(), tmp, tmp + n);
    std::fclose(f);
    int w, h, ch;
    unsigned char* pixels =
        stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &ch, 4);
    if (!pixels) return -1;
    unsigned int tex = render_upload_texture(pixels, w, h);
    stbi_image_free(pixels);
    int img = kwik_register_dynamic_image(tex, w, h);
    KwikSprite s{};
    s.first_frame = img;
    s.frame_count = 1;
    s.origin_x = xorig;
    s.origin_y = yorig;
    s.speed = 1;
    s.speed_type = 1;
    s.bbox_left = 0;
    s.bbox_top = 0;
    s.bbox_right = w - 1;
    s.bbox_bottom = h - 1;
    s.width = w;
    s.height = h;
    s.sep_masks = 0;
    s.mask_blob = -1;
    s.name = "dyn_sprite";
    return kwik_register_dynamic_sprite(s);
}

static int frame_of(int spr, int sub, const KwikSprite** out) {
    const KwikSprite* s = kwik_sprite_at(spr);
    if (!s || s->frame_count <= 0) return -1;
    *out = s;
    return s->first_frame + ((sub % s->frame_count) + s->frame_count) % s->frame_count;
}

bool kwik_sprite_size(int spr, int& w, int& h) {
    const KwikSprite* s = kwik_sprite_at(spr);
    if (!s) return false;
    w = s->width;
    h = s->height;
    return true;
}

void kwik_draw_sprite_general(int spr, int sub, double x, double y, double xs, double ys,
                              double angle, unsigned int blend, double alpha) {
    const KwikSprite* s;
    int frame = frame_of(spr, sub, &s);
    if (frame < 0) return;
    LoadedImage& img = load_image(frame);
    if (!img.ok) return;
    if (s->tile_repeat && angle == 0 && img.w > 0 && img.h > 0 &&
        (std::fabs(xs) != 1.0 || std::fabs(ys) != 1.0)) {
        double totw = img.w * std::fabs(xs), toth = img.h * std::fabs(ys);
        double ox = x - s->origin_x * std::fabs(xs);
        double oy = y - s->origin_y * std::fabs(ys);
        for (double py = 0; py < toth; py += img.h) {
            double ch = std::min((double)img.h, toth - py);
            for (double px = 0; px < totw; px += img.w) {
                double cw = std::min((double)img.w, totw - px);
                float u1 = (float)(cw / img.w), v1 = (float)(ch / img.h);
                render_draw_quad(img.tex, ox + px, oy + py, cw, ch, 0, 0, 1, 1, 0, 0, 0, u1, v1,
                                 blend, alpha);
            }
        }
        return;
    }
    render_draw_quad(img.tex, x, y, img.w, img.h, s->origin_x, s->origin_y, xs, ys, angle, 0, 0, 1,
                     1, blend, alpha);
}

void kwik_draw_sprite_part(int spr, int sub, double left, double top, double w, double h,
                           double x, double y, double xs, double ys, unsigned int blend,
                           double alpha) {
    const KwikSprite* s;
    int frame = frame_of(spr, sub, &s);
    if (frame < 0) return;
    LoadedImage& img = load_image(frame);
    if (!img.ok || img.w <= 0 || img.h <= 0) return;
    if (left < 0) { w += left; x -= left * xs; left = 0; }
    if (top < 0) { h += top; y -= top * ys; top = 0; }
    if (left + w > img.w) w = img.w - left;
    if (top + h > img.h) h = img.h - top;
    if (w <= 0 || h <= 0) return;
    float u0 = (float)(left / img.w), v0 = (float)(top / img.h);
    float u1 = (float)((left + w) / img.w), v1 = (float)((top + h) / img.h);
    render_draw_quad(img.tex, x, y, w, h, 0, 0, xs, ys, 0, u0, v0, u1, v1, blend, alpha);
}

void kwik_draw_sprite_stretched(int spr, int sub, double x, double y, double w, double h,
                                unsigned int blend, double alpha) {
    const KwikSprite* s;
    int frame = frame_of(spr, sub, &s);
    if (frame < 0) return;
    LoadedImage& img = load_image(frame);
    if (!img.ok || img.w <= 0 || img.h <= 0) return;
    render_draw_quad(img.tex, x, y, w, h, 0, 0, 1, 1, 0, 0, 0, 1, 1, blend, alpha);
}

void kwik_draw_sprite_tiled(int spr, int sub, double x, double y, double xs, double ys,
                            unsigned int blend, double alpha) {
    const KwikSprite* s;
    int frame = frame_of(spr, sub, &s);
    if (frame < 0) return;
    LoadedImage& img = load_image(frame);
    if (!img.ok || img.w <= 0 || img.h <= 0) return;
    double tw = img.w * xs, th = img.h * ys;
    if (tw <= 0.01 || th <= 0.01) return;
    Camera& cam = g_cameras[g_view_camera[0]];
    double vx1 = cam.x + cam.w, vy1 = cam.y + cam.h;
    double sx = x - std::ceil((x - cam.x) / tw) * tw;
    double sy = y - std::ceil((y - cam.y) / th) * th;
    for (double py = sy; py < vy1; py += th)
        for (double px = sx; px < vx1; px += tw)
            render_draw_quad(img.tex, px, py, img.w, img.h, 0, 0, xs, ys, 0, 0, 0, 1, 1, blend,
                             alpha);
}

void draw_self_instance(Instance* inst) {
    if (!inst) return;
    auto gv = [&](const char* n, double d) {
        auto it = inst->vars.find(n);
        return it == inst->vars.end() ? d : (double)it->second;
    };
    int spr = (int)gv("sprite_index", -1);
    if (spr < 0) return;
    kwik_draw_sprite_general(spr, (int)gv("image_index", 0), inst->x, inst->y,
                             gv("image_xscale", 1), gv("image_yscale", 1), gv("image_angle", 0),
                             (unsigned int)gv("image_blend", 16777215), gv("image_alpha", 1));
}

struct RtGlyph {
    int ch;
    int image;
    float u0, v0, u1, v1;
    double w, h;
    double shift, offset;
};

struct RtFont {
    std::vector<RtGlyph> glyphs;
    double line_height = 16;
};

static std::vector<RtFont> g_rt_fonts;
static std::vector<int> g_asset_font_map;
static int g_cur_font = -1;
static bool g_fonts_built = false;

static void build_fonts() {
    if (g_fonts_built) return;
    g_fonts_built = true;
    g_asset_font_map.resize(g_font_count, -1);
    for (int f = 0; f < g_font_count; ++f) {
        const KwikFont& kf = g_fonts[f];
        RtFont rf;
        LoadedImage& atlas = load_image(kf.atlas_image);
        double maxh = 1;
        for (int gi = 0; gi < kf.glyph_count; ++gi) {
            const KwikGlyph& g = g_glyphs[kf.glyph_start + gi];
            RtGlyph rg;
            rg.ch = g.ch;
            rg.image = kf.atlas_image;
            if (atlas.ok && atlas.w > 0 && atlas.h > 0) {
                rg.u0 = (float)g.x / atlas.w;
                rg.v0 = (float)g.y / atlas.h;
                rg.u1 = (float)(g.x + g.w) / atlas.w;
                rg.v1 = (float)(g.y + g.h) / atlas.h;
            } else {
                rg.u0 = rg.v0 = 0;
                rg.u1 = rg.v1 = 1;
            }
            rg.w = g.w;
            rg.h = g.h;
            rg.shift = g.shift;
            rg.offset = g.offset;
            if (g.h > maxh) maxh = g.h;
            rf.glyphs.push_back(rg);
        }
        rf.line_height = kf.size > 0 ? kf.size : maxh;
        g_asset_font_map[f] = (int)g_rt_fonts.size();
        g_rt_fonts.push_back(std::move(rf));
    }
}

int kwik_font_for_asset(int font_asset) {
    build_fonts();
    if (font_asset < 0 || font_asset >= (int)g_asset_font_map.size()) return -1;
    return g_asset_font_map[font_asset];
}

int kwik_font_add_sprite(int spr, const std::string& mapping, bool prop, int sep) {
    build_fonts();
    const KwikSprite* sp = kwik_sprite_at(spr);
    if (!sp) return -1;
    const KwikSprite& s = *sp;
    RtFont rf;
    double maxh = 1;
    for (size_t i = 0; i < mapping.size() && (int)i < s.frame_count; ++i) {
        int frame = s.first_frame + (int)i;
        LoadedImage& img = load_image(frame);
        RtGlyph rg;
        rg.ch = (unsigned char)mapping[i];
        rg.image = frame;
        rg.u0 = rg.v0 = 0;
        rg.u1 = rg.v1 = 1;
        rg.w = img.ok ? img.w : s.width;
        rg.h = img.ok ? img.h : s.height;
        rg.shift = (prop ? rg.w : (double)s.width) + sep;
        rg.offset = 0;
        if (rg.h > maxh) maxh = rg.h;
        rf.glyphs.push_back(rg);
    }
    rf.line_height = maxh;
    g_rt_fonts.push_back(std::move(rf));
    return (int)g_rt_fonts.size() - 1;
}

void kwik_set_font_rt(int rt_font) {
    build_fonts();
    g_cur_font = (rt_font >= 0 && rt_font < (int)g_rt_fonts.size()) ? rt_font : -1;
}

int kwik_get_font_rt() { return g_cur_font; }

static const RtGlyph* find_glyph(const RtFont& f, int ch) {
    for (const RtGlyph& g : f.glyphs)
        if (g.ch == ch) return &g;
    return nullptr;
}

static double line_width(const RtFont& f, const std::string& text, size_t a, size_t b) {
    double w = 0;
    for (size_t i = a; i < b; ++i) {
        const RtGlyph* g = find_glyph(f, (unsigned char)text[i]);
        if (g) w += g->shift;
    }
    return w;
}

static void split_lines(const std::string& text, std::vector<std::pair<size_t, size_t>>& lines) {
    size_t ls = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            size_t le = i;
            if (le > ls && text[le - 1] == '\r') le--;
            lines.push_back({ls, le});
            ls = i + 1;
        }
    }
}

double kwik_string_width(const std::string& s) {
    build_fonts();
    if (g_cur_font < 0) return 0;
    const RtFont& f = g_rt_fonts[g_cur_font];
    std::vector<std::pair<size_t, size_t>> lines;
    split_lines(s, lines);
    double best = 0;
    for (auto& ln : lines) best = std::max(best, line_width(f, s, ln.first, ln.second));
    return best;
}

double kwik_string_height(const std::string& s) {
    build_fonts();
    if (g_cur_font < 0) return 0;
    const RtFont& f = g_rt_fonts[g_cur_font];
    std::vector<std::pair<size_t, size_t>> lines;
    split_lines(s, lines);
    return f.line_height * (double)lines.size();
}

void kwik_draw_text_ext_rt(double x, double y, const std::string& text, double sep, double wrapw,
                           double xs, double ys, double angle) {
    build_fonts();
    if (g_cur_font < 0) return;
    const RtFont& f = g_rt_fonts[g_cur_font];
    std::string wrapped;
    if (wrapw > 0) {
        double linew = 0;
        size_t last_space = std::string::npos;
        double width_at_space = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            wrapped.push_back(c);
            if (c == '\n') {
                linew = 0;
                last_space = std::string::npos;
                continue;
            }
            const RtGlyph* g = find_glyph(f, (unsigned char)c);
            double adv = g ? g->shift : 0;
            if (c == ' ') {
                last_space = wrapped.size() - 1;
                width_at_space = linew;
            }
            linew += adv;
            if (linew > wrapw && last_space != std::string::npos) {
                wrapped[last_space] = '\n';
                linew -= width_at_space + (g && text[last_space] ? 0 : 0);
                linew = linew - width_at_space;
                last_space = std::string::npos;
            }
        }
    } else {
        wrapped = text;
    }
    double saved = -1;
    if (sep > 0) {
        saved = g_rt_fonts[g_cur_font].line_height;
        g_rt_fonts[g_cur_font].line_height = sep;
    }
    kwik_draw_text_rt(x, y, wrapped, xs, ys, angle);
    if (saved >= 0) g_rt_fonts[g_cur_font].line_height = saved;
}

void kwik_draw_text_rt(double x, double y, const std::string& text, double xs, double ys,
                       double angle) {
    build_fonts();
    static int dbg_left = std::getenv("KWIK_DEBUG_TEXT") ? 40 : 0;
    if (dbg_left > 0 && !text.empty()) {
        --dbg_left;
        int found = 0;
        if (g_cur_font >= 0)
            for (char c : text)
                if (find_glyph(g_rt_fonts[g_cur_font], (unsigned char)c)) ++found;
        std::fprintf(stderr, "[text] font=%d at(%.0f,%.0f) glyphs=%d/%zu \"%.40s\"\n", g_cur_font,
                     x, y, found, text.size(), text.c_str());
    }
    if (g_cur_font < 0) return;
    const RtFont& f = g_rt_fonts[g_cur_font];
    std::vector<std::pair<size_t, size_t>> lines;
    split_lines(text, lines);

    int halign = render_get_halign(), valign = render_get_valign();
    double line_h = f.line_height * ys;
    double total_h = line_h * (double)lines.size();
    double oy = 0;
    if (valign == 1) oy -= total_h * 0.5;
    else if (valign == 2) oy -= total_h;

    double rad = angle * 3.14159265358979 / 180.0;
    double ca = std::cos(rad), sa = std::sin(rad);
    unsigned int col = render_get_color();
    double alpha = render_get_alpha();

    unsigned int batch_tex = 0;
    std::vector<GlyphQuad> batch;
    auto flush_batch = [&]() {
        if (!batch.empty()) render_draw_glyphs_colored(batch_tex, batch.data(),
                                                        (int)batch.size(), col, alpha);
        batch.clear();
    };

    for (size_t li = 0; li < lines.size(); ++li) {
        double ox = 0;
        double w = line_width(f, text, lines[li].first, lines[li].second) * xs;
        if (halign == 1) ox -= w * 0.5;
        else if (halign == 2) ox -= w;
        double liney = oy + (double)li * line_h;
        double pen = ox;
        for (size_t i = lines[li].first; i < lines[li].second; ++i) {
            const RtGlyph* g = find_glyph(f, (unsigned char)text[i]);
            if (!g) continue;
            if (g->w > 0 && g->h > 0) {
                LoadedImage& img = load_image(g->image);
                if (img.ok) {
                    double lx = pen + g->offset * xs;
                    double ly = liney;
                    if (angle == 0.0) {
                        if (img.tex != batch_tex) flush_batch();
                        batch_tex = img.tex;
                        batch.push_back({x + lx, y + ly, g->w * xs, g->h * ys, g->u0, g->v0, g->u1,
                                        g->v1});
                    } else {
                        double gx = x + lx * ca + ly * sa;
                        double gy = y - lx * sa + ly * ca;
                        render_draw_quad(img.tex, gx, gy, g->w, g->h, 0, 0, xs, ys, angle, g->u0,
                                         g->v0, g->u1, g->v1, col, alpha);
                    }
                }
            }
            pen += g->shift * xs;
        }
    }
    flush_batch();
}

}
