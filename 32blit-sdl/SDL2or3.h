
#ifdef SDL3
#define SDL_ENABLE_OLD_NAMES
#include <SDL3/SDL.h>

#define SDL_WINDOW_SHOWN 0 // removed flag
#else
#include "SDL.h"
#endif
