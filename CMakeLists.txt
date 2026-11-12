# Space Taxi Mayhem
#
# Desktop (macOS / Linux / Windows):
#   cmake -S . -B build && cmake --build build
#   macOS: builds "Space Taxi Mayhem.app"; make it self-contained with
#   cmake --install build --prefix dist   (or `cpack` in build/ for a .dmg)
#
# Nintendo Switch (devkitPro, needs DEVKITPRO set):
#   cmake -S . -B build-switch -DPLATFORM=SWITCH && cmake --build build-switch
#   -> build-switch/spacetaxi.nro  (assets/ packed into the .nro as RomFS)
#
# PS Vita (VitaSDK, needs VITASDK set; runs on Vitas with custom firmware / HENkaku):
#   cmake -S . -B build-vita -DPLATFORM=VITA && cmake --build build-vita
#   -> build-vita/SpaceTaxiMayhem.vpk  (assets/ packed in as app0:/assets)
#
# Android: Gradle project in android/ (uses this version number), built with
#   sh android/build.sh  inside the docker/android image -> build-android/SpaceTaxiMayhem-<version>.apk
#
# Passing devkitPro's toolchain directly works too:
#   cmake -B build-switch -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake
#   (or use the aarch64-none-elf-cmake wrapper)
cmake_minimum_required(VERSION 3.16)

set(PLATFORM "DESKTOP" CACHE STRING "Target platform: DESKTOP, SWITCH or VITA")
set_property(CACHE PLATFORM PROPERTY STRINGS DESKTOP SWITCH VITA)
string(TOUPPER "${PLATFORM}" PLATFORM_UC)

# The toolchain must be chosen before project()
if(PLATFORM_UC STREQUAL "SWITCH" AND NOT CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "PLATFORM=SWITCH needs the DEVKITPRO environment variable "
                        "(e.g. export DEVKITPRO=/opt/devkitpro)")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake" CACHE FILEPATH "devkitPro Switch toolchain")
endif()
if(PLATFORM_UC STREQUAL "VITA" AND NOT CMAKE_TOOLCHAIN_FILE)
  if(NOT DEFINED ENV{VITASDK})
    message(FATAL_ERROR "PLATFORM=VITA needs the VITASDK environment variable "
                        "(e.g. export VITASDK=/usr/local/vitasdk)")
  endif()
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE FILEPATH "VitaSDK toolchain")
endif()

project(SpaceTaxiMayhem VERSION 1.5.0 LANGUAGES C CXX)

# Also recognise Switch / Vita builds configured via the toolchain file alone
if(NINTENDO_SWITCH)
  set(PLATFORM_UC "SWITCH")
elseif(VITA)
  set(PLATFORM_UC "VITA")
elseif(PLATFORM_UC STREQUAL "SWITCH" OR PLATFORM_UC STREQUAL "VITA")
  message(FATAL_ERROR "PLATFORM=${PLATFORM_UC} but its toolchain is not active "
                      "(delete the build folder and configure again)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(spacetaxi src/main.cpp)
target_include_directories(spacetaxi PRIVATE src)

if(PLATFORM_UC STREQUAL "SWITCH")
  # ---------------------------------------------------------------- Nintendo Switch
  # The devkitPro toolchain already adds -D__SWITCH__, the CPU flags, libnx and the
  # link specs. SDL2 + SDL2_ttf (switch-sdl2, switch-sdl2_ttf) come from portlibs via
  # aarch64-none-elf-pkg-config. Everything is linked statically.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL REQUIRED SDL2_ttf sdl2)
  target_include_directories(spacetaxi PRIVATE ${SDL_STATIC_INCLUDE_DIRS})
  target_compile_options(spacetaxi PRIVATE ${SDL_STATIC_CFLAGS_OTHER})
  target_link_libraries(spacetaxi PRIVATE ${SDL_STATIC_LDFLAGS})

  # RomFS: the assets/ folder becomes romfs:/ inside the .nro. The game reads all
  # assets from there (romfsInit() / assetDir = "romfs:/" in main.cpp). Every file is
  # registered so editing an asset rebuilds the .nro.
  dkp_add_asset_target(spacetaxi_romfs "${CMAKE_CURRENT_SOURCE_DIR}/assets")
  file(GLOB_RECURSE ROMFS_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
       CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
  dkp_track_assets(spacetaxi_romfs FILES ${ROMFS_FILES})

  nx_generate_nacp(OUTPUT spacetaxi.nacp
    NAME    "Space Taxi Mayhem"
    AUTHOR  "rsn8887"
    VERSION "${PROJECT_VERSION}")

  nx_create_nro(spacetaxi
    NACP  spacetaxi.nacp
    ICON  "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
    ROMFS spacetaxi_romfs)
  # -> ${CMAKE_BINARY_DIR}/spacetaxi.nro

elseif(PLATFORM_UC STREQUAL "VITA")
  # ---------------------------------------------------------------- PS Vita
  # VitaSDK's toolchain defines __vita__ and links with -Wl,-q. SDL2 + SDL2_ttf (and
  # freetype, harfbuzz, the Sce* system stubs, ...) come from VitaSDK's packages via
  # arm-vita-eabi-pkg-config and are linked statically.
  include("${VITASDK}/share/vita.cmake" REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL REQUIRED SDL2_ttf sdl2)
  target_include_directories(spacetaxi PRIVATE ${SDL_STATIC_INCLUDE_DIRS})
  target_compile_options(spacetaxi PRIVATE ${SDL_STATIC_CFLAGS_OTHER})
  target_link_libraries(spacetaxi PRIVATE ${SDL_STATIC_LDFLAGS})
  # VitaSDK's SDL2 is built with vitaGL, whose libvitashark calls into SceShaccCgExt
  # (which needs taiHEN); SDL2's pkg-config file doesn't list them, so add them after
  # the SDL libraries (static link order matters).
  find_library(SHACCCGEXT_LIB SceShaccCgExt)
  if(NOT SHACCCGEXT_LIB)
    message(FATAL_ERROR "libSceShaccCgExt not found in VitaSDK - install it with:  vdpm SceShaccCgExt")
  endif()
  target_link_libraries(spacetaxi PRIVATE SceShaccCgExt taihen_stub SceShaccCg_stub)

  # VPK version is ##.## : 1.5.0 -> "01.50"
  math(EXPR _vmin "${PROJECT_VERSION_MINOR} * 10 + ${PROJECT_VERSION_PATCH}")
  set(_vmaj "${PROJECT_VERSION_MAJOR}")
  if(_vmaj LESS 10)
    set(_vmaj "0${_vmaj}")
  endif()
  if(_vmin LESS 10)
    set(_vmin "0${_vmin}")
  endif()
  set(VITA_APP_VERSION "${_vmaj}.${_vmin}")
  set(VITA_TITLEID "STXM00001")   # 4 letters + 5 digits, unique per homebrew

  # Files packed into the .vpk: LiveArea graphics (8-bit indexed PNGs) and the game
  # assets, which the game reads from app0:/assets/ (see main.cpp).
  set(VPK_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/vita/sce_sys/icon0.png" sce_sys/icon0.png
    "${CMAKE_CURRENT_SOURCE_DIR}/vita/sce_sys/livearea/contents/bg.png" sce_sys/livearea/contents/bg.png
    "${CMAKE_CURRENT_SOURCE_DIR}/vita/sce_sys/livearea/contents/startup.png" sce_sys/livearea/contents/startup.png
    "${CMAKE_CURRENT_SOURCE_DIR}/vita/sce_sys/livearea/contents/template.xml" sce_sys/livearea/contents/template.xml)
  file(GLOB_RECURSE VITA_ASSETS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
       CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
  foreach(f IN LISTS VITA_ASSETS)
    list(APPEND VPK_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/${f}" "assets/${f}")
  endforeach()

  vita_create_self(eboot.bin spacetaxi)
  vita_create_vpk(SpaceTaxiMayhem.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_APP_VERSION}
    NAME "Space Taxi Mayhem"
    FILE ${VPK_FILES})
  # -> ${CMAKE_BINARY_DIR}/SpaceTaxiMayhem.vpk

else()
  # ---------------------------------------------------------------- Desktop
  find_package(SDL2 CONFIG QUIET)
  find_package(SDL2_ttf CONFIG QUIET)

  if(TARGET SDL2::SDL2 AND TARGET SDL2_ttf::SDL2_ttf)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(spacetaxi PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(spacetaxi PRIVATE SDL2::SDL2 SDL2_ttf::SDL2_ttf)
  else()
    # fallback: pkg-config (Linux distros, older Homebrew)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2 SDL2_ttf)
    target_link_libraries(spacetaxi PRIVATE PkgConfig::SDL2)
  endif()

  if(APPLE)
    # ------------------------------------------------------------ macOS .app bundle
    # Builds "Space Taxi Mayhem.app". The game finds its assets in Contents/Resources/assets
    # (SDL_GetBasePath() returns the bundle's Resources folder).
    set(MAC_ICON "${CMAKE_CURRENT_SOURCE_DIR}/macos/SpaceTaxiMayhem.icns")
    target_sources(spacetaxi PRIVATE "${MAC_ICON}")
    set_source_files_properties("${MAC_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(spacetaxi PROPERTIES
      OUTPUT_NAME "Space Taxi Mayhem"
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
      MACOSX_BUNDLE_BUNDLE_NAME "Space Taxi Mayhem"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.rsn8887.spacetaximayhem"
      MACOSX_BUNDLE_ICON_FILE "SpaceTaxiMayhem.icns"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_COPYRIGHT "rsn8887")
    add_custom_command(TARGET spacetaxi POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets"
              "$<TARGET_BUNDLE_CONTENT_DIR:spacetaxi>/Resources/assets")

    # `cmake --install build --prefix dist` -> dist/Space Taxi Mayhem.app, self-contained:
    # the SDL2 / SDL2_ttf dylibs (and their dependencies) are copied into
    # Contents/Frameworks, re-linked, and the bundle is ad-hoc code signed so it runs
    # on Apple Silicon Macs without Homebrew installed.
    install(TARGETS spacetaxi BUNDLE DESTINATION .)
    # Homebrew's "sdl2" is sdl2-compat: SDL2 implemented on top of SDL3, which it loads at
    # run time with dlopen("libSDL3.dylib") - first from its own folder. fixup_bundle can't
    # see run-time loads, so SDL3 is copied into Contents/Frameworks explicitly.
    find_library(SDL3_DYLIB NAMES SDL3 SDL3.0 PATHS /opt/homebrew/lib /usr/local/lib NO_CACHE)
    if(SDL3_DYLIB)
      message(STATUS "macOS bundle: will embed ${SDL3_DYLIB} (needed by sdl2-compat)")
    else()
      set(SDL3_DYLIB "")
    endif()
    install(CODE "set(_sdl3_dylib \"${SDL3_DYLIB}\")")
    install(CODE [[
      include(BundleUtilities)
      set(BU_CHMOD_BUNDLE_ITEMS ON)   # Homebrew dylibs are read-only
      set(_prefix "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
      if(NOT IS_ABSOLUTE "${_prefix}")   # e.g. --prefix dist
        get_filename_component(_prefix "${_prefix}" ABSOLUTE BASE_DIR "$ENV{PWD}")
      endif()
      set(_app "${_prefix}/Space Taxi Mayhem.app")
      if(NOT EXISTS "${_app}/Contents/MacOS/Space Taxi Mayhem")
        message(FATAL_ERROR
          "The app has not been built yet (no executable inside the bundle).\n"
          "Build it first, then install / cpack again:\n"
          "    cmake --build <build folder> -j")
      endif()
      set(_extra "")
      if(_sdl3_dylib)
        # next to the bundled libSDL2, under the exact name sdl2-compat dlopen()s
        set(_sdl3_dst "${_app}/Contents/Frameworks/libSDL3.dylib")
        file(MAKE_DIRECTORY "${_app}/Contents/Frameworks")
        file(COPY_FILE "${_sdl3_dylib}" "${_sdl3_dst}")      # follows the Homebrew symlink
        file(CHMOD "${_sdl3_dst}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
        set(_extra "${_sdl3_dst}")
      endif()
      fixup_bundle("${_app}" "${_extra}" "/opt/homebrew/lib;/usr/local/lib")
      execute_process(COMMAND codesign --force --deep --sign - "${_app}" RESULT_VARIABLE _rc)
      if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${_app}")
      endif()
    ]])

    # `cpack` in the build folder -> SpaceTaxiMayhem-<version>-macOS.dmg with the app
    # (CPack's DragNDrop generator adds the Applications shortcut to drag it onto).
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_NAME "Space Taxi Mayhem")
    set(CPACK_PACKAGE_FILE_NAME "SpaceTaxiMayhem-${PROJECT_VERSION}-macOS")
    set(CPACK_DMG_VOLUME_NAME "Space Taxi Mayhem")
    # styled Finder window: background art with an arrow, 128 pt icons, no toolbar
    # (background re-rendered with:  spacetaxi --dmg-shot out.bmp, then scaled 4x at 144 dpi)
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/macos/dmg-background.png")
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/macos/dmg-setup.applescript")
    include(CPack)
  else()
    # copy assets next to the executable so it runs from the build folder
    add_custom_command(TARGET spacetaxi POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:spacetaxi>/assets)
  endif()
endif()
