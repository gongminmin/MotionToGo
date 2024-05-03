set(gpu_source_files
    Gpu/GpuBuffer.cpp
    Gpu/GpuCommandList.cpp
    Gpu/GpuDescriptorAllocator.cpp
    Gpu/GpuDescriptorHeap.cpp
    Gpu/GpuMemoryAllocator.cpp
    Gpu/GpuResourceViews.cpp
    Gpu/GpuSystem.cpp
    Gpu/GpuTexture2D.cpp
)

set(gpu_header_files
    Gpu/GpuBuffer.hpp
    Gpu/GpuBufferHelper.hpp
    Gpu/GpuCommandList.hpp
    Gpu/GpuDescriptorAllocator.hpp
    Gpu/GpuDescriptorHeap.hpp
    Gpu/GpuMemoryAllocator.hpp
    Gpu/GpuResourceViews.hpp
    Gpu/GpuSystem.hpp
    Gpu/GpuTexture2D.hpp
)

set(mb_gen_source_files
    MotionBlurGenerator/MotionBlurGenerator.cpp
)

set(mb_gen_header_files
    MotionBlurGenerator/MotionBlurGenerator.hpp
)

set(mb_gen_shader_files
    MotionBlurGenerator/MotionBlurGatherCs.hlsl
    MotionBlurGenerator/MotionBlurNeighborMaxCs.hlsl
    MotionBlurGenerator/Nv12ToRgbCs.hlsl
    MotionBlurGenerator/OverlayMotionVectorCs.hlsl
    MotionBlurGenerator/RgbToNv12Cs.hlsl
)

set(reader_source_files
    Reader/ImageSeqReader.cpp
    Reader/Reader.cpp
    Reader/VideoReader.cpp
)

set(reader_header_files
    Reader/Reader.hpp
)

source_group("Source Files\\Gpu" FILES ${gpu_source_files})
source_group("Header Files\\Gpu" FILES ${gpu_header_files})
source_group("Source Files\\MotionBlurGenerator" FILES ${mb_gen_source_files})
source_group("Header Files\\MotionBlurGenerator" FILES ${mb_gen_header_files})
source_group("Source Files\\MotionBlurGenerator\\Shader Files" FILES ${mb_gen_shader_files})
source_group("Source Files\\Reader" FILES ${reader_source_files})
source_group("Header Files\\Reader" FILES ${reader_header_files})

add_executable(MotionToGo
    pch.hpp
    ErrorHandling.cpp
    ErrorHandling.hpp
    MotionToGo.cpp
    Noncopyable.hpp
    SmartPtrHelper.hpp
    Util.hpp
    ${gpu_source_files}
    ${gpu_header_files}
    ${mb_gen_source_files}
    ${mb_gen_header_files}
    ${mb_gen_shader_files}
    ${reader_source_files}
    ${reader_header_files}
)

macro(AddShaderFile file_name shader_type entry_point)
    get_filename_component(file_base_name ${file_name} NAME_WE)
    set(variable_name ${file_base_name}_shader)
    set(output_name "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/CompiledShaders/${file_base_name}.h")
    set(debug_option "-Zi;-Od;-Qembed_debug")
    set(release_option "-O2")

    add_custom_command(OUTPUT ${output_name}
        COMMAND dxc "$<IF:$<CONFIG:Debug>,${debug_option},${release_option}>" -T ${shader_type}_6_3 -Vn ${variable_name} -E "${entry_point}" -Fh "${output_name}" /nologo "${CMAKE_CURRENT_SOURCE_DIR}/${file_name}"
        COMMENT "Compiling ${file_name} to ${output_name}..."
        MAIN_DEPENDENCY ${file_name}
        DEPENDS ${file_name}
        VERBATIM COMMAND_EXPAND_LISTS
    )
endmacro()

foreach(file ${mb_gen_shader_files})
    AddShaderFile(${file} "cs" "main")
endforeach()

target_precompile_headers(MotionToGo
    PRIVATE
        pch.hpp
)

target_include_directories(MotionToGo
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}
)

target_link_libraries(MotionToGo
    PRIVATE
        cxxopts
        DirectX-Headers
        stb
        zlib
        d3d12
        dxgi
        dxguid
        mfplat
        mfreadwrite
)
