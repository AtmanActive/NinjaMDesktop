cmake_minimum_required(VERSION 3.16)

# Release version (major.minor.patch); the release workflow overrides it with -DNJD_VERSION=...
set(NJD_VERSION "0.1.0" CACHE STRING "NinjaMDesktop version")

project(NinjaMDesktop VERSION ${NJD_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  # static C/C++ runtime, so the .exe runs without the Visual C++ redistributable;
  # the policy default makes the fetched libogg/libvorbis (old cmake_minimum_required) follow suit
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(NINJAM_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/external/ninjam)
set(WDL_DIR      ${NINJAM_DIR}/WDL)
set(REANINJAM_DIR ${NINJAM_DIR}/jmde/fx/reaninjam)
set(MINIAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/miniaudio)
set(SRC_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT EXISTS ${REANINJAM_DIR}/winclient.cpp OR NOT EXISTS ${MINIAUDIO_DIR}/miniaudio.c)
  message(FATAL_ERROR "Submodules missing. Run: git submodule update --init --recursive")
endif()

if(WIN32)
  set(NJD_DEFAULT_SYSTEM_VORBIS OFF)
elseif(APPLE)
  set(NJD_DEFAULT_SYSTEM_VORBIS OFF)
else()
  set(NJD_DEFAULT_SYSTEM_VORBIS ON)
endif()
option(NJD_USE_SYSTEM_VORBIS "Link against system libogg/libvorbis instead of building them" ${NJD_DEFAULT_SYSTEM_VORBIS})

# ---------------------------------------------------------------------------
# Ogg / Vorbis
# ---------------------------------------------------------------------------
if(NJD_USE_SYSTEM_VORBIS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(VORBIS REQUIRED IMPORTED_TARGET vorbisenc vorbis ogg)
  set(NJD_VORBIS_LIBS PkgConfig::VORBIS)
else()
  include(FetchContent)
  # libogg/libvorbis declare cmake_minimum_required(2.8.12), which CMake 4 rejects by default
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG v1.3.5)
  FetchContent_MakeAvailable(ogg)
  # libvorbis' CMake looks for Ogg via find_package(); point it at the fetched target.
  set(OGG_INCLUDE_DIR ${ogg_SOURCE_DIR}/include ${ogg_BINARY_DIR}/include CACHE PATH "" FORCE)
  set(OGG_LIBRARY Ogg::ogg CACHE STRING "" FORCE)
  FetchContent_Declare(vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG v1.3.7)
  FetchContent_MakeAvailable(vorbis)
  set(NJD_VORBIS_LIBS vorbisenc vorbis Ogg::ogg)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# ReaNINJAM, compiled unmodified from the official Cockos repository.
# (vstframe.cpp, the VST2 wrapper, is replaced by src/host_*.cpp)
set(REANINJAM_SOURCES
  ${REANINJAM_DIR}/winclient.cpp
  ${REANINJAM_DIR}/chat.cpp
  ${REANINJAM_DIR}/license.cpp
  ${REANINJAM_DIR}/locchn.cpp
  ${REANINJAM_DIR}/remchn.cpp
  ${NINJAM_DIR}/ninjam/njclient.cpp
  ${NINJAM_DIR}/ninjam/mpb.cpp
  ${NINJAM_DIR}/ninjam/netmsg.cpp
  ${WDL_DIR}/sha.cpp
  ${WDL_DIR}/rng.cpp
  ${WDL_DIR}/wingui/wndsize.cpp
  ${WDL_DIR}/wingui/scrollbar/coolscroll.cpp
  # LICE (Cockos' bitmap library): used by coolscroll on all platforms and by SWELL on Linux
  ${WDL_DIR}/lice/lice.cpp
  ${WDL_DIR}/lice/lice_arc.cpp
  ${WDL_DIR}/lice/lice_colorspace.cpp
  ${WDL_DIR}/lice/lice_line.cpp
  ${WDL_DIR}/lice/lice_text.cpp
  ${WDL_DIR}/lice/lice_textnew.cpp
  ${WDL_DIR}/lice/lice_ico.cpp
  ${WDL_DIR}/lice/lice_bmp.cpp
  ${WDL_DIR}/jnetlib/asyncdns.cpp
  ${WDL_DIR}/jnetlib/connection.cpp
  ${WDL_DIR}/jnetlib/httpget.cpp
  ${WDL_DIR}/jnetlib/util.cpp
)

set(HOST_SOURCES
  ${SRC_DIR}/host_main.cpp
  ${SRC_DIR}/host_reaper_api.cpp
  ${SRC_DIR}/host_vorbis.cpp
  ${SRC_DIR}/host_controls.cpp
  ${SRC_DIR}/host_audio.cpp
  ${SRC_DIR}/host_audioconfig.cpp
  ${SRC_DIR}/host_paths.cpp
  ${SRC_DIR}/host_theme.cpp
  ${SRC_DIR}/host_icon.cpp
  ${SRC_DIR}/miniaudio_impl.c
)

set(NJD_INCLUDE_DIRS
  ${NINJAM_DIR}
  ${REANINJAM_DIR}
  ${SRC_DIR}
  # ReaNINJAM includes "../../localize.h" relative to jmde/fx/reaninjam;
  # this directory makes that resolve to src/compat/jmde/localize.h
  ${SRC_DIR}/compat/jmde/fx/reaninjam
  ${MINIAUDIO_DIR}
)

set(NJD_DEFINES
  REANINJAM
  MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE
  NJD_VERSION="${NJD_VERSION}"
)

# REAPER exports WDL's coolscroll functions to ReaNINJAM, which stores them in
# function-pointer variables of the same names. Rename the real functions (and the
# GetIconThemePointer() hook coolscroll expects from the app) so both can coexist.
set_source_files_properties(${WDL_DIR}/wingui/scrollbar/coolscroll.cpp PROPERTIES
  COMPILE_DEFINITIONS
    "GetIconThemePointer=NJD_CoolSB_GetIconThemePointer;InitializeCoolSB=NJD_InitializeCoolSB;UninitializeCoolSB=NJD_UninitializeCoolSB;CoolSB_SetScrollInfo=NJD_CoolSB_SetScrollInfo;CoolSB_GetScrollInfo=NJD_CoolSB_GetScrollInfo;CoolSB_SetScrollPos=NJD_CoolSB_SetScrollPos;CoolSB_SetScrollRange=NJD_CoolSB_SetScrollRange;CoolSB_SetMinThumbSize=NJD_CoolSB_SetMinThumbSize")

# ---------------------------------------------------------------------------
# Platform specifics
# ---------------------------------------------------------------------------
set(NJD_PLATFORM_SOURCES)
set(NJD_PLATFORM_LIBS)

if(WIN32)
  list(APPEND NJD_PLATFORM_SOURCES
    ${REANINJAM_DIR}/res.rc
    ${SRC_DIR}/host.rc
  )
  list(APPEND NJD_INCLUDE_DIRS ${SRC_DIR}/compat/win32 ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows) # rc: icon, manifest
  list(APPEND NJD_DEFINES _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _WINSOCK_DEPRECATED_NO_WARNINGS)
  list(APPEND NJD_PLATFORM_LIBS comctl32 ws2_32 ole32 shell32 comdlg32 gdi32 user32 advapi32 winmm uuid)

elseif(APPLE)
  enable_language(OBJC OBJCXX)
  set(SWELL_SOURCES
    ${WDL_DIR}/swell/swell.cpp
    ${WDL_DIR}/swell/swell-ini.cpp
    ${WDL_DIR}/swell/swell-dlg.mm
    ${WDL_DIR}/swell/swell-gdi.mm
    ${WDL_DIR}/swell/swell-kb.mm
    ${WDL_DIR}/swell/swell-menu.mm
    ${WDL_DIR}/swell/swell-misc.mm
    ${WDL_DIR}/swell/swell-miscdlg.mm
    ${WDL_DIR}/swell/swell-wnd.mm
    ${WDL_DIR}/swell/swellappmain.mm
    ${SRC_DIR}/mac/main.m
  )
  list(APPEND NJD_PLATFORM_SOURCES ${SWELL_SOURCES})
  list(APPEND NJD_PLATFORM_LIBS "-framework Cocoa" "-framework Carbon" "-framework CoreAudio"
    "-framework AudioToolbox" "-framework CoreFoundation" "-framework Metal" "-framework QuartzCore")

else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GDK3 REQUIRED IMPORTED_TARGET gdk-3.0)
  pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
  pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0) # desktop light/dark preference (host_theme.cpp)
  find_package(X11 REQUIRED)
  find_package(OpenGL REQUIRED)
  find_package(Threads REQUIRED)

  set(SWELL_SOURCES
    ${WDL_DIR}/swell/swell.cpp
    ${WDL_DIR}/swell/swell-ini.cpp
    ${WDL_DIR}/swell/swell-miscdlg-generic.cpp
    ${WDL_DIR}/swell/swell-wnd-generic.cpp
    ${WDL_DIR}/swell/swell-menu-generic.cpp
    ${WDL_DIR}/swell/swell-kb-generic.cpp
    ${WDL_DIR}/swell/swell-dlg-generic.cpp
    ${WDL_DIR}/swell/swell-gdi-generic.cpp
    ${WDL_DIR}/swell/swell-misc-generic.cpp
    ${WDL_DIR}/swell/swell-gdi-lice.cpp
    ${WDL_DIR}/swell/swell-generic-gdk.cpp
    ${WDL_DIR}/swell/swell-appstub-generic.cpp
    ${WDL_DIR}/swell/swell-modstub-generic.cpp
  )
  list(APPEND NJD_PLATFORM_SOURCES ${SWELL_SOURCES})
  list(APPEND NJD_DEFINES SWELL_TARGET_GDK=3 SWELL_LICE_GDI SWELL_FREETYPE SWELL_FONTCONFIG _FILE_OFFSET_BITS=64)
  list(APPEND NJD_PLATFORM_LIBS
    PkgConfig::GDK3 PkgConfig::FREETYPE PkgConfig::FONTCONFIG PkgConfig::GIO
    X11::X11 X11::Xi OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

# ---------------------------------------------------------------------------
# SWELL dialog/menu resources (Linux/macOS): generated from the .rc files
# ---------------------------------------------------------------------------
if(NOT WIN32)
  find_program(PERL_EXECUTABLE perl REQUIRED)
  set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/swellres)
  file(MAKE_DIRECTORY ${GEN_DIR})
  set(SWELL_RES_OUTPUTS)
  foreach(rc ${REANINJAM_DIR}/res.rc ${SRC_DIR}/host.rc)
    get_filename_component(rcname ${rc} NAME)
    add_custom_command(
      OUTPUT ${GEN_DIR}/${rcname}_mac_dlg ${GEN_DIR}/${rcname}_mac_menu
      COMMAND ${CMAKE_COMMAND} -E copy ${rc} ${GEN_DIR}/${rcname}
      COMMAND ${PERL_EXECUTABLE} ${WDL_DIR}/swell/swell_resgen.pl --force ${GEN_DIR}/${rcname}
      DEPENDS ${rc}
      COMMENT "Generating SWELL resources from ${rcname}")
    list(APPEND SWELL_RES_OUTPUTS ${GEN_DIR}/${rcname}_mac_dlg ${GEN_DIR}/${rcname}_mac_menu)
  endforeach()
  add_custom_target(swell_resources DEPENDS ${SWELL_RES_OUTPUTS})
  list(APPEND NJD_INCLUDE_DIRS ${GEN_DIR})
endif()

# Linux: compile the application icon into the executable (src/host_icon.cpp)
if(UNIX AND NOT APPLE)
  set(NJD_ICON_PNG ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ninjamdesktop-256.png)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NJD_ICON_PNG})
  file(READ ${NJD_ICON_PNG} NJD_ICON_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," NJD_ICON_BYTES "${NJD_ICON_HEX}")
  file(WRITE ${GEN_DIR}/app_icon_png.h.tmp
    "// generated from packaging/linux/ninjamdesktop-256.png\nstatic const unsigned char njd_app_icon_png[] = {${NJD_ICON_BYTES}};\n")
  configure_file(${GEN_DIR}/app_icon_png.h.tmp ${GEN_DIR}/app_icon_png.h COPYONLY)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
if(APPLE)
  set(MACOSX_BUNDLE_BUNDLE_NAME "NinjaMDesktop")
  set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.atmanactive.ninjamdesktop")
  set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
  set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  set(NJD_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/ninjamdesktop.icns)
  set_source_files_properties(${NJD_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(NinjaMDesktop MACOSX_BUNDLE ${REANINJAM_SOURCES} ${HOST_SOURCES} ${NJD_PLATFORM_SOURCES} ${NJD_ICNS})
  set_target_properties(NinjaMDesktop PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${SRC_DIR}/mac/Info.plist.in)
  # Compile the menu nib into the bundle
  find_program(IBTOOL ibtool HINTS "/usr/bin" "${OSX_DEVELOPER_ROOT}/usr/bin")
  if(IBTOOL)
    add_custom_command(TARGET NinjaMDesktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_CONTENT_DIR:NinjaMDesktop>/Resources
      COMMAND ${IBTOOL} --errors --warnings --notices --output-format human-readable-text
              --compile $<TARGET_BUNDLE_CONTENT_DIR:NinjaMDesktop>/Resources/MainMenu.nib
              ${SRC_DIR}/mac/MainMenu.xib)
  endif()
elseif(WIN32)
  add_executable(NinjaMDesktop WIN32 ${REANINJAM_SOURCES} ${HOST_SOURCES} ${NJD_PLATFORM_SOURCES})
  if(MSVC)
    target_link_options(NinjaMDesktop PRIVATE /MANIFEST:NO)
    target_compile_options(NinjaMDesktop PRIVATE /W3 /MP)
  endif()
else()
  add_executable(NinjaMDesktop ${REANINJAM_SOURCES} ${HOST_SOURCES} ${NJD_PLATFORM_SOURCES})
  target_compile_options(NinjaMDesktop PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-result -Wno-multichar -Wno-deprecated-declarations>
    -fno-math-errno -fvisibility=hidden)
endif()

if(NOT WIN32)
  add_dependencies(NinjaMDesktop swell_resources)
endif()

target_include_directories(NinjaMDesktop PRIVATE ${NJD_INCLUDE_DIRS})
target_compile_definitions(NinjaMDesktop PRIVATE ${NJD_DEFINES})
target_link_libraries(NinjaMDesktop PRIVATE ${NJD_VORBIS_LIBS} ${NJD_PLATFORM_LIBS})

if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(TARGETS NinjaMDesktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  # ON: the launcher uses the full path, so it works even when the bin directory isn't on the
  # session's PATH (e.g. ~/.local/bin). Packages turn this off and use the plain command name.
  option(NJD_DESKTOP_EXEC_ABSOLUTE "Use the absolute install path in the .desktop file" ON)
  if(NJD_DESKTOP_EXEC_ABSOLUTE)
    set(NJD_EXEC_EXPR "\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/NinjaMDesktop")
  else()
    set(NJD_EXEC_EXPR "NinjaMDesktop")
  endif()
  install(CODE "
    set(NJD_EXEC_PATH \"${NJD_EXEC_EXPR}\")
    configure_file(\"${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ninjamdesktop.desktop.in\"
                   \"${CMAKE_CURRENT_BINARY_DIR}/ninjamdesktop.desktop\" @ONLY)
    file(INSTALL \"${CMAKE_CURRENT_BINARY_DIR}/ninjamdesktop.desktop\"
         DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications\")
  ")
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ninjamdesktop.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/ninjamdesktop-256.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps RENAME ninjamdesktop.png)
endif()

# ---------------------------------------------------------------------------
# Linux packages (cpack -G DEB); used by .github/workflows/release.yml
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  set(CPACK_PACKAGE_NAME "ninjamdesktop")
  set(CPACK_PACKAGE_VENDOR "NinjaMDesktop")
  set(CPACK_PACKAGE_VERSION "${NJD_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NINJAM client (Cockos ReaNINJAM as a standalone app)")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/AtmanActive/NinjaMDesktop")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NinjaMDesktop <https://github.com/AtmanActive/NinjaMDesktop>")
  # extended description (the summary line comes from CPACK_PACKAGE_DESCRIPTION_SUMMARY)
  set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"A standalone desktop build of ReaNINJAM, the NINJAM client from Cockos
Incorporated, for jamming with musicians over the internet. It includes its
own audio device handling (PulseAudio/PipeWire, ALSA or JACK).")
  set(CPACK_DEBIAN_PACKAGE_SECTION "sound")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  # audio back-ends are loaded at run time, so dpkg-shlibdeps can't see them
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libasound2, libpulse0")
  set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libjack-jackd2-0 | libjack0")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  include(CPack)
endif()
