#include <cstdio>
#include <cstring>

#include "hardware/structs/rosc.h"
#include "hardware/structs/scb.h"
#include "hardware/vreg.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "audio.hpp"
#include "binary_info.hpp"
#include "config.h"
#include "display.hpp"
#include "file.hpp"
#include "input.hpp"
#include "led.hpp"
#include "multiplayer.hpp"
#include "usb.hpp"

#include "executable.hpp"

#include "engine/api_private.hpp"

using namespace blit;

const unsigned int game_block_size = 64 * 1024; // this is the 32blit's flash erase size, some parts of the API depend on this...

static blit::AudioChannel channels[CHANNEL_COUNT];

// blit API
static blit::APIConst blit_api_const;
static blit::APIData blit_api_data;

namespace blit {
  const APIConst &api = blit_api_const;
  APIData &api_data = blit_api_data;
}

static uint32_t now() {
  return to_ms_since_boot(get_absolute_time());
}

static uint32_t random() {
	return get_rand_32();
}

static void debug(const char *message) {
  auto p = message;
  while(*p)
    putchar(*p++);

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
    if((*tag_ptr)->type != BINARY_INFO_TYPE_ID_AND_STRING)
      continue;

    auto id_str_tag = (binary_info_id_and_string_t *)*tag_ptr;

    if((*tag_ptr)->tag == BINARY_INFO_TAG_RASPBERRY_PI) {
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
    } else if((*tag_ptr)->tag == BINARY_INFO_TAG_32BLIT) {
      switch(id_str_tag->id) {
        case BINARY_INFO_ID_32BLIT_AUTHOR:
          ret.author = id_str_tag->value;
          break;
        case BINARY_INFO_ID_32BLIT_CATEGORY:
          ret.category = id_str_tag->value;
          break;
      }
    }

  }
  return ret;
}

static bool launch(const char *path) {
  if(strncmp(path, "flash:/", 7) == 0) {
    int offset = atoi(path + 7) * game_block_size;

    // TODO: check valid
    auto addr = XIP_BASE + offset + 256;

    // disable all irqs
    irq_set_mask_enabled(~0u, false);

    // set VTOR
    scb_hw->vtor = addr;

    asm volatile(
      "ldr r0, [%0]\n"
      "ldr r1, [%0, #4]\n"
      "msr msp, r0\n" // set SP
      "bx r1" // branch to reset
      :
      : "r" (addr)
      : "r0", "r1"
    );
    // not reached
  }

  return false;
}

static void list_installed_games(std::function<void(const uint8_t *, uint32_t, uint32_t)> callback) {
  for(uint32_t off = 0; off < PICO_FLASH_SIZE_BYTES;) {
    auto header = (BlitGameHeader *)(XIP_NOCACHE_NOALLOC_BASE + off);

    // check header magic
    if(header->magic != blit_game_magic) {
      off += game_block_size;
      continue;
    }

    auto size = header->end;

    // check metadata
    auto meta_offset = off + size;
    if(memcmp((char *)(XIP_NOCACHE_NOALLOC_BASE + meta_offset), "BLITMETA", 8) != 0) {
      off += ((size - 1) / game_block_size + 1) * game_block_size;
      continue;
    }

    // add metadata size
    size += *(uint16_t *)(XIP_NOCACHE_NOALLOC_BASE + meta_offset + 8) + 10;

    callback((const uint8_t *)(XIP_NOCACHE_NOALLOC_BASE + off), off / game_block_size, size);

    off += ((size - 1) / game_block_size + 1) * game_block_size;
  }
}

// user funcs
void init();
void render(uint32_t);
void update(uint32_t);

bool core1_started = false;

void core1_main() {
  core1_started = true;
  multicore_lockout_victim_init();

  init_display_core1();

  while(true) {
    update_display_core1();
    sleep_us(1);
  }
}

int main() {
#if OVERCLOCK_250
  // Apply a modest overvolt, default is 1.10v.
  // this is required for a stable 250MHz on some RP2040s
  vreg_set_voltage(VREG_VOLTAGE_1_20);
  sleep_ms(10);
  set_sys_clock_khz(250000, false);
#endif

  stdio_init_all();

  blit_api_const.channels = ::channels;

  blit_api_const.set_screen_mode = ::set_screen_mode;
  blit_api_const.set_screen_palette = ::set_screen_palette;
  blit_api_const.set_screen_mode_format = ::set_screen_mode_format;
  blit_api_const.now = ::now;
  blit_api_const.random = ::random;
  // blit_api_const.exit = ::exit;

  // serial debug
  blit_api_const.debug = ::debug;

  // files
  blit_api_const.open_file = ::open_file;
  blit_api_const.read_file = ::read_file;
  blit_api_const.write_file = ::write_file;
  blit_api_const.close_file = ::close_file;
  blit_api_const.get_file_length = ::get_file_length;
  blit_api_const.list_files = ::list_files;
  blit_api_const.file_exists = ::file_exists;
  blit_api_const.directory_exists = ::directory_exists;
  blit_api_const.create_directory = ::create_directory;
  blit_api_const.rename_file = ::rename_file;
  blit_api_const.remove_file = ::remove_file;
  blit_api_const.get_save_path = ::get_save_path;
  blit_api_const.is_storage_available = ::is_storage_available;

  // profiler
  // blit_api_const.enable_us_timer = ::enable_us_timer;
  blit_api_const.get_us_timer = ::get_us_timer;
  blit_api_const.get_max_us_timer = ::get_max_us_timer;

  // jpeg
  // blit_api_const.decode_jpeg_buffer = ::decode_jpeg_buffer;
  // blit_api_const.decode_jpeg_file = ::decode_jpeg_file;

  // launcher
  blit_api_const.launch = ::launch;
  // blit_api_const.erase_game = ::erase_game;
  // blit_api_const.get_type_handler_metadata = ::get_type_handler_metadata;
  blit_api_const.list_installed_games = ::list_installed_games;

  blit_api_const.get_launch_path = ::get_launch_path;

  // multiplayer
  blit_api_const.is_multiplayer_connected = ::is_multiplayer_connected;
  blit_api_const.set_multiplayer_enabled = ::set_multiplayer_enabled;
  blit_api_const.send_message = ::send_multiplayer_message;

  // blit_api_const.flash_to_tmp = ::flash_to_tmp;
  // blit_api_const.tmp_file_closed = ::tmp_file_closed;

  blit_api_const.get_metadata = ::get_metadata;

  init_led();
  init_display();
  init_input();
  init_fs();
  init_usb();
  init_audio();

#if defined(ENABLE_CORE1)
  multicore_launch_core1(core1_main);
#endif

  blit::set_screen_mode(ScreenMode::lores);

  blit::render = ::render;
  blit::update = ::update;

  // user init
  ::init();

  while(true) {
    auto now = ::now();
    update_display(now);
    update_input();
    int ms_to_next_update = tick(::now());
    update_audio(now);
    update_led();
    update_usb();
    update_multiplayer();

    if(ms_to_next_update > 1 && !display_render_needed())
      best_effort_wfe_or_timeout(make_timeout_time_ms(ms_to_next_update - 1));
  }

  return 0;
}
