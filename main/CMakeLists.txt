idf_component_register(
    SRCS
        "app_main.cpp"
        "matter_setup.cpp"
        "status_led.cpp"
        "button.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver
)
