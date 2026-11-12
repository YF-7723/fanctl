cmake_minimum_required(VERSION 3.20)
project(FanCtl VERSION 1.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# AGL.framework is gone since macOS 10.14, but Qt's FindWrapOpenGL links it
# unconditionally.  Pre-defining the target makes the module bail out early.
if(APPLE AND NOT TARGET WrapOpenGL::WrapOpenGL)
  add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
  target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "-framework OpenGL")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Quick QuickControls2)
qt_standard_project_setup()

# ---- core: SMC + IPC, no Qt dependency (keeps the root daemon tiny) ----
add_library(fanctl_core STATIC
  src/core/smc.cpp
  src/core/ipc.cpp
)
target_include_directories(fanctl_core PUBLIC src/core)
target_link_libraries(fanctl_core PUBLIC
  "-framework IOKit"
  "-framework CoreFoundation"
)

# ---- ui backend: QML-facing object ----
add_library(fanctl_ui STATIC src/core/fancontroller.cpp)
target_link_libraries(fanctl_ui PUBLIC fanctl_core Qt6::Core)

# ---- fanctld: root daemon ----
add_executable(fanctld src/daemon/daemon.cpp)
target_link_libraries(fanctld PRIVATE fanctl_core)

# ---- fanctl: CLI ----
# NB: the target must not be called "fanctl": on a case-insensitive filesystem
# it would collide with FanCtl.dir from the bundle target.
add_executable(fanctl_cli src/cli/cli.cpp)
set_target_properties(fanctl_cli PROPERTIES OUTPUT_NAME fanctl)
target_link_libraries(fanctl_cli PRIVATE fanctl_core)

# ---- FanCtl.app: QML front-end ----
add_executable(FanCtl MACOSX_BUNDLE
  src/app/main.cpp
  src/app/native.cpp
  src/app/native_mac.mm
)
target_link_libraries(FanCtl PRIVATE fanctl_ui Qt6::Quick Qt6::QuickControls2 "-framework Cocoa")

qt_add_resources(FanCtl "qml"
  PREFIX "/qml"
  BASE qml
  FILES
    qml/Main.qml
    qml/Theme.qml
    qml/Card.qml
    qml/ModeButton.qml
    qml/IconButton.qml
    qml/FanSlider.qml
    qml/AutoPanel.qml
    qml/ManualPanel.qml
    qml/CurvePanel.qml
    qml/CurveChart.qml
    qml/TempSidebar.qml
    qml/Stepper.qml
    qml/DarkComboBox.qml
)

set_target_properties(FanCtl PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "FanCtl"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.fanctl.app"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
)

# ship the daemon inside the app bundle so the app can install itself
add_custom_command(TARGET FanCtl POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_CONTENT_DIR:FanCtl>/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy
          $<TARGET_FILE:fanctld>
          "$<TARGET_BUNDLE_CONTENT_DIR:FanCtl>/Resources/fanctld"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/packaging/com.fanctl.daemon.plist"
          "$<TARGET_BUNDLE_CONTENT_DIR:FanCtl>/Resources/"
  COMMAND ${CMAKE_COMMAND} -E copy
          "${CMAKE_CURRENT_SOURCE_DIR}/assets/FanCtl.icns"
          "$<TARGET_BUNDLE_CONTENT_DIR:FanCtl>/Resources/FanCtl.icns"
)

install(TARGETS fanctld fanctl_cli RUNTIME DESTINATION libexec)
install(TARGETS FanCtl BUNDLE DESTINATION .)
