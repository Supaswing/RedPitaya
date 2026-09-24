cmake_minimum_required(VERSION 3.14)

set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_VERBOSE_MAKEFILE OFF)

set(APP resonance_tracker)
set(BUILD_PATH ${CMAKE_BINARY_DIR}/${APP})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${BUILD_PATH})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BUILD_PATH})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BUILD_PATH})

# The Bazaar loader in this Red Pitaya tree loads <app_id>/controllerhf.so.
project(controllerhf)

if(NOT DEFINED INSTALL_DIR)
    message(FATAL_ERROR "Installation path not set.")
endif()
if(NOT DEFINED VERSION)
    set(VERSION dev)
endif()
if(NOT DEFINED REVISION)
    set(REVISION devbuild)
endif()

set(ZIP_FILE ../../../${APP}-${VERSION}-${REVISION}.zip)
list(APPEND r_paths /opt/redpitaya/lib /opt/redpitaya/lib/web)
list(APPEND include_paths
    ${INSTALL_DIR}/include
    ${INSTALL_DIR}/include/api2
    ${INSTALL_DIR}/include/apiApp
    ${INSTALL_DIR}/include/redpitaya
    ${INSTALL_DIR}/include/common
    ${INSTALL_DIR}/rp_sdk
    ${INSTALL_DIR}/rp_sdk/libjson
)
list(APPEND libs_paths ${INSTALL_DIR}/lib ${INSTALL_DIR}/lib/web ${INSTALL_DIR}/rp_sdk)
list(APPEND rp_libs rp-system rp-client rpapp rp rp-hw-calib rp-hw pthread)

set(CMAKE_INSTALL_RPATH ${r_paths})
set(CMAKE_BUILD_RPATH ${r_paths})
include_directories(${include_paths})
link_directories(${libs_paths})

file(GLOB SOURCES "src/*.cpp")
add_compile_options(-mcpu=cortex-a9 -mfpu=neon-fp16 -fPIC -Wall -Wextra -Wno-psabi)
add_compile_definitions(ARCH_ARM VERSION=${VERSION} REVISION=${REVISION})
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE rp_sdk cryptopp ${rp_libs})
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--no-undefined)
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

option(RESONANCE_TRACKER_BUILD_TESTS "Build hardware-independent resonance tracker tests" OFF)
if(RESONANCE_TRACKER_BUILD_TESTS)
    enable_testing()
    add_executable(iq_statistics_test tests/iq_statistics_test.cpp src/iq_statistics.cpp)
    target_include_directories(iq_statistics_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME iq_statistics_test COMMAND iq_statistics_test)
    add_executable(instrument_state_test tests/instrument_state_test.cpp src/instrument_state.cpp)
    target_include_directories(instrument_state_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME instrument_state_test COMMAND instrument_state_test)
    add_executable(resonance_analysis_test tests/resonance_analysis_test.cpp src/resonance_analysis.cpp)
    target_include_directories(resonance_analysis_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME resonance_analysis_test COMMAND resonance_analysis_test)
    add_executable(tracker_engine_test tests/tracker_engine_test.cpp src/tracker_engine.cpp src/tracking_quality.cpp src/resonance_analysis.cpp)
    target_include_directories(tracker_engine_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME tracker_engine_test COMMAND tracker_engine_test)
endif()

option(RESONANCE_TRACKER_BUILD_HARDWARE_TESTS "Build target-only raw-IQ hardware tests" OFF)
if(RESONANCE_TRACKER_BUILD_HARDWARE_TESTS)
    add_executable(raw_iq_hardware_test tests/raw_iq_hardware_test.cpp src/raw_iq_acquisition.cpp)
    target_include_directories(raw_iq_hardware_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_executable(raw_iq_noise_test tests/raw_iq_noise_test.cpp src/raw_iq_acquisition.cpp)
    target_include_directories(raw_iq_noise_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/css DESTINATION ${BUILD_PATH})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/info DESTINATION ${BUILD_PATH})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/js DESTINATION ${BUILD_PATH})
file(GLOB HTML "*.html")
install(FILES ${HTML} DESTINATION ${BUILD_PATH})
install(FILES fpga.conf DESTINATION ${BUILD_PATH})
install(PROGRAMS fpga.sh DESTINATION ${BUILD_PATH})
install(CODE "execute_process(COMMAND sed -i ${BUILD_PATH}/info/info.json -e \"s/REVISION/${REVISION}/\")")
install(CODE "execute_process(COMMAND sed -i ${BUILD_PATH}/info/info.json -e \"s/VERSION/${VERSION}/\")")
install(CODE "file(ARCHIVE_CREATE OUTPUT ${ZIP_FILE} FORMAT zip PATHS ${BUILD_PATH})")
install(CODE "file(ARCHIVE_EXTRACT INPUT ${ZIP_FILE} DESTINATION ${INSTALL_DIR}/www/apps)")
