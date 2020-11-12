cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
cmake_policy(SET CMP0069 NEW)

project(32blit)
include(32blit.cmake)
install(FILES 32blit.cmake DESTINATION lib/cmake/32blit)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/32blit 32blit)

if (${CMAKE_SYSTEM_NAME} STREQUAL Generic)
    include(${CMAKE_CURRENT_LIST_DIR}/32blit-stm32/executable.cmake)
else()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/32blit-sdl 32blit-sdl)
endif()

add_subdirectory(examples)

if(${CMAKE_SYSTEM_NAME} STREQUAL Generic)
    add_subdirectory(32blit-stm32)
    add_subdirectory(firmware)
endif()

if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL Generic AND NOT EMSCRIPTEN)
    add_subdirectory(tools/src)
endif()
