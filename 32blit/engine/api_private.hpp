#pragma once
#include <cstdarg>
#include <cstdint>
#include <vector>

#include "engine.hpp"
#include "file.hpp"
#include "../audio/audio.hpp"
#include "../engine/input.hpp"
#include "../graphics/jpeg.hpp"
#include "../graphics/surface.hpp"
#include "../types/vec2.hpp"
#include "../types/vec3.hpp"

#ifdef TARGET_32BLIT_HW
#define API_FUNC(name) (*name)
#else
#define API_FUNC(name) name
#endif

namespace blit {

  using AllocateCallback = uint8_t *(*)(size_t);

  constexpr uint32_t api_version = 0;

  #pragma pack(push, 4)
  struct API {
    uint32_t version = api_version;

    ButtonState buttons;
    float hack_left;
    float hack_right;
    float vibration;
    Vec2 joystick;
    Vec3 tilt;
    Pen LED;

    AudioChannel *channels;

    Surface &API_FUNC(set_screen_mode)  (ScreenMode new_mode);
    void API_FUNC(set_screen_palette)  (const Pen *colours, int num_cols);
    uint32_t API_FUNC(now)();
    uint32_t API_FUNC(random)();
    void API_FUNC(exit)(bool is_error);

    // serial debug
    void API_FUNC(debug)(const char *message);

    // files
    void *API_FUNC(open_file)(const std::string &file, int mode);
    int32_t API_FUNC(read_file)(void *fh, uint32_t offset, uint32_t length, char* buffer);
    int32_t API_FUNC(write_file)(void *fh, uint32_t offset, uint32_t length, const char* buffer);
    int32_t API_FUNC(close_file)(void *fh);
    uint32_t API_FUNC(get_file_length)(void *fh);
    void API_FUNC(list_files) (const std::string &path, std::function<void(FileInfo &)> callback);
    bool API_FUNC(file_exists) (const std::string &path);
    bool API_FUNC(directory_exists) (const std::string &path);
    bool API_FUNC(create_directory) (const std::string &path);
    bool API_FUNC(rename_file) (const std::string &old_name, const std::string &new_name);
    bool API_FUNC(remove_file) (const std::string &path);
    const char *API_FUNC(get_save_path)();
    bool API_FUNC(is_storage_available)();

    // profiler
    void API_FUNC(enable_us_timer)();
    uint32_t API_FUNC(get_us_timer)();
    uint32_t API_FUNC(get_max_us_timer)();

    // jepg
    JPEGImage API_FUNC(decode_jpeg_buffer)(const uint8_t *ptr, uint32_t len, AllocateCallback alloc);
    JPEGImage API_FUNC(decode_jpeg_file)(const std::string &filename, AllocateCallback alloc);

    // launcher APIs - only intended for use by launchers and only available on device
    bool API_FUNC(launch)(const char *filename);
    void API_FUNC(erase_game)(uint32_t offset);
  };
  #pragma pack(pop)

  extern API &api;
}
