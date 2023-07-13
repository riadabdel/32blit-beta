set(BLIT_BOARD_NAME "Pico DV")

set(BLIT_BOARD_DEFINITIONS
    PICO_AUDIO_I2S_PIO=1
    PICO_AUDIO_I2S_DATA_PIN=26
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=27

    DVI_DEFAULT_SERIAL_CONFIG=pimoroni_demo_hdmi_cfg
    DVI_16BPP_RED_MSB=4
    DVI_16BPP_RED_LSB=0
    DVI_16BPP_BLUE_MSB=15
    DVI_16BPP_BLUE_LSB=11
)

blit_driver(audio i2s)
blit_driver(display picodvi)
blit_driver(input usb_hid)
blit_driver(storage sd_spi)
blit_driver(usb host)
