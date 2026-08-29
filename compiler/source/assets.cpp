#include "assets.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace kwik {

struct TxtrPage {
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
};

static bool is_txtr_payload(const std::vector<uint8_t>& bytes, uint32_t off) {
    if ((size_t)off + 4 > bytes.size()) return false;
    if (bytes[off] == 'f' && bytes[off + 1] == 'i' && bytes[off + 2] == 'o' &&
        bytes[off + 3] == 'q')
        return true;
    if (bytes[off] == '2' && bytes[off + 1] == 'z' && bytes[off + 2] == 'o' &&
        bytes[off + 3] == 'q')
        return true;
    return (size_t)off + 8 <= bytes.size() && bytes[off] == 0x89 && bytes[off + 1] == 'P' &&
           bytes[off + 2] == 'N' && bytes[off + 3] == 'G';
}

static uint32_t find_txtr_payload_offset(const GameData& gd, uint32_t entry_ptr) {
    const auto& bytes = gd.bytes();
    for (int w = 0; w < 12; ++w) {
        uint32_t v = gd.u32(entry_ptr + w * 4);
        if (is_txtr_payload(bytes, v)) return v;
    }
    return 0;
}

static void extract_txtr_pages(const GameData& gd, std::vector<TxtrPage>& pages) {
    const Chunk* txtr = gd.chunk("TXTR");
    if (!txtr) return;
    const auto& bytes = gd.bytes();
    uint32_t count = gd.u32(txtr->offset);
    pages.assign(count, TxtrPage{});
    std::vector<uint32_t> payload_offsets;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t entry = gd.u32(txtr->offset + 4 + i * 4);
        uint32_t data = find_txtr_payload_offset(gd, entry);
        pages[i].data_offset = data;
        if (data) payload_offsets.push_back(data);
    }
    std::sort(payload_offsets.begin(), payload_offsets.end());
    payload_offsets.erase(std::unique(payload_offsets.begin(), payload_offsets.end()),
                          payload_offsets.end());
    uint32_t chunk_end = txtr->offset + txtr->size;
    for (TxtrPage& page : pages) {
        if (!page.data_offset || page.data_offset >= chunk_end || page.data_offset >= bytes.size())
            continue;
        auto it = std::upper_bound(payload_offsets.begin(), payload_offsets.end(), page.data_offset);
        uint32_t end = it == payload_offsets.end() ? chunk_end : *it;
        if (end > bytes.size()) end = (uint32_t)bytes.size();
        if (end > page.data_offset) page.data_size = end - page.data_offset;
    }
}

static std::vector<uint8_t> build_txtr_blob(const GameData& gd, const TxtrPage& page) {
    std::vector<uint8_t> e;
    auto w32 = [&](uint32_t v) {
        e.push_back(v);
        e.push_back(v >> 8);
        e.push_back(v >> 16);
        e.push_back(v >> 24);
    };
    w32(7);
    w32(page.data_size);
    if (page.data_size) {
        const auto& bytes = gd.bytes();
        e.insert(e.end(), bytes.begin() + page.data_offset,
                 bytes.begin() + page.data_offset + page.data_size);
    }
    return e;
}

static std::vector<uint8_t> build_image_entry(int w, int h, int tgt_x, int tgt_y, int page,
                                              int src_x, int src_y, int src_w, int src_h) {
    std::vector<uint8_t> e;
    auto w16 = [&](int v) { e.push_back(v & 0xff); e.push_back((v >> 8) & 0xff); };
    auto w32 = [&](uint32_t v) { e.push_back(v); e.push_back(v >> 8); e.push_back(v >> 16); e.push_back(v >> 24); };
    w16(w); w16(h); w16(tgt_x); w16(tgt_y); w16(1); w16(0);
    w32((uint32_t)page);
    w16(src_x); w16(src_y); w16(src_w); w16(src_h);
    return e;
}

struct MaskPayload {
    int sprite;
    std::vector<uint8_t> data;
};

bool extract_assets(const GameData& gd, const std::string& out_dir, AssetExtraction& out) {
    std::vector<MaskPayload> mask_payloads;
    std::vector<TxtrPage> pages;
    extract_txtr_pages(gd, pages);

    std::vector<std::vector<uint8_t>> images;
    std::vector<std::vector<uint8_t>> sounds;
    for (const TxtrPage& page : pages) sounds.push_back(build_txtr_blob(gd, page));
    int txtr_blob_count = (int)sounds.size();

    const Chunk* sprt = gd.chunk("SPRT");
    if (sprt) {
        uint32_t sc = gd.u32(sprt->offset);
        for (uint32_t i = 0; i < sc; ++i) {
            uint32_t sp = gd.u32(sprt->offset + 4 + i * 4);
            SpriteInfo info;
            info.name = gd.string_at_offset(gd.u32(sp));
            info.width = gd.i32(sp + 4);
            info.height = gd.i32(sp + 8);
            info.bbox_left = gd.i32(sp + 12);
            info.bbox_right = gd.i32(sp + 16);
            info.bbox_bottom = gd.i32(sp + 20);
            info.bbox_top = gd.i32(sp + 24);
            info.origin_x = gd.i32(sp + 48);
            info.origin_y = gd.i32(sp + 52);
            info.first_frame = images.size();
            info.frame_count = 0;

            info.sep_masks = gd.i32(sp + 44);

            uint32_t tex_list = 0;
            info.speed = 1.0;
            info.speed_type = 1;
            if (gd.i32(sp + 56) == -1) {
                uint32_t sver = gd.u32(sp + 60);
                uint32_t raw = gd.u32(sp + 68);
                float fspeed;
                std::memcpy(&fspeed, &raw, 4);
                info.speed = fspeed;
                info.speed_type = (int)gd.u32(sp + 72);
                uint32_t base = sp + 76;
                if (sver >= 2) base += 4;
                if (sver >= 3) {
                    uint32_t ns = gd.u32(sp + 80);
                    if (ns && (size_t)ns + 40 <= gd.bytes().size()) {
                        bool enabled = gd.u32(ns + 16) != 0;
                        bool margins0 = gd.i32(ns) == 0 && gd.i32(ns + 4) == 0 &&
                                        gd.i32(ns + 8) == 0 && gd.i32(ns + 12) == 0;
                        int center_mode = gd.i32(ns + 36);
                        if (enabled && margins0 && center_mode == 1) info.tile_repeat = 1;
                    }
                    base += 4;
                }
                tex_list = base;
            } else {
                tex_list = sp + 56;
            }

            uint32_t nframes = gd.u32(tex_list);
            if (nframes > 4096) nframes = 0;

            if (info.sep_masks == 1 && info.width > 0 && info.height > 0 && nframes > 0) {
                uint32_t mask_pos = tex_list + 4 + nframes * 4;
                uint32_t mcount = gd.u32(mask_pos);
                uint32_t rowbytes = (uint32_t)(info.width + 7) / 8;
                uint32_t per_mask = rowbytes * (uint32_t)info.height;
                if (mcount > 0 && mcount <= 4096 &&
                    (size_t)mask_pos + 4 + (size_t)mcount * per_mask <= gd.bytes().size()) {
                    MaskPayload mp;
                    mp.sprite = (int)i;
                    auto& d = mp.data;
                    auto w32m = [&](uint32_t v) {
                        d.push_back(v);
                        d.push_back(v >> 8);
                        d.push_back(v >> 16);
                        d.push_back(v >> 24);
                    };
                    w32m(mcount);
                    w32m((uint32_t)info.width);
                    w32m((uint32_t)info.height);
                    const uint8_t* src = &gd.bytes()[mask_pos + 4];
                    d.insert(d.end(), src, src + (size_t)mcount * per_mask);
                    mask_payloads.push_back(std::move(mp));
                }
            }
            for (uint32_t fr = 0; fr < nframes; ++fr) {
                uint32_t tpag = gd.u32(tex_list + 4 + fr * 4);
                int srcX = (int16_t)gd.u16(tpag + 0), srcY = (int16_t)gd.u16(tpag + 2);
                int srcW = (int16_t)gd.u16(tpag + 4), srcH = (int16_t)gd.u16(tpag + 6);
                int tgtX = (int16_t)gd.u16(tpag + 8), tgtY = (int16_t)gd.u16(tpag + 10);
                int texIdx = (int16_t)gd.u16(tpag + 20);

                int cw = info.width > 0 ? info.width : srcW;
                int ch = info.height > 0 ? info.height : srcH;
                if (cw <= 0 || ch <= 0) continue;
                images.push_back(build_image_entry(cw, ch, tgtX, tgtY, texIdx, srcX, srcY, srcW,
                                                   srcH));
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
            fi.atlas_image = images.size();
            images.push_back(build_image_entry(tw, th, 0, 0, texIdx, tsx, tsy, tw, th));

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

    const Chunk* bgnd = gd.chunk("BGND");
    if (bgnd) {
        uint32_t bc = gd.u32(bgnd->offset);
        out.tilesets.assign(bc, TilesetInfo{});
        for (uint32_t i = 0; i < bc; ++i) {
            uint32_t p = gd.u32(bgnd->offset + 4 + i * 4);
            int tile_w = gd.i32(p + 24), tile_h = gd.i32(p + 28);
            int border_x = gd.i32(p + 32), border_y = gd.i32(p + 36);
            int columns = gd.i32(p + 40);
            uint32_t tpag = gd.u32(p + 16);
            if (tile_w <= 0 || tile_h <= 0 || columns <= 0 || !tpag) continue;
            int srcX = (int16_t)gd.u16(tpag + 0), srcY = (int16_t)gd.u16(tpag + 2);
            int srcW = (int16_t)gd.u16(tpag + 4), srcH = (int16_t)gd.u16(tpag + 6);
            int texIdx = (int16_t)gd.u16(tpag + 20);
            if (srcW <= 0 || srcH <= 0) continue;
            TilesetInfo ti;
            ti.image = (int)images.size();
            ti.tile_w = tile_w;
            ti.tile_h = tile_h;
            ti.border_x = border_x;
            ti.border_y = border_y;
            ti.columns = columns;
            int frames = gd.i32(p + 44);
            int tile_count = gd.i32(p + 48);
            int64_t frame_us = (int64_t)gd.u32(p + 56) | ((int64_t)gd.u32(p + 60) << 32);
            if (frames >= 1 && frames <= 64 && tile_count > 0 && tile_count <= 100000) {
                ti.frames = frames;
                ti.tile_count = tile_count;
                ti.frame_ms = (int)(frame_us / 1000);
                bool identity = frames == 1;
                ti.tile_ids.resize((size_t)tile_count * frames);
                for (size_t k = 0; k < ti.tile_ids.size(); ++k) {
                    ti.tile_ids[k] = gd.u32(p + 64 + (uint32_t)k * 4);
                    if (identity && ti.tile_ids[k] != k) identity = false;
                }
                if (identity) ti.tile_ids.clear();
            }
            images.push_back(build_image_entry(srcW, srcH, 0, 0, texIdx, srcX, srcY, srcW, srcH));
            out.tilesets[i] = ti;
        }
    }

    auto pack_blob = [&](const uint8_t* data, uint32_t size) {
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
    };

    const Chunk* audo = gd.chunk("AUDO");
    if (audo) {
        const auto& bytes = gd.bytes();
        uint32_t ac = gd.u32(audo->offset);
        for (uint32_t i = 0; i < ac; ++i) {
            uint32_t p = gd.u32(audo->offset + 4 + i * 4);
            uint32_t size = gd.u32(p);
            if ((size_t)p + 4 + size > bytes.size()) size = bytes.size() - p - 4;
            pack_blob(&bytes[p + 4], size);
        }
    }

    int group1_base = (int)sounds.size();
    bool have_group1 = false;
    {
        std::string agpath = gd.source_dir() + "/audiogroup1.dat";
        std::ifstream ag(agpath, std::ios::binary);
        if (ag) {
            std::vector<uint8_t> agb((std::istreambuf_iterator<char>(ag)),
                                     std::istreambuf_iterator<char>());
            auto agu32 = [&](size_t o) -> uint32_t {
                if (o + 4 > agb.size()) return 0;
                return agb[o] | (agb[o + 1] << 8) | (agb[o + 2] << 16) | ((uint32_t)agb[o + 3] << 24);
            };
            if (agb.size() > 16 && !std::memcmp(agb.data(), "FORM", 4)) {
                size_t pos = 8;
                while (pos + 8 <= agb.size()) {
                    std::string cname((const char*)&agb[pos], 4);
                    uint32_t csize = agu32(pos + 4);
                    if (cname == "AUDO") {
                        uint32_t ac = agu32(pos + 8);
                        for (uint32_t i = 0; i < ac; ++i) {
                            uint32_t p = agu32(pos + 12 + i * 4);
                            uint32_t size = agu32(p);
                            if ((size_t)p + 4 + size > agb.size()) continue;
                            pack_blob(&agb[p + 4], size);
                        }
                        have_group1 = true;
                        break;
                    }
                    pos += 8 + csize;
                }
            }
        }
    }

    const Chunk* sond = gd.chunk("SOND");
    if (sond) {
        uint32_t sc = gd.u32(sond->offset);
        for (uint32_t i = 0; i < sc; ++i) {
            uint32_t p = gd.u32(sond->offset + 4 + i * 4);
            SoundInfo si;
            si.name = gd.string_at_offset(gd.u32(p));
            si.file = gd.string_at_offset(gd.u32(p + 12));
            uint32_t uvol = gd.u32(p + 20), upit = gd.u32(p + 24);
            float fvol, fpit;
            std::memcpy(&fvol, &uvol, 4);
            std::memcpy(&fpit, &upit, 4);
            si.volume = fvol;
            si.pitch = fpit;
            int32_t group = gd.i32(p + 28);
            int32_t audio_id = gd.i32(p + 32);
            if (audio_id < 0)
                si.blob = -1;
            else if (group >= 1 && have_group1)
                si.blob = group1_base + audio_id;
            else
                si.blob = txtr_blob_count + audio_id;
            out.sounds.push_back(si);
        }
    }

    for (auto& mp : mask_payloads) {
        std::vector<uint8_t> e;
        auto w32 = [&](uint32_t v) {
            e.push_back(v);
            e.push_back(v >> 8);
            e.push_back(v >> 16);
            e.push_back(v >> 24);
        };
        w32(4);
        w32((uint32_t)mp.data.size());
        e.insert(e.end(), mp.data.begin(), mp.data.end());
        out.sprites[mp.sprite].mask_blob = (int)sounds.size();
        sounds.push_back(std::move(e));
    }

    {
        const auto& rooms = gd.rooms();
        for (size_t ri = 0; ri < rooms.size(); ++ri) {
            for (size_t li = 0; li < rooms[ri].layers.size(); ++li) {
                const auto& l = rooms[ri].layers[li];
                if (l.type != 4 || l.grid.empty()) continue;
                std::vector<uint8_t> e;
                auto w32 = [&](uint32_t v) {
                    e.push_back(v);
                    e.push_back(v >> 8);
                    e.push_back(v >> 16);
                    e.push_back(v >> 24);
                };
                w32(6);
                w32((uint32_t)l.grid.size() * 4);
                for (uint32_t cell : l.grid) w32(cell);
                out.tilemap_blobs[(int)ri * 1000 + (int)li] = (int)sounds.size();
                sounds.push_back(std::move(e));
            }
        }
    }

    for (auto& ti : out.tilesets) {
        if (ti.tile_ids.empty()) continue;
        std::vector<uint8_t> e;
        auto w32 = [&](uint32_t v) {
            e.push_back(v);
            e.push_back(v >> 8);
            e.push_back(v >> 16);
            e.push_back(v >> 24);
        };
        w32(6);
        w32((uint32_t)ti.tile_ids.size() * 4);
        for (uint32_t id : ti.tile_ids) w32(id);
        ti.map_blob = (int)sounds.size();
        sounds.push_back(std::move(e));
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
