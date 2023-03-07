#include "display.hpp"

#include <cstring>

#include "config.h"

using namespace blit;

// height rounded up to handle the 135px display
// this is in bytes
static const int lores_page_size = (DISPLAY_WIDTH / 2) * ((DISPLAY_HEIGHT + 1) / 2) * 2;

SurfaceInfo cur_surf_info;

bool fb_double_buffer = true;

#if defined(BUILD_LOADER) || defined(BLIT_BOARD_PIMORONI_PICOVISION)
uint16_t *screen_fb = nullptr;
static uint32_t max_fb_size = 0;
#elif ALLOW_HIRES
uint16_t screen_fb[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static const uint32_t max_fb_size = sizeof(screen_fb);
#else
uint16_t screen_fb[lores_page_size]; // double-buffered
static const uint32_t max_fb_size = sizeof(screen_fb);
#endif

static Pen *screen_palette = nullptr;
uint16_t *screen_palette565 = nullptr;

static const Size lores_screen_size(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
static const Size hires_screen_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);

ScreenMode cur_screen_mode = ScreenMode::lores;

static void init_palette() {
  if(!screen_palette) {
    // allocate on first use
    screen_palette = new Pen[256];
    screen_palette565 = new uint16_t[256]();
  }
}

int get_display_page_size() {
  return cur_surf_info.bounds.area() * pixel_format_stride[int(cur_surf_info.format)];
}

// blit api

SurfaceInfo &set_screen_mode(ScreenMode mode) {
  SurfaceTemplate temp{nullptr, {0, 0}, mode == ScreenMode::hires_palette ? PixelFormat::P : DEFAULT_SCREEN_FORMAT};

  // may fail for hires/palette
  if(set_screen_mode_format(mode, temp)) {
    cur_surf_info.data = temp.data;
    cur_surf_info.palette = temp.palette;
  }

  return cur_surf_info;
}

bool set_screen_mode_format(ScreenMode new_mode, SurfaceTemplate &new_surf_template) {
  new_surf_template.data = (uint8_t *)screen_fb;

  if(new_surf_template.format == (PixelFormat)-1)
    new_surf_template.format = DEFAULT_SCREEN_FORMAT;

  switch(new_mode) {
    case ScreenMode::lores:
      if(new_surf_template.bounds.empty())
        new_surf_template.bounds = lores_screen_size;
      else
        new_surf_template.bounds /= 2;

      break;
    case ScreenMode::hires:
    case ScreenMode::hires_palette:
      if(new_surf_template.bounds.empty())
        new_surf_template.bounds = hires_screen_size;
      break;
  }

  // check the framebuffer is large enough for mode
  auto fb_size = uint32_t(new_surf_template.bounds.area()) * pixel_format_stride[int(new_surf_template.format)];
// TODO: more generic "doesn't have a framebuffer"?
#ifndef BLIT_BOARD_PIMORONI_PICOVISION
  if(max_fb_size < fb_size)
    return false;
#endif

  if(!display_mode_supported(new_mode, new_surf_template))
    return false;

  if(new_surf_template.format == PixelFormat::P) {
    init_palette();
    new_surf_template.palette = screen_palette;
  }

  fb_double_buffer = fb_size * 2 <= max_fb_size;
  if(!fb_double_buffer)
    screen.data = new_surf_template.data;

  cur_surf_info.bounds = new_surf_template.bounds;
  cur_surf_info.format = new_surf_template.format;

  display_mode_changed(new_mode, new_surf_template);

  cur_screen_mode = new_mode;

  return true;
}

void set_screen_palette(const Pen *colours, int num_cols) {
  init_palette();
  memcpy(screen_palette, colours, num_cols * sizeof(Pen));

  for(int i = 0; i < num_cols; i++)
    screen_palette565[i] = (colours[i].r >> 3) | ((colours[i].g >> 2) << 5) | ((colours[i].b >> 3) << 11);
}

void set_framebuffer(uint8_t *data, uint32_t max_size) {
#ifdef BUILD_LOADER
  screen_fb = (uint16_t *)data;
  max_fb_size = max_size;
#endif
}
