idf_component_register(
    SRCS
        "app_main.cpp"
        "matter_setup.cpp"
        "status_led.cpp"
        "button.cpp"
        "ld2410_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio esp_driver_uart esp_driver_rmt
)
