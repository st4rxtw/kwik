#pragma once

#include <string>

namespace gml {

bool render_init(const char* title, int width, int height, unsigned int bg_color);
bool render_should_close();
void render_begin_frame();
void render_end_frame();
void render_shutdown();

void render_draw_text(double x, double y, const std::string& text);
void render_draw_rectangle(double x1, double y1, double x2, double y2, bool outline);

void render_set_color(unsigned int bgr);
void render_set_alpha(double alpha);
void render_set_halign(int align);
void render_set_valign(int align);

int render_gui_width();
int render_gui_height();
void render_set_window_size(int width, int height);

bool render_key_down(int gml_vk);
bool render_key_pressed(int gml_vk);

unsigned int render_upload_texture(const unsigned char* rgba, int w, int h);
void render_draw_sprite(unsigned int tex, double x, double y, double w, double h,
                        double origin_x, double origin_y, double xscale, double yscale,
                        double angle_deg, double alpha);
void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1);
int render_get_halign();
int render_get_valign();

}
