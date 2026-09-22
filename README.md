# NinjaMDesktop

Cockos [NINJAM](https://www.ninjam.com)'s ReaNINJAM client, packaged as a standalone desktop app for Linux, Windows and macOS.

ReaNINJAM is the NINJAM client that ships with REAPER as a VST plug-in. NinjaMDesktop compiles
ReaNINJAM's own source files **unmodified** from the official Cockos repository and replaces only
the VST wrapper (`vstframe.cpp`) with a small host that supplies what REAPER normally provides.

## How it is put together

| Part | Source |
|---|---|
| ReaNINJAM UI and NINJAM client engine | [`external/ninjam`](https://github.com/justinfrankel/ninjam) (git submodule, unmodified) |
| Win32 API on Linux/macOS | Cockos SWELL (part of WDL, in the same submodule) |
| Audio I/O | [`external/miniaudio`](https://github.com/mackron/miniaudio) (git submodule): WASAPI/DirectSound on Windows, Core Audio on macOS, PulseAudio/ALSA/JACK on Linux |
| Ogg Vorbis | libogg/libvorbis (system packages on Linux, built from source on Windows/macOS) |
| Host layer | [`src/`](src) |

The host layer (`src/`) provides:

- `host_main.cpp`: application entry point, message loop, ReaNINJAM's keyboard shortcuts, menu changes
- `host_reaper_api.cpp`: the REAPER API functions ReaNINJAM imports (settings path, fader scale, scrollbars, localization pass-through)
- `host_controls.cpp`: REAPER's custom controls used by ReaNINJAM's dialogs (faders, VU meters, mute/solo buttons)
- `host_audio.cpp`, `host_audioconfig.cpp`: audio device handling and the *Audio configuration* dialog
- `host_vorbis.cpp`: the Vorbis encoder/decoder REAPER normally lends to ReaNINJAM
- `host_theme.cpp`, `host_theme_win.cpp`: light/dark theme and zoom
- `host_license.cpp`: remembers accepted server license agreements

## Differences from ReaNINJAM inside REAPER

- **File > Audio configuration...** chooses the audio system, devices, sample rate and buffer size (REAPER's audio preferences do this in REAPER).
- The **Sync** button and menu are removed. They control REAPER's transport and tempo, which don't exist here.
- Local channels set to *Session mode* don't transmit, and remote session-mode channels don't play, because there is no project timeline to sync them to. Normal and *Voice chat* channels work as usual.
- Settings are stored in `reaninjam.ini` in the per-user config folder: `~/.config/NinjaMDesktop` (Linux), `%APPDATA%\NinjaMDesktop` (Windows), `~/Library/Application Support/NinjaMDesktop` (macOS). If a `reaninjam.ini` exists next to the executable, that one is used instead (portable mode, a ReaNINJAM feature).
- Session recordings (File > Preferences) default to `NINJAMsessions` in your Documents folder.
- **File > Theme** follows the system's light/dark setting, or forces light or dark (Linux; Windows 10 1809 and later). **File > Zoom** sets the UI size (Linux).
- A server's license agreement is shown only the first time you connect, and again if the server changes its license text. Accepted licenses are listed in the `[ninjamdesktop_licenses]` section of `reaninjam.ini`; delete an entry to see that server's license again.

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/AtmanActive/NinjaMDesktop.git
```

(or `git submodule update --init --recursive` in an existing clone)

### Linux (Debian/Ubuntu)

```bash
sudo apt install cmake g++ perl pkgconf libgtk-3-dev libfreetype-dev libfontconfig-dev libgl-dev libx11-dev libxi-dev libvorbis-dev libogg-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/NinjaMDesktop
```

### Windows

Visual Studio 2022 (Desktop development with C++) and CMake:

```bat
cmake -B build -A x64
cmake --build build --config Release
```

libogg/libvorbis are downloaded and built automatically.

### macOS

Xcode command line tools and CMake:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/NinjaMDesktop.app
```

The macOS build has not been tested yet.

### Installing a local build (Linux)

```bash
cmake --install build --prefix ~/.local
```

This adds NinjaMDesktop to the desktop's application menu.

## Releases

Releases are built by the **release** workflow, which is started manually:
GitHub > Actions > release > Run workflow. Enter a version such as `0.2.0`, or leave it
empty to use `NJD_VERSION` from `CMakeLists.txt`. It builds:

- Linux: `.deb`, `.AppImage` and `.tar.gz` (built on Ubuntu 22.04 for wide compatibility)
- Windows: `.zip` (x64, no Visual C++ runtime needed)
- macOS: `.dmg` (universal: Apple silicon and Intel; not notarized)
- a source archive that includes the submodules, as the GPL requires

and puts them in a **draft** release `v<version>`, which you can edit and publish.
Running it again for the same version replaces the files in the draft.

## License

GPL v2 or later, the same as NINJAM. See [LICENSE](LICENSE).
NINJAM, ReaNINJAM, WDL and SWELL are by Cockos Incorporated. miniaudio is public domain / MIT-0.
