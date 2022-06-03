#pragma once
#include <cstdint>

#include "engine/api_private.hpp"
#include "config.h"

// height rounded up to handle the 135px display
// this is in bytes
static const int lores_page_size = (DISPLAY_WIDTH / 2) * ((DISPLAY_HEIGHT + 1) / 2) * 2;

extern blit::ScreenMode cur_screen_mode;
#ifndef BLIT_BOARD_PIMORONI_PICOVISION
extern uint16_t screen_fb[];
#endif
extern uint16_t *screen_palette565;

inline int get_display_page_size() {
  if(cur_screen_mode == blit::ScreenMode::lores) // paletted is half the size
    return blit::screen.format == blit::PixelFormat::P ? lores_page_size / 2 : lores_page_size;
  else // paletted hires
    return DISPLAY_WIDTH * DISPLAY_HEIGHT;
}

void init_display();
void update_display(uint32_t time);

void init_display_core1();
void update_display_core1();

bool display_render_needed();

bool display_mode_supported(blit::ScreenMode new_mode, const blit::SurfaceTemplate &new_surf_template);

void display_mode_changed(blit::ScreenMode new_mode, blit::SurfaceTemplate &new_surf_template);

blit::SurfaceInfo &set_screen_mode(blit::ScreenMode mode);
bool set_screen_mode_format(blit::ScreenMode new_mode, blit::SurfaceTemplate &new_surf_template);

void set_screen_palette(const blit::Pen *colours, int num_cols);
