cmake_minimum_required(VERSION 3.19)
project(fuda_tool LANGUAGES CXX)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets SerialBus LinguistTools)

qt_standard_project_setup()

qt_add_executable(fuda_tool
    WIN32 MACOSX_BUNDLE
    canidcodec.cpp
    canidcodec.h
    canservice.cpp
    canservice.h
    main.cpp
    mainwindow.cpp
    mainwindow.h
    payloadcodec.cpp
    payloadcodec.h
    protocol.cpp
    protocol.h
)

qt_add_translations(
    TARGETS fuda_tool
    TS_FILES fuda_tool_en_US.ts
)

target_link_libraries(fuda_tool
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::SerialBus
)

target_compile_features(fuda_tool PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(fuda_tool PRIVATE /utf-8)
endif()

install(TARGETS fuda_tool
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET fuda_tool
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
