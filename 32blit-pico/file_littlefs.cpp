#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "config.h"

#include "lfs.h"
#include "hardware/flash.h" // FLASH_PAGE_SIZE

#include "file.hpp"
#include "storage.hpp"

static lfs_t fs;
static lfs_config fs_cfg = {};

std::vector<void *> open_files;

static int lfs_block_read(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
  return storage_read(block, off, buffer, size) == size ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_block_prog(const struct lfs_config *cfg, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
  return storage_program(block, off, (const uint8_t *)buffer, size) == size ? LFS_ERR_OK : LFS_ERR_IO;
}

static int lfs_block_erase(const struct lfs_config *cfg, lfs_block_t block) {
  storage_erase(block);
  return LFS_ERR_OK;
}

static int lfs_block_sync(const struct lfs_config *cfg) {
  return LFS_ERR_OK;
}


void init_fs() {
  fs_cfg.read = lfs_block_read;
  fs_cfg.prog = lfs_block_prog;
  fs_cfg.erase = lfs_block_erase;
  fs_cfg.sync = lfs_block_sync;

  // FIXME: these are wrong if !STORAGE_FLASH
  fs_cfg.read_size = 1;
  fs_cfg.prog_size = FLASH_PAGE_SIZE;

  uint16_t block_size;
  uint32_t num_blocks;
  get_storage_size(block_size, num_blocks);

  fs_cfg.block_size = block_size;
  fs_cfg.block_count = num_blocks;

  fs_cfg.cache_size = fs_cfg.prog_size;
  fs_cfg.lookahead_size = 16;
  fs_cfg.block_cycles = 500;

  int err = lfs_mount(&fs, &fs_cfg);

  // TODO: format if err == LFS_ERR_CORRUPT

  if(err != LFS_ERR_OK)
    printf("Failed to mount filesystem! (%i)\n", err);
}

bool get_files_open() {
  return open_files.size() > 0;
}

void close_open_files() {
  while(!open_files.empty())
    close_file(open_files.back());
}

void *open_file(const std::string &file, int mode) {
  auto f = new lfs_file_t();

  int flags = 0;

  if(mode & blit::OpenMode::read)
    flags |= LFS_O_RDONLY;

  if(mode & blit::OpenMode::write)
    flags |= LFS_O_WRONLY;

  if(mode == blit::OpenMode::write)
    flags |= LFS_O_CREAT;

  int err = lfs_file_open(&fs, f, file.c_str(), flags);

  if(err == LFS_ERR_OK) {
    open_files.push_back(f);
    return f;
  }

  delete f;
  return nullptr;
}

int32_t read_file(void *fh, uint32_t offset, uint32_t length, char *buffer) {
  int r = LFS_ERR_OK;
  auto *f = (lfs_file_t *)fh;

  if(offset != lfs_file_tell(&fs, f))
    r = lfs_file_seek(&fs, f, offset, LFS_SEEK_SET);

  if(r == LFS_ERR_OK){
    r = lfs_file_read(&fs, f, buffer, length);
    if(r >= 0)
      return r;
  }

  return -1;
}

int32_t write_file(void *fh, uint32_t offset, uint32_t length, const char *buffer) {
  int r = LFS_ERR_OK;
  auto *f = (lfs_file_t *)fh;

  if(offset != lfs_file_tell(&fs, f))
    r = lfs_file_seek(&fs, f, offset, LFS_SEEK_SET);

  if(r == LFS_ERR_OK){
    r = lfs_file_write(&fs, f, buffer, length);
    if(r >= 0)
      return r;
  }

  return -1;
}

int32_t close_file(void *fh) {
  auto r = lfs_file_close(&fs, (lfs_file_t *)fh);

  for(auto it = open_files.begin(); it != open_files.end(); ++it) {
    if(*it == fh) {
      open_files.erase(it);
      break;
    }
  }

  delete (lfs_file_t *)fh;
  return r == LFS_ERR_OK ? 0 : -1;
}

uint32_t get_file_length(void *fh) {
  return lfs_file_size(&fs, (lfs_file_t *)fh);
}

void list_files(const std::string &path, std::function<void(blit::FileInfo &)> callback) {
  lfs_dir_t dir;

  if(lfs_dir_open(&fs, &dir, path.c_str()) != LFS_ERR_OK)
    return;

  lfs_info info;

  while(lfs_dir_read(&fs, &dir, &info) == LFS_ERR_OK) {
    blit::FileInfo blit_info{info.name, info.type == LFS_TYPE_REG ? 0 : blit::FileFlags::directory, info.size};

    callback(blit_info);
  }

  lfs_dir_close(&fs, &dir);
}

bool file_exists(const std::string &path) {
  lfs_info info;
  return lfs_stat(&fs, path.c_str(), &info) == LFS_ERR_OK && info.type == LFS_TYPE_REG;
}

bool directory_exists(const std::string &path) {
  lfs_info info;
  return lfs_stat(&fs, path.c_str(), &info) == LFS_ERR_OK && info.type == LFS_TYPE_DIR;
}

bool create_directory(const std::string &path) {
  int err;

  // strip trailing slash
  if(path.back() == '/')
    err = lfs_mkdir(&fs, path.substr(0, path.length() - 1).c_str());
  else
    err = lfs_mkdir(&fs, path.c_str());

  return err == LFS_ERR_OK || err == LFS_ERR_EXIST;
}

bool rename_file(const std::string &old_name, const std::string &new_name) {
  return lfs_rename(&fs, old_name.c_str(), new_name.c_str()) == LFS_ERR_OK;
}

bool remove_file(const std::string &path) {
  return lfs_remove(&fs, path.c_str()) == LFS_ERR_OK;
}
