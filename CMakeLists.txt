Project(ViGEmClient)
cmake_minimum_required(VERSION 3.20.2)

set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/ViGEmClient.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/Internal.h ${CMAKE_CURRENT_SOURCE_DIR}/src/resource.h ${CMAKE_CURRENT_SOURCE_DIR}/src/ViGEmClient.rc)
add_library(ViGEmClient EXCLUDE_FROM_ALL ${SOURCES})
target_include_directories(ViGEmClient PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# DS5 test program
add_executable(ds5_test EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/test/ds5_test.cpp)
target_link_libraries(ds5_test PRIVATE ViGEmClient)

# DS5 Bridge - Real DualSense to Virtual DualSense bridge
add_executable(ds5_bridge EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/Ds5Bridge/main.cpp)
target_include_directories(ds5_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ds5_bridge PRIVATE ViGEmClient setupapi hid)
