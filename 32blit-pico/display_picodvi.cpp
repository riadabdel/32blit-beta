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
static volatile int buf_index = 0;

static volatile bool do_render = true;
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
        auto in = screen_fb + buf_index * (lores_w * lores_h) + (y / 2) * lores_w;
        for(int i = 0; i < lores_w; i++) {
          auto pixel = *in++;
          *out++ = pixel | pixel << 16;
        }
      }

      scanbuf = double_buf;
    } else {
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

void init_display() {
  // assuming the default overclock hasn't been disabled, overvolt should already be set
  set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);

  dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
}

void update_display(uint32_t time) {
  if(do_render) {
    if(cur_screen_mode == ScreenMode::lores)
      screen.data = (uint8_t *)screen_fb + (buf_index ^ 1) * lores_page_size; // only works because there's no "firmware" here
    ::render(time);
    do_render = false;
  }
}

void init_display_core1() {
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  // TODO: this assumes nothing else wants to use core 1 (it doesn't return)
  dvi_loop();
}

void update_display_core1() {
}

bool display_render_needed() {
  return do_render;
}

void display_mode_changed(blit::ScreenMode new_mode) {
}
