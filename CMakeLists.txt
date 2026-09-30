cmake_minimum_required(VERSION 3.21)
project(amnezia_vpn_tunnel_update VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Widgets)

add_library(amnezia_tunnel_update_core
    src/core.cpp
    src/core.h
    src/installer.cpp
    src/installer.h
)
target_link_libraries(amnezia_tunnel_update_core PUBLIC Qt6::Core Qt6::Network)
target_include_directories(amnezia_tunnel_update_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(amnezia-vpn-tunnel-update WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(amnezia-vpn-tunnel-update PRIVATE amnezia_tunnel_update_core Qt6::Widgets)
set_target_properties(amnezia-vpn-tunnel-update PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Amnezia VPN Tunnel Update"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.tim8es.amnezia-vpn-tunnel-update"
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_executable(amnezia-vpn-tunnel-update-tests tests/test_core.cpp)
    target_link_libraries(amnezia-vpn-tunnel-update-tests PRIVATE amnezia_tunnel_update_core Qt6::Test)
    add_test(NAME amnezia-vpn-tunnel-update-tests COMMAND amnezia-vpn-tunnel-update-tests)
endif()
