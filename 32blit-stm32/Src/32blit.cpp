#include <cstring>

#include "32blit.h"
#include "32blit/battery.hpp"
#include "32blit/i2c.hpp"

#include "adc.hpp"
#include "sound.hpp"
#include "display.hpp"
#include "gpio.hpp"
#include "file.hpp"
#include "jpeg.hpp"
#include "executable.hpp"
#include "multiplayer.hpp"
#include "power.hpp"
#include "quadspi.hpp"

#include "tim.h"
#include "rng.h"
#include "fatfs.h"
#include "quadspi.h"
#include "usbd_core.h"
#include "USBManager.h"
#include "usbd_cdc_if.h"

#include "engine/api_private.hpp"

#include "SystemMenu/system_menu_controller.hpp"

using namespace blit;

extern USBD_HandleTypeDef hUsbDeviceHS;
extern USBManager g_usbManager;

FATFS filesystem;
extern Disk_drvTypeDef disk;
static bool fs_mounted = false;

bool exit_game = false;
bool toggle_menu = false;
bool take_screenshot = false;
const float volume_log_base = 2.0f;

const uint32_t long_press_exit_time = 1000;

__attribute__((section(".persist"))) Persist persist;

static int (*do_tick)(uint32_t time) = blit::tick;

// pointers to user code
static int (*user_tick)(uint32_t time) = nullptr;
static void (*user_render)(uint32_t time) = nullptr;
static bool user_code_disabled = false;

static bool game_switch_requested = false;

// flash cache, most of this is hanlded by the firmware. This needs to be here so switch_execution can reset it
bool cached_file_in_tmp = false;

void DFUBoot(void)
{
  // Set the special magic word value that's checked by the assembly entry Point upon boot
  // This will trigger a jump into DFU mode upon reboot
  *((uint32_t *)0x2001FFFC) = 0xCAFEBABE; // Special Key to End-of-RAM

  SCB_CleanDCache();
  NVIC_SystemReset();
}

static void init_api_shared() {
  // Reset button state, this prevents the user app immediately seeing the last button transition used to launch the game
  api.buttons.state = 0;
  api.buttons.pressed = 0;
  api.buttons.released = 0;

  // reset shared outputs
  api.vibration = 0.0f;
  api.LED = Pen();

  for(int i = 0; i < CHANNEL_COUNT; i++)
    api.channels[i] = AudioChannel();

  api.message_received = nullptr;
}

bool g_bConsumerConnected = true;
void blit_debug(const char *message) {
  if(g_usbManager.GetType() == USBManager::usbtCDC)
  {
    // The mad STM CDC implementation relies on a USB packet being received to set TxState
    // Also calls to CDC_Transmit_HS do not buffer the data so we have to rely on TxState before sending new data.
    // So if there is no consumer running at the other end we will hang, so we need to check for this
    if(g_bConsumerConnected)
    {
      uint32_t tickstart = HAL_GetTick();
      while(g_bConsumerConnected && CDC_Transmit_HS((uint8_t *)message, strlen(message)) == USBD_BUSY)
        g_bConsumerConnected = !(HAL_GetTick() > (tickstart + 2));
    }
    else
    {
      USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;
      g_bConsumerConnected = !(hcdc->TxState != 0);
    }
  }
}

void blit_exit(bool is_error) {
  if(is_error)
    blit_reset_with_error(); // likely an abort
  else {
    persist.reset_target = prtFirmware;
    SCB_CleanDCache();
    NVIC_SystemReset();
  }
}

void enable_us_timer()
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t get_us_timer()
{
	uint32_t uTicksPerUs = SystemCoreClock / 1000000;
	return DWT->CYCCNT/uTicksPerUs;
}

uint32_t get_max_us_timer()
{
	uint32_t uTicksPerUs = SystemCoreClock / 1000000;
	return UINT32_MAX / uTicksPerUs;
}

static const char *get_launch_path() {
  if(!persist.launch_path[0])
    return nullptr;

  return persist.launch_path;
}

static GameMetadata get_metadata() {
  GameMetadata ret;

  auto meta = blit_get_running_game_metadata();
  if(meta) {
    ret.title = meta->title;
    ret.author = meta->author;
    ret.description = meta->description;
    ret.version = meta->version;

    if(memcmp(meta + 1, "BLITTYPE", 8) == 0) {
      auto type_meta = reinterpret_cast<RawTypeMetadata *>(reinterpret_cast<char *>(meta) + sizeof(*meta) + 8);
      ret.url = type_meta->url;
      ret.category = type_meta->category;
    } else {
      ret.url = "";
      ret.category = "none";
    }
  }

  return ret;
}

static void do_render() {
  if(display::needs_render) {
    blit::render(blit::now());
    display::enable_vblank_interrupt();
  }
}

void render_yield() {
  do_render();
}

void blit_tick() {
  if(exit_game) {
    if(blit_user_code_running()) {
      blit::LED.r = 0;
      blit_switch_execution(0, false);
    }
    exit_game = false;
  }

  if(toggle_menu) {
    blit_menu();
  }

  power::update();

  do_render();

  i2c::tick();
  blit_process_input();
  blit_update_led();
  blit_update_vibration();

  // skip update if "off"
  if(power::is_off()) {
    __WFI();
    return;
  }

  multiplayer::update();

  // SD card inserted/removed
  if(blit_sd_detected() != fs_mounted) {
    if(!fs_mounted)
      fs_mounted = f_mount(&filesystem, "", 1) == FR_OK;
    else
      fs_mounted = false;

    disk.is_initialized[0] = fs_mounted; // this gets set without checking if the init succeeded, un-set it if the init failed (or the card was removed)
  }

  auto time_to_next_tick = do_tick(blit::now());

  // handle delayed switch
  if(game_switch_requested) {
    user_tick = nullptr;
    if(!blit_switch_execution(persist.last_game_offset, true)) {
      // new game failed and old game will now be broken
      // reset and let the firmware show the error
      SCB_CleanDCache();
      NVIC_SystemReset();
    }
    game_switch_requested = false;
  }

  // got a while until the next tick, sleep a bit
  if(time_to_next_tick > 1)
    __WFI();
}

bool blit_sd_detected() {
  return HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_11) == 1;
}

bool blit_sd_mounted() {
  return fs_mounted && g_usbManager.GetType() != USBManager::usbtMSC;
}

void hook_render(uint32_t time) {
  /*
  Replace blit::render = ::render; with blit::render = hook_render;
  and do silly on-screen debug stuff here for the great justice.
  */
  ::render(time);

  blit::screen.pen = Pen(255, 255, 255);
  /*for(auto i = 0; i < ADC_BUFFER_SIZE; i++) {
    int x = i / 8;
    int y = i % 8;
    blit::screen.text(std::to_string(adc1data[i]), minimal_font, Point(x * 30, y * 10));
  }*/
}

void blit_update_volume() {
    float volume = persist.is_muted ? 0.0f : persist.volume * power::sleep_fade;
    blit::volume = (uint16_t)(65535.0f * log(1.0f + (volume_log_base - 1.0f) * volume) / log(volume_log_base));
}

static void save_screenshot() {
  int index = 0;
  char buf[200];
  std::string app_name;
  const std::string screenshots_dir_name = "screenshots";

  if(!blit_user_code_running()) {
    app_name = "_firmware";
  } else {
    auto meta = blit_get_running_game_metadata();

    if(meta) {
      app_name = meta->title;
    } else {
      // fallback to offset
      app_name = std::to_string(persist.last_game_offset);
    }
  }

  do {
    snprintf(buf, 200, "%s/%s%i.bmp", screenshots_dir_name.c_str(), app_name.c_str(), index);

    if(!::directory_exists(screenshots_dir_name)){
      ::create_directory(screenshots_dir_name);
    }

    if(!::file_exists(buf))
      break;

    index++;
  } while(true);

  screen.save(buf);
}

void blit_init() {
    // enable backup sram
    __HAL_RCC_RTC_ENABLE();
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
    HAL_PWREx_EnableBkUpReg();

    // need to wit for sram, I tried a few things I found on the net to wait
    // based on PWR flags but none seemed to work, a simple delay does work!
    HAL_Delay(5);

    if(persist.magic_word != persistence_magic_word) {
      // Set persistent defaults if the magic word does not match
      persist.magic_word = persistence_magic_word;
      persist.volume = 0.5f;
      persist.backlight = 1.0f;
      persist.selected_menu_item = 0;
      persist.reset_target = prtFirmware;
      persist.reset_error = false;
      persist.last_game_offset = 0;
      memset(persist.launch_path, 0, sizeof(persist.launch_path));

      // clear LTDC buffer to avoid flash of uninitialised data
      extern char __ltdc_start;
      int len = 320 * 240 * 2;
      memset(&__ltdc_start, 0, len);
    }

    // don't switch to game if it crashed, or home is held
    if(persist.reset_target == prtGame && (HAL_GPIO_ReadPin(BUTTON_HOME_GPIO_Port,  BUTTON_HOME_Pin) || persist.reset_error))
      persist.reset_target = prtFirmware;

    init_api_shared();

    blit_update_volume();

    // enable cycle counting
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    fs_mounted = f_mount(&filesystem, "", 1) == FR_OK;  // this shouldn't be necessary here right?

    i2c::init();

    blit::api.version_major = api_version_major;
    blit::api.version_minor = api_version_minor;

    blit::api.debug = blit_debug;
    blit::api.now = HAL_GetTick;
    blit::api.random = HAL_GetRandom;
    blit::api.exit = blit_exit;

    blit::api.set_screen_mode = display::set_screen_mode;
    blit::api.set_screen_palette = display::set_screen_palette;
    blit::api.set_screen_mode_format = display::set_screen_mode_format;

    display::set_screen_mode(blit::lores);

    blit::update = ::update;
    blit::render = ::render;
    blit::init   = ::init;

    blit::api.open_file = ::open_file;
    blit::api.read_file = ::read_file;
    blit::api.write_file = ::write_file;
    blit::api.close_file = ::close_file;
    blit::api.get_file_length = ::get_file_length;
    blit::api.list_files = ::list_files;
    blit::api.file_exists = ::file_exists;
    blit::api.directory_exists = ::directory_exists;
    blit::api.create_directory = ::create_directory;
    blit::api.rename_file = ::rename_file;
    blit::api.remove_file = ::remove_file;
    blit::api.get_save_path = ::get_save_path;
    blit::api.is_storage_available = blit_sd_mounted;

    blit::api.enable_us_timer = ::enable_us_timer;
    blit::api.get_us_timer = ::get_us_timer;
    blit::api.get_max_us_timer = ::get_max_us_timer;

    blit::api.decode_jpeg_buffer = blit_decode_jpeg_buffer;
    blit::api.decode_jpeg_file = blit_decode_jpeg_file;

    blit::api.get_launch_path = ::get_launch_path;

    blit::api.is_multiplayer_connected = multiplayer::is_connected;
    blit::api.set_multiplayer_enabled = multiplayer::set_enabled;
    blit::api.send_message = multiplayer::send_message;

    blit::api.get_metadata = ::get_metadata;

    blit::api.tick_function_changed = false;

  display::init();

  multiplayer::init();

  blit::init();
}

// ==============================
// SYSTEM MENU CODE
// ==============================
static const Pen menu_colours[]{
  {0},
  { 30,  30,  50, 200}, // background
  {255, 255, 255}, // foreground
  { 40,  40,  60}, // bar background
  { 50,  50,  70}, // selected item background
  {255, 128,   0}, // battery unknown
  {  0, 255,   0}, // battery usb host/adapter port
  {255,   0,   0}, // battery otg
  {100, 100, 255}, // battery charging
  {235, 245, 255}, // header/footer bg
  {  3,   5,   7}, // header/footer fg
  {245, 235,   0}, // header/footer fg warning
};

static constexpr int num_menu_colours = sizeof(menu_colours) / sizeof(Pen);
static Pen menu_saved_colours[num_menu_colours];

Pen get_menu_colour(int index) {
    return screen.format == PixelFormat::P ? Pen(index) : menu_colours[index];
};

//
// Update the system menu
//
void blit_menu_update(uint32_t time) {
  system_menu.update(time);
}

//
// Render the system menu
//
void blit_menu_render(uint32_t time) {

  if(user_render && !user_code_disabled)
    user_render(time);
  else
    ::render(time);

  // save screenshot before we render the menu over it
  if(take_screenshot) {
    // restore game colours
    if(screen.format == PixelFormat::P)
      set_screen_palette(menu_saved_colours, num_menu_colours);

    save_screenshot();
    take_screenshot = false;

    if(screen.format == PixelFormat::P)
      set_screen_palette(menu_colours, num_menu_colours);
  }

  system_menu.render(time);
}

//
// Setup the system menu to be shown over the current content / user content
//
void blit_menu() {
  toggle_menu = false;
  if(blit::update == blit_menu_update && do_tick == blit::tick) {
    if (user_tick && !user_code_disabled) {
      // user code was running
      do_tick = user_tick;
      api.tick_function_changed = true;
      blit::render = user_render;
    } else {
      blit::update = ::update;
      blit::render = ::render;
    }

    if(!user_code_disabled)
      sound::enabled = true;

    // restore game colours
    if(screen.format == PixelFormat::P) {
      set_screen_palette(menu_saved_colours, num_menu_colours);
    }
  } else {
    sound::enabled = false;
    system_menu.prepare();

    blit::update = blit_menu_update;
    blit::render = blit_menu_render;
    do_tick = blit::tick;
    api.tick_function_changed = true;

    if(screen.format == PixelFormat::P) {
      memcpy(menu_saved_colours, screen.palette, num_menu_colours * sizeof(Pen));
      set_screen_palette(menu_colours, num_menu_colours);
    }
  }
}

// ==============================
// SYSTEM MENU CODE ENDS HERE
// ==============================


void blit_update_vibration() {
    __HAL_TIM_SetCompare(&htim4, TIM_CHANNEL_1, blit::vibration * 2000.0f);
}

void blit_update_led() {
    int scale = 10000 * power::sleep_fade;

    // RED Led
    int compare_r = (blit::LED.r * scale) / 255;
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_3, compare_r);

    // GREEN Led
    int compare_g = (blit::LED.g * scale) / 255;
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, compare_g);

    // BLUE Led
    int compare_b = (blit::LED.b * scale) / 255;
    __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_2, compare_b);

    // Backlight
    __HAL_TIM_SetCompare(&htim15, TIM_CHANNEL_1, (962 - (962 * persist.backlight)) * power::sleep_fade + (1024 * (1.0f - power::sleep_fade)));
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if(htim == &htim2) {
    bool pressed = HAL_GPIO_ReadPin(BUTTON_HOME_GPIO_Port, BUTTON_HOME_Pin);
    if(pressed && blit_user_code_running()) { // if button was pressed and we are inside a game, queue the game exit
      exit_game = true;
    }
    HAL_TIM_Base_Stop_IT(&htim2);
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  bool pressed = HAL_GPIO_ReadPin(BUTTON_HOME_GPIO_Port, BUTTON_HOME_Pin);
  if(pressed) {
    /*
    The timer will generate a spurious interrupt as soon as it's enabled- apparently to load the compare value.
    We disable interrupts and clear this early interrupt flag before re-enabling them so that the *real*
    interrupt can fire.
    */
    if(!((&htim2)->Instance->CR1 & TIM_CR1_CEN)){
      HAL_NVIC_DisableIRQ(TIM2_IRQn);
      __HAL_TIM_SetCounter(&htim2, 0);
      __HAL_TIM_SetCompare(&htim2, TIM_CHANNEL_1, long_press_exit_time * 10); // press-to-reset-time
      HAL_TIM_Base_Start_IT(&htim2);
      __HAL_TIM_CLEAR_FLAG(&htim2, TIM_SR_UIF);
      HAL_NVIC_EnableIRQ(TIM2_IRQn);
    }

  } else {
    if(__HAL_TIM_GetCounter(&htim2) > 200){ // 20ms debounce time

      // we're powered down, reset
      if(power::is_off()) {
        NVIC_SystemReset();
      }

      toggle_menu = true;
      HAL_TIM_Base_Stop_IT(&htim2);
      __HAL_TIM_SetCounter(&htim2, 0);
    }
  }
}

void blit_process_input() {
  // Read buttons
  blit::buttons =
    (!HAL_GPIO_ReadPin(DPAD_UP_GPIO_Port,     DPAD_UP_Pin)      ? uint32_t(DPAD_UP)    : 0) |
    (!HAL_GPIO_ReadPin(DPAD_DOWN_GPIO_Port,   DPAD_DOWN_Pin)    ? uint32_t(DPAD_DOWN)  : 0) |
    (!HAL_GPIO_ReadPin(DPAD_LEFT_GPIO_Port,   DPAD_LEFT_Pin)    ? uint32_t(DPAD_LEFT)  : 0) |
    (!HAL_GPIO_ReadPin(DPAD_RIGHT_GPIO_Port,  DPAD_RIGHT_Pin)   ? uint32_t(DPAD_RIGHT) : 0) |
    (!HAL_GPIO_ReadPin(BUTTON_A_GPIO_Port,    BUTTON_A_Pin)     ? uint32_t(A)          : 0) |
    (!HAL_GPIO_ReadPin(BUTTON_B_GPIO_Port,    BUTTON_B_Pin)     ? uint32_t(B)          : 0) |
    (!HAL_GPIO_ReadPin(BUTTON_X_GPIO_Port,    BUTTON_X_Pin)     ? uint32_t(X)          : 0) |
    (!HAL_GPIO_ReadPin(BUTTON_Y_GPIO_Port,    BUTTON_Y_Pin)     ? uint32_t(Y)          : 0) |
    (HAL_GPIO_ReadPin(BUTTON_HOME_GPIO_Port,  BUTTON_HOME_Pin)  ? uint32_t(HOME)       : 0) |  // INVERTED LOGIC!
    (!HAL_GPIO_ReadPin(BUTTON_MENU_GPIO_Port, BUTTON_MENU_Pin)  ? uint32_t(MENU)       : 0) |
    (!HAL_GPIO_ReadPin(JOYSTICK_BUTTON_GPIO_Port, JOYSTICK_BUTTON_Pin) ? uint32_t(JOYSTICK)   : 0);

  if(blit::buttons.state)
    power::update_active();

  // Process ADC readings
  int joystick_x = (adc::get_value(adc::Value::joystick_x) >> 1) - 16384;
  joystick_x = std::max(-8192, std::min(8192, joystick_x));
  if(joystick_x < -1024) {
    joystick_x += 1024;
  }
  else if(joystick_x > 1024) {
    joystick_x -= 1024;
  } else {
    joystick_x = 0;
  }
  blit::joystick.x = joystick_x / 7168.0f;

  int joystick_y = (adc::get_value(adc::Value::joystick_y) >> 1) - 16384;
  joystick_y = std::max(-8192, std::min(8192, joystick_y));
  if(joystick_y < -1024) {
    joystick_y += 1024;
  }
  else if(joystick_y > 1024) {
    joystick_y -= 1024;
  } else {
    joystick_y = 0;
  }
  blit::joystick.y = -joystick_y / 7168.0f;

  if(blit::joystick.length() > 0.01f)
    power::update_active();

  blit::hack_left = (adc::get_value(adc::Value::hack_left) >> 1) / 32768.0f;
  blit::hack_right = (adc::get_value(adc::Value::hack_right) >> 1)  / 32768.0f;

  battery::update_charge(6.6f * adc::get_value(adc::Value::battery_charge) / 65535.0f);
}

// blit_switch_execution
//
// Attempts to init a new game and switch render/tick to run it.
// Can also be used to exit back to the firmware.
//

bool blit_switch_execution(uint32_t address, bool force_game)
{
  if(blit_user_code_running() && !force_game)
    persist.reset_target = prtFirmware;
  else
    persist.reset_target = prtGame;

  init_api_shared();

  // returning from game running on top of the firmware
  if(user_tick && !force_game) {
    user_tick = nullptr;
    user_render = nullptr;

    // close the menu (otherwise we'll end up in an inconsistent state)
    if(blit::update == blit_menu_update)
      blit_menu();

    blit::render = ::render;
    blit::update = ::update;
    do_tick = blit::tick;
    api.tick_function_changed = true;

    cached_file_in_tmp = false;
    close_open_files();

    return true;
  }

	// switch to user app in external flash
  qspi_enable_memorymapped_mode();

  auto game_header = ((__IO BlitGameHeader *) (qspi_flash_address + address));

  if(game_header->magic == blit_game_magic) {

    persist.last_game_offset = address;

    // game possibly running, wait until it isn't
    if(user_tick && !user_code_disabled) {
      game_switch_requested = true;
      return true;
    }

    // avoid starting a game disabled (will break sound and the menu)
    if(user_code_disabled)
      blit_enable_user_code();

    cached_file_in_tmp = false;
    close_open_files();

    // load function pointers
    auto init = (BlitInitFunction)((uint8_t *)game_header->init + address);

    // set these up early so that blit_user_code_running works in code called from init
    user_render = (BlitRenderFunction) ((uint8_t *)game_header->render + address);
    user_tick = (BlitTickFunction) ((uint8_t *)game_header->tick + address);

    if(!init(address)) {
      user_render = nullptr;
      user_tick = nullptr;

      qspi_disable_memorymapped_mode();

      return false;
    }

    blit::render = user_render;
    do_tick = user_tick;
    return true;
  }

  return false;
}

bool blit_user_code_running() {
  // loaded user-only game from flash
  return user_tick != nullptr;
}

void blit_reset_with_error() {
  persist.reset_error = true;
  SCB_CleanDCache();
  NVIC_SystemReset();
}

void blit_enable_user_code() {
  if(!user_tick)
    return;

  do_tick = user_tick;
  api.tick_function_changed = true;
  blit::render = user_render;
  user_code_disabled = false;
  sound::enabled = true;
}

void blit_disable_user_code() {
  if(!user_tick)
    return;

  do_tick = blit::tick;
  api.tick_function_changed = true;
  blit::render = ::render;
  sound::enabled = false;
  user_code_disabled = true;
}

RawMetadata *blit_get_running_game_metadata() {
  if(!blit_user_code_running())
    return nullptr;

  auto game_ptr = reinterpret_cast<uint8_t *>(qspi_flash_address + persist.last_game_offset);

  auto header = reinterpret_cast<BlitGameHeader *>(game_ptr);

  if(header->magic == blit_game_magic) {
    auto end_ptr = game_ptr + (header->end - qspi_flash_address);
    if(memcmp(end_ptr, "BLITMETA", 8) == 0)
      return reinterpret_cast<RawMetadata *>(end_ptr + 10);
  }

  return nullptr;
}
