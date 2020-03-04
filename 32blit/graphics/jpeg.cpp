#include "jpeg.hpp"
#include "../engine/api_private.hpp"

namespace blit {

  static uint8_t *alloc_func(size_t len) {
    return new uint8_t[len];
  }

  JPEGImage decode_jpeg_buffer(uint8_t *ptr, uint32_t len) {
    return api.decode_jpeg_buffer(ptr, len, alloc_func);
  }
  JPEGImage decode_jpeg_file(std::string filename) {
    return api.decode_jpeg_file(filename, alloc_func);
  }
}