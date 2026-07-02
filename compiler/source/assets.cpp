#include "assets.h"

#include <bzlib.h>
#include <zlib.h>

#include <cstring>
#include <fstream>
#include <map>

namespace kwik {

struct Page {
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;
    bool ok = false;
};

static bool gm_qoi_decode(const uint8_t* d, size_t n, std::vector<uint8_t>& out, int& W, int& H) {
    if (n < 12 || d[0] != 'f' || d[1] != 'i' || d[2] != 'o' || d[3] != 'q') return false;
    W = d[4] | (d[5] << 8);
    H = d[6] | (d[7] << 8);
    uint32_t length = d[8] | (d[9] << 8) | (d[10] << 16) | ((uint32_t)d[11] << 24);
    const uint8_t* px = d + 12;
    size_t plen = length;
    if ((size_t)12 + plen > n) plen = n - 12;
    out.assign((size_t)W * H * 4, 0);
    size_t pos = 0;
    int run = 0;
    int r = 0, g = 0, b = 0, a = 255;
    uint8_t index[64 * 4] = {0};
    size_t total = (size_t)W * H * 4;
    for (size_t rp = 0; rp < total; rp += 4) {
        if (run > 0) {
            run--;
        } else if (pos < plen) {
            int b1 = px[pos++];
            if ((b1 & 0xC0) == 0x00) {
                int ip = b1 << 2;
                r = index[ip]; g = index[ip + 1]; b = index[ip + 2]; a = index[ip + 3];
            } else if ((b1 & 0xE0) == 0x40) {
                run = b1 & 0x1f;
            } else if ((b1 & 0xE0) == 0x60) {
                int b2 = px[pos++];
                run = (((b1 & 0x1f) << 8) | b2) + 32;
            } else if ((b1 & 0xC0) == 0x80) {
                r = (uint8_t)(r + (((b1 & 48) << 26 >> 30) & 0xff));
                g = (uint8_t)(g + (((b1 & 12) << 28 >> 22 >> 8) & 0xff));
                b = (uint8_t)(b + (((b1 & 3) << 30 >> 14 >> 16) & 0xff));
            } else if ((b1 & 0xE0) == 0xc0) {
                int b2 = px[pos++];
                int m = (b1 << 8) | b2;
                r = (uint8_t)(r + (((m & 7936) << 19 >> 27) & 0xff));
                g = (uint8_t)(g + (((m & 240) << 24 >> 20 >> 8) & 0xff));
                b = (uint8_t)(b + (((m & 15) << 28 >> 12 >> 16) & 0xff));
            } else if ((b1 & 0xF0) == 0xe0) {
                int b2 = px[pos++], b3 = px[pos++];
                int m = (b1 << 16) | (b2 << 8) | b3;
                r = (uint8_t)(r + (((m & 1015808) << 12 >> 27) & 0xff));
                g = (uint8_t)(g + (((m & 31744) << 17 >> 19 >> 8) & 0xff));
                b = (uint8_t)(b + (((m & 992) << 22 >> 11 >> 16) & 0xff));
                a = (uint8_t)(a + (((m & 31) << 27 >> 3 >> 24) & 0xff));
            } else if ((b1 & 0xF0) == 0xf0) {
                if (b1 & 8) r = px[pos++];
                if (b1 & 4) g = px[pos++];
                if (b1 & 2) b = px[pos++];
                if (b1 & 1) a = px[pos++];
            }
            int ip2 = ((r ^ g ^ b ^ a) & 63) << 2;
            index[ip2] = r; index[ip2 + 1] = g; index[ip2 + 2] = b; index[ip2 + 3] = a;
        }
        out[rp] = r; out[rp + 1] = g; out[rp + 2] = b; out[rp + 3] = a;
    }
    return true;
}

static Page decode_page(const GameData& gd, uint32_t entry_ptr) {
    Page pg;
    const auto& bytes = gd.bytes();
    uint32_t dptr = 0;
    for (int w = 0; w < 12; ++w) {
        uint32_t v = gd.u32(entry_ptr + w * 4);
        if ((size_t)v + 4 <= bytes.size() && bytes[v] == '2' && bytes[v + 1] == 'z' &&
            bytes[v + 2] == 'o' && bytes[v + 3] == 'q') {
            dptr = v;
            break;
        }
    }
    if (!dptr) return pg;
    uint32_t declen = gd.u32(dptr + 8);
    std::vector<char> dec((size_t)declen + 4096);
    unsigned int destlen = dec.size();
    unsigned int inlen = bytes.size() - (dptr + 12);
    if (BZ2_bzBuffToBuffDecompress(dec.data(), &destlen, (char*)&bytes[dptr + 12], inlen, 0, 0) != 0)
        return pg;
    pg.ok = gm_qoi_decode((uint8_t*)dec.data(), destlen, pg.rgba, pg.w, pg.h);
    return pg;
}

static void write_png(std::vector<uint8_t>& out, int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> raw((size_t)h * (w * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw[(size_t)y * (w * 4 + 1)] = 0;
        std::memcpy(&raw[(size_t)y * (w * 4 + 1) + 1], &rgba[(size_t)y * w * 4], w * 4);
    }
    uLongf clen = compressBound(raw.size());
    std::vector<uint8_t> comp(clen);
    compress2(comp.data(), &clen, raw.data(), raw.size(), 9);

    auto put32 = [&](uint32_t v) {
        out.push_back(v >> 24); out.push_back(v >> 16); out.push_back(v >> 8); out.push_back(v);
    };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        put32(data.size());
        size_t start = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        uint32_t crc = crc32(0, &out[start], 4 + data.size());
        put32(crc);
    };

    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.insert(out.end(), sig, sig + 8);

    std::vector<uint8_t> ihdr;
    auto ph = [&](uint32_t v) { ihdr.push_back(v >> 24); ihdr.push_back(v >> 16); ihdr.push_back(v >> 8); ihdr.push_back(v); };
    ph(w); ph(h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk("IHDR", ihdr);
    chunk("IDAT", std::vector<uint8_t>(comp.begin(), comp.begin() + clen));
    chunk("IEND", {});
}

static std::vector<uint8_t> crop_canvas(const std::map<int, Page>& pages, int texIdx, int srcX,
                                        int srcY, int srcW, int srcH, int tgtX, int tgtY, int cw,
                                        int ch) {
    std::vector<uint8_t> canvas((size_t)cw * ch * 4, 0);
    auto it = pages.find(texIdx);
    if (it == pages.end() || !it->second.ok) return canvas;
    const Page& pg = it->second;
    for (int y = 0; y < srcH; ++y) {
        int dy = tgtY + y;
        if (dy < 0 || dy >= ch || srcY + y < 0 || srcY + y >= pg.h) continue;
        for (int x = 0; x < srcW; ++x) {
            int dx = tgtX + x;
            if (dx < 0 || dx >= cw || srcX + x < 0 || srcX + x >= pg.w) continue;
            const uint8_t* s = &pg.rgba[((size_t)(srcY + y) * pg.w + (srcX + x)) * 4];
            uint8_t* d = &canvas[((size_t)dy * cw + dx) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }
    return canvas;
}

static std::vector<uint8_t> build_image_entry(int w, int h, int hot_x, int hot_y,
                                              const std::vector<uint8_t>& png) {
    std::vector<uint8_t> e;
    auto w16 = [&](int v) { e.push_back(v & 0xff); e.push_back((v >> 8) & 0xff); };
    auto w32 = [&](uint32_t v) { e.push_back(v); e.push_back(v >> 8); e.push_back(v >> 16); e.push_back(v >> 24); };
    w16(w); w16(h); w16(hot_x); w16(hot_y); w16(0); w16(0);
    w32(png.size());
    e.insert(e.end(), png.begin(), png.end());
    return e;
}

bool extract_assets(const GameData& gd, const std::string& out_dir, AssetExtraction& out) {
    std::map<int, Page> pages;
    const Chunk* txtr = gd.chunk("TXTR");
    if (txtr) {
        uint32_t tc = gd.u32(txtr->offset);
        for (uint32_t i = 0; i < tc; ++i)
            pages[i] = decode_page(gd, gd.u32(txtr->offset + 4 + i * 4));
    }

    std::vector<std::vector<uint8_t>> images;
    std::vector<std::vector<uint8_t>> sounds;

    const Chunk* sprt = gd.chunk("SPRT");
    if (sprt) {
        uint32_t sc = gd.u32(sprt->offset);
        for (uint32_t i = 0; i < sc; ++i) {
            uint32_t sp = gd.u32(sprt->offset + 4 + i * 4);
            SpriteInfo info;
            info.name = gd.string_at_offset(gd.u32(sp));
            info.width = gd.i32(sp + 4);
            info.height = gd.i32(sp + 8);
            info.origin_x = gd.i32(sp + 48);
            info.origin_y = gd.i32(sp + 52);
            info.first_frame = images.size();
            info.frame_count = 0;

            uint32_t tex_list = 0;
            if (gd.i32(sp + 56) == -1) {
                uint32_t sver = gd.u32(sp + 60);
                uint32_t base = sp + 76;
                if (sver >= 2) base += 4;
                if (sver >= 3) base += 4;
                tex_list = base;
            } else {
                tex_list = sp + 56;
            }

            uint32_t nframes = gd.u32(tex_list);
            if (nframes > 4096) nframes = 0;
            for (uint32_t fr = 0; fr < nframes; ++fr) {
                uint32_t tpag = gd.u32(tex_list + 4 + fr * 4);
                int srcX = (int16_t)gd.u16(tpag + 0), srcY = (int16_t)gd.u16(tpag + 2);
                int srcW = (int16_t)gd.u16(tpag + 4), srcH = (int16_t)gd.u16(tpag + 6);
                int tgtX = (int16_t)gd.u16(tpag + 8), tgtY = (int16_t)gd.u16(tpag + 10);
                int texIdx = (int16_t)gd.u16(tpag + 20);

                int cw = info.width > 0 ? info.width : srcW;
                int ch = info.height > 0 ? info.height : srcH;
                if (cw <= 0 || ch <= 0) continue;
                std::vector<uint8_t> canvas((size_t)cw * ch * 4, 0);
                auto it = pages.find(texIdx);
                if (it != pages.end() && it->second.ok) {
                    const Page& pg = it->second;
                    for (int y = 0; y < srcH; ++y) {
                        int dy = tgtY + y;
                        if (dy < 0 || dy >= ch || srcY + y >= pg.h) continue;
                        for (int x = 0; x < srcW; ++x) {
                            int dx = tgtX + x;
                            if (dx < 0 || dx >= cw || srcX + x >= pg.w) continue;
                            const uint8_t* s = &pg.rgba[((size_t)(srcY + y) * pg.w + (srcX + x)) * 4];
                            uint8_t* d = &canvas[((size_t)dy * cw + dx) * 4];
                            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                        }
                    }
                }
                std::vector<uint8_t> png;
                write_png(png, cw, ch, canvas);
                images.push_back(build_image_entry(cw, ch, info.origin_x, info.origin_y, png));
                info.frame_count++;
            }
            out.sprites.push_back(info);
        }
    }

    const Chunk* font = gd.chunk("FONT");
    if (font) {
        uint32_t fcnt = gd.u32(font->offset);
        for (uint32_t i = 0; i < fcnt; ++i) {
            uint32_t p = gd.u32(font->offset + 4 + i * 4);
            FontInfo fi;
            fi.name = gd.string_at_offset(gd.u32(p));
            uint32_t tpag = gd.u32(p + 28);
            int tsx = (int16_t)gd.u16(tpag), tsy = (int16_t)gd.u16(tpag + 2);
            int tw = (int16_t)gd.u16(tpag + 4), th = (int16_t)gd.u16(tpag + 6);
            int texIdx = (int16_t)gd.u16(tpag + 20);
            if (tw <= 0 || th <= 0) { tw = 1; th = 1; }
            std::vector<uint8_t> canvas = crop_canvas(pages, texIdx, tsx, tsy, tw, th, 0, 0, tw, th);
            std::vector<uint8_t> png;
            write_png(png, tw, th, canvas);
            fi.atlas_image = images.size();
            images.push_back(build_image_entry(tw, th, 0, 0, png));

            uint32_t goff = 0, gcnt = 0;
            for (uint32_t o = 40; o <= 96; o += 4) {
                uint32_t n = gd.u32(p + o);
                uint32_t g0 = gd.u32(p + o + 4);
                if (n >= 1 && n <= 2000 && g0 > p && (size_t)g0 + 14 <= gd.bytes().size()) {
                    uint16_t ch = gd.u16(g0);
                    int gw = gd.u16(g0 + 6), gh = gd.u16(g0 + 8);
                    if (ch >= 1 && ch < 0x1000 && gw <= 4096 && gh <= 4096) { goff = o; gcnt = n; break; }
                }
            }
            fi.glyph_start = out.glyphs.size();
            fi.glyph_count = 0;
            fi.size = 0;
            for (uint32_t gi = 0; gi < gcnt; ++gi) {
                uint32_t gp = gd.u32(p + goff + 4 + gi * 4);
                GlyphInfo g;
                g.ch = gd.u16(gp);
                g.x = gd.u16(gp + 2);
                g.y = gd.u16(gp + 4);
                g.w = gd.u16(gp + 6);
                g.h = gd.u16(gp + 8);
                g.shift = (int16_t)gd.u16(gp + 10);
                g.offset = (int16_t)gd.u16(gp + 12);
                out.glyphs.push_back(g);
                fi.glyph_count++;
                if (g.h > fi.size) fi.size = g.h;
            }
            out.fonts.push_back(fi);
        }
    }

    const Chunk* audo = gd.chunk("AUDO");
    if (audo) {
        const auto& bytes = gd.bytes();
        uint32_t ac = gd.u32(audo->offset);
        for (uint32_t i = 0; i < ac; ++i) {
            uint32_t p = gd.u32(audo->offset + 4 + i * 4);
            uint32_t size = gd.u32(p);
            const uint8_t* data = &bytes[p + 4];
            uint32_t type = 0;
            if (size >= 4) {
                if (!std::memcmp(data, "RIFF", 4)) type = 1;
                else if (!std::memcmp(data, "OggS", 4)) type = 2;
                else type = 3;
            }
            std::vector<uint8_t> e;
            auto w32 = [&](uint32_t v) { e.push_back(v); e.push_back(v >> 8); e.push_back(v >> 16); e.push_back(v >> 24); };
            w32(type);
            w32(size);
            e.insert(e.end(), data, data + size);
            sounds.push_back(std::move(e));
            out.sound_names.push_back("sound_" + std::to_string(i));
        }
    }

    out.image_count = images.size();
    out.sound_count = sounds.size();

    uint32_t header_size = (uint32_t)(images.size() + sounds.size()) * 4 + (uint32_t)images.size() * 2;
    std::vector<uint8_t> header, data;
    auto h16 = [&](int v) { header.push_back(v & 0xff); header.push_back((v >> 8) & 0xff); };
    auto h32 = [&](uint32_t v) { header.push_back(v); header.push_back(v >> 8); header.push_back(v >> 16); header.push_back(v >> 24); };
    for (size_t i = 0; i < images.size(); ++i) h16((int)i);
    for (auto& img : images) { h32(data.size() + header_size); data.insert(data.end(), img.begin(), img.end()); }
    for (auto& snd : sounds) { h32(data.size() + header_size); data.insert(data.end(), snd.begin(), snd.end()); }

    std::ofstream f(out_dir + "/Assets.dat", std::ios::binary);
    if (!f) return false;
    f.write((const char*)header.data(), header.size());
    f.write((const char*)data.data(), data.size());
    return true;
}

}
