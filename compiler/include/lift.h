#pragma once

#include <string>

#include "iff.h"

namespace kwik {

struct ExportOptions {
    std::string target;
    std::string nx_runtime_root;
    std::string nx_template_dir;
};

std::string lift_code_entry(const GameData& gd, const CodeEntry& entry);
bool emit_cpp(const GameData& gd, const std::string& out_path);
bool emit_dir(const GameData& gd, const std::string& out_dir, const ExportOptions& options = {});

}
