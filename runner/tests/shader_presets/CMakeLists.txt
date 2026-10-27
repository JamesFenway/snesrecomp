cmake_minimum_required(VERSION 3.20)
project(shader_preset_presenter C)
find_package(SDL3 REQUIRED CONFIG)
get_filename_component(FRAMEWORK "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_executable(presenter presenter.c
  ${FRAMEWORK}/runner/src/desktop/opengl.c
  ${FRAMEWORK}/runner/src/desktop/glsl_shader.c
  ${FRAMEWORK}/runner/src/util.c
  ${FRAMEWORK}/third_party/gl_core/gl_core_3_1.c)
target_compile_definitions(presenter PRIVATE SNESRECOMP_SDL3=1)
target_include_directories(presenter PRIVATE
  ${FRAMEWORK}/runner/src ${FRAMEWORK}/runner/src/desktop
  ${FRAMEWORK}/third_party ${FRAMEWORK}/third_party/gl_core)
target_link_libraries(presenter PRIVATE SDL3::SDL3 m)
