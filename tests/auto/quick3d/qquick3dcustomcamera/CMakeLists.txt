# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Generated from qquick3dcustomcamera.pro.

#####################################################################
## qquick3dcustomcamera Test:
#####################################################################

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    cmake_minimum_required(VERSION 3.16)
    project(tst_qquick3dcustomcamera LANGUAGES CXX)
    find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
endif()

qt_internal_add_test(tst_qquick3dcustomcamera
    SOURCES
        tst_qquick3dcustomcamera.cpp
    LIBRARIES
        Qt::Quick3DPrivate
        Qt::Quick3DRuntimeRenderPrivate
)

#### Keys ignored in scope 1:.:.:qquick3dcustomcamera.pro:<TRUE>:
# TEMPLATE = "app"
