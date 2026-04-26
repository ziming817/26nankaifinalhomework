cmake_minimum_required(VERSION 3.15)
project(PixelEscape LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(pixel_escape WIN32 src/main.cpp)
target_link_libraries(pixel_escape PRIVATE user32 gdi32 gdiplus)

add_custom_command(TARGET pixel_escape POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pixel_escape>/assets")
set(_FAIL_PNG "${CMAKE_SOURCE_DIR}/assets/fail_taunt.png")
if(EXISTS "${_FAIL_PNG}")
  add_custom_command(TARGET pixel_escape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_FAIL_PNG}" "$<TARGET_FILE_DIR:pixel_escape>/assets/fail_taunt.png"
    COMMENT "Copy fail_taunt.png next to executable")
endif()
set(_WIN_PNG "${CMAKE_SOURCE_DIR}/assets/win_taunt.png")
if(EXISTS "${_WIN_PNG}")
  add_custom_command(TARGET pixel_escape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_WIN_PNG}" "$<TARGET_FILE_DIR:pixel_escape>/assets/win_taunt.png"
    COMMENT "Copy win_taunt.png next to executable")
endif()
set(_BOSS_PNG "${CMAKE_SOURCE_DIR}/assets/boss_dark.png")
if(EXISTS "${_BOSS_PNG}")
  add_custom_command(TARGET pixel_escape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_BOSS_PNG}" "$<TARGET_FILE_DIR:pixel_escape>/assets/boss_dark.png"
    COMMENT "Copy boss_dark.png next to executable")
endif()
set(_PLAYER_PNG "${CMAKE_SOURCE_DIR}/assets/player_nailong.png")
if(EXISTS "${_PLAYER_PNG}")
  add_custom_command(TARGET pixel_escape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_PLAYER_PNG}" "$<TARGET_FILE_DIR:pixel_escape>/assets/player_nailong.png"
    COMMENT "Copy player_nailong.png next to executable")
endif()
set(_WIN_TRUE_PNG "${CMAKE_SOURCE_DIR}/assets/win_true_nailong.png")
if(EXISTS "${_WIN_TRUE_PNG}")
  add_custom_command(TARGET pixel_escape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_WIN_TRUE_PNG}" "$<TARGET_FILE_DIR:pixel_escape>/assets/win_true_nailong.png"
    COMMENT "Copy win_true_nailong.png next to executable")
endif()

if(MSVC)
  target_compile_options(pixel_escape PRIVATE /W4 /utf-8)
else()
  target_compile_options(pixel_escape PRIVATE -Wall -Wextra)
endif()
