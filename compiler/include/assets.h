#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "iff.h"

namespace kwik {

struct SpriteInfo {
    std::string name;
    int first_frame;
    int frame_count;
    int origin_x;
    int origin_y;
    int width;
    int height;
    int bbox_left;
    int bbox_top;
    int bbox_right;
    int bbox_bottom;
    double speed;
    int speed_type;
};

struct GlyphInfo {
    int ch;
    int x, y, w, h;
    int shift, offset;
};

struct FontInfo {
    std::string name;
    int atlas_image;
    int glyph_start;
    int glyph_count;
    int size;
};

struct AssetExtraction {
    int image_count = 0;
    int sound_count = 0;
    std::vector<SpriteInfo> sprites;
    std::vector<std::string> sound_names;
    std::vector<int> sound_audio_id;
    std::vector<FontInfo> fonts;
    std::vector<GlyphInfo> glyphs;
};

bool extract_assets(const GameData& gd, const std::string& out_dir, AssetExtraction& out);

}
