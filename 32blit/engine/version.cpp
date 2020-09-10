#include "version.hpp"
#include "version_defs.hpp"
#include "api_private.hpp"

namespace blit {
  /**
   * Get the version number of the library e.g. "v1.0.0"
   * 
   * \returns Version number string
   */
  const char *get_version_string() {
    return BLIT_BUILD_VER;
  }

  /**
   * Get the build date of the library e.g. "2020-03-04"
   * 
   * \returns Build date string
   */
  const char *get_build_date() {
    return BLIT_BUILD_DATE;
  }

  GameMetadata get_metadata() {
    return api.get_metadata();
  }
}
