#pragma once

#include <string>

#include "iff.h"

namespace kwik {

std::string lift_code_entry(const GameData& gd, const CodeEntry& entry);
bool emit_cpp(const GameData& gd, const std::string& out_path);

}
