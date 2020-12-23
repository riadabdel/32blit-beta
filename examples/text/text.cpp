#include "text.hpp"
#include "font_asset.hpp"

using namespace blit;

TextFlags flags = TextFlags::top_left;
const Font custom_font(press_start_font);

std::string alignment_to_string(TextFlags flags) {
  switch (flags) {
    case TextFlags::bottom_left:
      return "align: bottom_left";
    case TextFlags::bottom_right:
      return "align: bottom_right";
    case TextFlags::top_left:
      return "align: top_left";
    case TextFlags::top_right:
      return "align: top_right";
    case TextFlags::center_center:
      return "align: center_center";
    case TextFlags::center_left:
      return "align: center_left";
    case TextFlags::center_right:
      return "align: center_right";
    case TextFlags::top_center:
      return "align: top_center";
    case TextFlags::bottom_center:
      return "align: bottom_center";
  }
  return "";
}

void init() {
  set_screen_mode(ScreenMode::hires);
}

void render(uint32_t time) {
  screen.clip = Rect(Point(0, 0), screen.bounds);
  screen.pen = Pen(0, 0, 0);
  screen.clear();

  screen.alpha = 255;
  screen.pen = Pen(255, 255, 255);
  screen.rectangle(Rect(0, 0, 320, 14));
  screen.pen = Pen(0, 0, 0);
  screen.text("Text Rendering", minimal_font, Point(5, 4));

  // alignment
  Rect text_rect(20, 20, 120, 80);

  screen.pen = Pen(64, 64, 64);
  screen.rectangle(text_rect);

  std::string text = "This is some aligned text!\nUse the dpad to change the alignment\nand A to toggle variable-width.";
  text = screen.wrap_text(text, text_rect.w, minimal_font, flags);

  screen.pen = Pen(0xFF, 0xFF, 0xFF);
  screen.text(text, minimal_font, text_rect, flags);

  screen.text(alignment_to_string(flags), minimal_font, Point(80, 102), TextFlags::center_h);

  auto size = screen.measure_text(text, minimal_font, variable_width);
  screen.text("bounds: " + std::to_string(size.w) + "x" + std::to_string(size.h), minimal_font, Point(80, 110), TextFlags::center_h);

  text_rect.x += 160;

  // clipping
  Rect clip(text_rect.x + 30 + 30 * cosf(time / 1000.0f), text_rect.y, 60, 80);
  screen.pen = Pen(64, 64, 64);
  screen.rectangle(text_rect);

  text = "This text is clipped!\nIt's slightly hard to read since half of it is missing.";
  text = screen.wrap_text(text, text_rect.w, minimal_font, variable_width);

  screen.pen = Pen(0xFF, 0xFF, 0xFF);
  screen.clip = clip;
  screen.text(text, minimal_font, text_rect, TextFlags::center_center);
  screen.clip = Rect(Point(0, 0), screen.bounds); // Reset the clip!

  // Using a custom font
  text_rect.x -= 160;
  text_rect.y += 120;

  screen.pen = Pen(64, 64, 64);
  screen.rectangle(text_rect);

  text = "This text uses\nan imported\nTrueType font.";
  text = screen.wrap_text(text, text_rect.w, minimal_font, flags);

  screen.pen = Pen(0xFF, 0xFF, 0xFF);
  screen.text(text, custom_font, text_rect, flags);

  // Alignment around a Point rather than a Rect
  Point text_point(240,180);

  screen.pen = Pen(64, 64, 64);
  screen.line(text_point - Point(20,0), text_point + (Point(20,0)));
  screen.line(text_point - Point(0,20), text_point + (Point(0,20)));

  text = "This text is\naligned to a\nPoint instead\nof a Rect.";
  text = screen.wrap_text(text, text_rect.w, minimal_font, flags);

  screen.pen = Pen(0xFF, 0xFF, 0xFF);
  screen.text(text, minimal_font, text_point, flags);
}

void update(uint32_t time) {
  if (buttons.released & Button::A)
    flags = (TextFlags)(flags ^ TextFlags::fixed_width);

  // clear alignment
  flags = (TextFlags)(flags & ~0xF);

  if (buttons & Button::DPAD_DOWN) {
    flags = (TextFlags)(flags | TextFlags::bottom);
  }
  else if (!(buttons & Button::DPAD_UP)) {
    flags = (TextFlags)(flags | TextFlags::center_v);
  }

  if (buttons & Button::DPAD_RIGHT) {
    flags = (TextFlags)(flags | TextFlags::right);
  }
  else if (!(buttons & Button::DPAD_LEFT)) {
    flags = (TextFlags)(flags | TextFlags::center_h);
  }
}
