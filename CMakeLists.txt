project(main C CXX)
cmake_minimum_required(VERSION 3.3.2)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_CXX_FLAGS "-O3 -framework OpenGL -fno-rtti")
else()
  set(CMAKE_CXX_FLAGS "-O3 -fno-rtti")
endif()
# Set common sources
set(SOURCES "main.cc" "${CMAKE_CURRENT_SOURCE_DIR}/skia/modules/skottie/utils/SkottieUtils.cpp")

add_subdirectory(glfw)
add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} glfw)


target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libskia.a)
target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libskottie.a)
target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libskshaper.a)
target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libsksg.a)
target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libskunicode.a)
target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static/libskparagraph.a)

#link_directories(${CMAKE_CURRENT_SOURCE_DIR}/skia/out/Static)
#target_link_libraries(${PROJECT_NAME} skia skottie skshaper)

if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  target_link_libraries(${PROJECT_NAME} freetype GL EGL fontconfig)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE glfw/include)
target_include_directories(${PROJECT_NAME} PRIVATE skia)
set_target_properties(${CMAKE_PROJECT_NAME}
  PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
