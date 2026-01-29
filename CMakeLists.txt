cmake_minimum_required(VERSION 3.18)

project(
    fooyin-converter
    VERSION 0.1.0
    DESCRIPTION "Audio Converter Plugin for Fooyin"
    HOMEPAGE_URL "https://github.com/jalabulajunx/fooyin_converter"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find dependencies
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
find_package(Fooyin REQUIRED)

# Source files
set(SOURCES
    src/converterplugin.cpp
    src/converterplugin.h
    src/converterwidget.cpp
    src/converterwidget.h
    src/conversionmanager.cpp
    src/conversionmanager.h
    src/convertersettings.h
    src/convertersettingspage.cpp
    src/convertersettingspage.h
    src/codecwrapper.cpp
    src/codecwrapper.h
    src/flacwrapper.cpp
    src/flacwrapper.h
    src/lamewrapper.cpp
    src/lamewrapper.h
    src/opuswrapper.cpp
    src/opuswrapper.h
    src/oggwrapper.cpp
    src/oggwrapper.h
)

# Create plugin using Fooyin's helper function
create_fooyin_plugin(
    fooyin-converter
    DEPENDS Fooyin::Core Fooyin::Gui Fooyin::Utils Qt6::Widgets
    SOURCES ${SOURCES}
)

# Set custom output name
set_target_properties(fooyin-converter PROPERTIES OUTPUT_NAME "fooyin_converterplugin")

# Copy metadata
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json"
    "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    COPYONLY
)

# Install metadata alongside the plugin
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/metadata.json"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/fooyin/plugins"
)
