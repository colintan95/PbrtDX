add_executable(PbrtDX WIN32
    App.cpp
    App.h
    gen/Shader.h
    ImageLoader.cpp
    ImageLoader.h
    main.cpp
    Mesh.cpp
    Mesh.h)

set(COMMON_SHADER_FLAGS -Zi -Od -Qembed_debug)

compile_shader(
    OUTPUT gen/Shader.h
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Shader.hlsl
    VAR_NAME g_shader
    ARGS ${COMMON_SHADER_FLAGS} -T lib_6_3)

# For including headers generated in the build directory.
target_include_directories(PbrtDX PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    TARGET PbrtDX POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${PBRT_SCENES_DIR} $<TARGET_FILE_DIR:PbrtDX>/scenes)

target_compile_definitions(PbrtDX PRIVATE UNICODE NOMINMAX)
target_compile_options(PbrtDX PRIVATE /W4 /WX /await)

target_link_libraries(PbrtDX PRIVATE glm)
target_link_libraries(PbrtDX PRIVATE RPly)

target_include_directories(PbrtDX PRIVATE ${PROJECT_SOURCE_DIR}/external/d3dx12)

target_link_libraries(PbrtDX PRIVATE d3d12.lib dxgi.lib OneCore.lib)
