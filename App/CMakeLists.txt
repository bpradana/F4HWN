

add_library(App INTERFACE)
target_include_directories(App INTERFACE . usb)
target_link_libraries(App INTERFACE PY32F071_Driver CherryUSB)
target_compile_definitions(App INTERFACE PRINTF_INCLUDE_CONFIG_H)

# Version and edition strings
target_compile_definitions(App INTERFACE 
    "AUTHOR_STRING=\"${AUTHOR_STRING}\""
    "AUTHOR_STRING_1=\"${AUTHOR_STRING_1}\""
    "AUTHOR_STRING_2=\"${AUTHOR_STRING_2}\""
    "VERSION_STRING=\"${VERSION_STRING}\""
    "VERSION_STRING_1=\"${VERSION_STRING_1}\""
    "VERSION_STRING_2=\"${VERSION_STRING_2}\""
    "EDITION_STRING=\"${EDITION_STRING}\""
    ALERT_TOT=10
    SQL_TONE=550
)

# All source files for Fusion build (no conditional compilation)
target_sources(App INTERFACE
    # Drivers
    driver/backlight.c
    driver/bk4829.c
    driver/bk1080.c
    driver/crc.c
    driver/eeprom_compat.c
    driver/gpio.c
    driver/i2c.c
    driver/keyboard.c
    driver/py25q16.c
    driver/st7565.c
    driver/system.c
    driver/systick.c
    driver/uart.c
    driver/vcp.c
    
    # Application
    app/action.c
    app/aircopy.c
    app/app.c
    app/breakout.c
    app/chFrScanner.c
    app/common.c
    app/dtmf.c
    app/flashlight.c
    app/fm.c
    app/generic.c
    app/main.c
    app/menu.c
    app/scanner.c
    app/spectrum.c
    app/uart.c
    
    # Core
    audio.c
    bitmaps.c
    board.c
    dcs.c
    font.c
    frequencies.c
    functions.c
    main.c
    misc.c
    radio.c
    scheduler.c
    screenshot.c
    settings.c
    version.c
    
    # Helpers
    helper/battery.c
    helper/boot.c
    
    # UI
    ui/aircopy.c
    ui/battery.c
    ui/fmradio.c
    ui/helper.c
    ui/inputbox.c
    ui/main.c
    ui/menu.c
    ui/scanner.c
    ui/status.c
    ui/ui.c
    ui/welcome.c
    
    # USB
    usb/usbd_cdc_if.c
    
    # External
    external/printf/printf.c
)
