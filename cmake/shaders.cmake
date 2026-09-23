# GLSL -> every format SDL_GPU can load, at BUILD time (ADR-0009).
#
# One source per stage, written once in GLSL 4.50 with SDL_GPU's binding
# conventions:
#
#   vertex    uniform buffers  set = 1        samplers  set = 0
#   fragment  uniform buffers  set = 3        samplers  set = 2
#
# and compiled into
#
#   SPIR-V    glslang           Vulkan (Linux)
#   MSL       SPIRV-Cross       Metal  (macOS)
#   HLSL      SPIRV-Cross       Direct3D 12 (Windows; compiled with fxc there)
#
# The binaries are embedded in the executable: there is no shader file to lose
# next to it, and no runtime compiler to ship.  `--msl-decoration-binding` makes
# SPIRV-Cross use the GLSL binding number as the Metal index, which is exactly
# the slot SDL_GPU binds -- without it the index is "the order of declaration",
# and reordering two uniforms in a shader would silently swap two textures.

set(SPACEFLIGHT_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/embed_shaders.cmake")

function(spaceflight_compile_shaders)
    cmake_parse_arguments(ARG "" "OUTPUT;INCLUDE_DIR" "SOURCES;DEPENDS" ${ARGN})

    set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${_out_dir}")
    set(_manifest "")
    set(_products "")

    foreach(_source IN LISTS ARG_SOURCES)
        get_filename_component(_abs "${_source}" ABSOLUTE)
        get_filename_component(_name "${_source}" NAME)      # e.g. star.vert
        string(REPLACE "." "_" _symbol "${_name}")            # star_vert
        get_filename_component(_ext "${_source}" LAST_EXT)
        if(_ext STREQUAL ".vert")
            set(_stage vert)
        elseif(_ext STREQUAL ".frag")
            set(_stage frag)
        else()
            message(FATAL_ERROR "unknown shader stage for ${_source}")
        endif()

        set(_spv "${_out_dir}/${_name}.spv")
        set(_msl "${_out_dir}/${_name}.msl")

        add_custom_command(
            OUTPUT "${_spv}"
            COMMAND $<TARGET_FILE:glslang-standalone> -V --target-env vulkan1.0
                    -S ${_stage} -I${ARG_INCLUDE_DIR} -o "${_spv}" "${_abs}"
            DEPENDS "${_abs}" ${ARG_DEPENDS} glslang-standalone
            COMMENT "glslang ${_name}"
            VERBATIM)

        add_custom_command(
            OUTPUT "${_msl}"
            COMMAND $<TARGET_FILE:spirv-cross> "${_spv}" --msl --msl-version 20100
                    --msl-decoration-binding --output "${_msl}"
            DEPENDS "${_spv}" spirv-cross
            COMMENT "spirv-cross ${_name} -> MSL"
            VERBATIM)

        set(_entry "${_symbol}|${_stage}|${_spv}|${_msl}")
        list(APPEND _products "${_spv}" "${_msl}")

        # Direct3D 12: HLSL shader model 5.1 from SPIRV-Cross, then DXBC from fxc,
        # which ships with every Windows SDK.  Planned, not yet exercised: the
        # build only knows how to produce it on a Windows host.
        if(WIN32)
            set(_hlsl "${_out_dir}/${_name}.hlsl")
            set(_dxbc "${_out_dir}/${_name}.dxbc")
            if(_stage STREQUAL "vert")
                set(_profile vs_5_1)
            else()
                set(_profile ps_5_1)
            endif()
            add_custom_command(
                OUTPUT "${_hlsl}"
                COMMAND $<TARGET_FILE:spirv-cross> "${_spv}" --hlsl --shader-model 51
                        --output "${_hlsl}"
                DEPENDS "${_spv}" spirv-cross
                VERBATIM)
            add_custom_command(
                OUTPUT "${_dxbc}"
                COMMAND fxc /nologo /T ${_profile} /E main /Fo "${_dxbc}" "${_hlsl}"
                DEPENDS "${_hlsl}"
                VERBATIM)
            string(APPEND _entry "|${_dxbc}")
            list(APPEND _products "${_dxbc}")
        else()
            string(APPEND _entry "|")
        endif()
        list(APPEND _manifest "${_entry}")
    endforeach()

    string(REPLACE ";" "\n" _manifest_text "${_manifest}")
    file(WRITE "${_out_dir}/manifest.txt.in" "${_manifest_text}\n")
    configure_file("${_out_dir}/manifest.txt.in" "${_out_dir}/manifest.txt" COPYONLY)

    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -DMANIFEST=${_out_dir}/manifest.txt -DOUTPUT=${ARG_OUTPUT}
                -P "${SPACEFLIGHT_EMBED_SCRIPT}"
        DEPENDS ${_products} "${_out_dir}/manifest.txt" "${SPACEFLIGHT_EMBED_SCRIPT}"
        COMMENT "embedding the compiled shaders"
        VERBATIM)
endfunction()
