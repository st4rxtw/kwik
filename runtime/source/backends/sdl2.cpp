#include "render.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace gml {

static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static bool g_quit = false;
static int g_win_w = 640;
static int g_win_h = 480;
static int g_gui_w = 640;
static int g_gui_h = 480;
static int g_room_w = 640;
static int g_room_h = 480;
static double g_view_x = 0, g_view_y = 0, g_view_w = 640, g_view_h = 480;
static bool g_fog_on = false;
static unsigned char g_fog_col[3] = {0, 0, 0};
static bool g_fullscreen = false;
static int g_saved_x = 100, g_saved_y = 100, g_saved_w = 640, g_saved_h = 480;

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

static double g_wheel_accum = 0.0;
static double g_wheel_frame = 0.0;

struct RtTexture {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    bool target = false;
    bool alive = false;
};
static std::vector<RtTexture> g_textures;

static RtTexture* tex_of(unsigned int id) {
    if (id == 0 || (size_t)id > g_textures.size()) return nullptr;
    RtTexture* t = &g_textures[id - 1];
    return t->alive ? t : nullptr;
}

static unsigned int tex_register(SDL_Texture* tex, int w, int h, bool target) {
    RtTexture rt;
    rt.tex = tex;
    rt.w = w;
    rt.h = h;
    rt.target = target;
    rt.alive = true;
    for (size_t i = 0; i < g_textures.size(); ++i)
        if (!g_textures[i].alive) {
            g_textures[i] = rt;
            return (unsigned int)i + 1;
        }
    g_textures.push_back(rt);
    return (unsigned int)g_textures.size();
}

struct RtSurface {
    unsigned int tex_id = 0;
    int w = 0, h = 0;
    bool alive = false;
};
static std::vector<RtSurface> g_surfaces;
static std::vector<int> g_target_stack;

static unsigned int g_app_tex = 0;
static int g_fbo_w = 0;
static int g_fbo_h = 0;

struct ViewXf {
    double ox = 0, oy = 0;
    double sx = 1, sy = 1;
};
static ViewXf g_xf;
static std::vector<ViewXf> g_xf_stack;

static float tx(double x) { return (float)((x - g_xf.ox) * g_xf.sx); }
static float ty(double y) { return (float)((y - g_xf.oy) * g_xf.sy); }

static int g_blend_src = 5, g_blend_dst = 6, g_blend_asrc = 5, g_blend_adst = 6;
static bool g_colormask[4] = {true, true, true, true};

static SDL_BlendFactor gm_blend_factor(int f) {
    switch (f) {
        case 1: return SDL_BLENDFACTOR_ZERO;
        case 2: return SDL_BLENDFACTOR_ONE;
        case 3: return SDL_BLENDFACTOR_SRC_COLOR;
        case 4: return SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case 5: return SDL_BLENDFACTOR_SRC_ALPHA;
        case 6: return SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case 7: return SDL_BLENDFACTOR_DST_ALPHA;
        case 8: return SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
        case 9: return SDL_BLENDFACTOR_DST_COLOR;
        case 10: return SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case 11: return SDL_BLENDFACTOR_ONE;
        default: return SDL_BLENDFACTOR_ONE;
    }
}

static SDL_BlendMode current_blendmode() {
    SDL_BlendFactor cs = gm_blend_factor(g_blend_src);
    SDL_BlendFactor cd = gm_blend_factor(g_blend_dst);
    SDL_BlendFactor as = gm_blend_factor(g_blend_asrc);
    SDL_BlendFactor ad = gm_blend_factor(g_blend_adst);
    bool rgb = g_colormask[0] || g_colormask[1] || g_colormask[2];
    if (!rgb) {
        cs = SDL_BLENDFACTOR_ZERO;
        cd = SDL_BLENDFACTOR_ONE;
    }
    if (!g_colormask[3]) {
        as = SDL_BLENDFACTOR_ZERO;
        ad = SDL_BLENDFACTOR_ONE;
    }
    return SDL_ComposeCustomBlendMode(cs, cd, SDL_BLENDOPERATION_ADD, as, ad,
                                      SDL_BLENDOPERATION_ADD);
}

static double time_seconds() {
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
}

static void pump_events() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: g_quit = true; break;
            case SDL_MOUSEWHEEL:
#if SDL_VERSION_ATLEAST(2, 0, 18)
                g_wheel_accum += ev.wheel.preciseY;
#else
                g_wheel_accum += ev.wheel.y;
#endif
                break;
            default: break;
        }
    }
}

bool render_app_surface_available() { return g_app_tex != 0; }
unsigned int render_app_texture() { return g_app_tex; }
int render_app_width() { return g_fbo_w > 0 ? g_fbo_w : g_gui_w; }
int render_app_height() { return g_fbo_h > 0 ? g_fbo_h : g_gui_h; }

static unsigned int create_target_texture(int w, int h) {
    SDL_Texture* tex =
        SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) return 0;
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_Texture* prev = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, tex);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 0);
    SDL_RenderClear(g_renderer);
    SDL_SetRenderTarget(g_renderer, prev);
    return tex_register(tex, w, h, true);
}

int render_surface_create(int w, int h) {
    if (!g_renderer || w <= 0 || h <= 0) return -1;
    unsigned int tid = create_target_texture(w, h);
    if (!tid) return -1;
    RtSurface sf;
    sf.tex_id = tid;
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
    if (id == 0) return g_app_tex != 0;
    return surf_of(id) != nullptr;
}

void render_surface_free(int id) {
    RtSurface* sf = surf_of(id);
    if (!sf) return;
    RtTexture* t = tex_of(sf->tex_id);
    if (t) {
        SDL_DestroyTexture(t->tex);
        t->alive = false;
        t->tex = nullptr;
    }
    sf->alive = false;
}

unsigned int render_surface_texture(int id) {
    if (id == 0) return g_app_tex;
    RtSurface* sf = surf_of(id);
    return sf ? sf->tex_id : 0;
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

static void set_surface_ortho(int w, int h) {
    g_xf.ox = 0;
    g_xf.oy = 0;
    g_xf.sx = 1;
    g_xf.sy = 1;
    (void)w;
    (void)h;
}

bool render_surface_set_target(int id) {
    RtSurface* sf = surf_of(id);
    unsigned int tid = id == 0 ? g_app_tex : (sf ? sf->tex_id : 0);
    RtTexture* t = tex_of(tid);
    if (!t) return false;
    g_target_stack.push_back(id);
    g_xf_stack.push_back(g_xf);
    SDL_SetRenderTarget(g_renderer, t->tex);
    set_surface_ortho(t->w, t->h);
    return true;
}

void render_surface_reset_target() {
    if (g_target_stack.empty()) return;
    g_target_stack.pop_back();
    if (!g_xf_stack.empty()) {
        g_xf = g_xf_stack.back();
        g_xf_stack.pop_back();
    }
    int prev = g_target_stack.empty() ? 0 : g_target_stack.back();
    unsigned int tid = prev == 0 ? g_app_tex : render_surface_texture(prev);
    RtTexture* t = tex_of(tid);
    SDL_SetRenderTarget(g_renderer, t ? t->tex : nullptr);
}

bool render_surface_getpixel(int id, int x, int y, unsigned char* rgba_out) {
    unsigned int tid = id == 0 ? g_app_tex : render_surface_texture(id);
    RtTexture* t = tex_of(tid);
    if (!t) return false;
    SDL_Texture* prev = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, t->tex);
    SDL_Rect rc = {x, y, 1, 1};
    bool ok = SDL_RenderReadPixels(g_renderer, &rc, SDL_PIXELFORMAT_ABGR8888, rgba_out, 4) == 0;
    SDL_SetRenderTarget(g_renderer, prev);
    return ok;
}

void render_surface_clear(unsigned int bgr, double alpha) {
    SDL_SetRenderDrawColor(g_renderer, bgr & 0xFF, (bgr >> 8) & 0xFF, (bgr >> 16) & 0xFF,
                           (Uint8)std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
    SDL_RenderClear(g_renderer);
}

bool render_surface_snapshot(int id, int x, int y, int w, int h, unsigned char* rgba_out) {
    unsigned int tid = id == 0 ? g_app_tex : render_surface_texture(id);
    RtTexture* t = tex_of(tid);
    if (!t) return false;
    SDL_Texture* prev = SDL_GetRenderTarget(g_renderer);
    SDL_SetRenderTarget(g_renderer, t->tex);
    SDL_Rect rc = {x, y, w, h};
    bool ok = SDL_RenderReadPixels(g_renderer, &rc, SDL_PIXELFORMAT_ABGR8888, rgba_out, w * 4) == 0;
    SDL_SetRenderTarget(g_renderer, prev);
    return ok;
}

bool render_app_snapshot(int x, int y, int w, int h, unsigned char* rgba_out) {
    return render_surface_snapshot(0, x, y, w, h, rgba_out);
}

unsigned int render_upload_texture(const unsigned char* rgba, int w, int h) {
    SDL_Texture* tex =
        SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return 0;
    SDL_UpdateTexture(tex, nullptr, rgba, w * 4);
    SDL_SetTextureScaleMode(tex, SDL_ScaleModeNearest);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    return tex_register(tex, w, h, false);
}

static SDL_Color vcol(unsigned int bgr, double alpha) {
    SDL_Color c;
    if (g_fog_on) {
        c.r = g_fog_col[0];
        c.g = g_fog_col[1];
        c.b = g_fog_col[2];
    } else {
        c.r = (Uint8)(bgr & 0xFF);
        c.g = (Uint8)((bgr >> 8) & 0xFF);
        c.b = (Uint8)((bgr >> 16) & 0xFF);
    }
    c.a = (Uint8)std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0);
    return c;
}

static void draw_tex_quad(RtTexture* t, const float* xs, const float* ys, float u0, float v0,
                          float u1, float v1, unsigned int blend_bgr, double alpha) {
    if (t->target) {
        v0 = 1.0f - v0;
        v1 = 1.0f - v1;
    }
    SDL_SetTextureBlendMode(t->tex, current_blendmode());
    SDL_Color c = vcol(blend_bgr, alpha);
    SDL_Vertex v[4];
    float us[4] = {u0, u1, u1, u0};
    float vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; ++i) {
        v[i].position.x = xs[i];
        v[i].position.y = ys[i];
        v[i].color = c;
        v[i].tex_coord.x = us[i];
        v[i].tex_coord.y = vs[i];
    }
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(g_renderer, t->tex, v, 4, idx, 6);
}

void render_draw_quad(unsigned int tex, double x, double y, double dw, double dh,
                      double origin_x, double origin_y, double xscale, double yscale,
                      double angle_deg, float u0, float v0, float u1, float v1,
                      unsigned int blend_bgr, double alpha) {
    RtTexture* t = tex_of(tex);
    if (!t) return;
    double rad = angle_deg * 3.14159265358979323846 / 180.0;
    double c = std::cos(rad), s = std::sin(rad);
    double lx[4] = {0.0, dw, dw, 0.0};
    double ly[4] = {0.0, 0.0, dh, dh};
    float vx[4], vy[4];
    for (int i = 0; i < 4; ++i) {
        double px = (lx[i] - origin_x) * xscale;
        double py = (ly[i] - origin_y) * yscale;
        vx[i] = tx(x + px * c + py * s);
        vy[i] = ty(y - px * s + py * c);
    }
    draw_tex_quad(t, vx, vy, u0, v0, u1, v1, blend_bgr, alpha);
}

void render_draw_glyph_colored(unsigned int tex, double dx, double dy, double dw, double dh,
                               float u0, float v0, float u1, float v1, unsigned int bgr,
                               double alpha) {
    RtTexture* t = tex_of(tex);
    if (!t) return;
    float vx[4] = {tx(dx), tx(dx + dw), tx(dx + dw), tx(dx)};
    float vy[4] = {ty(dy), ty(dy), ty(dy + dh), ty(dy + dh)};
    draw_tex_quad(t, vx, vy, u0, v0, u1, v1, bgr, alpha);
}

void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1) {
    render_draw_glyph_colored(tex, dx, dy, dw, dh, u0, v0, u1, v1, g_color_bgr, g_alpha);
}

void render_draw_glyphs_colored(unsigned int tex, const GlyphQuad* quads, int count,
                                unsigned int bgr, double alpha) {
    for (int i = 0; i < count; ++i) {
        const GlyphQuad& q = quads[i];
        render_draw_glyph_colored(tex, q.x, q.y, q.w, q.h, q.u0, q.v0, q.u1, q.v1, bgr, alpha);
    }
}

static void geometry_fill(const SDL_Vertex* verts, int nverts, const int* idx, int nidx) {
    SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
    SDL_RenderGeometry(g_renderer, nullptr, verts, nverts, idx, nidx);
}

static SDL_Vertex mkv(double x, double y, unsigned int bgr, double alpha) {
    SDL_Vertex v;
    v.position.x = tx(x);
    v.position.y = ty(y);
    v.color = vcol(bgr, alpha);
    v.tex_coord.x = 0;
    v.tex_coord.y = 0;
    return v;
}

void render_draw_rectangle_color(double x1, double y1, double x2, double y2, unsigned int c1,
                                 unsigned int c2, unsigned int c3, unsigned int c4, bool outline) {
    if (outline) {
        SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
        SDL_Color c = vcol(c1, g_alpha);
        SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
        SDL_FRect rc = {tx(x1), ty(y1), (float)((x2 - x1) * g_xf.sx + 1),
                        (float)((y2 - y1) * g_xf.sy + 1)};
        SDL_RenderDrawRectF(g_renderer, &rc);
        return;
    }
    SDL_Vertex v[4] = {mkv(x1, y1, c1, g_alpha), mkv(x2 + 1, y1, c2, g_alpha),
                       mkv(x2 + 1, y2 + 1, c3, g_alpha), mkv(x1, y2 + 1, c4, g_alpha)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry_fill(v, 4, idx, 6);
}

void render_draw_rectangle(double x1, double y1, double x2, double y2, bool outline) {
    render_draw_rectangle_color(x1, y1, x2, y2, g_color_bgr, g_color_bgr, g_color_bgr, g_color_bgr,
                                outline);
}

void render_draw_line(double x1, double y1, double x2, double y2, double width, unsigned int c1,
                      unsigned int c2) {
    if (width <= 1.0) {
        SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
        SDL_Color c = vcol(c1, g_alpha);
        SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
        SDL_RenderDrawLineF(g_renderer, tx(x1 + 0.5), ty(y1 + 0.5), tx(x2 + 0.5), ty(y2 + 0.5));
        return;
    }
    double dx = x2 - x1, dy = y2 - y1;
    double len = std::hypot(dx, dy);
    if (len < 1e-9) len = 1;
    double nx = -dy / len * width * 0.5, ny = dx / len * width * 0.5;
    SDL_Vertex v[4] = {mkv(x1 + nx, y1 + ny, c1, g_alpha), mkv(x1 - nx, y1 - ny, c1, g_alpha),
                       mkv(x2 - nx, y2 - ny, c2, g_alpha), mkv(x2 + nx, y2 + ny, c2, g_alpha)};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    geometry_fill(v, 4, idx, 6);
}

void render_draw_ellipse(double x1, double y1, double x2, double y2, unsigned int c1,
                         unsigned int c2, bool outline) {
    double cx = (x1 + x2) * 0.5, cy = (y1 + y2) * 0.5;
    double rx = std::fabs(x2 - x1) * 0.5, ry = std::fabs(y2 - y1) * 0.5;
    const int seg = 48;
    if (outline) {
        SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
        SDL_Color c = vcol(c2, g_alpha);
        SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
        SDL_FPoint pts[seg + 1];
        for (int i = 0; i <= seg; ++i) {
            double a = i * 2.0 * 3.14159265358979 / seg;
            pts[i].x = tx(cx + std::cos(a) * rx);
            pts[i].y = ty(cy + std::sin(a) * ry);
        }
        SDL_RenderDrawLinesF(g_renderer, pts, seg + 1);
        return;
    }
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back(mkv(cx, cy, c1, g_alpha));
    for (int i = 0; i <= seg; ++i) {
        double a = i * 2.0 * 3.14159265358979 / seg;
        v.push_back(mkv(cx + std::cos(a) * rx, cy + std::sin(a) * ry, c2, g_alpha));
    }
    for (int i = 1; i <= seg; ++i) {
        idx.push_back(0);
        idx.push_back(i);
        idx.push_back(i + 1);
    }
    geometry_fill(v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void render_draw_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          unsigned int c1, unsigned int c2, unsigned int c3, bool outline) {
    if (outline) {
        SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
        SDL_Color c = vcol(c1, g_alpha);
        SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, c.a);
        SDL_FPoint pts[4] = {{tx(x1), ty(y1)}, {tx(x2), ty(y2)}, {tx(x3), ty(y3)}, {tx(x1), ty(y1)}};
        SDL_RenderDrawLinesF(g_renderer, pts, 4);
        return;
    }
    SDL_Vertex v[3] = {mkv(x1, y1, c1, g_alpha), mkv(x2, y2, c2, g_alpha),
                       mkv(x3, y3, c3, g_alpha)};
    const int idx[3] = {0, 1, 2};
    geometry_fill(v, 3, idx, 3);
}

void render_draw_point(double x, double y, unsigned int c) {
    SDL_SetRenderDrawBlendMode(g_renderer, current_blendmode());
    SDL_Color col = vcol(c, g_alpha);
    SDL_SetRenderDrawColor(g_renderer, col.r, col.g, col.b, col.a);
    SDL_FRect rc = {tx(x), ty(y), (float)g_xf.sx, (float)g_xf.sy};
    SDL_RenderFillRectF(g_renderer, &rc);
}

struct PrimVert {
    double x, y, u, v;
    unsigned int color;
    double alpha;
};
static int g_prim_kind = 0;
static unsigned int g_prim_tex = 0;
static std::vector<PrimVert> g_prim_verts;

void render_primitive_begin(int kind, unsigned int tex) {
    g_prim_kind = kind;
    g_prim_tex = tex;
    g_prim_verts.clear();
}

void render_primitive_vertex(double x, double y, double u, double v, unsigned int color,
                             double alpha, bool textured) {
    PrimVert pv;
    pv.x = x;
    pv.y = y;
    pv.u = textured ? u : 0;
    pv.v = textured ? v : 0;
    pv.color = color;
    pv.alpha = alpha;
    g_prim_verts.push_back(pv);
}

void render_primitive_end() {
    int n = (int)g_prim_verts.size();
    if (n == 0) return;
    RtTexture* t = g_prim_tex ? tex_of(g_prim_tex) : nullptr;

    if (g_prim_kind == 1) {
        for (const PrimVert& pv : g_prim_verts) render_draw_point(pv.x, pv.y, pv.color);
        return;
    }
    if (g_prim_kind == 2) {
        for (int i = 0; i + 1 < n; i += 2)
            render_draw_line(g_prim_verts[i].x, g_prim_verts[i].y, g_prim_verts[i + 1].x,
                             g_prim_verts[i + 1].y, 1, g_prim_verts[i].color,
                             g_prim_verts[i + 1].color);
        return;
    }
    if (g_prim_kind == 3) {
        for (int i = 0; i + 1 < n; ++i)
            render_draw_line(g_prim_verts[i].x, g_prim_verts[i].y, g_prim_verts[i + 1].x,
                             g_prim_verts[i + 1].y, 1, g_prim_verts[i].color,
                             g_prim_verts[i + 1].color);
        return;
    }

    std::vector<SDL_Vertex> verts(n);
    for (int i = 0; i < n; ++i) {
        const PrimVert& pv = g_prim_verts[i];
        verts[i].position.x = tx(pv.x);
        verts[i].position.y = ty(pv.y);
        verts[i].color = vcol(pv.color, pv.alpha);
        float vv = (float)pv.v;
        if (t && t->target) vv = 1.0f - vv;
        verts[i].tex_coord.x = (float)pv.u;
        verts[i].tex_coord.y = vv;
    }
    std::vector<int> idx;
    if (g_prim_kind == 5) {
        for (int i = 0; i + 2 < n; ++i) {
            idx.push_back(i);
            idx.push_back(i + 1);
            idx.push_back(i + 2);
        }
    } else if (g_prim_kind == 6) {
        for (int i = 1; i + 1 < n; ++i) {
            idx.push_back(0);
            idx.push_back(i);
            idx.push_back(i + 1);
        }
    } else {
        for (int i = 0; i + 2 < n; i += 3) {
            idx.push_back(i);
            idx.push_back(i + 1);
            idx.push_back(i + 2);
        }
    }
    if (idx.empty()) return;
    if (t) {
        SDL_SetTextureBlendMode(t->tex, current_blendmode());
        SDL_RenderGeometry(g_renderer, t->tex, verts.data(), n, idx.data(), (int)idx.size());
    } else {
        geometry_fill(verts.data(), n, idx.data(), (int)idx.size());
    }
}

static bool key_state(int vk) {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    if (!ks) return false;
    switch (vk) {
        case 37: return ks[SDL_SCANCODE_LEFT];
        case 38: return ks[SDL_SCANCODE_UP];
        case 39: return ks[SDL_SCANCODE_RIGHT];
        case 40: return ks[SDL_SCANCODE_DOWN];
        case 32: return ks[SDL_SCANCODE_SPACE];
        case 13: return ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
        case 27: return ks[SDL_SCANCODE_ESCAPE];
        case 16: return ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        case 17: return ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
        case 18: return ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT];
        case 8: return ks[SDL_SCANCODE_BACKSPACE];
        case 9: return ks[SDL_SCANCODE_TAB];
        case 46: return ks[SDL_SCANCODE_DELETE];
        case 45: return ks[SDL_SCANCODE_INSERT];
        case 36: return ks[SDL_SCANCODE_HOME];
        case 35: return ks[SDL_SCANCODE_END];
        case 33: return ks[SDL_SCANCODE_PAGEUP];
        case 34: return ks[SDL_SCANCODE_PAGEDOWN];
        default:
            if (vk >= 'A' && vk <= 'Z') return ks[SDL_SCANCODE_A + (vk - 'A')];
            if (vk == '0') return ks[SDL_SCANCODE_0];
            if (vk >= '1' && vk <= '9') return ks[SDL_SCANCODE_1 + (vk - '1')];
            if (vk >= 112 && vk <= 123) return ks[SDL_SCANCODE_F1 + (vk - 112)];
            if (vk == 96) return ks[SDL_SCANCODE_KP_0];
            if (vk >= 97 && vk <= 105) return ks[SDL_SCANCODE_KP_1 + (vk - 97)];
            return false;
    }
}

bool render_init(const char* title, int width, int height, unsigned int bg_color) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "kwik: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                                height, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::fprintf(stderr, "kwik: window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC |
                                        SDL_RENDERER_TARGETTEXTURE);
    if (!g_renderer)
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_TARGETTEXTURE);
    if (!g_renderer) {
        std::fprintf(stderr, "kwik: renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
        SDL_Quit();
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

    g_app_tex = create_target_texture(width, height);
    if (g_app_tex) {
        g_fbo_w = width;
        g_fbo_h = height;
    } else {
        std::fprintf(stderr, "kwik: application surface texture unavailable\n");
    }
    return true;
}

bool render_should_close() { return g_window == nullptr || g_quit; }

static void apply_view_xf() {
    double vw = g_view_w > 0 ? g_view_w : g_room_w;
    double vh = g_view_h > 0 ? g_view_h : g_room_h;
    int tw = g_fbo_w > 0 ? g_fbo_w : g_win_w;
    int th = g_fbo_h > 0 ? g_fbo_h : g_win_h;
    g_xf.ox = g_view_x;
    g_xf.oy = g_view_y;
    g_xf.sx = vw > 0 ? tw / vw : 1;
    g_xf.sy = vh > 0 ? th / vh : 1;
}

void render_begin_frame() {
    g_target_stack.clear();
    g_xf_stack.clear();
    g_fog_on = false;
    RtTexture* t = tex_of(g_app_tex);
    SDL_SetRenderTarget(g_renderer, t ? t->tex : nullptr);
    apply_view_xf();
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
}

void render_begin_gui() {
    g_xf.ox = 0;
    g_xf.oy = 0;
    g_xf.sx = g_gui_w > 0 ? (double)(g_fbo_w > 0 ? g_fbo_w : g_win_w) / g_gui_w : 1;
    g_xf.sy = g_gui_h > 0 ? (double)(g_fbo_h > 0 ? g_fbo_h : g_win_h) / g_gui_h : 1;
}

void render_set_title(const char* title) {
    if (g_window && title) SDL_SetWindowTitle(g_window, title);
}

void render_set_view(double x, double y, double w, double h) {
    g_view_x = x;
    g_view_y = y;
    if (w > 0) g_view_w = w;
    if (h > 0) g_view_h = h;
    apply_view_xf();
}

double render_delta_time() { return g_dt; }
double render_time_ms() { return time_seconds() * 1000.0; }

static void blit_app_to_window() {
    RtTexture* t = tex_of(g_app_tex);
    if (!t) return;
    SDL_SetRenderTarget(g_renderer, nullptr);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    int ww, wh;
    SDL_GetRendererOutputSize(g_renderer, &ww, &wh);
    double scale = std::min((double)ww / t->w, (double)wh / t->h);
    float dw = (float)(t->w * scale);
    float dh = (float)(t->h * scale);
    SDL_FRect dst = {(float)((ww - dw) * 0.5), (float)((wh - dh) * 0.5), dw, dh};
    SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_NONE);
    SDL_RenderCopyF(g_renderer, t->tex, nullptr, &dst);
    SDL_SetTextureBlendMode(t->tex, SDL_BLENDMODE_BLEND);
}

void render_present_last() {
    if (!g_window) return;
    blit_app_to_window();
    SDL_RenderPresent(g_renderer);
    pump_events();
}

void render_end_frame() {
    blit_app_to_window();
    SDL_RenderPresent(g_renderer);
    double now = time_seconds();
    g_dt = g_last_time > 0.0 ? now - g_last_time : 0.0;
    if (g_dt > 0.25) g_dt = 0.25;
    g_last_time = now;
    for (int i = 0; i < 512; ++i) g_keys_prev[i] = g_keys_now[i];
    for (int i = 0; i < 3; ++i) g_mouse_prev[i] = g_mouse_now[i];
    pump_events();
    for (int i = 0; i < 512; ++i) g_keys_now[i] = key_state(i);
    bool any = false;
    for (int i = 2; i < 512; ++i) any = any || g_keys_now[i];
    g_keys_now[1] = any;
    g_keys_now[0] = false;
    Uint32 mb = SDL_GetMouseState(nullptr, nullptr);
    g_mouse_now[0] = (mb & SDL_BUTTON_LMASK) != 0;
    g_mouse_now[1] = (mb & SDL_BUTTON_RMASK) != 0;
    g_mouse_now[2] = (mb & SDL_BUTTON_MMASK) != 0;
    g_wheel_frame = g_wheel_accum;
    g_wheel_accum = 0.0;
}

double render_wheel_delta() { return g_wheel_frame; }

void render_idle() { pump_events(); }

bool render_has_focus() {
    return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

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

static void mouse_to_gui(double& gx, double& gy) {
    gx = 0;
    gy = 0;
    if (!g_window) return;
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    int ww, wh;
    SDL_GetWindowSize(g_window, &ww, &wh);
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
        case 1:
            g_blend_src = 5; g_blend_dst = 2; g_blend_asrc = 5; g_blend_adst = 2;
            break;
        case 2:
            g_blend_src = 5; g_blend_dst = 4; g_blend_asrc = 5; g_blend_adst = 4;
            break;
        case 3:
            g_blend_src = 1; g_blend_dst = 4; g_blend_asrc = 1; g_blend_adst = 4;
            break;
        default:
            g_blend_src = 5; g_blend_dst = 6; g_blend_asrc = 5; g_blend_adst = 6;
            break;
    }
}

void render_set_blendmode_ext(int src, int dst) {
    g_blend_src = src;
    g_blend_dst = dst;
    g_blend_asrc = src;
    g_blend_adst = dst;
}

void render_set_blendmode_sepalpha(int src, int dst, int asrc, int adst) {
    g_blend_src = src;
    g_blend_dst = dst;
    g_blend_asrc = asrc;
    g_blend_adst = adst;
}

void render_set_colorwrite(bool r, bool g, bool b, bool a) {
    g_colormask[0] = r;
    g_colormask[1] = g;
    g_colormask[2] = b;
    g_colormask[3] = a;
}

void render_set_fog(bool on, unsigned int bgr) {
    static bool disabled = std::getenv("KWIK_NO_FOG") != nullptr;
    if (disabled) return;
    g_fog_on = on;
    g_fog_col[0] = (Uint8)(bgr & 0xFF);
    g_fog_col[1] = (Uint8)((bgr >> 8) & 0xFF);
    g_fog_col[2] = (Uint8)((bgr >> 16) & 0xFF);
}

int render_gui_width() { return g_gui_w; }
int render_gui_height() { return g_gui_h; }

void render_set_window_size(int width, int height) {
    g_win_w = width;
    g_win_h = height;
    if (g_window && !g_fullscreen) SDL_SetWindowSize(g_window, width, height);
}

void render_set_fullscreen(bool fs) {
    if (!g_window || fs == g_fullscreen) return;
    if (fs) {
        SDL_GetWindowPosition(g_window, &g_saved_x, &g_saved_y);
        SDL_GetWindowSize(g_window, &g_saved_w, &g_saved_h);
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    } else {
        SDL_SetWindowFullscreen(g_window, 0);
        SDL_SetWindowSize(g_window, g_saved_w, g_saved_h);
        SDL_SetWindowPosition(g_window, g_saved_x, g_saved_y);
    }
    g_fullscreen = fs;
}

bool render_get_fullscreen() { return g_fullscreen; }

void render_center_window() {
    if (!g_window || g_fullscreen) return;
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

int render_window_width() {
    int ww = g_win_w, wh;
    if (g_window) SDL_GetWindowSize(g_window, &ww, &wh);
    return ww;
}
int render_window_height() {
    int ww, wh = g_win_h;
    if (g_window) SDL_GetWindowSize(g_window, &ww, &wh);
    return wh;
}
int render_display_width() {
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) return mode.w;
    return 1920;
}
int render_display_height() {
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) return mode.h;
    return 1080;
}

void render_set_room(int width, int height, unsigned int) {
    g_room_w = width;
    g_room_h = height;
}

void render_shutdown() {
    for (auto& t : g_textures)
        if (t.alive && t.tex) SDL_DestroyTexture(t.tex);
    g_textures.clear();
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

}
