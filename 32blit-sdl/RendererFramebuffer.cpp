#include <algorithm>
#include <cstdio>
#include <cmath>
#include <iostream>
#include "SDL.h"

#include "graphics/surface.hpp"

#include "Renderer.hpp"
#include "System.hpp"

// only handles RGBA888/RGBX8888 with no scaling

Renderer::Renderer(SDL_Window *window, int width, int height) : sys_width(width), sys_height(height), window(window) {

  surface = SDL_GetWindowSurface(window);

	if (surface == nullptr) {
		std::cerr << "could not get window surface: " << SDL_GetError() << std::endl;
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	resize(w, h);
}

Renderer::~Renderer() {
}

void Renderer::set_mode(Mode new_mode) {

}

void Renderer::resize(int width, int height) {
	win_width = width;
	win_height = height;

  surface = SDL_GetWindowSurface(window);

  memset(surface->pixels, 0, surface->pitch * surface->h);
}

void Renderer::update(System *sys) {
  auto format = blit::PixelFormat(sys->format());
  bool is_lores = sys->mode() == 0;
  auto framebuffer = sys->get_framebuffer();

  auto width = is_lores ? sys_width / 2 : sys_width;
  auto height = is_lores ? sys_height / 2 : sys_height;

  auto out_bpp = surface->format->BytesPerPixel;
  auto out_off = (surface->w - sys_width) / 2 * out_bpp
               + (surface->h - sys_height) / 2 * surface->pitch;

  // TODO: palette
  // TODO: non-RGBA/X8888 output?

  auto in = framebuffer;

  auto out = reinterpret_cast<uint32_t *>(surface->pixels) + out_off / 4;
  auto out2 = out + surface->pitch / 4;

  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      int r, g, b;

      if(format == blit::PixelFormat::RGB565) {
        auto rgb565 = *reinterpret_cast<uint16_t *>(in);
        in += 2;

        r =  rgb565        & 0x1F; r = r << 3 | r >> 2;
        g = (rgb565 >>  5) & 0x3F; g = g << 2 | g >> 4;
        b = (rgb565 >> 11) & 0x1F; b = b << 3 | b >> 2;

      } else {
        r = *in++; g = *in++; b = *in++;
      }

      uint32_t rgba = 0xFF | b << 8 | g << 16 | r << 24;
      *out++ = rgba;

      if(is_lores) {
        *out++ = rgba;
        *out2++ = rgba;
        *out2++ = rgba;
      }
    }

    if(is_lores) {
      out += (surface->pitch + (surface->pitch - sys_width * out_bpp)) / 4;
      out2 = out + surface->pitch / 4;
    } else
      out += (surface->pitch - sys_width * out_bpp) / 4;
  }
}

void Renderer::_render(SDL_Texture *target, SDL_Rect *destination) {
}

void Renderer::present() {
  SDL_UpdateWindowSurface(window);
}

void Renderer::read_pixels(int width, int height, Uint32 format, Uint8 *buffer) {
}

