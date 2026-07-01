#include "gml_runtime.h"
#include "render.h"

namespace gml {

double& global_var(const std::string& name) {
    static std::unordered_map<std::string, double> globals;
    return globals[name];
}

void draw_text(double x, double y, const std::string& text) {
    render_draw_text(x, y, text);
}

}
