#include "display.hpp"

#include <cstring>

#include "config.h"

using namespace blit;

static SurfaceInfo cur_surf_info;

#if ALLOW_HIRES
uint16_t screen_fb[DISPLAY_WIDTH * DISPLAY_HEIGHT];
#else
uint16_t screen_fb[lores_page_size]; // double-buffered
#endif

static Pen *screen_palette = nullptr;
uint16_t *screen_palette565 = nullptr;

static const Size lores_screen_size(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
static const Size hires_screen_size(DISPLAY_WIDTH, DISPLAY_HEIGHT);

ScreenMode cur_screen_mode = ScreenMode::lores;
// double buffering for lores
static volatile int buf_index = 0;

static volatile bool do_render = true;

static void init_palette() {
  if(!screen_palette) {
    // allocate on first use
    screen_palette = new Pen[256];
    screen_palette565 = new uint16_t[256]();
  }
}

// blit api

SurfaceInfo &set_screen_mode(ScreenMode mode) {
  SurfaceTemplate temp{nullptr, {0, 0}, mode == ScreenMode::hires_palette ? PixelFormat::P : PixelFormat::RGB565};

  // may fail for hires/palette
  if(set_screen_mode_format(mode, temp)) {
    cur_surf_info.data = temp.data;
    cur_surf_info.bounds = temp.bounds;
    cur_surf_info.format = temp.format;
    cur_surf_info.palette = temp.palette;
  }

  return cur_surf_info;
}

bool set_screen_mode_format(ScreenMode new_mode, SurfaceTemplate &new_surf_template) {
  new_surf_template.data = (uint8_t *)screen_fb;

  switch(new_mode) {
    case ScreenMode::lores:
      new_surf_template.bounds = lores_screen_size;
      break;
    case ScreenMode::hires:
    case ScreenMode::hires_palette:
#if ALLOW_HIRES
      new_surf_template.bounds = hires_screen_size;
      break;
#else
      return false; // no hires for scanvideo
#endif
  }

  display_mode_changed(new_mode);

  if(new_surf_template.format == PixelFormat::P) {
#ifdef DISPLAY_PICODVI // only handled here so far

    init_palette();
    new_surf_template.palette = screen_palette;

#else
    return false;
#endif
  } else if(new_surf_template.format != PixelFormat::RGB565)
    return false; // don't support any other formats for various reasons (RAM, no format conversion, pixel double PIO)

  cur_screen_mode = new_mode;

  return true;
}

void set_screen_palette(const Pen *colours, int num_cols) {
  init_palette();
  memcpy(screen_palette, colours, num_cols * sizeof(Pen));

  for(int i = 0; i < num_cols; i++)
    screen_palette565[i] = (colours[i].r >> 3) | ((colours[i].g >> 2) << 5) | ((colours[i].b >> 3) << 11);
}
