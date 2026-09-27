# Offline shader compilation.
#
# SDL's GPU API takes each backend's own shader format: DXIL for Direct3D 12,
# SPIR-V for Vulkan, MSL for Metal. opane's shaders are written once, in HLSL,
# and compiled here to every format the platform being built for can run:
#
#   Windows  DXIL, with the Windows SDK's dxc; and SPIR-V, with the Vulkan
#            SDK's dxc, when the Vulkan SDK is installed
#   macOS    MSL: SPIR-V from the Vulkan SDK's dxc, translated by its
#            spirv-cross
#   Linux    SPIR-V
#
# Every result is embedded in the binary, so no compiler travels with the
# program. A backend whose format a build lacks cannot be chosen at runtime.
#
# OPANE_SHADER_FORMATS, a list drawn from DXIL, SPIRV, and MSL, overrides the
# choice; every format it names must then be buildable. The compilers are
# found on their own and can be named by hand: OPANE_DXC (DXIL),
# OPANE_DXC_SPIRV (SPIR-V and MSL), OPANE_SPIRV_CROSS (MSL).

set(OPANE_SHADER_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedBinary.cmake")

set(OPANE_SHADER_FORMATS "" CACHE STRING
    "Shader formats to build, from DXIL, SPIRV, and MSL. Empty builds every format this platform runs.")

# The Windows SDK's dxc, which signs its output with the dxil.dll beside it.
# Direct3D 12 refuses an unsigned shader, so a dxc without that file (such as
# the Vulkan SDK's) cannot make DXIL.
function(_opane_find_dxil_compiler)
    if(OPANE_DXC)
        return()
    endif()

    set(SearchHints "")
    file(GLOB KitDirectories "C:/Program Files (x86)/Windows Kits/10/bin/10.*")
    if(KitDirectories)
        list(SORT KitDirectories)
        list(REVERSE KitDirectories)
        foreach(Kit IN LISTS KitDirectories)
            list(APPEND SearchHints "${Kit}/x64")
        endforeach()
    endif()

    find_program(OPANE_DXC NAMES dxc HINTS ${SearchHints} NO_SYSTEM_ENVIRONMENT_PATH
        DOC "dxc for DXIL, the Direct3D 12 format; dxil.dll must be beside it")
    if(OPANE_DXC)
        get_filename_component(Directory "${OPANE_DXC}" DIRECTORY)
        if(NOT EXISTS "${Directory}/dxil.dll")
            message(STATUS "opane: ignoring ${OPANE_DXC} for DXIL, which has no dxil.dll to sign it with")
            unset(OPANE_DXC CACHE)
        endif()
    endif()
endfunction()

# A dxc built with the SPIR-V backend, which the Vulkan SDK's is and the
# Windows SDK's is not. Tried on a one-line shader before it is trusted.
function(_opane_find_spirv_compiler)
    if(OPANE_DXC_SPIRV AND OPANE_DXC_SPIRV STREQUAL OPANE_DXC_SPIRV_CHECKED)
        return()
    endif()

    find_program(OPANE_DXC_SPIRV NAMES dxc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        DOC "dxc with SPIR-V output, for Vulkan and Metal; the Vulkan SDK has one")
    if(NOT OPANE_DXC_SPIRV)
        return()
    endif()

    set(Probe "${CMAKE_BINARY_DIR}/opane_spirv_probe.hlsl")
    file(WRITE "${Probe}" "float4 main() : SV_Target { return 0; }\n")
    execute_process(COMMAND "${OPANE_DXC_SPIRV}" -spirv -T ps_6_0 -E main -Fo "${Probe}.spv" "${Probe}"
                    RESULT_VARIABLE Result OUTPUT_QUIET ERROR_QUIET)
    if(Result EQUAL 0)
        set(OPANE_DXC_SPIRV_CHECKED "${OPANE_DXC_SPIRV}" CACHE INTERNAL "")
    else()
        message(STATUS "opane: ignoring ${OPANE_DXC_SPIRV} for SPIR-V, which was built without it")
        unset(OPANE_DXC_SPIRV CACHE)
    endif()
endfunction()

function(_opane_find_spirv_cross)
    if(OPANE_SPIRV_CROSS)
        return()
    endif()
    find_program(OPANE_SPIRV_CROSS NAMES spirv-cross
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
        DOC "spirv-cross, which turns SPIR-V into MSL for Metal; the Vulkan SDK has one")
endfunction()

# Sets OutVar to the formats this configure builds, deciding them the first
# time it is asked.
function(opane_shader_formats OutVar)
    get_property(Formats GLOBAL PROPERTY OPANE_SHADER_FORMATS_BUILT)
    if(Formats)
        set(${OutVar} "${Formats}" PARENT_SCOPE)
        return()
    endif()

    set(Strict FALSE)
    if(OPANE_SHADER_FORMATS)
        set(Wanted ${OPANE_SHADER_FORMATS})
        set(Strict TRUE)
    elseif(WIN32)
        set(Wanted DXIL SPIRV)
    elseif(APPLE)
        set(Wanted MSL)
    else()
        set(Wanted SPIRV)
    endif()

    set(Formats "")
    set(AllMissing "")
    foreach(Format IN LISTS Wanted)
        set(Missing "")
        if(Format STREQUAL "DXIL")
            _opane_find_dxil_compiler()
            if(NOT OPANE_DXC)
                set(Missing "DXIL, for Direct3D 12, needs the Windows SDK's dxc.exe with dxil.dll beside it; "
                            "install the Windows SDK or set -DOPANE_DXC=<path to dxc.exe>.")
            endif()
        elseif(Format STREQUAL "SPIRV")
            _opane_find_spirv_compiler()
            if(NOT OPANE_DXC_SPIRV)
                set(Missing "SPIR-V, for Vulkan, needs a dxc with SPIR-V output; install the Vulkan SDK "
                            "(https://vulkan.lunarg.com) or set -DOPANE_DXC_SPIRV=<path to dxc>.")
            endif()
        elseif(Format STREQUAL "MSL")
            _opane_find_spirv_compiler()
            _opane_find_spirv_cross()
            if(NOT OPANE_DXC_SPIRV OR NOT OPANE_SPIRV_CROSS)
                set(Missing "MSL, for Metal, needs a dxc with SPIR-V output and spirv-cross; install the Vulkan "
                            "SDK (https://vulkan.lunarg.com), or set -DOPANE_DXC_SPIRV and -DOPANE_SPIRV_CROSS.")
            endif()
        else()
            message(FATAL_ERROR
                "opane: OPANE_SHADER_FORMATS names ${Format}; the formats are DXIL, SPIRV, and MSL.")
        endif()

        if(Missing)
            if(Strict)
                message(FATAL_ERROR "opane: ${Missing}")
            endif()
            message(STATUS "opane: not building ${Format} shaders, so that backend is unavailable. ${Missing}")
            string(APPEND AllMissing " ${Missing}")
        else()
            list(APPEND Formats ${Format})
        endif()
    endforeach()

    if(NOT Formats)
        message(FATAL_ERROR "opane: no shader compiler was found.${AllMissing}")
    endif()

    message(STATUS "opane shader formats: ${Formats}")
    set_property(GLOBAL PROPERTY OPANE_SHADER_FORMATS_BUILT "${Formats}")
    set(${OutVar} "${Formats}" PARENT_SCOPE)
endfunction()

# Compiles one entry point to every format this configure builds and embeds
# each in the target, as <SYMBOL>Dxil, <SYMBOL>Spirv, and <SYMBOL>Msl, each with
# a Size beside it. A format that is not built is embedded empty, so all three
# symbols always exist and code tells them apart by size.
function(_opane_compile_embedded)
    cmake_parse_arguments(PARSE_ARGV 0 Arg "" "TARGET;SOURCE;ENTRY;PROFILE;SYMBOL;DIRECTORY;INCLUDE_DIR;DEFINE"
                          "DEPENDS")
    opane_shader_formats(Formats)

    get_filename_component(SourcePath "${Arg_SOURCE}" ABSOLUTE)
    set(Directory "${Arg_DIRECTORY}")
    set(Includes "")
    if(Arg_INCLUDE_DIR)
        set(Includes -I "${Arg_INCLUDE_DIR}")
    endif()
    set(Depends "${SourcePath}" ${Arg_DEPENDS})
    set(SpirvArguments -spirv -fspv-target-env=vulkan1.0 -T ${Arg_PROFILE} -E ${Arg_ENTRY} ${Includes} -O3)

    foreach(Suffix Dxil Spirv Msl)
        set(Symbol "${Arg_SYMBOL}${Suffix}")
        set(GeneratedPath "${Directory}/${Symbol}.cpp")

        if(Suffix STREQUAL "Dxil" AND "DXIL" IN_LIST Formats)
            set(BlobPath "${Directory}/${Arg_SYMBOL}.dxil")
            add_custom_command(
                OUTPUT "${BlobPath}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${Directory}"
                COMMAND "${OPANE_DXC}" -T ${Arg_PROFILE} -E ${Arg_ENTRY} ${Includes} -O3
                        -Fo "${BlobPath}" "${SourcePath}"
                DEPENDS ${Depends}
                COMMENT "Compiling ${Arg_ENTRY} from ${Arg_SOURCE} to DXIL"
                VERBATIM)
        elseif(Suffix STREQUAL "Spirv" AND "SPIRV" IN_LIST Formats)
            set(BlobPath "${Directory}/${Arg_SYMBOL}.spv")
            add_custom_command(
                OUTPUT "${BlobPath}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${Directory}"
                COMMAND "${OPANE_DXC_SPIRV}" ${SpirvArguments} -Fo "${BlobPath}" "${SourcePath}"
                DEPENDS ${Depends}
                COMMENT "Compiling ${Arg_ENTRY} from ${Arg_SOURCE} to SPIR-V"
                VERBATIM)
        elseif(Suffix STREQUAL "Msl" AND "MSL" IN_LIST Formats)
            # Metal numbers its buffer slots differently from Vulkan; the
            # define tells the shader includes to follow Metal's numbering.
            set(BlobPath "${Directory}/${Arg_SYMBOL}.metal")
            add_custom_command(
                OUTPUT "${BlobPath}"
                BYPRODUCTS "${Directory}/${Arg_SYMBOL}.metal.spv"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${Directory}"
                COMMAND "${OPANE_DXC_SPIRV}" ${SpirvArguments} -D ${Arg_DEFINE}
                        -Fo "${Directory}/${Arg_SYMBOL}.metal.spv" "${SourcePath}"
                COMMAND "${OPANE_SPIRV_CROSS}" "${Directory}/${Arg_SYMBOL}.metal.spv"
                        --msl --msl-version 20100 --msl-decoration-binding --output "${BlobPath}"
                DEPENDS ${Depends}
                COMMENT "Compiling ${Arg_ENTRY} from ${Arg_SOURCE} to MSL"
                VERBATIM)
        else()
            # Written only when it differs, so reconfiguring rebuilds nothing.
            file(WRITE "${GeneratedPath}.in"
                "// ${Arg_ENTRY} from ${Arg_SOURCE} was not compiled to this format. Do not edit.\n"
                "\n"
                "#include <cstddef>\n"
                "\n"
                "extern const unsigned char ${Symbol}[] = { 0 };\n"
                "extern const size_t ${Symbol}Size = 0;\n")
            configure_file("${GeneratedPath}.in" "${GeneratedPath}" COPYONLY)
            target_sources(${Arg_TARGET} PRIVATE "${GeneratedPath}")
            continue()
        endif()

        add_custom_command(
            OUTPUT "${GeneratedPath}"
            COMMAND ${CMAKE_COMMAND}
                    -DINPUT=${BlobPath}
                    -DOUTPUT=${GeneratedPath}
                    -DSYMBOL=${Symbol}
                    -P "${OPANE_SHADER_EMBED_SCRIPT}"
            DEPENDS "${BlobPath}" "${OPANE_SHADER_EMBED_SCRIPT}"
            COMMENT "Embedding ${Symbol}"
            VERBATIM)
        target_sources(${Arg_TARGET} PRIVATE "${GeneratedPath}")
    endforeach()
endfunction()

# opane_add_shader(<target> <source> <entry point> <profile> <symbol> [included files...])
#
# Compiles one entry point and adds the embedded results to the target's
# sources. Anything listed after the symbol is a file the shader includes, so
# an edit to it rebuilds the shader.
function(opane_add_shader Target Source Entry Profile Symbol)
    _opane_compile_embedded(
        TARGET ${Target}
        SOURCE "${Source}"
        ENTRY ${Entry}
        PROFILE ${Profile}
        SYMBOL ${Symbol}
        DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        DEFINE OPANE_METAL
        DEPENDS ${ARGN})
endfunction()

# opane_embed_file(<target> <file> <symbol>)
#
# Embeds a file's bytes in the target, as <symbol> and <symbol>Size, the way the
# compiled shaders are.
function(opane_embed_file Target File Symbol)
    get_filename_component(FilePath "${File}" ABSOLUTE)
    set(GeneratedPath "${CMAKE_CURRENT_BINARY_DIR}/generated/${Symbol}.cpp")

    add_custom_command(
        OUTPUT "${GeneratedPath}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND ${CMAKE_COMMAND}
                -DINPUT=${FilePath}
                -DOUTPUT=${GeneratedPath}
                -DSYMBOL=${Symbol}
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedBinary.cmake"
        DEPENDS "${FilePath}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EmbedBinary.cmake"
        COMMENT "Embedding ${Symbol}"
        VERBATIM)

    target_sources(${Target} PRIVATE "${GeneratedPath}")
endfunction()

# opane_add_material(<target> <source.hlsl> SYMBOL <name> [ENTRY <entry point>])
#
# Compiles a material shader when <target> is built and embeds it, in every
# format this build makes, so a program that ships needs no shader compiler
# and no .hlsl files:
#
#   opane_add_material(my_game shaders/glow.hlsl SYMBOL GlowShader)
#
#   #include "GlowShader.h"
#   ... CreateMaterial({ .Bytecode = GlowShader(), ... });
#
# GlowShader() returns an opane::ShaderBytecode holding each format, and the
# material takes the one its device runs. The shader is compiled against
# material.hlsli as this build of opane has it, and rebuilt when the source or
# it changes. ENTRY defaults to FragmentMain, as MaterialDesc::EntryPoint does.
# The profile is ps_6_0, as opane's own shaders use.
function(opane_add_material Target Source)
    cmake_parse_arguments(PARSE_ARGV 2 Material "" "SYMBOL;ENTRY" "")
    if(NOT Material_SYMBOL)
        message(FATAL_ERROR "opane_add_material(${Target} ${Source}) needs SYMBOL <name>.")
    endif()
    if(NOT Material_ENTRY)
        set(Material_ENTRY FragmentMain)
    endif()

    # Beside this file in a source tree, under share/ in an installed package.
    set(IncludeDir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../shaders")
    if(NOT EXISTS "${IncludeDir}/material.hlsli")
        get_filename_component(IncludeDir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../share/opane/shaders" ABSOLUTE)
    endif()
    if(NOT EXISTS "${IncludeDir}/material.hlsli")
        message(FATAL_ERROR
            "opane_add_material could not find material.hlsli beside ${CMAKE_CURRENT_FUNCTION_LIST_DIR}.")
    endif()

    set(Directory "${CMAKE_CURRENT_BINARY_DIR}/opane_materials")
    set(HeaderPath "${Directory}/include/${Material_SYMBOL}.h")
    set(Name "${Material_SYMBOL}")

    file(WRITE "${HeaderPath}.in"
        "// Generated by opane_add_material from ${Source}. Do not edit.\n"
        "\n"
        "#pragma once\n"
        "\n"
        "#include <opane/opane.h>\n"
        "\n"
        "#include <cstddef>\n"
        "\n"
        "extern const unsigned char ${Name}Dxil[];\n"
        "extern const size_t ${Name}DxilSize;\n"
        "extern const unsigned char ${Name}Spirv[];\n"
        "extern const size_t ${Name}SpirvSize;\n"
        "extern const unsigned char ${Name}Msl[];\n"
        "extern const size_t ${Name}MslSize;\n"
        "\n"
        "// The material in every format this build compiled it to. A format it\n"
        "// was not compiled to is empty.\n"
        "inline opane::ShaderBytecode ${Name}()\n"
        "{\n"
        "    return { ${Name}Dxil, ${Name}DxilSize, ${Name}Spirv, ${Name}SpirvSize,\n"
        "             ${Name}Msl, ${Name}MslSize };\n"
        "}\n")
    configure_file("${HeaderPath}.in" "${HeaderPath}" COPYONLY)

    _opane_compile_embedded(
        TARGET ${Target}
        SOURCE "${Source}"
        ENTRY ${Material_ENTRY}
        PROFILE ps_6_0
        SYMBOL ${Material_SYMBOL}
        DIRECTORY "${Directory}"
        INCLUDE_DIR "${IncludeDir}"
        DEFINE OPANE_METAL
        DEPENDS "${IncludeDir}/material.hlsli")

    target_sources(${Target} PRIVATE "${HeaderPath}")
    target_include_directories(${Target} PRIVATE "${Directory}/include")
endfunction()
