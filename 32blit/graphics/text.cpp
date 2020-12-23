
#include <string>

#include "../types/point.hpp"
#include "../types/rect.hpp"
#include "font.hpp"
#include "surface.hpp"

using namespace blit;

namespace blit {

  /**
   * Draw text to surface using the specified font and the current pen.
   *
   * \param message Text to draw
   * \param font Font to use
   * \param p Point to align text to
   * \param flags Flags for alignment/variable width
   */
  void Surface::text(std::string_view message, const Font &font, const Point &p, int flags) {
    text(message, font, Rect(p.x, p.y, 0, 0), flags);
  }

  /**
   * Draw text to surface using the specified font and the current pen.
   *
   * \param message Text to draw
   * \param font Font to use
   * \param r Rect to align text to
   * \param variable Draw text using variable character widths
   * \param align ::TextFlags for alignment/variable width
   */
  void Surface::text(std::string_view message, const Font &font, const Rect &r, int flags) {
    Point c(r.x, r.y); // caret position

    if(!clip.intersects(r))
      return;

    bool variable = !(flags & TextFlags::fixed_width);

    // check vertical alignment
    if ((flags & 0b11) != TextFlags::top) {
      Size bounds = measure_text(message, font, flags);

      if ((flags & 0b11) == TextFlags::bottom)
        c.y += r.h - bounds.h;
      else // center
        c.y += (r.h - bounds.h) / 2;
    }

    // check horizontal alignment
    if ((flags & 0b1100) != TextFlags::left) {
      Size bounds = measure_text(message.substr(0, message.find_first_of('\n')), font, flags);

      if ((flags & 0b1100) == TextFlags::right)
        c.x += r.w - bounds.w;
      else // center
        c.x += (r.w - bounds.w) / 2;
    }

    const int height_bytes = (font.char_h + 7) / 8;
    const int char_size = font.char_w * height_bytes;

    size_t char_off = 0;

    for (char chr : message) {
      // draw character

      uint8_t chr_idx = chr & 0x7F;
      chr_idx = chr_idx < ' ' ? 0 : chr_idx - ' ';

      uint8_t char_width = 0;

      const uint8_t* font_chr = &font.data[chr_idx * char_size];

      // If this is a narrow character in fixed-width, center it in the render box
      if (!variable) {
        uint8_t fix_width = (font.char_w - font.char_w_variable[chr_idx]) / 2;
        c.x += fix_width;
        char_width = font.char_w - fix_width;
      } else {
        char_width = font.char_w_variable[chr_idx];
      }

      for (uint8_t y = 0; y < font.char_h; y++) {
        if (c.y + y < 0)
          continue;

        uint32_t po = offset(Point(c.x, c.y + y));

        for (uint8_t x = 0; x < font.char_w; x++) {
          int bit = 1 << (y & 7);
          if (font_chr[x * height_bytes + y / 8] & bit) {
            if(clip.contains(Point(c.x + x, c.y + y)))
              pbf(&pen, this, po, 1);
          }

          po++;
        }
      }

      // increment the cursor
      c.x += char_width;
      if (chr == 10) {
        c.x = r.x;
        c.y += font.char_h + font.spacing_y;

        // check horizontal alignment
        if ((flags & 0b1100) != TextFlags::left) {
          auto end = message.find_first_of('\n', char_off + 1);
          if(end != std::string::npos)
            end -= char_off + 1;

          Size bounds = measure_text(message.substr(char_off + 1, end), font, flags);

          if ((flags & 0b1100) == TextFlags::right)
            c.x += r.w - bounds.w;
          else // center
            c.x += (r.w - bounds.w) / 2;
        }
      }

      char_off++;
    }
  }

  uint8_t get_char_width(const Font &font, char c, bool variable) {
    if (!variable)
      return font.char_w;

    uint8_t chr_idx = c & 0x7F;
    chr_idx = chr_idx < ' ' ? 0 : chr_idx - ' ';

    return font.char_w_variable[chr_idx];
  }

  /**
   * Calculate the size that text would take up using the specified font
   *
   * \param message Text to measure
   * \param font Font to use for measurement
   * \param flags ::TextFlags
   * 
   * \returns Measured Size of text
   */
  Size Surface::measure_text(std::string_view message, const Font &font, int flags) {
    const int line_height = font.char_h + font.spacing_y;

    Size bounds(0, 0);

    size_t char_off = 0;
    int line_len = 0;

    while (char_off < message.length()) {
      // newline
      if (message[char_off] == '\n') {
        if (line_len > bounds.w)
          bounds.w = line_len;

        bounds.h += line_height;

        line_len = 0;
        char_off++;
      } else if (!(flags & TextFlags::fixed_width)) {
        line_len += get_char_width(font, message[char_off], true);
        char_off++;
      } else {
        // calculate a line at a time if using fixed-width characters
        size_t end = message.find_first_of('\n', char_off);
        if (end == std::string::npos)
          end = message.length();

        line_len = int(end - char_off) * font.char_w;
        char_off = end;
      }
    }

    // update for final line
    if (line_len > bounds.w)
      bounds.w = line_len;

    bounds.h += line_height;

    return bounds;
  }
}

/**
 * Wrap text to fit the specified width
 *
 * \param message Text to wrap
 * \param width Maximum width of a line of text
 * \param font Font to use for measurement
 * \param flags ::TextFlags
 * \param words Attempt to break lines between words if `true`
 * 
 * \returns Wrapped text
 */
std::string Surface::wrap_text(std::string_view message, int32_t width, const Font &font, int flags, bool words) {
  std::string ret;

  int current_x = 0;
  size_t last_space = std::string::npos;
  size_t copied_off = 0;

  for (size_t i = 0; i < message.length(); i++) {
    if (message[i] == ' ')
      last_space = i;

    if(message[i] == '\n') {
      ret += message.substr(copied_off, i - copied_off);
      copied_off = i;
      current_x = 0;
      last_space = std::string::npos;
      continue;
    }

    int char_width = get_char_width(font, message[i], !(flags & TextFlags::fixed_width));
    current_x += char_width;

    if (current_x > width) {
      if(!words || last_space == std::string::npos) {
        // no space to break at or we're not breaking on words
        ret += message.substr(copied_off, i - copied_off - 1);
        ret += "\n";
        copied_off = i - 1;
        current_x = char_width;
      } else {
        // break at last space
        ret += message.substr(copied_off, last_space - copied_off);
        ret += "\n";
        copied_off = last_space + 1; // don't copy the space
        last_space = std::string::npos;
        current_x = measure_text(message.substr(copied_off, i - copied_off + 1), font, flags).w;
      }
    }
  }

  ret += message.substr(copied_off);

  return ret;
}
