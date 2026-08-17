cmake_minimum_required(VERSION 3.16.0)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

# Patch esp_matter so the application can reach the registered OccupancySensing
# cluster instance. Without this the occupancy endpoint silently never reports:
# attribute::update() returns ESP_OK but reads are served from the cluster
# object, which it does not write. managed_components/ is gitignored and
# refetched, so the patch has to be reapplied on every configure rather than
# committed. See README.md for the full explanation and how to retire it.
#
# Idempotent: skipped when the accessor is already present, so repeat configures
# and incremental builds are safe.
set(ESP_MATTER_DIR "${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_matter")
set(OCCUPANCY_INTEGRATION_CPP
    "${ESP_MATTER_DIR}/components/esp_matter/data_model_provider/clusters/occupancy_sensing/integration.cpp")
if(EXISTS "${OCCUPANCY_INTEGRATION_CPP}")
    file(READ "${OCCUPANCY_INTEGRATION_CPP}" _occ_src)
    string(FIND "${_occ_src}" "esp_matter_get_occupancy_cluster" _occ_patched)
    if(_occ_patched EQUAL -1)
        message(STATUS "Applying esp_matter occupancy accessor patch")
        execute_process(
            COMMAND patch -p1 -i "${CMAKE_CURRENT_LIST_DIR}/patches/esp_matter_occupancy_accessor.patch"
            WORKING_DIRECTORY "${ESP_MATTER_DIR}"
            RESULT_VARIABLE _occ_patch_result
            OUTPUT_VARIABLE _occ_patch_output
            ERROR_VARIABLE  _occ_patch_output)
        if(NOT _occ_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply patches/esp_matter_occupancy_accessor.patch:\n${_occ_patch_output}\n"
                "Occupancy reporting will not work without it — see README.md.")
        endif()
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_demo_matter)
