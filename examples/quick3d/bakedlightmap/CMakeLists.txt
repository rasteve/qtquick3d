# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(bakedlightmap LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Quick3D)

qt_add_executable(bakedlightmap
    main.cpp
)

# Do not set WIN32_EXECUTABLE, we want a console
set_target_properties(bakedlightmap PROPERTIES
    MACOSX_BUNDLE TRUE
)

target_link_libraries(bakedlightmap PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Quick
    Qt::Quick3D
)

qt_add_qml_module(bakedlightmap
    URI Example
    VERSION 1.0
    QML_FILES main.qml Box.qml
    RESOURCES box.mesh
    NO_RESOURCE_TARGET_PATH
)

install(TARGETS bakedlightmap
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET bakedlightmap
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})
