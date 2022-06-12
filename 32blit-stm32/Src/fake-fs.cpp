#include <algorithm>
#include <cstdint>
#include <cstring>

//
const uint32_t qspi_flash_sector_size = 64 * 1024;
const uint32_t qspi_flash_size = 32768 * 1024;
const uint32_t qspi_flash_address = 0x90000000;
constexpr uint32_t qspi_tmp_reserved = 4 * 1024 * 1024;
#include "executable.hpp"
#include <cstdio>
//

struct [[gnu::packed]] VBR {
  // BPB
  uint8_t jump_instruction[3];
  char oem_name[8];
  uint16_t bytes_per_sector;
  uint8_t sectors_per_cluster;
  uint16_t num_reserved_sectors;
  uint8_t num_fats;
  uint16_t max_root_entries;
  uint16_t total_sectors;
  uint8_t media_descriptor;
  uint16_t sectors_per_fat;
  uint16_t sectors_per_track;
  uint16_t num_heads;
  uint32_t num_hidden_sectors;
  uint32_t total_sectors32;

  // EBPB
  uint8_t drive_number;
  uint8_t reserved;
  uint8_t extended_boot_sig; // = 0x29
  uint32_t volume_id;
  char volume_label[11];
  char fs_type[8];
};

static_assert(sizeof(VBR) == 62);

struct DirEntry {
  char short_name[8];
  char short_ext[3];
  uint8_t attributes;
  uint8_t reserved;
  uint8_t create_time_fine;
  uint16_t create_time;
  uint16_t create_date;
  uint16_t access_date;
  uint16_t ea_index; // high bytes of cluster for FAT32
  uint16_t modified_time;
  uint16_t modified_date;
  uint16_t start_cluster;
  uint32_t file_size;
};

static_assert(sizeof(DirEntry) == 32);

static constexpr unsigned int next_power_of_2(unsigned int i) {
  i--;
  i |= i >> 1;
  i |= i >> 2;
  i |= i >> 4;
  i |= i >> 8;
  i |= i >> 16;
  i++;
  return i;
}

void fake_fs_read_sector(uint32_t sector, uint8_t *buf) {
  const int target_size = qspi_flash_size; // how much data we want

  const int num_reserved_sectors = 1;
  //const int max_root_entries = 16;
  const int max_root_entries = 28 * 1024 / 64; // 32M - 4M reserved / block size
  const int sector_size = 512;

  const int max_clusters = 4085; // FAT12

  // worst-case FAT size
  const int largest_fat = 12; // 4096 clusters

  // single-sector FAT
  //const int max_clusters = 341;
  //const int largest_fat = 1;

  const int padded_sectors = (target_size + (sector_size - 1)) / sector_size + num_reserved_sectors + (max_root_entries * 32) / sector_size + largest_fat;

  // calc cluster size
  const int sectors_per_cluster = next_power_of_2((padded_sectors + max_clusters - 1) / max_clusters);

  const int num_clusters = padded_sectors / sectors_per_cluster;

  // + 1 to round, + 2 for reserved
  const int sectors_per_fat = (((num_clusters + 3) / 2 * 3) + (sector_size - 1)) / sector_size;

  // shrink to actual FAT size
  const int num_sectors = padded_sectors - (largest_fat - sectors_per_fat);

  const char *label = "DAFTVOLUME "; // 11 chars

  // offsets
  const int root_dir_start = num_reserved_sectors + sectors_per_fat /* * num_fats*/;
  const int data_region_start = root_dir_start + max_root_entries * 32 / sector_size;

  if(sector == 0) {// VBR
    memset(buf, 0, sector_size);
    auto vbr = reinterpret_cast<VBR *>(buf);

    vbr->jump_instruction[0] = 0xEB;
    vbr->jump_instruction[1] = 0x3C;
    vbr->jump_instruction[2] = 0x90;

    memcpy(vbr->oem_name, "DAFTFAT ", 8);

    vbr->bytes_per_sector = sector_size;
    vbr->sectors_per_cluster = sectors_per_cluster;
    vbr->num_reserved_sectors = num_reserved_sectors;
    vbr->num_fats = 1;
    vbr->max_root_entries = max_root_entries;

    if(num_sectors > 0xFFFF) {
      vbr->total_sectors = 0;
      vbr->total_sectors32 = num_sectors;
    } else
      vbr->total_sectors = uint16_t(num_sectors);

    vbr->media_descriptor = 0xF8; // fixed disk
    vbr->sectors_per_fat = sectors_per_fat;

    vbr->drive_number = 0x80; // first fixed disk
    vbr->extended_boot_sig = 0x29;
    vbr->volume_id = 0x12345678;
    memcpy(vbr->volume_label, label, 11);
    memcpy(vbr->fs_type, "FAT12   ", 8);
  } else if(sector >= num_reserved_sectors && sector < root_dir_start) { // FAT
    auto fat_sector = sector - num_reserved_sectors;

    memset(buf, 0, sector_size);

    auto start_cluster = fat_sector * sector_size / 3 * 2;
    auto end_cluster = ((fat_sector + 1) * sector_size + 2) / 3 * 2; // rounded up
    // reserved clusters
    if(fat_sector == 0){
      buf[0] = 0xF8;
      buf[1] = 0xFF;
      buf[2] = 0xFF;
      start_cluster = 2;
    }

    // chains for files
    uint32_t cluster = start_cluster;

    auto cluster_size = sectors_per_cluster * sector_size;
    bool first = true;

    while(cluster < end_cluster) {
      uint32_t file_len_bytes = 0;

      // search for the game start
      uint32_t offset = (cluster - 2) * cluster_size;
      offset &= ~(qspi_flash_sector_size - 1);

      // start searching backwards if not first sector
      int step = first ? -qspi_flash_sector_size : qspi_flash_sector_size;
      first = false;

      for(; offset >= 0 && offset < qspi_flash_size - qspi_tmp_reserved; offset += step) {
        auto header = (BlitGameHeader *)(qspi_flash_address + offset);

        if(header->magic == blit_game_magic) {
          // get the size
          file_len_bytes = header->end - qspi_flash_address;

          // + metadata
          auto metadata_offset = offset + file_len_bytes;
          if(memcmp((uint8_t *)qspi_flash_address + metadata_offset, "BLITMETA", 8) == 0)
            file_len_bytes += *(uint16_t *)(qspi_flash_address + metadata_offset + 8);

          if((offset + file_len_bytes) / cluster_size > cluster - 2)
            break; // found file, contains this cluster
          else {
            // file already ended, search forwards instead
            step = qspi_flash_sector_size;
            file_len_bytes = 0;
          }
        }

        // search forwards
        if(!offset && step < 0)
          step = qspi_flash_sector_size;
      }

      // no more games
      if(!file_len_bytes)
        break;

      // convert to clusters
      uint32_t file_start = offset / cluster_size + 2;
      uint32_t file_len = (file_len_bytes + cluster_size - 1) / cluster_size;

      // clamping
      cluster = std::max(file_start, cluster);
      auto file_end_cluster = std::min(file_start + file_len, end_cluster);

      for(; cluster < file_end_cluster; cluster++) {
        int pair_off = cluster / 2 * 3 - fat_sector * sector_size;

        // next or EOF
        int val = cluster == file_start + file_len - 1 ? 0xFFF : cluster + 1;

        if(cluster & 1) {
          // high bits
          if(pair_off != -2) // low bits are in previous sector
            buf[pair_off + 1] = (buf[pair_off + 1] & 0xF) | (val & 0xF) << 4;

          if(pair_off != sector_size - 2) // high bits are in next sector
            buf[pair_off + 2] = val >> 4;
        } else {
          // low bits
          if(pair_off != -1) // low bits are in previous sector
            buf[pair_off + 0] = val & 0xFF;

          if(pair_off != sector_size - 1) // high bits are in next sector
            buf[pair_off + 1] = (buf[pair_off + 1] & 0xF0) | val >> 8;
        }
      }
    }
  }
  else if(sector >= root_dir_start && sector < data_region_start) { // root dir
    memset(buf, 0, sector_size);
    auto entries = reinterpret_cast<DirEntry *>(buf);

    int entries_per_sector = sector_size / 32;

    int entry = 0;
    int entry_off = (sector - root_dir_start) * entries_per_sector;

    if(sector == root_dir_start) {
      memcpy(entries, label, 11); // name+ext of first entry
      entries[0].attributes = 0x8; // label
      entry++;
    } else
      entry_off--; // first sector has one less entry

    // search for games

    for(uint32_t offset = 0; offset < qspi_flash_size - qspi_tmp_reserved && entry < entries_per_sector; offset += qspi_flash_sector_size) {
      auto header = (BlitGameHeader *)(qspi_flash_address + offset);

      if(header->magic != blit_game_magic)
        continue;

      // skip games that already have an entry
      // TODO: probably a bit slow
      // (cache last offset?)
      if(entry_off && entry_off--)
        continue;

      snprintf(entries[entry].short_name, 8, "GAME%03i", offset / qspi_flash_sector_size);
      entries[entry].short_name[7] = ' ';
      memcpy(entries[entry].short_ext, "BLT", 3);

      // cluster size should be smaller than flash sector size
      entries[entry].start_cluster = 2 + offset / (sectors_per_cluster * sector_size);
      entries[entry].file_size = header->end - qspi_flash_address;

      auto metadata_offset = offset + entries[entry].file_size;
      if(memcmp((uint8_t *)qspi_flash_address + metadata_offset, "BLITMETA", 8) == 0) {
        auto metadata_size = *(uint16_t *)(qspi_flash_address + metadata_offset + 8);
        auto metadata = (RawMetadata *)(qspi_flash_address + metadata_offset + 10);

        // TODO: use meta
        // TODO: LFN

        entries[entry].file_size += metadata_size + 10;
      }

      // FIXME: should skip to end
      entry++;
    }

  } else if(sector >= data_region_start) { // data region
    uint32_t off = (sector - data_region_start) * sector_size;
    if(off + sector_size <= qspi_flash_size) {
      memcpy(buf, (uint8_t *)qspi_flash_address + off, sector_size);
    } else
      memset(buf, 0, sector_size);
  }
}
