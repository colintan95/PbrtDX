function(compile_shader)
    set(one_value_args OUTPUT SOURCE VAR_NAME)
    set(multi_value_args ARGS)
    cmake_parse_arguments(SHADER "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(dxc_path ${PROJECT_SOURCE_DIR}/external/dxc/dxc.exe)

    file(TO_NATIVE_PATH "${SHADER_OUTPUT}" SHADER_OUTPUT)
    file(TO_NATIVE_PATH "${SHADER_SOURCE}" SHADER_SOURCE)

    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${dxc_path} ${SHADER_SOURCE} -Fh ${SHADER_OUTPUT} -Vn ${SHADER_VAR_NAME}
                -DHLSL ${SHADER_ARGS}
        MAIN_DEPENDENCY ${SHADER_SOURCE})
endfunction()
