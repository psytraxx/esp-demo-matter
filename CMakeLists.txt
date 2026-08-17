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
        # Appended rather than applied with patch(1): the addition goes at
        # end-of-file, where a unified diff's context is sensitive to trailing
        # whitespace and line-count drift between component versions.
        file(READ "${CMAKE_CURRENT_LIST_DIR}/patches/esp_matter_occupancy_accessor.cpp.in" _occ_addition)
        file(APPEND "${OCCUPANCY_INTEGRATION_CPP}" "${_occ_addition}")

        file(READ "${OCCUPANCY_INTEGRATION_CPP}" _occ_verify)
        string(FIND "${_occ_verify}" "esp_matter_get_occupancy_cluster" _occ_ok)
        if(_occ_ok EQUAL -1)
            message(FATAL_ERROR
                "Failed to append the esp_matter occupancy accessor to\n"
                "  ${OCCUPANCY_INTEGRATION_CPP}\n"
                "Occupancy reporting will not work without it — see README.md.")
        endif()
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_demo_matter)
