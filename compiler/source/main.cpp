#include <cstdio>
#include <cstring>
#include <string>

#include "disasm.h"
#include "iff.h"
#include "lift.h"

using namespace kwik;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <game.unx> [<out_dir>] [--emit <out.cpp>] [--emit-dir <out_dir>]\n",
                     argv[0]);
        return 1;
    }

    std::string emit_path;
    std::string emit_dir_path;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--emit") == 0 && i + 1 < argc)
            emit_path = argv[++i];
        else if (std::strcmp(argv[i], "--emit-dir") == 0 && i + 1 < argc)
            emit_dir_path = argv[++i];
        else if (argv[i][0] != '-' && emit_dir_path.empty())
            emit_dir_path = argv[i];
    }

    GameData gd;
    if (!gd.load(argv[1])) {
        std::fprintf(stderr, "failed to load %s\n", argv[1]);
        return 1;
    }

    if (!emit_dir_path.empty()) {
        if (!emit_dir(gd, emit_dir_path)) {
            std::fprintf(stderr, "failed to write %s\n", emit_dir_path.c_str());
            return 1;
        }
        std::printf("wrote %s/\n", emit_dir_path.c_str());
        return 0;
    }

    if (!emit_path.empty()) {
        if (!emit_cpp(gd, emit_path)) {
            std::fprintf(stderr, "failed to write %s\n", emit_path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", emit_path.c_str());
        return 0;
    }

    std::printf("== chunks ==\n");
    for (const auto& kv : gd.chunks())
        std::printf("  %-4s  offset=0x%08x  size=%u\n", kv.second.name.c_str(), kv.second.offset, kv.second.size);

    std::printf("\n== strings (%zu) ==\n", gd.strings().size());
    for (size_t i = 0; i < gd.strings().size(); ++i)
        std::printf("  [%zu] \"%s\"\n", i, gd.strings()[i].c_str());

    std::printf("\n== code (%zu entries) ==\n", gd.code().size());
    for (const auto& e : gd.code()) {
        std::printf("\n%s  (len=%u locals=%u args=%u bc=0x%x)\n", e.name.c_str(), e.length, e.locals, e.args,
                    e.bytecode_offset);
        for (const auto& in : disassemble(gd, e))
            std::printf("  0x%04x:  %s\n", in.address, format_instruction(gd, in).c_str());
    }

    return 0;
}
