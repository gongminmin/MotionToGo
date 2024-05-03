add_definitions(-DUNICODE -D_UNICODE)

if(MSVC)
    set(CMAKE_CXX_FLAGS "/W4 /WX /EHsc /MP /bigobj /Zc:strictStrings /Zc:rvalueCast /Gw")

    set(CMAKE_CXX_STANDARD 20)

    if(CMAKE_C_COMPILER_ID MATCHES Clang)
        set(motion_to_go_compiler_name "clangcl")
        set(motion_to_go_compiler_clangcl TRUE)
        set(CMAKE_C_COMPILER ${ClangCL_Path}clang-cl.exe)
        set(CMAKE_CXX_COMPILER ${ClangCL_Path}clang-cl.exe)

        execute_process(COMMAND ${CMAKE_C_COMPILER} --version OUTPUT_VARIABLE CLANG_VERSION)
        string(REGEX MATCHALL "[0-9]+" clang_version_components ${CLANG_VERSION})
        list(GET clang_version_components 0 clang_major)
        list(GET clang_version_components 1 clang_minor)
        set(motion_to_go_compiler_version ${clang_major}${clang_minor})
        if(motion_to_go_compiler_version LESS "90")
            message(FATAL_ERROR "Unsupported compiler version. Please install clang-cl 9.0 or up.")
        endif()

        set(CMAKE_C_FLAGS "/W4 /WX /bigobj /Gw")
    else()
        set(motion_to_go_compiler_name "vc")
        set(motion_to_go_compiler_msvc TRUE)
        if(MSVC_VERSION GREATER_EQUAL 1930)
            set(motion_to_go_compiler_version "143")
        elseif(MSVC_VERSION GREATER_EQUAL 1920)
            set(motion_to_go_compiler_version "142")
        else()
            message(FATAL_ERROR "Unsupported compiler version. Please install VS2019 or up.")
        endif()

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zc:throwingNew /permissive- /Zc:externConstexpr")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4819") # Allow non-ANSI characters.
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /JMC")
        foreach(flag_var
            CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
            set(${flag_var} "${${flag_var}} /GS-")
        endforeach()
        
        foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS CMAKE_MODULE_LINKER_FLAGS)
            set(${flag_var} "/WX /pdbcompress")
        endforeach()
        foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_SHARED_LINKER_FLAGS_DEBUG)
            set(${flag_var} "/DEBUG:FASTLINK")
        endforeach()
        foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO)
            set(${flag_var} "/DEBUG:FASTLINK /INCREMENTAL:NO /LTCG:incremental /OPT:REF /OPT:ICF")
        endforeach()
        foreach(flag_var
            CMAKE_EXE_LINKER_FLAGS_MINSIZEREL CMAKE_SHARED_LINKER_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_SHARED_LINKER_FLAGS_RELEASE)
            set(${flag_var} "/INCREMENTAL:NO /LTCG /OPT:REF /OPT:ICF")
        endforeach()
        foreach(flag_var
            CMAKE_MODULE_LINKER_FLAGS_RELEASE CMAKE_MODULE_LINKER_FLAGS_MINSIZEREL)
            set(${flag_var} "/INCREMENTAL:NO /LTCG")
        endforeach()
        foreach(flag_var
            CMAKE_STATIC_LINKER_FLAGS_RELEASE CMAKE_STATIC_LINKER_FLAGS_MINSIZEREL)
            set(${flag_var} "${${flag_var}} /LTCG")
        endforeach()
        set(CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_STATIC_LINKER_FLAGS_RELWITHDEBINFO} /LTCG:incremental")
        set(CMAKE_STATIC_LINKER_FLAGS "/WX")

        set(CMAKE_C_FLAGS ${CMAKE_CXX_FLAGS})
    endif()

    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /DGOLDEN_SUN_SHIP")
    foreach(flag_var
        CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        set(${flag_var} "${${flag_var}} /fp:fast /Ob2 /GL /Qpar")
    endforeach()

    add_definitions(-DWIN32 -D_WINDOWS)
endif()

set(CMAKE_C_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
set(CMAKE_C_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
set(CMAKE_C_FLAGS_RELWITHDEBINFO ${CMAKE_CXX_FLAGS_RELWITHDEBINFO})
set(CMAKE_C_FLAGS_MINSIZEREL ${CMAKE_CXX_FLAGS_MINSIZEREL})
if(motion_to_go_compiler_msvc OR motion_to_go_compiler_clangcl)
    set(rtti_flag "/GR")
    set(no_rtti_flag "/GR-")
else()
    set(rtti_flag "-frtti")
    set(no_rtti_flag "-fno-rtti")
endif()
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${rtti_flag}")
foreach(flag_var
    CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    set(${flag_var} "${${flag_var}} ${no_rtti_flag}")
endforeach()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
