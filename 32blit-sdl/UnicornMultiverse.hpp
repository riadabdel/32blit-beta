#pragma once
#include <string>
#include <vector>

#include "types/rect.hpp"
#include "types/size.hpp"

class Renderer;

class Multiverse {
  public:
    Multiverse();

    void update(Renderer *source);

  private:
    class SerialPort {
      public:
        SerialPort(const std::string &port_name);

        bool open(const std::string &port_name);

        void write(const uint8_t *buf, size_t len);
      private:

      int fd = -1;
    };

    class Display {
      public:
        Display(const std::string &port_name, blit::Rect rect);

        void update(const uint8_t *buf, blit::Size bounds);

        const blit::Rect &get_rect() const {return rect;}

      private:
        int thread_run();
        static int static_thread_run(void *arg);

        SerialPort port;
        blit::Rect rect;

        SDL_Thread *thread;
        SDL_sem *write_sem, *done_sem;
        uint8_t *buf;
    };

    std::vector<Display> displays;
    blit::Size bounds;

    uint8_t *buf;
};
