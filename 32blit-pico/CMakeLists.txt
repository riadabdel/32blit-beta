set(CMAKE_C_STANDARD 11)

# Initialise the Pico SDK
include (pico_sdk_import.cmake)

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
    ${CMAKE_CURRENT_LIST_DIR}/st7789.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb.cpp
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
)

pico_generate_pio_header(BlitHalPico ${CMAKE_CURRENT_LIST_DIR}/st7789-spi.pio)
pico_generate_pio_header(BlitHalPico ${CMAKE_CURRENT_LIST_DIR}/st7789-8bit.pio)
pico_generate_pio_header(BlitHalPico ${CMAKE_CURRENT_LIST_DIR}/spi.pio)
pico_generate_pio_header(BlitHalPico ${CMAKE_CURRENT_LIST_DIR}/sd_card.pio)

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
    target_link_libraries(BlitHalPico INTERFACE pico_scanvideo_dpi pico_audio_i2s tinyusb_host)
    target_compile_definitions(BlitHalPico INTERFACE
        PICO_SCANVIDEO_PLANE1_VARIABLE_FRAGMENT_DMA=1
        PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=12
        AUDIO_I2S
        DISPLAY_SCANVIDEO
        USB_HOST
        INPUT_USB_HID
        STORAGE_SD
    )
    set(REQUIRE_PICO_EXTRAS TRUE) # scanvideo/audio_i2s
    set(STORAGE_DRIVER sd_spi)
elseif(${PICO_BOARD} STREQUAL "pimoroni_picosystem")
    message("Using picosystem...")
    target_compile_definitions(BlitHalPico INTERFACE
        DISPLAY_ST7789
        INPUT_GPIO

        LED_R_PIN=PICOSYSTEM_LED_R_PIN
        LED_G_PIN=PICOSYSTEM_LED_G_PIN
        LED_B_PIN=PICOSYSTEM_LED_B_PIN

        LCD_CS_PIN=PICOSYSTEM_LCD_CSN_PIN
        LCD_DC_PIN=PICOSYSTEM_LCD_DC_PIN
        LCD_SCK_PIN=PICOSYSTEM_LCD_SCLK_PIN
        LCD_MOSI_PIN=PICOSYSTEM_LCD_MOSI_PIN
        LCD_BACKLIGHT_PIN=PICOSYSTEM_BACKLIGHT_PIN
        LCD_VSYNC_PIN=PICOSYSTEM_LCD_VSYNC_PIN
        LCD_RESET_PIN=PICOSYSTEM_LCD_RESET_PIN

        USB_PRODUCT_STR="PicoSystem"
    )
    if(AUDIO_PWM)
        target_link_libraries(BlitHalPico INTERFACE pico_audio_pwm)
        target_compile_definitions(BlitHalPico INTERFACE
            PICO_AUDIO_PWM_MONO_PIN=PICOSYSTEM_AUDIO_PIN
            PICO_AUDIO_PWM_PIO=1
            AUDIO_PWM
        )
        set(REQUIRE_PICO_EXTRAS TRUE) # audio_pwm
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
    elseif(${PICO_ADDON} STREQUAL "pico_dv")
        # grab a copy of PicoDVI
        include(FetchContent)

        FetchContent_Populate(picodvi
            GIT_REPOSITORY https://github.com/Wren6991/PicoDVI
            GIT_TAG 62a41787f881f3ad8038fb7d45335c8f457bc4a2
        )
        add_subdirectory(${picodvi_SOURCE_DIR}/software/libdvi ${picodvi_BINARY_DIR} EXCLUDE_FROM_ALL)

        target_link_libraries(BlitHalPico INTERFACE libdvi pico_audio_i2s tinyusb_host)
        target_include_directories(BlitHalPico INTERFACE ${picodvi_SOURCE_DIR}/software/include) # common_dvi_pin_configs.h

        target_compile_definitions(BlitHalPico INTERFACE
            PICO_AUDIO_I2S_PIO=1
            PICO_AUDIO_I2S_DATA_PIN=26
            PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
            AUDIO_I2S
            DISPLAY_PICODVI
            DVI_DEFAULT_SERIAL_CONFIG=pimoroni_demo_hdmi_cfg
            DVI_16BPP_RED_MSB=4
            DVI_16BPP_RED_LSB=0
            DVI_16BPP_BLUE_MSB=15
            DVI_16BPP_BLUE_LSB=11
            USB_HOST
            INPUT_USB_HID
            STORAGE_SD
        )
        set(REQUIRE_PICO_EXTRAS TRUE) # audio_i2s
        set(STORAGE_DRIVER sd_spi)
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
    set(REQUIRE_PICO_EXTRAS TRUE) # audio_i2s
endif()

if(REQUIRE_PICO_EXTRAS)
    include(pico_extras_import.cmake)
endif()

# default drivers
if(NOT STORAGE_DRIVER)
    set(STORAGE_DRIVER "flash")
endif()

target_sources(BlitHalPico INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/storage_${STORAGE_DRIVER}.cpp
)

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

    # create metadata/binary info source at build time
    set(METADATA_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_binary_info.cpp")

    add_custom_command(
        OUTPUT ${METADATA_SOURCE}
        COMMAND cd ${CMAKE_CURRENT_SOURCE_DIR} && ${PYTHON_EXECUTABLE} -m ttblit metadata --force --config ${CMAKE_CURRENT_SOURCE_DIR}/${FILE} --pico-bi ${METADATA_SOURCE}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}
    )

    # add the generated source
    target_sources(${TARGET} PRIVATE ${METADATA_SOURCE})

endfunction()
