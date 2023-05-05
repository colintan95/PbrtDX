add_executable(PbrtDX WIN32 main.cpp)

target_compile_definitions(PbrtDX PRIVATE UNICODE NOMINMAX)

target_compile_options(PbrtDX PRIVATE /W4 /WX /await)
