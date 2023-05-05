add_executable(PbrtDX WIN32
    App.cpp
    App.h
    main.cpp)

target_compile_definitions(PbrtDX PRIVATE UNICODE NOMINMAX)

target_compile_options(PbrtDX PRIVATE /W4 /WX /await)

target_include_directories(PbrtDX PRIVATE ${PROJECT_SOURCE_DIR}/external/d3dx12)

target_link_libraries(PbrtDX PRIVATE d3d12.lib dxgi.lib OneCore.lib)
