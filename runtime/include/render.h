#pragma once

namespace gml {

bool render_init(const char* title, int width, int height, unsigned int bg_color);
bool render_should_close();
void render_begin_frame();
void render_begin_gui();
void render_end_frame();
void render_idle();
void render_shutdown();

void render_set_view(double x, double y, double w, double h);
void render_set_room(int width, int height, unsigned int bg_color);
void render_set_title(const char* title);

double render_delta_time();
double render_time_ms();

int render_gui_width();
int render_gui_height();
void render_set_window_size(int width, int height);
void render_set_fullscreen(bool fs);
bool render_get_fullscreen();
void render_center_window();
int render_window_width();
int render_window_height();
int render_display_width();
int render_display_height();

bool render_key_down(int gml_vk);
bool render_key_pressed(int gml_vk);
bool render_key_released(int gml_vk);
double render_mouse_x();
double render_mouse_y();
bool render_mouse_down(int button);
bool render_mouse_pressed(int button);

void render_set_color(unsigned int bgr);
unsigned int render_get_color();
void render_set_alpha(double alpha);
double render_get_alpha();
void render_set_halign(int align);
void render_set_valign(int align);
int render_get_halign();
int render_get_valign();
void render_set_blendmode(int mode);

unsigned int render_upload_texture(const unsigned char* rgba, int w, int h);
bool render_app_surface_available();
unsigned int render_app_texture();
int render_app_width();
int render_app_height();
bool render_app_snapshot(int x, int y, int w, int h, unsigned char* rgba_out);
void render_draw_quad(unsigned int tex, double x, double y, double dw, double dh,
                      double origin_x, double origin_y, double xscale, double yscale,
                      double angle_deg, float u0, float v0, float u1, float v1,
                      unsigned int blend_bgr, double alpha);
void render_draw_glyph(unsigned int tex, double dx, double dy, double dw, double dh,
                       float u0, float v0, float u1, float v1);
void render_draw_glyph_colored(unsigned int tex, double dx, double dy, double dw, double dh,
                               float u0, float v0, float u1, float v1, unsigned int bgr,
                               double alpha);

void render_draw_rectangle(double x1, double y1, double x2, double y2, bool outline);
void render_draw_rectangle_color(double x1, double y1, double x2, double y2, unsigned int c1,
                                 unsigned int c2, unsigned int c3, unsigned int c4, bool outline);
void render_draw_line(double x1, double y1, double x2, double y2, double width, unsigned int c1,
                      unsigned int c2);
void render_draw_ellipse(double x1, double y1, double x2, double y2, unsigned int c1,
                         unsigned int c2, bool outline);
void render_draw_triangle(double x1, double y1, double x2, double y2, double x3, double y3,
                          unsigned int c1, unsigned int c2, unsigned int c3, bool outline);
void render_draw_point(double x, double y, unsigned int c);

}
