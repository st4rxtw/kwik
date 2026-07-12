#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kwik {

struct Chunk {
    std::string name;
    uint32_t offset;
    uint32_t size;
};

struct CodeEntry {
    std::string name;
    uint32_t length;
    uint16_t locals;
    uint16_t args;
    uint32_t bytecode_offset;
};

struct VarRef {
    std::string name;
    int32_t instance_type;
};

struct GameObject {
    std::string name;
    int32_t sprite_index = -1;
    int32_t parent_index = -100;
    int32_t persistent = 0;
    int32_t visible = 1;
    int32_t depth = 0;
    int32_t mask_index = -1;
};

struct RoomInstance {
    int32_t x;
    int32_t y;
    int32_t object_index;
    int32_t id;
    double scale_x;
    double scale_y;
    int32_t image_index;
    double angle;
    int32_t depth = 0;
    int32_t creation_code = -1;
    int32_t precreate_code = -1;
};

struct RoomLayerRec {
    std::string name;
    int32_t id = 0;
    int32_t type = 0;
    int32_t depth = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t visible = 1;
    int32_t sprite = -1;
    int32_t htiled = 0;
    int32_t vtiled = 0;
    int32_t stretch = 0;
    uint32_t color = 0xFFFFFFFF;
    int32_t tile_first = 0;
    int32_t tile_count = 0;
    int32_t tileset = -1;
    int32_t grid_w = 0;
    int32_t grid_h = 0;
    std::vector<uint32_t> grid;
};

struct RoomTile {
    int32_t x;
    int32_t y;
    int32_t sprite;
    int32_t src_x;
    int32_t src_y;
    int32_t w;
    int32_t h;
    int32_t depth;
    double scale_x = 1.0;
    double scale_y = 1.0;
    uint32_t color = 0xFFFFFFFF;
    double angle = 0.0;
    int32_t frame = 0;
    int32_t whole = 0;
};

struct Room {
    std::string name;
    int32_t width;
    int32_t height;
    uint32_t bg_color;
    int32_t speed = 30;
    int32_t persistent = 0;
    int32_t creation_code = -1;
    int32_t view_x = 0;
    int32_t view_y = 0;
    int32_t view_w = 0;
    int32_t view_h = 0;
    int32_t view_border_x = 32;
    int32_t view_border_y = 32;
    int32_t view_speed_x = -1;
    int32_t view_speed_y = -1;
    int32_t view_object = -1;
    std::vector<RoomInstance> instances;
    std::vector<RoomLayerRec> layers;
    std::vector<RoomTile> tiles;
};

class GameData {
public:
    bool load(const std::string& path);
    const std::string& source_path() const { return source_path_; }
    std::string source_dir() const {
        size_t p = source_path_.find_last_of("/\\");
        return p == std::string::npos ? std::string(".") : source_path_.substr(0, p);
    }

    const std::vector<uint8_t>& bytes() const { return data_; }
    const std::unordered_map<std::string, Chunk>& chunks() const { return chunks_; }
    const Chunk* chunk(const std::string& name) const;

    const std::vector<std::string>& strings() const { return strings_; }
    std::string string_at_offset(uint32_t off) const;

    const std::vector<CodeEntry>& code() const { return code_; }
    const std::vector<GameObject>& objects() const { return objects_; }
    const std::vector<Room>& rooms() const { return rooms_; }
    const std::vector<uint32_t>& global_init_ids() const { return global_init_; }
    int window_w() const { return window_w_; }
    int window_h() const { return window_h_; }
    int game_fps() const { return game_fps_; }
    int start_room() const { return start_room_; }
    const std::string& game_name() const { return game_name_; }
    const std::string& display_name() const { return display_name_; }

    std::string function_at_call(uint32_t call_addr) const;
    std::string function_by_index(uint32_t idx) const {
        return idx < func_names_.size() ? func_names_[idx] : std::string();
    }
    int32_t script_code_index(const std::string& name) const {
        auto it = scripts_.find(name);
        return it == scripts_.end() ? -1 : it->second;
    }
    VarRef var_at(uint32_t push_addr) const;

    uint8_t u8(uint32_t off) const;
    uint16_t u16(uint32_t off) const;
    uint32_t u32(uint32_t off) const;
    int32_t i32(uint32_t off) const;

private:
    void parse_chunks();
    void parse_strings();
    void parse_functions();
    void parse_variables();
    void parse_code();
    void parse_objects();
    void parse_scripts();
    void parse_rooms();
    void parse_glob();
    void parse_gen8();

    std::vector<uint8_t> data_;
    std::unordered_map<std::string, Chunk> chunks_;
    std::vector<std::string> strings_;
    std::vector<CodeEntry> code_;
    std::unordered_map<uint32_t, std::string> func_by_call_;
    std::vector<std::string> func_names_;
    std::unordered_map<uint32_t, VarRef> var_by_addr_;
    std::vector<GameObject> objects_;
    std::unordered_map<std::string, int32_t> scripts_;
    std::vector<Room> rooms_;
    std::vector<uint32_t> global_init_;
    std::string source_path_;
    int window_w_ = 640;
    int window_h_ = 480;
    int game_fps_ = 30;
    int start_room_ = 0;
    std::string game_name_ = "kwik_game";
    std::string display_name_;
};

}
