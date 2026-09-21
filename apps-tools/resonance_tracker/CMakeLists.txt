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
add_compile_options(-mcpu=cortex-a9 -mfpu=neon-fp16 -fPIC -Wall -Wextra)
add_compile_definitions(ARCH_ARM VERSION=${VERSION} REVISION=${REVISION})
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE cryptopp rp_sdk ${rp_libs})
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

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
