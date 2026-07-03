#include "disasm.h"

#include <cstdio>

namespace kwik {

std::string mnemonic(uint8_t op) {
    switch (op) {
        case 0x07: return "conv";
        case 0x08: return "mul";
        case 0x09: return "div";
        case 0x0A: return "rem";
        case 0x0B: return "mod";
        case 0x0C: return "add";
        case 0x0D: return "sub";
        case 0x0E: return "and";
        case 0x0F: return "or";
        case 0x10: return "xor";
        case 0x11: return "neg";
        case 0x12: return "not";
        case 0x13: return "shl";
        case 0x14: return "shr";
        case 0x15: return "cmp";
        case 0x45: return "pop";
        case 0x84: return "pushi";
        case 0x86: return "dup";
        case 0x9C: return "ret";
        case 0x9D: return "exit";
        case 0x9E: return "popz";
        case 0xB6: return "b";
        case 0xB7: return "bt";
        case 0xB8: return "bf";
        case 0xBA: return "pushenv";
        case 0xBB: return "popenv";
        case 0xC0: return "push";
        case 0xC1: return "pushloc";
        case 0xC2: return "pushglb";
        case 0xC3: return "pushbltn";
        case 0xD9: return "call";
        case 0x99: return "callv";
        case 0xFF: return "break";
        default: return "???";
    }
}

static bool is_push(uint8_t op) {
    return op == 0xC0 || op == 0xC1 || op == 0xC2 || op == 0xC3;
}

static uint8_t push_extra_words(uint8_t type1) {
    switch (type1) {
        case 0x0: return 2;
        case 0x3: return 2;
        case 0x1: return 1;
        case 0x2: return 1;
        case 0x4: return 1;
        case 0x5: return 1;
        case 0x6: return 1;
        default: return 0;
    }
}

std::vector<Instruction> disassemble(const GameData& gd, const CodeEntry& e) {
    std::vector<Instruction> out;
    uint32_t addr = e.bytecode_offset;
    uint32_t end = e.bytecode_offset + e.length;
    while (addr < end) {
        uint32_t word = gd.u32(addr);
        Instruction in{};
        in.address = addr;
        in.opcode = (word >> 24) & 0xFF;
        uint8_t type_byte = (word >> 16) & 0xFF;
        in.type1 = type_byte & 0x0F;
        in.type2 = (type_byte >> 4) & 0x0F;
        in.operand = static_cast<int16_t>(word & 0xFFFF);
        in.has_extra = false;
        in.extra = 0;
        in.size = 4;
        in.jump_target = 0;
        in.cmp_kind = 0;

        if (in.opcode == 0xD9 || in.opcode == 0x45) {
            in.has_extra = true;
            in.extra = gd.u32(addr + 4);
            in.size = 8;
        } else if (is_push(in.opcode)) {
            uint8_t words = push_extra_words(in.type1);
            if (words > 0) {
                in.has_extra = true;
                in.extra = gd.u32(addr + 4);
                in.size = 4 + words * 4;
            }
        } else if (in.opcode == 0xFF) {
            if (in.operand == -11) {
                in.has_extra = true;
                in.extra = gd.u32(addr + 4);
                in.size = 8;
            }
        } else if (in.opcode == 0xB6 || in.opcode == 0xB7 || in.opcode == 0xB8 ||
                   in.opcode == 0xBA || in.opcode == 0xBB) {
            int32_t off = word & 0x007FFFFF;
            if (off & 0x00400000) off |= 0xFF800000;
            in.jump_target = addr + off * 4;
        } else if (in.opcode == 0x15) {
            in.cmp_kind = (word >> 8) & 0xFF;
        }
        out.push_back(in);
        addr += in.size;
    }
    return out;
}

static const char* type_name(uint8_t t) {
    switch (t) {
        case 0x0: return "double";
        case 0x1: return "float";
        case 0x2: return "int32";
        case 0x3: return "int64";
        case 0x4: return "bool";
        case 0x5: return "var";
        case 0x6: return "string";
        case 0xF: return "int16";
        default: return "?";
    }
}

std::string format_instruction(const GameData& gd, const Instruction& in) {
    char buf[256];
    std::string m = mnemonic(in.opcode);

    if (in.opcode == 0xD9 || in.opcode == 0x99) {
        std::string fn = gd.function_at_call(in.address + 4);
        if (fn.empty()) fn = "func@" + std::to_string(in.extra);
        std::snprintf(buf, sizeof(buf), "%-8s %s argc=%d", m.c_str(), fn.c_str(), in.operand);
        return buf;
    }

    if (is_push(in.opcode)) {
        if (in.type1 == 0x6 && in.extra < gd.strings().size()) {
            std::snprintf(buf, sizeof(buf), "%-8s string \"%s\"", m.c_str(), gd.strings()[in.extra].c_str());
            return buf;
        }
        if (in.type1 == 0xF) {
            std::snprintf(buf, sizeof(buf), "%-8s int16 %d", m.c_str(), in.operand);
            return buf;
        }
        if (in.has_extra) {
            std::snprintf(buf, sizeof(buf), "%-8s %s %u", m.c_str(), type_name(in.type1), in.extra);
            return buf;
        }
        std::snprintf(buf, sizeof(buf), "%-8s %s", m.c_str(), type_name(in.type1));
        return buf;
    }

    if (in.opcode == 0x84) {
        std::snprintf(buf, sizeof(buf), "%-8s %d", m.c_str(), in.operand);
        return buf;
    }

    if (in.opcode == 0x07) {
        std::snprintf(buf, sizeof(buf), "%-8s %s -> %s", m.c_str(), type_name(in.type1), type_name(in.type2));
        return buf;
    }

    std::snprintf(buf, sizeof(buf), "%s", m.c_str());
    return buf;
}

}
