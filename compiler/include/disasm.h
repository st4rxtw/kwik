#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "iff.h"

namespace kwik {

enum class DataType : uint8_t {
    Double = 0x0,
    Float = 0x1,
    Int32 = 0x2,
    Int64 = 0x3,
    Bool = 0x4,
    Variable = 0x5,
    String = 0x6,
    Int16 = 0xF,
};

struct Instruction {
    uint32_t address;
    uint8_t opcode;
    uint8_t type1;
    uint8_t type2;
    int16_t operand;
    bool has_extra;
    uint32_t extra;
    uint8_t size;
};

std::vector<Instruction> disassemble(const GameData& gd, const CodeEntry& entry);
std::string mnemonic(uint8_t opcode);
std::string format_instruction(const GameData& gd, const Instruction& in);

}
