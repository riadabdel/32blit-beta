#include "display.hpp"

#include <cstring>

#include "config.h"

#ifdef DISPLAY_ST7789
#include "st7789.hpp"
#elif defined(DISPLAY_SCANVIDEO)
#include "hardware/clocks.h"
#include "pico/time.h"
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#elif defined(DISPLAY_PICODVI)
#include "hardware/irq.h"
#include "pico/stdlib.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

extern "C" {
#include "dvi.h"
#include "tmds_encode.h"
#include "common_dvi_pin_configs.h"
}

#pragma GCC diagnostic pop

#define DVI_TIMING dvi_timing_640x480p_60hz
#endif

using namespace blit;

static SurfaceInfo cur_surf_info;

// height rounded up to handle the 135px display
// this is in bytes
static const int lores_page_size = (DISPLAY_WIDTH / 2) * ((DISPLAY_HEIGHT + 1) / 2) * 2;

#if ALLOW_HIRES
static uint16_t screen_fb[DISPLAY_WIDTH * DISPLAY_HEIGHT];
#else
static uint16_t screen_fb[lores_page_size]; // double-buffered
#endif

static Pen *screen_palette = nullptr;
static uint16_t *screen_palette565 = nullptr;

static const blit::SurfaceTemplate lores_screen{(uint8_t *)screen_fb, Size(DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2), blit::PixelFormat::RGB565, nullptr};
static const blit::SurfaceTemplate hires_screen{(uint8_t *)screen_fb, Size(DISPLAY_WIDTH, DISPLAY_HEIGHT), blit::PixelFormat::RGB565, nullptr};

static ScreenMode cur_screen_mode = ScreenMode::lores;
// double buffering for lores
static volatile int buf_index = 0;

static volatile bool do_render = true;

// user render function
void render(uint32_t);

#ifdef DISPLAY_ST7789
static bool have_vsync = false;
static bool backlight_enabled = false;
static uint32_t last_render = 0;

static void vsync_callback(uint gpio, uint32_t events) {
  if(!do_render && !st7789::dma_is_busy()) {
    st7789::update();
    do_render = true;
  }
}
#endif

#ifdef DISPLAY_SCANVIDEO
static bool do_render_soon = false; // slightly delayed to handle the queue

static void fill_scanline_buffer(struct scanvideo_scanline_buffer *buffer) {
  static uint32_t postamble[] = {
    0x0000u | (COMPOSABLE_EOL_ALIGN << 16)
  };

  int w = screen.bounds.w;

  buffer->data[0] = 4;
  buffer->data[1] = host_safe_hw_ptr(buffer->data + 8);
  buffer->data[2] = (w - 4) / 2; // first four pixels are handled separately
  uint16_t *pixels = screen_fb + buf_index * (160 * 120) + scanvideo_scanline_number(buffer->scanline_id) * w;
  buffer->data[3] = host_safe_hw_ptr(pixels + 4);
  buffer->data[4] = count_of(postamble);
  buffer->data[5] = host_safe_hw_ptr(postamble);
  buffer->data[6] = 0;
  buffer->data[7] = 0;
  buffer->data_used = 8;

  // 3 pixel run followed by main run, consuming the first 4 pixels
  buffer->data[8] = (pixels[0] << 16u) | COMPOSABLE_RAW_RUN;
  buffer->data[9] = (pixels[1] << 16u) | 0;
  buffer->data[10] = (COMPOSABLE_RAW_RUN << 16u) | pixels[2];
  buffer->data[11] = (((w - 3) + 1 - 3) << 16u) | pixels[3]; // note we add one for the black pixel at the end
}
#endif

#ifdef DISPLAY_PICODVI
static dvi_inst dvi0;

static void __not_in_flash_func(dvi_loop)() {
  auto inst = &dvi0;

  uint32_t double_buf[160];
  int y = 0;

  while(true) {

    uint32_t *scanbuf;

    if(cur_screen_mode == ScreenMode::lores) {
      const auto lores_w = DISPLAY_WIDTH / 2;
      const auto lores_h = DISPLAY_HEIGHT / 2;

      // pixel double x2
      if(!(y & 1)) {
        auto out = double_buf;

        if(screen.format == PixelFormat::P) {
          auto in = (uint8_t *)screen_fb + buf_index * (lores_w * lores_h) + (y / 2) * lores_w;

          for(int i = 0; i < lores_w; i++) {
            auto pixel = screen_palette565[*in++];
            *out++ = pixel | pixel << 16;
          }
        } else {
          // RGB565
          auto in = screen_fb + buf_index * (lores_w * lores_h) + (y / 2) * lores_w;
          for(int i = 0; i < lores_w; i++) {
            auto pixel = *in++;
            *out++ = pixel | pixel << 16;
          }
        }
      }

      scanbuf = double_buf;
    } else if(screen.format == PixelFormat::P) {
      // paletted hires
      auto out = double_buf;
      auto in = (uint8_t *)screen_fb + buf_index * (DISPLAY_WIDTH * DISPLAY_HEIGHT) * ALLOW_HIRES + y * DISPLAY_WIDTH;

      for(int i = 0; i < 160; i++) {
        auto pixel0 = screen_palette565[*in++];
        auto pixel1 = screen_palette565[*in++];
        *out++ = pixel0 | pixel1 << 16;
      }

      scanbuf = double_buf;
    } else {
      // hires
      scanbuf = (uint32_t *)(screen_fb + y * DISPLAY_WIDTH);
    }

    //copy/paste of dvi_prepare_scanline_16bpp
    uint32_t *tmdsbuf;
    queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
    uint pixwidth = inst->timing->h_active_pixels;
    uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB,  DVI_16BPP_BLUE_LSB );
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
    tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB,   DVI_16BPP_RED_LSB  );
    queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);

    y++;
    if(y == DISPLAY_HEIGHT) {
      y = 0;
      if(!do_render) {
        do_render = true;
        buf_index ^= 1;
      }
    }
  }
}
#endif

void init_display() {
#ifdef DISPLAY_ST7789
  st7789::frame_buffer = screen_fb;
  st7789::init();
  st7789::clear();

  have_vsync = st7789::vsync_callback(vsync_callback);

#elif defined(DISPLAY_PICODVI)
  // assuming the default overclock hasn't been disabled, overvolt should already be set
  set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

  dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
#endif
}

void update_display(uint32_t time) {

#ifdef DISPLAY_ST7789
  if((do_render || (!have_vsync && time - last_render >= 20)) && (cur_screen_mode == ScreenMode::lores || !st7789::dma_is_busy())) {
    if(cur_screen_mode == ScreenMode::lores) {
      buf_index ^= 1;

      screen.data = (uint8_t *)screen_fb + (buf_index) * lores_page_size;
      st7789::frame_buffer = (uint16_t *)screen.data;
    }

    ::render(time);

    if(!have_vsync) {
      while(st7789::dma_is_busy()) {} // may need to wait for lores.
      st7789::update();
    }

    if(last_render && !backlight_enabled) {
      // the first render should have made it to the screen at this point
      st7789::set_backlight(255);
      backlight_enabled = true;
    }

    last_render = time;
    do_render = false;
  }

#elif defined(DISPLAY_SCANVIDEO) || defined(DISPLAY_PICODVI)
  if(do_render) {
    if(cur_screen_mode == ScreenMode::lores || (screen.format == PixelFormat::P && ALLOW_HIRES)) {
      // swap pages
      int page_size;
      if(cur_screen_mode == ScreenMode::lores)
        page_size = screen.format == PixelFormat::P ? lores_page_size / 2 : lores_page_size;
      else // paletted hires
        page_size = DISPLAY_WIDTH * DISPLAY_HEIGHT;

      screen.data = (uint8_t *)screen_fb + (buf_index ^ 1) * page_size; // only works because there's no "firmware" here
    }

    ::render(time);
    do_render = false;
  }
#endif
}

void init_display_core1() {
#ifdef DISPLAY_SCANVIDEO
  // no mode switching yet
#if ALLOW_HIRES
#if DISPLAY_HEIGHT == 160 // extra middle mode
  scanvideo_setup(&vga_mode_213x160_60);
#else
  scanvideo_setup(&vga_mode_320x240_60);
#endif
#else
  scanvideo_setup(&vga_mode_160x120_60);
#endif

  scanvideo_timing_enable(true);

#elif defined(DISPLAY_PICODVI)
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  // TODO: this assumes nothing else wants to use core 1 (it doesn't return)
  dvi_loop();
#endif
}

void update_display_core1() {
#ifdef DISPLAY_SCANVIDEO
  struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation(true);
  while (buffer) {
    fill_scanline_buffer(buffer);
    scanvideo_end_scanline_generation(buffer);

    const int height = screen.bounds.h;

    if(scanvideo_scanline_number(buffer->scanline_id) == height - 1 && !do_render) {
      // swap buffers at the end of the frame, but don't start a render yet
      // (the last few lines of the old buffer are still in the queue)
      if(cur_screen_mode == ScreenMode::lores) {
        do_render_soon = true;
        buf_index ^= 1;
      } else {
        // hires is single buffered and disabled by default
        // rendering correctly is the user's problem
        do_render = true;
      }
      break;
    } else if(do_render_soon && scanvideo_scanline_number(buffer->scanline_id) == PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT - 1) {
      // should be safe to reuse old buffer now
      do_render = do_render_soon;
      do_render_soon = false;
    }

    buffer = scanvideo_begin_scanline_generation(false);
  }
#endif
}

bool display_render_needed() {
  return do_render;
}

static void init_palette() {
    if(!screen_palette) {
    // allocate on first use
    screen_palette = new Pen[256];
    screen_palette565 = new uint16_t[256]();
  }
}

// blit api

SurfaceInfo &set_screen_mode(ScreenMode mode) {
  switch(mode) {
    case ScreenMode::lores:
      cur_surf_info = lores_screen;
      break;

    case ScreenMode::hires:
#if ALLOW_HIRES
      cur_surf_info = hires_screen;
#else
      return cur_surf_info;
#endif
      break;

    case ScreenMode::hires_palette:
      return cur_surf_info; // unsupported
  }

#ifdef DISPLAY_ST7789
  if(have_vsync)
    do_render = true; // prevent starting an update during switch

  st7789::set_pixel_double(mode == ScreenMode::lores);

  if(mode == ScreenMode::hires)
    st7789::frame_buffer = screen_fb;
#endif

  cur_screen_mode = mode;

  return cur_surf_info;
}

bool set_screen_mode_format(ScreenMode new_mode, SurfaceTemplate &new_surf_template) {
  new_surf_template.data = (uint8_t *)screen_fb;

  switch(new_mode) {
    case ScreenMode::lores:
      new_surf_template.bounds = lores_screen.bounds;
      break;
    case ScreenMode::hires:
    case ScreenMode::hires_palette:
      new_surf_template.bounds = hires_screen.bounds;
#if ALLOW_HIRES
      break;
#else
      // can squeeze single-buffered hires paletted into the double-buffered lores fb
      if(new_surf_template.format != PixelFormat::P)
        return false;
#endif
  }

  if(new_surf_template.format == PixelFormat::P) {
#ifdef DISPLAY_PICODVI // only handled here so far

    init_palette();
    new_surf_template.palette = screen_palette;

#else
    return false;
#endif
  } else if(new_surf_template.format != PixelFormat::RGB565)
    return false; // don't support any other formats for various reasons (RAM, no format conversion, pixel double PIO)

#ifdef DISPLAY_ST7789
  if(have_vsync)
    do_render = true; // prevent starting an update during switch

  st7789::set_pixel_double(new_mode == ScreenMode::lores);

  if(new_mode == ScreenMode::hires)
    st7789::frame_buffer = screen_fb;
#endif

  cur_screen_mode = new_mode;

  return true;
}

void set_screen_palette(const Pen *colours, int num_cols) {
  init_palette();
  memcpy(screen_palette, colours, num_cols * sizeof(Pen));

  for(int i = 0; i < num_cols; i++)
    screen_palette565[i] = (colours[i].r >> 3) | ((colours[i].g >> 2) << 5) | ((colours[i].b >> 3) << 11);
}
