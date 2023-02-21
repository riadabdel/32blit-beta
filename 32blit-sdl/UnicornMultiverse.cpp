// TODO: windows
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#endif

#include "SDL.h"

#include "UnicornMultiverse.hpp"

#include "Renderer.hpp"

Multiverse::Multiverse() : bounds(0, 0) {
  displays.emplace_back("/dev/serial/by-id/usb-Raspberry_Pi_Picoprobe_E6614C311B81A836-if00", blit::Rect{0, 0, 53, 11});

  for(auto &display : displays) {
    auto max = display.get_rect().br();

    if(max.x > bounds.w)
      bounds.w = max.x;
    if(max.y > bounds.h)
      bounds.h = max.y;
  }

  buf = new uint8_t[bounds.area() * 4];
}

void Multiverse::update(Renderer *source) {
  source->read_pixels(bounds.w, bounds.h, SDL_PIXELFORMAT_RGB888, buf);

  for(auto &display : displays)
    display.update(buf, bounds);
}

Multiverse::SerialPort::SerialPort(const std::string &port_name) {
  open(port_name);
}

bool Multiverse::SerialPort::open(const std::string &port_name) {
#ifdef _WIN32
  return false;
#else
  if(fd >= 0)
    close(fd);

  fd = ::open(port_name.c_str(), O_RDWR | O_NOCTTY | O_SYNC);

  if(fd < 0)
    return false;

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  struct termios tio;
  tcgetattr(fd, &tio);
  cfmakeraw(&tio);
  tcsetattr(fd, TCSANOW, &tio);

  return true;
#endif
}

void Multiverse::SerialPort::write(const uint8_t *buf, size_t len) {
#ifndef _WIN32
  auto remaining = len;
  auto p = buf;

  while(remaining) {
    auto written = ::write(fd, p, remaining);

    if(written < 0)
      break;

    remaining -= written;
    buf += written;
  }

  tcdrain(fd);
#endif
}

Multiverse::Display::Display(const std::string &port_name, blit::Rect rect) : port(port_name), rect(rect) {
  thread = SDL_CreateThread(static_thread_run, "MultiverseDisplay", this);
  write_sem = SDL_CreateSemaphore(0);
  done_sem = SDL_CreateSemaphore(0);

  buf = new uint8_t[rect.size().area() * 4];
}

void Multiverse::Display::update(const uint8_t *buf, blit::Size bounds) {

  if(!SDL_SemWaitTimeout(done_sem, 0))
    return;

  for(int y = 0; y < rect.h; y++) {
    memcpy(this->buf + y * rect.w * 4, buf + (rect.x + (rect.y + y) * bounds.w) * 4, rect.w * 4);
  }

  SDL_SemPost(write_sem);
}


int Multiverse::Display::thread_run() {
  //TODO: stop thread

  while(true) {
    SDL_SemWait(write_sem);
    port.write(buf, rect.size().area() * 4);
    SDL_SemPost(done_sem);
  }
}

int Multiverse::Display::static_thread_run(void *arg) {
  return ((Display *)arg)->thread_run();
}
