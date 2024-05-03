UpdateExternalLib("zlib" "https://github.com/madler/zlib.git" "09155eaa2f9270dc4ed1fa13e2b4b2613e6e4851" "" "")

set(BUILD_SHARED_LIBS OFF)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)

add_subdirectory(zlib EXCLUDE_FROM_ALL)

set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB FALSE CACHE BOOL "" FORCE)
set(ZLIB_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/zlib
    ${PROJECT_BINARY_DIR}/External/zlib/zlib
    CACHE INTERNAL "" FORCE
)
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} CACHE INTERNAL "" FORCE)
set(ZLIB_LIBRARY zlib CACHE INTERNAL "" FORCE)
set(ZLIB_LIBRARIES ${ZLIB_LIBRARY} CACHE INTERNAL "" FORCE)
set(ZLIB_FOUND TRUE CACHE INTERNAL "" FORCE)

target_include_directories(${ZLIB_LIBRARY}
    PUBLIC
        ${ZLIB_INCLUDE_DIRS}
)

set_target_properties("example" "minigzip" "zlib" "zlibstatic"
    PROPERTIES FOLDER "External/zlib"
)
