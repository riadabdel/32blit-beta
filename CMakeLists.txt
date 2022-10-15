cmake_minimum_required(VERSION 3.9)

# has to be before project
include(32blit-pico/sdk_import.cmake)

project(32blit)

if(MSVC)
    add_compile_options("/W4" "/wd4244" "/wd4324" "/wd4458" "/wd4100")
else()
    add_compile_options("-Wall" "-Wextra" "-Wdouble-promotion" "-Wno-unused-parameter")
endif()

if(NINTENDO_3DS)
    include(FetchContent)

    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL
        GIT_TAG        main
        #GIT_TAG        release-2.24.0
    )

    set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE) # fails to find SDL2main

    FetchContent_Declare(
        SDL2_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image
        GIT_TAG        main
        #GIT_TAG        release-2.6.2
    )

    #FetchContent_Declare(
    #    SDL2_net
    #    GIT_REPOSITORY https://github.com/libsdl-org/SDL_net
    #    GIT_TAG        release-2.2.0
    #)

    FetchContent_MakeAvailable(SDL2 SDL2_image) #SDL2_net

    add_library(SDL2_image::SDL2_image ALIAS SDL2_image) # static built and we only search for shared...
    #add_library(SDL2_net::SDL2_net ALIAS SDL2_net)
endif()

find_package (32BLIT CONFIG REQUIRED PATHS .)
add_subdirectory(examples)

add_subdirectory(launcher-shared)

if(32BLIT_HW)
    add_subdirectory(32blit-stm32)
    add_subdirectory(firmware)
    add_subdirectory(firmware-update)
endif()

if(NOT 32BLIT_PICO)
    add_subdirectory(launcher)
endif()

# include dist files in install
install(DIRECTORY
    ${CMAKE_CURRENT_LIST_DIR}/dist/
    DESTINATION .
)

# setup release packages
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
