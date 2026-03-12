cmake_minimum_required(VERSION 3.16)
project(HyperV-Manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# ElaWidgetTools
set(ELAWIDGETTOOLS_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
add_subdirectory(3rdparty/ElaWidgetTools)

set(PROJECT_SOURCES
    main.cpp
    MainWindow.h MainWindow.cpp
    BasePage.h BasePage.cpp
    HomePage.h HomePage.cpp
    VirtualMachinePage.h VirtualMachinePage.cpp
    CreateVMPage.h CreateVMPage.cpp
    GpuSettingsPage.h GpuSettingsPage.cpp
    NetworkPage.h NetworkPage.cpp
    DDAPage.h DDAPage.cpp
    USBPage.h USBPage.cpp
    UsbTunnelManager.h UsbTunnelManager.cpp
    SettingPage.h SettingPage.cpp
    HyperVManager.h HyperVManager.cpp
    VMSettingsDialog.h VMSettingsDialog.cpp
)

qt_add_executable(${PROJECT_NAME}
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets
    ElaWidgetTools
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# UAC 管理员权限
target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")

qt_finalize_executable(${PROJECT_NAME})
