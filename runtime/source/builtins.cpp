#include "gml_runtime.h"

#include <cstdio>

namespace gml {

double& global_var(const std::string& name) {
    static std::unordered_map<std::string, double> globals;
    return globals[name];
}

void draw_text(double x, double y, const std::string& text) {
    std::printf("draw_text @ (%g, %g): \"%s\"\n", x, y, text.c_str());
}

}
