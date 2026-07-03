#include "iff.h"

#include <cstdio>
#include <cstring>

namespace kwik {

bool GameData::load(const std::string& path) {
    source_path_ = path;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) { std::fclose(f); return false; }
    data_.resize(static_cast<size_t>(size));
    size_t got = std::fread(data_.data(), 1, data_.size(), f);
    std::fclose(f);
    if (got != data_.size()) return false;

    parse_chunks();
    parse_strings();
    parse_functions();
    parse_variables();
    parse_code();
    parse_objects();
    parse_rooms();
    parse_glob();
    parse_gen8();
    return true;
}

void GameData::parse_gen8() {
    const Chunk* c = chunk("GEN8");
    if (!c) return;
    int w = i32(c->offset + 60), h = i32(c->offset + 64);
    if (w > 0 && w <= 8192 && h > 0 && h <= 8192) {
        window_w_ = w;
        window_h_ = h;
    }
    auto plausible = [&](uint32_t off) {
        std::string s = string_at_offset(u32(c->offset + off));
        if (s.empty() || s.size() > 80) return std::string();
        for (char ch : s)
            if ((unsigned char)ch < 32) return std::string();
        return s;
    };
    std::string nm = plausible(40);
    if (!nm.empty()) game_name_ = nm;
    display_name_ = plausible(100);
    if (display_name_.empty()) display_name_ = game_name_;
    if (c->size >= 24) {
        uint32_t raw = u32(c->offset + c->size - 24);
        float f;
        std::memcpy(&f, &raw, 4);
        if (f >= 1.0f && f <= 1000.0f) game_fps_ = (int)f;
    }
}

void GameData::parse_glob() {
    const Chunk* c = chunk("GLOB");
    if (!c) return;
    uint32_t count = u32(c->offset);
    for (uint32_t i = 0; i < count; ++i) global_init_.push_back(u32(c->offset + 4 + i * 4));
}

uint8_t GameData::u8(uint32_t off) const {
    if (off >= data_.size()) return 0;
    return data_[off];
}

uint16_t GameData::u16(uint32_t off) const {
    return static_cast<uint16_t>(u8(off) | (u8(off + 1) << 8));
}

uint32_t GameData::u32(uint32_t off) const {
    return static_cast<uint32_t>(u8(off)) | (static_cast<uint32_t>(u8(off + 1)) << 8) |
           (static_cast<uint32_t>(u8(off + 2)) << 16) | (static_cast<uint32_t>(u8(off + 3)) << 24);
}

int32_t GameData::i32(uint32_t off) const {
    return static_cast<int32_t>(u32(off));
}

const Chunk* GameData::chunk(const std::string& name) const {
    auto it = chunks_.find(name);
    return it == chunks_.end() ? nullptr : &it->second;
}

void GameData::parse_chunks() {
    if (data_.size() < 8) return;
    std::string root(reinterpret_cast<const char*>(data_.data()), 4);
    if (root != "FORM") return;

    uint32_t total = u32(4);
    uint32_t pos = 8;
    uint32_t end = 8 + total;
    while (pos + 8 <= end && pos + 8 <= data_.size()) {
        Chunk c;
        c.name.assign(reinterpret_cast<const char*>(&data_[pos]), 4);
        c.size = u32(pos + 4);
        c.offset = pos + 8;
        chunks_[c.name] = c;
        pos = c.offset + c.size;
    }
}

std::string GameData::string_at_offset(uint32_t off) const {
    if (off + 4 <= data_.size()) {
        uint32_t len = u32(off);
        uint32_t data_start = off + 4;
        if (len > 0 && len < 4096 && data_start + len < data_.size() && data_[data_start + len] == 0) {
            bool printable = true;
            for (uint32_t i = 0; i < len; ++i) {
                uint8_t c = data_[data_start + i];
                if (c < 0x09) { printable = false; break; }
            }
            if (printable)
                return std::string(reinterpret_cast<const char*>(&data_[data_start]), len);
        }
    }
    std::string out;
    uint32_t p = off;
    while (p < data_.size() && data_[p] != 0) {
        out.push_back(static_cast<char>(data_[p]));
        ++p;
    }
    return out;
}

void GameData::parse_strings() {
    const Chunk* c = chunk("STRG");
    if (!c) return;
    uint32_t count = u32(c->offset);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t ptr = u32(c->offset + 4 + i * 4);
        strings_.push_back(string_at_offset(ptr));
    }
}

void GameData::parse_functions() {
    const Chunk* c = chunk("FUNC");
    if (!c) return;
    uint32_t p = c->offset;
    uint32_t count = u32(p);
    p += 4;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t name_off = u32(p);
        uint32_t occurrences = u32(p + 4);
        uint32_t addr = u32(p + 8);
        p += 12;
        std::string name = string_at_offset(name_off);
        func_names_.push_back(name);
        for (uint32_t k = 0; k < occurrences && addr < data_.size(); ++k) {
            func_by_call_[addr] = name;
            uint32_t raw = u32(addr);
            uint32_t next = raw & 0x07FFFFFF;
            if (next == 0) break;
            addr += next;
        }
    }
}

std::string GameData::function_at_call(uint32_t call_addr) const {
    auto it = func_by_call_.find(call_addr);
    return it == func_by_call_.end() ? std::string() : it->second;
}

void GameData::parse_variables() {
    const Chunk* c = chunk("VARI");
    if (!c) return;
    uint32_t p = c->offset + 12;
    uint32_t end = c->offset + c->size;
    while (p + 20 <= end) {
        uint32_t name_off = u32(p);
        int32_t instance_type = i32(p + 4);
        uint32_t occurrences = u32(p + 12);
        uint32_t addr = u32(p + 16);
        p += 20;
        std::string name = string_at_offset(name_off);
        for (uint32_t k = 0; k < occurrences && addr < data_.size(); ++k) {
            var_by_addr_[addr] = VarRef{name, instance_type};
            uint32_t raw = u32(addr + 4);
            uint32_t next = raw & 0x07FFFFFF;
            if (next == 0) break;
            addr += next;
        }
    }
}

VarRef GameData::var_at(uint32_t push_addr) const {
    auto it = var_by_addr_.find(push_addr);
    return it == var_by_addr_.end() ? VarRef{"", 0} : it->second;
}

void GameData::parse_objects() {
    const Chunk* c = chunk("OBJT");
    if (!c) return;
    uint32_t count = u32(c->offset);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t ptr = u32(c->offset + 4 + i * 4);
        GameObject o;
        o.name = string_at_offset(u32(ptr));
        o.sprite_index = i32(ptr + 4);
        o.visible = i32(ptr + 8);
        o.depth = i32(ptr + 20);
        o.persistent = i32(ptr + 24);
        o.parent_index = i32(ptr + 28);
        o.mask_index = i32(ptr + 32);
        objects_.push_back(o);
    }
}

void GameData::parse_rooms() {
    const Chunk* c = chunk("ROOM");
    if (!c) return;
    uint32_t count = u32(c->offset);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t ptr = u32(c->offset + 4 + i * 4);
        Room r;
        r.name = string_at_offset(u32(ptr));
        r.width = i32(ptr + 8);
        r.height = i32(ptr + 12);
        r.speed = i32(ptr + 16);
        r.persistent = i32(ptr + 20);
        r.bg_color = u32(ptr + 24);
        r.creation_code = i32(ptr + 32);
        r.view_x = 0;
        r.view_y = 0;
        r.view_w = r.width;
        r.view_h = r.height;
        uint32_t flags = u32(ptr + 36);
        uint32_t views_ptr = u32(ptr + 44);
        if ((flags & 1) && views_ptr && u32(views_ptr) > 0) {
            uint32_t v0 = u32(views_ptr + 4);
            if (i32(v0) != 0) {
                r.view_x = i32(v0 + 4);
                r.view_y = i32(v0 + 8);
                int32_t vw = i32(v0 + 12), vh = i32(v0 + 16);
                if (vw > 0 && vh > 0) { r.view_w = vw; r.view_h = vh; }
                r.view_border_x = i32(v0 + 36);
                r.view_border_y = i32(v0 + 40);
                r.view_speed_x = i32(v0 + 44);
                r.view_speed_y = i32(v0 + 48);
                r.view_object = i32(v0 + 52);
            }
        }
        uint32_t inst_list = u32(ptr + 48);
        uint32_t ic = u32(inst_list);
        for (uint32_t k = 0; k < ic; ++k) {
            uint32_t ip = u32(inst_list + 4 + k * 4);
            RoomInstance ri;
            ri.x = i32(ip);
            ri.y = i32(ip + 4);
            ri.object_index = i32(ip + 8);
            ri.id = i32(ip + 12);
            ri.creation_code = i32(ip + 16);
            float sx, sy, rot;
            uint32_t usx = u32(ip + 20), usy = u32(ip + 24), urot = u32(ip + 40);
            std::memcpy(&sx, &usx, 4);
            std::memcpy(&sy, &usy, 4);
            std::memcpy(&rot, &urot, 4);
            ri.scale_x = (sx == 0.0f) ? 1.0 : sx;
            ri.scale_y = (sy == 0.0f) ? 1.0 : sy;
            ri.image_index = i32(ip + 32);
            ri.angle = rot;
            r.instances.push_back(ri);
        }

        const Chunk* sc = chunk("SPRT");
        int sprite_count = sc ? static_cast<int>(u32(sc->offset)) : 0;
        uint32_t layers = 0;
        for (uint32_t o : {88u, 84u, 80u, 92u, 56u, 60u, 64u}) {
            uint32_t lp = u32(ptr + o);
            if (lp <= c->offset || lp >= c->offset + c->size) continue;
            uint32_t n = u32(lp);
            if (n < 1 || n > 128) continue;
            uint32_t e0 = u32(lp + 4);
            if (e0 <= c->offset || e0 >= c->offset + c->size) continue;
            if (u32(e0 + 8) <= 7 && !string_at_offset(u32(e0)).empty()) { layers = lp; break; }
        }
        if (layers) {
            uint32_t lc = u32(layers);
            for (uint32_t k = 0; k < lc; ++k) {
                uint32_t lp = u32(layers + 4 + k * 4);
                RoomLayerRec rl;
                rl.name = string_at_offset(u32(lp));
                rl.id = i32(lp + 4);
                rl.type = (int32_t)u32(lp + 8);
                rl.depth = i32(lp + 12);
                rl.x = i32(lp + 16);
                rl.y = i32(lp + 20);
                rl.visible = i32(lp + 32) ? 1 : 0;
                if (rl.type == 1) {
                    int32_t sprite = i32(lp + 56);
                    rl.color = u32(lp + 72);
                    if (sprite >= 0 && sprite < sprite_count) rl.sprite = sprite;
                    uint32_t ht = u32(lp + 60), vt = u32(lp + 64), st = u32(lp + 68);
                    if (ht <= 1) rl.htiled = (int32_t)ht;
                    if (vt <= 1) rl.vtiled = (int32_t)vt;
                    if (st <= 1) rl.stretch = (int32_t)st;
                    r.layers.push_back(rl);
                } else if (rl.type == 2) {
                    uint32_t n = u32(lp + 48);
                    if (n <= r.instances.size() * 4 + 16) {
                        for (uint32_t j = 0; j < n; ++j) {
                            uint32_t id = u32(lp + 52 + j * 4);
                            for (auto& ins : r.instances)
                                if (static_cast<uint32_t>(ins.id) == id) ins.depth = rl.depth;
                        }
                    }
                    r.layers.push_back(rl);
                } else if (rl.type == 3) {
                    uint32_t tl = u32(lp + 48);
                    rl.tile_first = (int32_t)r.tiles.size();
                    if (tl > c->offset && tl < c->offset + c->size) {
                        uint32_t tn = u32(tl);
                        if (tn <= 100000) {
                            for (uint32_t ti = 0; ti < tn; ++ti) {
                                uint32_t tp = u32(tl + 4 + ti * 4);
                                RoomTile t;
                                t.x = i32(tp);
                                t.y = i32(tp + 4);
                                t.sprite = i32(tp + 8);
                                t.src_x = i32(tp + 12);
                                t.src_y = i32(tp + 16);
                                t.w = i32(tp + 20);
                                t.h = i32(tp + 24);
                                t.depth = i32(tp + 28);
                                float fsx, fsy;
                                uint32_t usx = u32(tp + 36), usy = u32(tp + 40);
                                std::memcpy(&fsx, &usx, 4);
                                std::memcpy(&fsy, &usy, 4);
                                t.scale_x = fsx;
                                t.scale_y = fsy;
                                t.color = u32(tp + 44);
                                if (t.sprite >= 0 && t.sprite < sprite_count && t.w > 0 && t.h > 0)
                                    r.tiles.push_back(t);
                            }
                        }
                    }
                    rl.tile_count = (int32_t)r.tiles.size() - rl.tile_first;
                    r.layers.push_back(rl);
                } else {
                    r.layers.push_back(rl);
                }
            }
        }
        rooms_.push_back(r);
    }
}

void GameData::parse_code() {
    const Chunk* c = chunk("CODE");
    if (!c) return;
    uint32_t count = u32(c->offset);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t eoff = u32(c->offset + 4 + i * 4);
        CodeEntry e;
        uint32_t name_off = u32(eoff);
        e.name = string_at_offset(name_off);
        e.length = u32(eoff + 4);
        e.locals = u16(eoff + 8);
        e.args = u16(eoff + 10) & 0x7FFF;
        int32_t rel = i32(eoff + 12);
        uint32_t child_off = u32(eoff + 16);
        e.bytecode_offset = (eoff + 12) + rel + child_off;
        if (child_off < e.length) e.length -= child_off;
        code_.push_back(e);
    }
}

}
