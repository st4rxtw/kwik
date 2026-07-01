#pragma once

#include <string>

namespace gml {

bool render_init(const char* title, int width, int height, unsigned int bg_color);
bool render_should_close();
void render_begin_frame();
void render_end_frame();
void render_draw_text(double x, double y, const std::string& text);
void render_shutdown();

}
