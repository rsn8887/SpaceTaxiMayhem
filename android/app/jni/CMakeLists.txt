# Native part of the Android build: SDL2, SDL2_ttf (with its bundled FreeType) and the game.
# SDL and SDL_ttf are links to the unpacked release sources, set up by android/build.sh.
cmake_minimum_required(VERSION 3.16)
project(SpaceTaxiMayhemAndroid C CXX)

if(NOT GAME_SRC)
  message(FATAL_ERROR "GAME_SRC (the Space Taxi Mayhem source folder) is not set")
endif()

set(SDL_SHARED ON  CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL)

set(SDL2TTF_VENDORED ON  CACHE BOOL "" FORCE)   # FreeType from SDL_ttf/external, linked in statically
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory(SDL_ttf)

# SDLActivity loads libmain.so and calls SDL_main() (main() is renamed by SDL_main.h)
add_library(main SHARED ${GAME_SRC}/src/main.cpp)
target_include_directories(main PRIVATE ${GAME_SRC}/src)
target_compile_features(main PRIVATE cxx_std_17)
if(TARGET SDL2::SDL2main)
  target_link_libraries(main PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(main PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
