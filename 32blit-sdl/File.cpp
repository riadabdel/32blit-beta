#if __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#endif

#include <string>

#include "SDL.h"

#include "File.hpp"
#include "UserCode.hpp"

static std::string base_path, save_path;

static std::string map_path(const std::string &path) {
  // check if the path is under the save path
  if(path.compare(0, save_path.length(), save_path) == 0)
    return path;

  // otherwise it should be under the base path
  return base_path + path;
}

void setup_base_path() {
  auto base_path_str = SDL_GetBasePath();
  if(base_path_str)
    base_path = std::string(base_path_str);
  SDL_free(base_path_str);

  auto tmp = SDL_GetPrefPath(metadata_author, metadata_title);
  if(tmp)
    save_path = std::string(tmp);

  SDL_free(tmp);
}

void *open_file(const std::string &name, int mode) {
  const char *str_mode;

  if(mode == blit::OpenMode::read)
    str_mode = "rb";
  else if(mode == blit::OpenMode::write)
    str_mode = "wb";
  else if(mode == (blit::OpenMode::read | blit::OpenMode::write))
    str_mode = "r+";
  else
    return nullptr;

  auto file = SDL_RWFromFile(map_path(name).c_str(), str_mode);

  return file;
}

int32_t read_file(void *fh, uint32_t offset, uint32_t length, char *buffer) {
  auto file = (SDL_RWops *)fh;

  if(file && SDL_RWseek(file, offset, RW_SEEK_SET) != -1) {
    size_t bytes_read = SDL_RWread(file, buffer, 1, length);

    if(bytes_read > 0)
      return (int32_t)bytes_read;
  }

  return -1;
}

int32_t write_file(void *fh, uint32_t offset, uint32_t length, const char *buffer) {
  auto file = (SDL_RWops *)fh;

  if(file && SDL_RWseek(file, offset, RW_SEEK_SET) != -1) {
    size_t bytes_written = SDL_RWwrite(file, buffer, 1, length);

    if(bytes_written > 0)
      return (int32_t)bytes_written;
  }

  return -1;
}

int32_t close_file(void *fh) {
  return SDL_RWclose((SDL_RWops *)fh) == 0 ? 0 : -1;
}

uint32_t get_file_length(void *fh)
{
  auto file = (SDL_RWops *)fh;
  SDL_RWseek(file, 0, RW_SEEK_END);

  return (uint32_t)SDL_RWtell(file);
}

void list_files(const std::string &path, std::function<void(blit::FileInfo &)> callback) {

  std::error_code err;
  for(auto &entry : fs::directory_iterator(map_path(path), fs::directory_options::follow_directory_symlink, err)) {
    blit::FileInfo info;

    info.name = entry.path().filename().generic_string();

    info.flags = 0;
    info.size = entry.file_size();

    if(entry.status().type() == fs::file_type::directory)
      info.flags |= blit::FileFlags::directory;

    callback(info);
  }
}

bool file_exists(const std::string &path) {
  std::error_code err;
  return fs::status(map_path(path), err).type() == fs::file_type::regular;
}

bool directory_exists(const std::string &path) {
  std::error_code err;
  return fs::status(map_path(path), err).type() == fs::file_type::directory;
}

bool create_directory(const std::string &path) {
  std::error_code err;
  return directory_exists(path) || fs::create_directory(map_path(path), err);
}

bool rename_file(const std::string &old_name, const std::string &new_name) {
  std::error_code err;
  fs::rename(map_path(old_name), map_path(new_name), err);
  return !err;
}

bool remove_file(const std::string &path) {
  std::error_code err;
  return fs::remove(map_path(path), err);
}

const char *get_save_path() {
  return save_path.c_str();
}

bool is_storage_available() {
  return true;
}
