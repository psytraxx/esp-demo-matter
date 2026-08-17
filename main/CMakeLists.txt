idf_component_register(
    SRCS
        "app_main.cpp"
        "matter_setup.cpp"
        "status_led.cpp"
        "button.cpp"
        "ld2410_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver
)

# The plater/esp32-ld2410 component's ld2410.h declares a plain C function
# named va_copy(), which collides with the compiler builtin of the same name
# once included from a .cpp translation unit. -fpermissive downgrades that
# clash from an error to a warning; scoped to this one file rather than the
# whole project so it doesn't mask unrelated strictness elsewhere.
set_source_files_properties(
    "ld2410_bridge.cpp"
    PROPERTIES COMPILE_OPTIONS "-fpermissive"
)
