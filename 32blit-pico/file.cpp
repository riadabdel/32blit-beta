#include <cstdio>

#include "pico/binary_info.h"

#include "file.hpp"

static char save_path[32]; // max game title length is 24 + ".blit/" + "/"

const char *get_save_path() {
  const char *app_name = "_unknown";

  if(!directory_exists(".blit"))
    create_directory(".blit");

  app_name = "_unknown";

  // fint the program name in the binary info
  extern binary_info_t *__binary_info_start, *__binary_info_end;

  for(auto tag_ptr = &__binary_info_start; tag_ptr != &__binary_info_end ; tag_ptr++) {
    if((*tag_ptr)->type != BINARY_INFO_TYPE_ID_AND_STRING || (*tag_ptr)->tag != BINARY_INFO_TAG_RASPBERRY_PI)
      continue;

    auto id_str_tag = (binary_info_id_and_string_t *)*tag_ptr;

    if(id_str_tag->id == BINARY_INFO_ID_RP_PROGRAM_NAME) {
      app_name = id_str_tag->value;
      break;
    }
  }

  snprintf(save_path, sizeof(save_path), ".blit/%s/", app_name);

  // make sure it exists
  if(!directory_exists(save_path))
    create_directory(save_path);


  return save_path;
}
