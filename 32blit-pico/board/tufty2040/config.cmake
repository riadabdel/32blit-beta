set(BLIT_BOARD_NAME "Tufty 2040")

set(BLIT_BOARD_DEFINITIONS
    PICO_FLASH_SIZE_BYTES=8388608

    DISPLAY_WIDTH=320
    ST7789_8BIT
    ST7789_ROTATE_180
    LCD_CS_PIN=10
    LCD_DC_PIN=11
    LCD_SCK_PIN=12 # WR
    LCD_RD_PIN=13
    LCD_MOSI_PIN=14 # DB0
    LCD_BACKLIGHT_PIN=2
    # LCD_VSYNC_PIN=11 # shared

    # there is a white LED
)

blit_driver(display st7789)
blit_driver(input tufty)
