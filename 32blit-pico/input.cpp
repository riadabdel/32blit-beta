#include "input.hpp"

#include "hardware/gpio.h"

#include "pico/binary_info.h"

#include "engine/api_private.hpp"
#include "engine/input.hpp"

#ifdef INPUT_GPIO

enum class ButtonIO {
#ifdef PIMORONI_PICOSYSTEM
  UP = 23,
  DOWN = 20,
  LEFT = 22,
  RIGHT = 21,

  A = 18,
  B = 19,
  X = 17,
  Y = 16,
#else
  UP = 2,
  DOWN = 3,
  LEFT = 4,
  RIGHT = 5,

  A = 12,
  B = 13,
  X = 14,
  Y = 15,
#endif
};

static void init_button(ButtonIO b) {
  int gpio = static_cast<int>(b);
  gpio_set_function(gpio, GPIO_FUNC_SIO);
  gpio_set_dir(gpio, GPIO_IN);
  gpio_pull_up(gpio);
}

inline bool get_button(ButtonIO b) {
  return !gpio_get(static_cast<int>(b));
}
#endif

#ifdef INPUT_USB_HID

// from USB code
extern uint32_t hid_gamepad_id;
extern uint8_t hid_joystick[2];
extern uint8_t hid_hat;
extern uint32_t hid_buttons;
struct GamepadMapping {
  uint32_t id; // vid:pid
  uint8_t a, b, x, y;
  uint8_t menu, home, joystick;
};

static const GamepadMapping gamepad_mappings[]{
  {0x15320705,  0,  1,  3,  4,  16, 15, 13}, // Razer Raiju Mobile
  {0x20D6A711,  2,  1,  3,  0,  8,  12, 10}, // PowerA wired Switch pro controller
  {0x00000000,  0,  1,  2,  3,  4,   5,  6}  // probably wrong fallback
};

// hat -> dpad
const uint32_t dpad_map[]{
  blit::Button::DPAD_UP,
  blit::Button::DPAD_UP | blit::Button::DPAD_RIGHT,
  blit::Button::DPAD_RIGHT,
  blit::Button::DPAD_DOWN | blit::Button::DPAD_RIGHT,
  blit::Button::DPAD_DOWN,
  blit::Button::DPAD_DOWN | blit::Button::DPAD_LEFT,
  blit::Button::DPAD_LEFT,
  blit::Button::DPAD_UP | blit::Button::DPAD_LEFT,
  0
};

#endif

void init_input() {
#ifdef INPUT_GPIO
  init_button(ButtonIO::UP);
  init_button(ButtonIO::DOWN);
  init_button(ButtonIO::LEFT);
  init_button(ButtonIO::RIGHT);
  init_button(ButtonIO::A);
  init_button(ButtonIO::B);
  init_button(ButtonIO::X);
  init_button(ButtonIO::Y);

  #define BUTTON_DECL(btn) bi_decl(bi_1pin_with_name(static_cast<int>(ButtonIO::btn), #btn" Button"));
  BUTTON_DECL(UP)
  BUTTON_DECL(DOWN)
  BUTTON_DECL(LEFT)
  BUTTON_DECL(RIGHT)
  BUTTON_DECL(A)
  BUTTON_DECL(B)
  BUTTON_DECL(X)
  BUTTON_DECL(Y)
  #undef BUTTON_DECL
#endif
}

void update_input() {
  using namespace blit;

#ifdef INPUT_GPIO
  api.buttons = (get_button(ButtonIO::LEFT)  ? uint32_t(Button::DPAD_LEFT) : 0)
              | (get_button(ButtonIO::RIGHT) ? uint32_t(Button::DPAD_RIGHT) : 0)
              | (get_button(ButtonIO::UP)    ? uint32_t(Button::DPAD_UP) : 0)
              | (get_button(ButtonIO::DOWN)  ? uint32_t(Button::DPAD_DOWN) : 0)
              | (get_button(ButtonIO::A)     ? uint32_t(Button::A) : 0)
              | (get_button(ButtonIO::B)     ? uint32_t(Button::B) : 0)
              | (get_button(ButtonIO::X)     ? uint32_t(Button::X) : 0)
              | (get_button(ButtonIO::Y)     ? uint32_t(Button::Y) : 0);
#endif

#ifdef INPUT_USB_HID
  if(!hid_gamepad_id)
    return;

  auto mapping = gamepad_mappings;
  while(mapping->id && mapping->id != hid_gamepad_id)
    mapping++;

  api.buttons = dpad_map[hid_hat > 8 ? 8 : hid_hat]
                    | (hid_buttons & (1 << mapping->a)        ? uint32_t(Button::A) : 0)
                    | (hid_buttons & (1 << mapping->b)        ? uint32_t(Button::B) : 0)
                    | (hid_buttons & (1 << mapping->x)        ? uint32_t(Button::X) : 0)
                    | (hid_buttons & (1 << mapping->y)        ? uint32_t(Button::Y) : 0)
                    | (hid_buttons & (1 << mapping->menu)     ? uint32_t(Button::MENU) : 0)
                    | (hid_buttons & (1 << mapping->home)     ? uint32_t(Button::HOME) : 0)
                    | (hid_buttons & (1 << mapping->joystick) ? uint32_t(Button::JOYSTICK) : 0);

  api.joystick.x = (float(hid_joystick[0]) - 0x80) / 0x80;
  api.joystick.y = (float(hid_joystick[1]) - 0x80) / 0x80;
#endif
}
