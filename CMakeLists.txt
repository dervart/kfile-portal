cmake_minimum_required(VERSION 3.20)

project(xdg-desktop-portal-kfile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(KF6KIO REQUIRED)

add_executable(kfile-helper
    kfile-helper.cpp
)

target_include_directories(kfile-helper PRIVATE
    /usr/include/KF6/KIOFileWidgets
    /usr/include/KF6/KIOWidgets
)

target_link_libraries(kfile-helper PRIVATE
    Qt6::Widgets
    KF6::KIOFileWidgets
)
