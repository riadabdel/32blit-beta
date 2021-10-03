set(CMAKE_C_STANDARD 11)

# Initialise the Pico SDK
include (pico_sdk_import.cmake)
include (pico_extras_import.cmake)
pico_sdk_init()

set(32BLIT_PICO 1 PARENT_SCOPE)

add_library(BlitHalPico INTERFACE)
target_sources(BlitHalPico INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/../3rd-party/fatfs/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/../3rd-party/fatfs/ffunicode.c

    ${CMAKE_CURRENT_LIST_DIR}/audio.cpp
    ${CMAKE_CURRENT_LIST_DIR}/display.cpp
    ${CMAKE_CURRENT_LIST_DIR}/file.cpp
    ${CMAKE_CURRENT_LIST_DIR}/input.cpp
    ${CMAKE_CURRENT_LIST_DIR}/led.cpp
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/storage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/st7789.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

pico_generate_pio_header(BlitHalPico ${CMAKE_CURRENT_LIST_DIR}/st7789.pio)

target_link_libraries(BlitHalPico INTERFACE hardware_dma hardware_pio hardware_pwm hardware_spi pico_multicore pico_stdlib pico_unique_id tinyusb_device)
target_include_directories(BlitHalPico INTERFACE
    ${CMAKE_CURRENT_LIST_DIR} # for tusb_config
    ${CMAKE_CURRENT_LIST_DIR}/../3rd-party/fatfs
)

target_compile_definitions(BlitHalPico INTERFACE
    PICO_AUDIO_I2S_MONO_INPUT=1
    PICO_AUDIO_DMA_IRQ=1
)

target_compile_options(BlitHalPico INTERFACE
    -Wno-ignored-qualifiers # pico-sdk generates a lot of these
)

if(${PICO_BOARD} STREQUAL "vgaboard")
    message("Using VGA board...")
    target_link_libraries(BlitHalPico INTERFACE pico_scanvideo_dpi pico_audio_i2s)
    target_compile_definitions(BlitHalPico INTERFACE
        PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA=1
        AUDIO_I2S
        DISPLAY_SCANVIDEO
    )
elseif(${PICO_BOARD} STREQUAL "pimoroni_picosystem")
    message("Using picosystem...")
    target_compile_definitions(BlitHalPico INTERFACE
        DISPLAY_ST7789
        INPUT_GPIO
        LED_R_PIN=PICOSYSTEM_LED_R_PIN
        LED_G_PIN=PICOSYSTEM_LED_G_PIN
        LED_B_PIN=PICOSYSTEM_LED_B_PIN
        USB_PRODUCT_STR="PicoSystem"
    )
    if(AUDIO_PWM)
        target_link_libraries(BlitHalPico INTERFACE pico_audio_pwm)
        target_compile_definitions(BlitHalPico INTERFACE
            PICO_AUDIO_PWM_MONO_PIN=PICOSYSTEM_AUDIO_PIN
            PICO_AUDIO_PWM_PIO=1
            AUDIO_PWM
        )
    else()
        target_compile_definitions(BlitHalPico INTERFACE
            AUDIO_BEEP_PIN=PICOSYSTEM_AUDIO_PIN
            AUDIO_BEEP
        )
    endif()
elseif(DEFINED PICO_ADDON)
    message(STATUS "No config for \"${PICO_BOARD}\", checking for add-on \"${PICO_ADDON}\"")
    if(${PICO_ADDON} STREQUAL "explorer_base")
        set(AUDIO_PIN 7 CACHE STRING "Pin for audio output")

        target_compile_definitions(BlitHalPico INTERFACE
            AUDIO_BEEP_PIN=${AUDIO_PIN} # not connected to anything by default
            AUDIO_BEEP
            DISPLAY_ST7789
            INPUT_GPIO # only ABXY
        )
    elseif(${PICO_ADDON} STREQUAL "display_pack")
        target_compile_definitions(BlitHalPico INTERFACE
            DISPLAY_ST7789
            DISPLAY_HEIGHT=135
            INPUT_GPIO # only ABXY
            LED_INVERTED
            LED_R_PIN=6
            LED_G_PIN=7
            LED_B_PIN=8
        )
    elseif(${PICO_ADDON} STREQUAL "display_pack_2")
        target_compile_definitions(BlitHalPico INTERFACE
            DISPLAY_ST7789
            DISPLAY_WIDTH=320
            INPUT_GPIO # only ABXY
            LED_INVERTED
            LED_R_PIN=6
            LED_G_PIN=7
            LED_B_PIN=8
        )
    else()
        message(FATAL_ERROR "Unknown add-on ${PICO_ADDON}!")
    endif()
else()
    message(WARNING "Using default config for \"${PICO_BOARD}\"...")
    target_link_libraries(BlitHalPico INTERFACE pico_audio_i2s)
    # messy explorer-based setup
    target_compile_definitions(BlitHalPico INTERFACE
        PICO_AUDIO_I2S_DATA_PIN=6
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=20
        AUDIO_I2S
        DISPLAY_ST7789
        INPUT_GPIO
    )
endif()


function(blit_executable_common NAME)
    target_link_libraries(${NAME} BlitEngine)

endfunction()

function(blit_executable_int_flash NAME SOURCES)
    add_executable(${NAME} ${SOURCES} ${ARGN})
    target_link_libraries(${NAME} BlitHalPico BlitEngine)

    pico_enable_stdio_uart(${NAME} 1)
    pico_enable_stdio_usb(${NAME} 0)

    pico_add_extra_outputs(${NAME})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
        DESTINATION bin
    )
endfunction()

function(blit_executable NAME SOURCES)
    blit_executable_int_flash(${NAME} ${SOURCES} ${ARGN})
endfunction()

function(blit_metadata TARGET FILE)
    # cause cmake to reconfigure whenever the asset list changes
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${FILE})

    # get the inputs/outputs for the asset tool (at configure time)
    execute_process(COMMAND ${PYTHON_EXECUTABLE} -m ttblit cmake --config ${CMAKE_CURRENT_SOURCE_DIR}/${FILE} --cmake ${CMAKE_CURRENT_BINARY_DIR}/metadata.cmake)
    include(${CMAKE_CURRENT_BINARY_DIR}/metadata.cmake)

    pico_set_program_name(${TARGET} "${METADATA_TITLE}")
    pico_set_program_description(${TARGET} "${METADATA_DESCRIPTION}")
    pico_set_program_version(${TARGET} "${METADATA_VERSION}")
    pico_set_program_url(${TARGET} "${METADATA_URL}")
endfunction()
