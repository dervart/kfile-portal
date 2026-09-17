cmake_minimum_required(VERSION 3.20)

project(xdg-desktop-portal-kfile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
find_package(KF6KIO REQUIRED)

# KF6KIO exports KF6::KIOWidgets / KF6::KIOFileWidgets targets directly;
# this distribution does not ship separate KF6KIOWidgets /
# KF6KIOFileWidgets package config files, so they must not be
# find_package()-d individually.

include(GNUInstallDirs)

add_executable(kfile-helper
    kfile-helper.cpp
)

# No hardcoded include paths: KF6::KIOFileWidgets / KF6::KIOWidgets already
# carry their public include directories as usage requirements.
target_link_libraries(kfile-helper PRIVATE
    Qt6::Widgets
    KF6::KIOFileWidgets
    KF6::KIOWidgets
)

install(TARGETS kfile-helper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
