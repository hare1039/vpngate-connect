cmake_minimum_required(VERSION 3.5.1)
#avoid cmake adds -Wl,-search_paths_first that makes mac compiled binary segmentation fault
set(HAVE_FLAG_SEARCH_PATHS_FIRST 0)

project(vpngate-connect)

set(CMAKE_CXX_STANDARD 17)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

conan_basic_setup()

add_executable(vpngate-connect vpngate-connect.cpp)
target_link_libraries(vpngate-connect ${CONAN_LIBS})

IF ("${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
   target_link_libraries(vpngate-connect ws2_32 wsock32)
ENDIF ()
