#include "32blit.hpp"

using namespace blit;

void init(void) {
    set_screen_mode(hires);
    LED.b = 200;
}

void update(uint32_t time_ms) {
    LED.g = joystick.y * 255;
}

void render(uint32_t time_ms) {
    LED.r++;// 255;

    screen.pen = Pen(255, 0, 255);
    screen.clear();
}