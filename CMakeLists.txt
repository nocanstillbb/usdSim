cmake_minimum_required(VERSION 3.16)

project(usdSim VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Widgets Quick Qml QuickControls2   REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
# ---- Find OpenUSD ----
find_package(pxr REQUIRED usd stage usdGeom usdLux sdf tf vt gf ar boost python)
include_directories( "${PXR_INCLUDE_DIRS}"
                     "$ENV{CONDA_PREFIX}/include/python3.11")
set(OPENUSD_links    $ENV{CONDA_PREFIX}/lib/libpython3.11.so)

add_subdirectory(third_party/playqmlright)
add_subdirectory(third_party/prism_all)

message("111111111")
message("${PXR_INCLUDE_DIRS}")
message("22222222")

qt_add_executable(usdSim
    main.cpp
    PrimInfo.h
    AttrInfo.h
    UndoStack.h
    UndoStack.cpp
    UndoCommands.h
    UndoCommands.cpp
    UsdDocument.h
    UsdDocument.cpp
    UsdViewportItem.h
    UsdViewportItem.cpp
)


set_source_files_properties(AppStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(usdSim
    URI usdSim
    VERSION 1.0
    QML_FILES main.qml PrimTreePanel.qml ViewportPanel.qml AttributePanel.qml HistoryPanel.qml
              AppStyle.qml PrimContextMenu.qml AddAttributeDialog.qml EditSchemaDialog.qml
    RESOURCES icons/eye-solid.svg icons/eye-slash-solid.svg
)

qt_add_shaders(usdSim "viewport_shaders"
    PREFIX "/"
    FILES
        shaders/viewport.vert
        shaders/viewport.frag
)

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


target_link_libraries(usdSim PRIVATE
    qmlinspector
    prism::prism
    prism::qt::core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
    Qt6::GuiPrivate
    ${PXR_usd_LIBRARY}
    ${PXR_stage_LIBRARY}
    ${PXR_usdGeom_LIBRARY}
    ${PXR_usdLux_LIBRARY}
    ${PXR_sdf_LIBRARY}
    ${PXR_tf_LIBRARY}
    ${PXR_vt_LIBRARY}
    ${PXR_gf_LIBRARY}
    ${PXR_ar_LIBRARY}
    ${PXR_boost_LIBRARY}
    ${PXR_python_LIBRARY}
    ${OPENUSD_links}
)

include(GNUInstallDirs)
install(TARGETS usdSim
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
