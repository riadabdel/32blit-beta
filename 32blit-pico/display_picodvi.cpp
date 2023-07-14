#include "display.hpp"

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

using namespace blit;

// double buffering for lores
static uint8_t *cur_display_buffer = nullptr;

static volatile bool do_render = true;
static dvi_inst dvi0;
static int y = 0;

static void __no_inline_not_in_flash_func(dvi_update)() {
  auto inst = &dvi0;

  if(!cur_display_buffer)
    return;

  uint32_t double_buf[160];

  while(true) {
    uint32_t *tmdsbuf;
    if(!queue_try_remove_u32(&inst->q_tmds_free, &tmdsbuf)) {
      // blocking wait for second pixel-doubled line
      // or we'll lose the data
      if(cur_screen_mode == ScreenMode::lores && (y & 1))
        queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
      else
        return;
    }

    const auto w = cur_surf_info.bounds.w;
    uint32_t *scanbuf;

    if(cur_screen_mode == ScreenMode::lores) {

      // pixel double x2
      if(!(y & 1)) {
        auto out = double_buf;

        int pad_x = ((DISPLAY_WIDTH / 2) - w) / 2;

        for(int i = 0; i < pad_x; i++)
          *out++ = 0;

        if(cur_surf_info.format == PixelFormat::P) {
          auto in = cur_display_buffer + (y / 2) * w;

          for(int i = 0; i < w; i++) {
            auto pixel = screen_palette565[*in++];
            *out++ = pixel | pixel << 16;
          }
        } else {
          // RGB565
          auto in = (uint16_t *)cur_display_buffer + (y / 2) * w;
          for(int i = 0; i < w; i++) {
            auto pixel = *in++;
            *out++ = pixel | pixel << 16;
          }
        }

        for(int i = 0; i < pad_x; i++)
          *out++ = 0;
      }

      scanbuf = double_buf;
    } else if(cur_surf_info.format == PixelFormat::P) {
      // paletted hires
      auto out = double_buf;
      auto in = cur_display_buffer + y * w;

      for(int i = 0; i < w; i++) {
        auto pixel0 = screen_palette565[*in++];
        auto pixel1 = screen_palette565[*in++];
        *out++ = pixel0 | pixel1 << 16;
      }

      scanbuf = double_buf;
    } else {
      // hires
      scanbuf = (uint32_t *)(cur_display_buffer + y * w / 2);
    }

    // dvi_prepare_scanline_16bpp
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
        if(fb_double_buffer)
          std::swap(screen.data, cur_display_buffer);

        do_render = true;
      }
    }
  }
}

void init_display() {
  // assuming the default overclock hasn't been disabled, overvolt should already be set
  set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

  dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

void update_display(uint32_t time) {
  if(do_render) {
    ::render(time);
    do_render = false;
  }
}

void init_display_core1() {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);
}

void update_display_core1() {
  dvi_update();
}

bool display_render_needed() {
  return do_render;
}

void display_mode_changed(blit::ScreenMode new_mode, blit::PixelFormat new_format) {
  auto display_buf_base = (uint8_t *)screen_fb;

  if(!fb_double_buffer || !cur_display_buffer)
    cur_display_buffer = fb_double_buffer ? display_buf_base + get_display_page_size() : display_buf_base;
}
