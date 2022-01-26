#include <stdio.h>
#include <random>

#include "hardware/clocks.h"
#include "hardware/structs/rosc.h"
#include "hardware/vreg.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#ifdef DISPLAY_SCANVIDEO
#include "pico/scanvideo.h"
#include "pico/scanvideo/composable_scanline.h"
#endif
#ifdef DISPLAY_PICODVI
#include "hardware/irq.h"
#include "hardware/vreg.h"

extern "C" {
#include "dvi.h"
#include "tmds_encode.h"
#include "common_dvi_pin_configs.h"
}
#endif

#include "audio.hpp"
#include "config.h"
#include "file.hpp"
#include "input.hpp"
#include "led.hpp"
#include "st7789.hpp"
#include "usb.hpp"

#include "engine/api_private.hpp"
#include "engine/engine.hpp"
#include "graphics/surface.hpp"

using namespace blit;

static SurfaceInfo cur_surf_info;

#ifdef DISPLAY_ST7789
// height rounded up to handle the 135px display
static const int lores_page_size = (ST7789_WIDTH / 2) * ((ST7789_HEIGHT + 1) / 2) * 2;

#if ALLOW_HIRES
uint8_t screen_fb[ST7789_WIDTH * ST7789_HEIGHT * 2];
#else
uint8_t screen_fb[lores_page_size * 2]; // double-buffered
#endif
static bool have_vsync = false;

static const blit::SurfaceTemplate lores_screen{screen_fb, Size(ST7789_WIDTH / 2, ST7789_HEIGHT / 2), blit::PixelFormat::RGB565, nullptr};
static const blit::SurfaceTemplate hires_screen{screen_fb, Size(ST7789_WIDTH, ST7789_HEIGHT), blit::PixelFormat::RGB565, nullptr};

#elif defined(DISPLAY_SCANVIDEO)
uint8_t screen_fb[160 * 120 * 4];
static const blit::SurfaceTemplate lores_screen{screen_fb, Size(160, 120), blit::PixelFormat::RGB565, nullptr};
#elif defined(DISPLAY_PICODVI)
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

static dvi_inst dvi0;

uint8_t screen_fb[160 * 120 * 4];
static const  blit::SurfaceTemplate lores_screen{screen_fb, Size(160, 120), PixelFormat::RGB565, nullptr};
#endif

static blit::AudioChannel channels[CHANNEL_COUNT];


ScreenMode cur_screen_mode = ScreenMode::lores;
// double buffering for lores
static volatile int buf_index = 0;

static volatile bool do_render = true;

static SurfaceInfo &set_screen_mode(ScreenMode mode) {
  switch(mode) {
    case ScreenMode::lores:
      cur_surf_info = lores_screen;
      // window
#ifdef DISPLAY_ST7789
      if(have_vsync)
        do_render = true; // prevent starting an update during switch

      st7789::set_pixel_double(true);
#endif
      break;

    case ScreenMode::hires:
#if defined(DISPLAY_ST7789) && ALLOW_HIRES
      if(have_vsync)
        do_render = true;

      cur_surf_info = hires_screen;
      st7789::frame_buffer = (uint16_t *)screen_fb;
      st7789::set_pixel_double(false);
#else
      return cur_surf_info;
#endif
      break;

    //case ScreenMode::hires_palette:
    //  screen = hires_palette_screen;
    //  break;
  }

  cur_screen_mode = mode;

  return cur_surf_info;
}

static void set_screen_palette(const Pen *colours, int num_cols) {

}

static bool set_screen_mode_format(ScreenMode new_mode, SurfaceTemplate &new_surf_template) {
  new_surf_template.data = screen_fb;

  switch(new_mode) {
    case ScreenMode::lores:
      new_surf_template.bounds = lores_screen.bounds;
      break;
    case ScreenMode::hires:
    case ScreenMode::hires_palette:
#if defined(DISPLAY_ST7789) && ALLOW_HIRES
      new_surf_template.bounds = hires_screen.bounds;
      break;
#else
      return false; // no hires for scanvideo
#endif
  }

#ifdef DISPLAY_ST7789
      if(have_vsync)
        do_render = true; // prevent starting an update during switch

      st7789::set_pixel_double(new_mode == ScreenMode::lores);

      if(new_mode == ScreenMode::hires)
        st7789::frame_buffer = (uint16_t *)screen_fb;
#endif

  // don't support any other formats for various reasons (RAM, no format conversion, pixel double PIO)
  if(new_surf_template.format != PixelFormat::RGB565)
    return false;

  cur_screen_mode = new_mode;

  return true;
}

static uint32_t now() {
  return to_ms_since_boot(get_absolute_time());
}

static uint32_t get_random_seed() {
  uint32_t seed = 0;

  // use the hardware random bit to seed
  for(int i = 0; i < 32; i++) {
    seed <<= 1;
    seed |= rosc_hw->randombit & 1;
    sleep_us(1); // don't read too fast
  }

  return seed;
}

static uint32_t random() {
  static std::mt19937 random_generator(get_random_seed());
  static std::uniform_int_distribution<uint32_t> random_distribution;

	return random_distribution(random_generator);
}

static void debug(const char *message) {
  fputs(message, stdout);

  usb_debug(message);
}

static bool is_storage_available() {
  return true; // TODO: optional storage?
}

static uint32_t get_us_timer() {
  return to_us_since_boot(get_absolute_time());
}

static uint32_t get_max_us_timer() {
  return 0xFFFFFFFF; // it's a 64bit timer...
}

const char *get_launch_path()  {
  return nullptr;
}

static GameMetadata get_metadata() {
  GameMetadata ret;

  // parse binary info
  extern binary_info_t *__binary_info_start, *__binary_info_end;

  for(auto tag_ptr = &__binary_info_start; tag_ptr != &__binary_info_end ; tag_ptr++) {
    if((*tag_ptr)->type != BINARY_INFO_TYPE_ID_AND_STRING || (*tag_ptr)->tag != BINARY_INFO_TAG_RASPBERRY_PI)
      continue;

    auto id_str_tag = (binary_info_id_and_string_t *)*tag_ptr;

    switch(id_str_tag->id) {
      case BINARY_INFO_ID_RP_PROGRAM_NAME:
        ret.title = id_str_tag->value;
        break;
      case BINARY_INFO_ID_RP_PROGRAM_VERSION_STRING:
        ret.version = id_str_tag->value;
        break;
      case BINARY_INFO_ID_RP_PROGRAM_URL:
        ret.url = id_str_tag->value;
        break;
      case BINARY_INFO_ID_RP_PROGRAM_DESCRIPTION:
        ret.description = id_str_tag->value;
        break;
    }

  }
  return ret;
}

// user funcs
void init();
void render(uint32_t);
void update(uint32_t);

#ifdef DISPLAY_ST7789
void vsync_callback(uint gpio, uint32_t events) {
  if(!do_render && !st7789::dma_is_busy()) {
    st7789::update();
    do_render = true;
  }
}
#endif

#ifdef DISPLAY_SCANVIDEO

static void fill_scanline_buffer(struct scanvideo_scanline_buffer *buffer) {
  static uint32_t postamble[] = {
    0x0000u | (COMPOSABLE_EOL_ALIGN << 16)
  };

  int w = screen.bounds.w;

  buffer->data[0] = 4;
  buffer->data[1] = host_safe_hw_ptr(buffer->data + 8);
  buffer->data[2] = (w - 4) / 2; // first four pixels are handled separately
  uint16_t *pixels = ((uint16_t *)screen_fb) + buf_index * (160 * 120) + scanvideo_scanline_number(buffer->scanline_id) * w;
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
static void __not_in_flash_func(dvi_loop)() {
  auto inst = &dvi0;

  uint32_t scanbuf[160];
  int y = 0;

  while(true) {

    // pixel double x2
    if(!(y & 1)) {
      auto out = scanbuf;
      auto in = reinterpret_cast<uint16_t *>(screen_fb) + buf_index * (160 * 120) + (y / 2) * 160;
      for(int i = 0; i < 160; i++) {
        auto pixel = *in++;
        *out++ = pixel | pixel << 16;
      }
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
    if(y == 240) {
      y = 0;
      do_render = true;
    }
  }
}
#endif

bool core1_started = false;

void core1_main() {
  core1_started = true;
  multicore_lockout_victim_init();

#ifdef DISPLAY_SCANVIDEO
  int last_frame = 0;
  //scanvideo_setup(&vga_mode_320x240_60); // not quite
  scanvideo_setup(&vga_mode_160x120_60);
  scanvideo_timing_enable(true);
#endif

#ifdef DISPLAY_PICODVI
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);
  dvi_loop();
#endif

  while(true) {
#ifdef DISPLAY_SCANVIDEO
    struct scanvideo_scanline_buffer *buffer = scanvideo_begin_scanline_generation(true);
    while (buffer) {
      fill_scanline_buffer(buffer);
      scanvideo_end_scanline_generation(buffer);

      auto next_frame = scanvideo_frame_number(scanvideo_get_next_scanline_id());
      if(next_frame != last_frame) {
      //if(scanvideo_in_vblank() && !do_render) {
        do_render = true;
        last_frame = next_frame;
        break;
      }

      buffer = scanvideo_begin_scanline_generation(false);
    }
#endif
  sleep_us(1);
  }
}

int main() {

#ifdef DISPLAY_PICODVI
	vreg_set_voltage(VREG_VSEL);
	sleep_ms(10);
	set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
#elif OVERCLOCK_250
  // Apply a modest overvolt, default is 1.10v.
  // this is required for a stable 250MHz on some RP2040s
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(10);
  set_sys_clock_khz(250000, false);
#endif

  stdio_init_all();

  api.channels = ::channels;

  api.set_screen_mode = ::set_screen_mode;
  api.set_screen_palette = ::set_screen_palette;
  api.set_screen_mode_format = ::set_screen_mode_format;
  api.now = ::now;
  api.random = ::random;
  // api.exit = ::exit;

  // serial debug
  api.debug = ::debug;

  // files
  api.open_file = ::open_file;
  api.read_file = ::read_file;
  api.write_file = ::write_file;
  api.close_file = ::close_file;
  api.get_file_length = ::get_file_length;
  api.list_files = ::list_files;
  api.file_exists = ::file_exists;
  api.directory_exists = ::directory_exists;
  api.create_directory = ::create_directory;
  api.rename_file = ::rename_file;
  api.remove_file = ::remove_file;
  api.get_save_path = ::get_save_path;
  api.is_storage_available = ::is_storage_available;

  // profiler
  // api.enable_us_timer = ::enable_us_timer;
  api.get_us_timer = ::get_us_timer;
  api.get_max_us_timer = ::get_max_us_timer;

  // jpeg
  // api.decode_jpeg_buffer = ::decode_jpeg_buffer;
  // api.decode_jpeg_file = ::decode_jpeg_file;

  // launcher
  // api.launch = ::launch;
  // api.erase_game = ::erase_game;
  // api.get_type_handler_metadata = ::get_type_handler_metadata;

  api.get_launch_path = ::get_launch_path;

  // multiplayer
  api.is_multiplayer_connected = ::is_multiplayer_connected;
  api.set_multiplayer_enabled = ::set_multiplayer_enabled;
  api.send_message = ::send_multiplayer_message;

  // api.flash_to_tmp = ::flash_to_tmp;
  // api.tmp_file_closed = ::tmp_file_closed;

  api.get_metadata = ::get_metadata;

  init_led();

#ifdef DISPLAY_ST7789
  bool backlight_enabled = false;
  st7789::frame_buffer = (uint16_t *)screen_fb;
  st7789::init();
  st7789::clear();

  have_vsync = st7789::vsync_callback(vsync_callback);
#endif

#ifdef DISPLAY_PICODVI
	dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());
#endif

  init_input();
  init_fs();
  init_usb();

  multicore_launch_core1(core1_main);

  ::set_screen_mode(ScreenMode::lores);

  blit::render = ::render;
  blit::update = ::update;

  init_audio();

  // user init
  ::init();

  uint32_t last_render = 0;

  while(true) {
    auto now = ::now();

#ifdef DISPLAY_ST7789
    if((do_render || (!have_vsync && now - last_render >= 20)) && (cur_screen_mode == ScreenMode::lores || !st7789::dma_is_busy())) {
      if(cur_screen_mode == ScreenMode::lores) {
        buf_index ^= 1;

        screen.data = screen_fb + (buf_index) * lores_page_size;
        st7789::frame_buffer = (uint16_t *)screen.data;
      }

      ::render(now);

      if(!have_vsync) {
        while(st7789::dma_is_busy()) {} // may need to wait for lores.
        st7789::update();
      }

      if(last_render && !backlight_enabled) {
        // the first render should have made it to the screen at this point
        st7789::set_backlight(255);
        backlight_enabled = true;
      }

      last_render = now;
      do_render = false;
    }

#elif defined(DISPLAY_SCANVIDEO) || defined(DISPLAY_PICODVI)
    if(do_render) {
      screen.data = screen_fb + (buf_index ^ 1) * (160 * 120 * 2); // only works because there's no "firmware" here
      ::render(now);
      buf_index ^= 1;
      do_render = false;
    }
#endif
    update_input();
    int ms_to_next_update = tick(::now());
    update_audio(now);
    update_led();
    update_usb();

    if(ms_to_next_update > 1)
      best_effort_wfe_or_timeout(make_timeout_time_ms(ms_to_next_update - 1));
  }

  return 0;
}
