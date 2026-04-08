cmake_minimum_required(VERSION 3.16)

project(usdSim VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets Quick Qml QuickControls2   REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
# ---- Find OpenUSD ----
find_package(pxr REQUIRED usd stage usdGeom usdLux usdUtils sdf tf vt gf ar boost python)
include_directories( "${PXR_INCLUDE_DIRS}"
                     "$ENV{CONDA_PREFIX}/include/python3.11"
                     "${CMAKE_SOURCE_DIR}/src")
if(APPLE)
    set(OPENUSD_links "$ENV{CONDA_PREFIX}/lib/libpython3.11.dylib")
else()
    set(OPENUSD_links "$ENV{CONDA_PREFIX}/lib/libpython3.11.so")
endif()

add_subdirectory(third_party/playqmlright)
add_subdirectory(third_party/prism_all)

message("${PXR_INCLUDE_DIRS}")

# ---- Shared library: usdSimLib ----
qt_add_library(usdSimLib SHARED
    src/UsdSimApp.h
    src/UsdSimApp.cpp
    src/PrimInfo.h
    src/AttrInfo.h
    src/UndoStack.h
    src/UndoStack.cpp
    src/UndoCommands.h
    src/UndoCommands.cpp
    src/UsdDocument.h
    src/UsdDocument.cpp
    src/UsdViewportItem.h
    src/UsdViewportItem.cpp
)

# Strip src/qml/ prefix so resource paths stay qrc:/usdSim/main.qml etc.
set_source_files_properties(src/qml/main.qml              PROPERTIES QT_RESOURCE_ALIAS main.qml)
set_source_files_properties(src/qml/PrimTreePanel.qml      PROPERTIES QT_RESOURCE_ALIAS PrimTreePanel.qml)
set_source_files_properties(src/qml/ViewportPanel.qml      PROPERTIES QT_RESOURCE_ALIAS ViewportPanel.qml)
set_source_files_properties(src/qml/AttributePanel.qml     PROPERTIES QT_RESOURCE_ALIAS AttributePanel.qml)
set_source_files_properties(src/qml/HistoryPanel.qml       PROPERTIES QT_RESOURCE_ALIAS HistoryPanel.qml)
set_source_files_properties(src/qml/AppStyle.qml           PROPERTIES QT_RESOURCE_ALIAS AppStyle.qml QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(src/qml/PrimContextMenu.qml    PROPERTIES QT_RESOURCE_ALIAS PrimContextMenu.qml)
set_source_files_properties(src/qml/AddAttributeDialog.qml PROPERTIES QT_RESOURCE_ALIAS AddAttributeDialog.qml)
set_source_files_properties(src/qml/EditSchemaDialog.qml   PROPERTIES QT_RESOURCE_ALIAS EditSchemaDialog.qml)
set_source_files_properties(src/icons/eye-solid.svg        PROPERTIES QT_RESOURCE_ALIAS icons/eye-solid.svg)
set_source_files_properties(src/icons/eye-slash-solid.svg  PROPERTIES QT_RESOURCE_ALIAS icons/eye-slash-solid.svg)

qt_add_qml_module(usdSimLib
    URI usdSim
    VERSION 1.0
    QML_FILES src/qml/main.qml src/qml/PrimTreePanel.qml src/qml/ViewportPanel.qml src/qml/AttributePanel.qml src/qml/HistoryPanel.qml
              src/qml/AppStyle.qml src/qml/PrimContextMenu.qml src/qml/AddAttributeDialog.qml src/qml/EditSchemaDialog.qml
    RESOURCES src/icons/eye-solid.svg src/icons/eye-slash-solid.svg
)

# Strip src/ prefix so shaders stay at :/shaders/viewport.*.qsb
qt_add_shaders(usdSimLib "viewport_shaders"
    PREFIX "/"
    FILES
        src/shaders/viewport.vert
        src/shaders/viewport.frag
    OUTPUTS
        shaders/viewport.vert.qsb
        shaders/viewport.frag.qsb
)

qt_add_shaders(usdSimLib "outline_shaders"
    PREFIX "/"
    FILES
        src/shaders/outline_post.vert
        src/shaders/outline_post.frag
    OUTPUTS
        shaders/outline_post.vert.qsb
        shaders/outline_post.frag.qsb
)

qt_add_shaders(usdSimLib "shadow_shaders"
    PREFIX "/"
    FILES
        src/shaders/shadow.vert
        src/shaders/shadow.frag
    OUTPUTS
        shaders/shadow.vert.qsb
        shaders/shadow.frag.qsb
)

set_target_properties(usdSimLib PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    INSTALL_RPATH "${USD_INSTALL_DIR}/lib"
    BUILD_RPATH   "${USD_INSTALL_DIR}/lib"
)

target_link_libraries(usdSimLib PUBLIC
    qmlinspector
    prism::prism
    prism::qt::core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
    ${PXR_usd_LIBRARY}
    ${PXR_stage_LIBRARY}
    ${PXR_usdGeom_LIBRARY}
    ${PXR_usdLux_LIBRARY}
    ${PXR_usdUtils_LIBRARY}
    ${PXR_sdf_LIBRARY}
    ${PXR_tf_LIBRARY}
    ${PXR_vt_LIBRARY}
    ${PXR_gf_LIBRARY}
    ${PXR_ar_LIBRARY}
    ${PXR_boost_LIBRARY}
    ${PXR_python_LIBRARY}
)

# macOS: python3 executable embeds the runtime; explicitly linking libpython
# here would create a second _PyRuntime → pybind11 crash on import.
# Use -undefined dynamic_lookup so Python symbols resolve from whoever
# provides the runtime at load time (the running interpreter OR usdSim itself).
# On Linux, libpython is a shared lib in the process and safe to link directly.
if(APPLE)
    target_link_options(usdSimLib PUBLIC -undefined dynamic_lookup)
else()
    target_link_libraries(usdSimLib PUBLIC ${OPENUSD_links})
endif()

# ---- Executable: usdSim ----
qt_add_executable(usdSim src/main.cpp)

set_target_properties(usdSim PROPERTIES
    OUTPUT_NAME usdSim
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    INSTALL_RPATH "${USD_INSTALL_DIR}/lib"
    BUILD_RPATH   "${USD_INSTALL_DIR}/lib"
)

target_link_libraries(usdSim PRIVATE usdSimLib ${OPENUSD_links})
# Qt adds -dead_strip_dylibs which drops libpython because main.cpp has no
# direct Python symbol refs. -needed_library forces the dylib to be kept so
# that usdSimLib's -undefined dynamic_lookup can resolve Python symbols when
# the app runs standalone (no Python host process to supply them).
if(APPLE AND OPENUSD_links)
    target_link_options(usdSim PRIVATE "LINKER:-needed_library,${OPENUSD_links}")
endif()

# ---- pybind11 module: pyusdSim ----
find_package(pybind11 QUIET)
if(pybind11_FOUND)
    pybind11_add_module(pyusdSim src/bindings.cpp)
    target_link_libraries(pyusdSim PRIVATE usdSimLib)
    set_target_properties(pyusdSim PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )
    message(STATUS "pybind11 found — pyusdSim module will be built")
else()
    message(STATUS "pybind11 not found — skipping pyusdSim module")
endif()

include(GNUInstallDirs)
install(TARGETS usdSimLib usdSim
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
