#include <cerrno>
#include <string>
#include <map>

#ifdef WIN32
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "SDL.h"

#include "File.hpp"

static std::string basePath;

void setup_base_path()
{
  auto basePathPtr = SDL_GetBasePath();
  basePath = std::string(basePathPtr);
  SDL_free(basePathPtr);
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

  auto file = SDL_RWFromFile((basePath + name).c_str(), str_mode);
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

std::vector<blit::FileInfo> list_files(const std::string &path) {
  std::vector<blit::FileInfo> ret;

#ifdef WIN32
  std::error_code err;
  for(auto &entry: fs::directory_iterator(basePath + path, fs::directory_options::follow_directory_symlink, err)) {
    blit::FileInfo info;

    info.name = entry.path().filename().generic_string();

    info.flags = 0;
    info.size = entry.file_size();

    if(entry.is_directory())
      info.flags |= blit::FileFlags::directory;

    ret.push_back(info);
  }
#else
  auto dir = opendir((basePath + path).c_str());

  if(!dir)
    return ret;

  struct dirent *ent;

  while((ent = readdir(dir))) {
    blit::FileInfo info;

    info.name = ent->d_name;

    if(info.name == "." || info.name == "..")
      continue;

    struct stat stat_buf;

    if(stat((basePath + path + "/" + info.name).c_str(), &stat_buf) < 0)
      continue;

    info.flags = 0;
    info.size = stat_buf.st_size;

    if(S_ISDIR(stat_buf.st_mode))
      info.flags |= blit::FileFlags::directory;

    ret.push_back(info);
  }

  closedir(dir);
#endif

  return ret;
}

bool file_exists(const std::string &path) {
#ifdef WIN32
  std::error_code err;
  return fs::status(basePath + path, err).type() == fs::file_type::regular;
#else
  struct stat stat_buf;
  return (stat((basePath + path).c_str(), &stat_buf) == 0 && S_ISREG(stat_buf.st_mode));
#endif
}

bool directory_exists(const std::string &path) {
#ifdef WIN32
  std::error_code err;
  return fs::status(basePath + path, err).type() == fs::file_type::directory;
#else
  struct stat stat_buf;
  return (stat((basePath + path).c_str(), &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode));
#endif
}

bool create_directory(const std::string &path) {
#ifdef WIN32
  std::error_code err;
  return directory_exists(path) || fs::create_directory(path, err);
#else
  return mkdir((basePath + path).c_str(), 0755) == 0 || errno == EEXIST;
#endif
}

bool rename_file(const std::string &old_name, const std::string &new_name) {
  return rename((basePath + old_name).c_str(), (basePath + new_name).c_str()) == 0;
}

bool remove_file(const std::string &path) {
  return remove((basePath + path).c_str()) == 0;
}