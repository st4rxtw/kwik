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
};

struct RoomInstance {
    int32_t x;
    int32_t y;
    int32_t object_index;
    int32_t id;
};

struct Room {
    std::string name;
    int32_t width;
    int32_t height;
    uint32_t bg_color;
    std::vector<RoomInstance> instances;
};

class GameData {
public:
    bool load(const std::string& path);

    const std::vector<uint8_t>& bytes() const { return data_; }
    const std::unordered_map<std::string, Chunk>& chunks() const { return chunks_; }
    const Chunk* chunk(const std::string& name) const;

    const std::vector<std::string>& strings() const { return strings_; }
    std::string string_at_offset(uint32_t off) const;

    const std::vector<CodeEntry>& code() const { return code_; }
    const std::vector<GameObject>& objects() const { return objects_; }
    const std::vector<Room>& rooms() const { return rooms_; }

    std::string function_at_call(uint32_t call_addr) const;
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
    void parse_rooms();

    std::vector<uint8_t> data_;
    std::unordered_map<std::string, Chunk> chunks_;
    std::vector<std::string> strings_;
    std::vector<CodeEntry> code_;
    std::unordered_map<uint32_t, std::string> func_by_call_;
    std::unordered_map<uint32_t, VarRef> var_by_addr_;
    std::vector<GameObject> objects_;
    std::vector<Room> rooms_;
};

}
