add_executable(MotionToGoTest
    Test.cpp
)

target_compile_definitions(MotionToGoTest
    PRIVATE
        -DTEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Data/"
        -DMOTION_TO_GO_APP="$<TARGET_FILE:MotionToGo>"
)

target_include_directories(MotionToGoTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}
)

target_link_libraries(MotionToGoTest
    PRIVATE
        gtest
        stb
)

add_dependencies(MotionToGoTest MotionToGo)
