# SPDX-License-Identifier: MIT
# Copyright (c) 2026 José González Divassón
#
# VoyagerCalc for Linux: Qt 6 front end around the C emulator core in core/.
# Build:  cmake -S . -B build -G Ninja [-DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64] && cmake --build build
cmake_minimum_required(VERSION 3.21)
project(VoyagerCalc VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Test)

# --- the emulator core (plain C11) --------------------------------------
set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core")
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CORE_DIR}/src/*.c")
add_library(voyagercore STATIC ${CORE_SOURCES})
target_include_directories(voyagercore PUBLIC "${CORE_DIR}/include")
if(NOT MSVC)
  target_link_libraries(voyagercore PUBLIC m)
  target_compile_options(voyagercore PRIVATE -O2)
endif()

# --- the logic: engine, machine, inspector, analysis, documents, PDF, USB link (QtCore + QtGui, no widgets) --------
set(LOGIC_SOURCES
  src/Emulator.cpp src/Machine.cpp src/Inspector.cpp src/Model.cpp src/ProgramAnalysis.cpp src/ProgramDocument.cpp
  src/Stores.cpp src/EngineWorker.cpp src/KeyGlyphs.cpp src/PdfExport.cpp
  src/usb/CalculatorLink.cpp src/usb/UsbPermissions.cpp src/usb/HidrawTransport.cpp src/usb/UsbLog.cpp src/usb/StepGuard.cpp src/usb/UsbSelfTest.cpp)
add_library(voyagerlogic STATIC ${LOGIC_SOURCES} resources/voyagercalc.qrc)
target_include_directories(voyagerlogic PUBLIC src)
target_link_libraries(voyagerlogic PUBLIC voyagercore Qt6::Core Qt6::Gui)

# --- the widgets -----------------------------------------------------------------------------------------------
set(UI_SOURCES "")
foreach(f LcdPainter CalculatorWidget InspectorPanel ProgramInfoDialog WelcomeWidget MainWindow)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${f}.cpp")
    list(APPEND UI_SOURCES src/${f}.cpp)
  endif()
endforeach()
add_library(voyagerui STATIC ${UI_SOURCES})
target_compile_definitions(voyagerlogic PUBLIC VOYAGERCALC_VERSION="${PROJECT_VERSION}")
target_link_libraries(voyagerui PUBLIC voyagerlogic Qt6::Widgets)

add_executable(voyagercalc src/main.cpp)
target_link_libraries(voyagercalc PRIVATE voyagerui)
# Release packaging: the Qt libraries sit next to the program (<app>/bin/voyagercalc, <app>/lib), found without LD_LIBRARY_PATH.
option(VC_PORTABLE_RPATH "link voyagercalc with RUNPATH $ORIGIN/../lib (for the tarball)" OFF)
if(VC_PORTABLE_RPATH)
  set_target_properties(voyagercalc PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/../lib")
endif()

# --- tests -----------------------------------------------------------------------------------------------------
enable_testing()
function(vc_test name)
  add_executable(tst_${name} tests/tst_${name}.cpp)
  target_link_libraries(tst_${name} PRIVATE voyagerui Qt6::Test)
  target_compile_definitions(tst_${name} PRIVATE VC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${name} COMMAND tst_${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
foreach(t facemodel emulator widget models machine inspector analysis document link usb pdf app)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/tst_${t}.cpp")
    vc_test(${t})
  endif()
endforeach()
