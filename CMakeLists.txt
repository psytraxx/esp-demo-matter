cmake_minimum_required(VERSION 3.16.0)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

# Firmware version. Bump BOTH on every release you want to ship over OTA:
# PROJECT_VER is the human-readable string (Basic Information
# SoftwareVersionString), PROJECT_VER_NUMBER is the integer the OTA provider
# compares — a device only accepts an image whose number is higher than the
# one it is running.
set(PROJECT_VER "1.0.1")
set(PROJECT_VER_NUMBER 2)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_demo_matter)
