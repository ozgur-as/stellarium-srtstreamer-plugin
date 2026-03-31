<img src="resources/stellarium_logo.png" alt="Stellarium" width="80"> &nbsp; <img src="resources/srt_banner.png" alt="SRT" width="200">

# Stellarium SRT Streamer Plugin

[![Release](https://img.shields.io/github/v/release/ozgur-as/stellarium-srtstreamer-plugin?style=flat-square&label=release)](https://github.com/ozgur-as/stellarium-srtstreamer-plugin/releases)
[![License](https://img.shields.io/badge/license-GPL--2.0-blue?style=flat-square)](COPYING)
[![Stellarium](https://img.shields.io/badge/Stellarium-25.x+-orange?style=flat-square)](https://stellarium.org)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey?style=flat-square)
![Qt](https://img.shields.io/badge/Qt-6.x-41cd52?style=flat-square&logo=qt&logoColor=white)
![FFmpeg](https://img.shields.io/badge/FFmpeg-required-007808?style=flat-square&logo=ffmpeg&logoColor=white)

Captures the rendered sky framebuffer and streams it as H.264 video over **SRT** (Secure Reliable Transport).
Supports hardware encoding (NVENC, VAAPI) and works with any SRT receiver — VLC, OBS, ffplay.

---

## Features

- **Direct framebuffer capture** via OpenGL (same hook point as SpoutSender)
- **SRT streaming** with caller and listener modes
- **Hardware encoding** — libx264 (CPU), h264_nvenc (NVIDIA), h264_vaapi (Linux)
- **Async encoding** on a separate thread with double-buffered frame handoff
- **PBO double-buffering** for async GPU-to-CPU readback
- **Non-blocking connection** with timeout and cancel support
- **Toolbar integration** — SRT button in Stellarium's bottom toolbar
- **Configurable** — SRT URL, bitrate, resolution, and frame rate

## Installation (Windows)

1. Download all files from the [latest release](https://github.com/ozgur-as/stellarium-srtstreamer-plugin/releases).
2. Back up your `C:\Program Files\Stellarium\` folder.
3. Copy all downloaded files into `C:\Program Files\Stellarium\`, replacing the originals.
4. Launch Stellarium and enable the plugin in Configuration > Plugins > SRT Video Streamer.

> The release includes FFmpeg DLLs with H.264 encoding support (libx264, h264_nvenc) since the default Stellarium FFmpeg DLLs only support decoding.

> **Why a full executable?** Stellarium does not support dynamic plugins on Windows ([stellarium#385](https://github.com/Stellarium/stellarium/issues/385)), so the plugin must be compiled into the binary as a static plugin. The release is built from unmodified Stellarium source with only the SRT Streamer plugin added. All other Stellarium features and plugins work as usual.

### Installation (Linux)

1. Download `stellarium-srtstreamer-linux64.tar.gz` from the [latest release](https://github.com/ozgur-as/stellarium-srtstreamer-plugin/releases).
2. Extract into your Stellarium modules directory:
   ```bash
   tar xzf stellarium-srtstreamer-linux64.tar.gz -C ~/.stellarium/modules/
   ```
3. Make sure FFmpeg with H.264 encoding support is installed:
   ```bash
   # Ubuntu/Debian
   sudo apt install libavcodec-extra
   # Fedora
   sudo dnf install ffmpeg-libs
   ```
4. Launch Stellarium and enable the plugin in Configuration > Plugins > SRT Video Streamer.

## Usage

1. **Enable the plugin:** Configuration > Plugins > SRT Video Streamer > Load at startup, then restart Stellarium.
2. **Open settings** via the SRT toolbar button in the bottom bar, or press `Ctrl+Shift+R`.
3. **Configure:**
   - SRT URL (default: `srt://127.0.0.1:9000`)
   - Mode: Caller (connect out) or Listener (wait for connections)
   - Encoder: libx264 / h264_nvenc / h264_vaapi
   - Bitrate (default: 6000 kbps), frame rate, resolution
4. **Start streaming** by clicking the toolbar button, or press `Ctrl+Shift+S`.

### Keyboard shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+Shift+S` | Toggle streaming on/off |
| `Ctrl+Shift+R` | Open SRT Streamer settings |

### Toolbar button

The plugin adds an **SRT** icon to Stellarium's bottom toolbar:
- **Left-click** toggles streaming on/off
- **Right-click** opens the configuration dialog

### Status indicator

A status text appears in the top-left corner of the window (not visible in the stream):
- **SRT: Connecting...** (orange) — waiting for SRT connection
- **SRT: Streaming** (green) — actively streaming

The config dialog also shows the current status with error messages when a connection fails or times out.

## Receiving the stream

Start a listener on the receiving side **before** clicking Start (when using the default Caller mode):

```bash
ffplay -fflags nobuffer srt://0.0.0.0:9000?mode=listener
vlc srt://0.0.0.0:9000?mode=listener
mpv srt://0.0.0.0:9000?mode=listener
```

**OBS Studio:** Add Media Source > uncheck Local File > enter `srt://0.0.0.0:9000?mode=listener`

---

## Building from source

<details>
<summary><b>Static plugin (Windows & Linux)</b></summary>

Static plugins are compiled directly into `stellarium.exe`. This is the recommended approach and the only option on Windows.

### Requirements

- Stellarium source tree (25.x+)
- Qt 6 (matching the Stellarium build)
- FFmpeg development libraries (`libavcodec`, `libavformat`, `libavutil`, `libswscale`) with SRT support
- CMake 3.16+
- A C++ compiler matching your Stellarium build (MSVC on Windows, GCC on Linux)

### Steps

**1.** Clone this repo into the Stellarium plugins directory:
```bash
cd stellarium/plugins
git clone https://github.com/ozgur-as/stellarium-srtstreamer-plugin.git SrtStreamer
```

**2.** Add the plugin to Stellarium's `CMakeLists.txt`, alongside the other `ADD_PLUGIN` lines:
```cmake
ADD_PLUGIN(SrtStreamer 1)
```

**3.** Add `Q_IMPORT_PLUGIN` to Stellarium's `src/core/StelApp.cpp`:
```cpp
#ifdef USE_STATIC_PLUGIN_SRTSTREAMER
Q_IMPORT_PLUGIN(SrtStreamerPluginInterface)
#endif
```

**4.** Configure and build:

**Windows (MSVC + Ninja):**
```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd stellarium
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH=D:/Qt/6.8.3/msvc2022_64 ^
  -DFFMPEG_ROOT=C:/ffmpeg ^
  -DUSE_PLUGIN_SRTSTREAMER=1
ninja
```

**Linux:**
```bash
cd stellarium
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DFFMPEG_ROOT=/path/to/ffmpeg \
  -DUSE_PLUGIN_SRTSTREAMER=1
make -j$(nproc)
```

</details>

<details>
<summary><b>Dynamic plugin (Linux only)</b></summary>

```bash
export STELROOT=/path/to/stellarium
cd stellarium-srtstreamer-plugin
mkdir build && cd build
cmake .. -DSTELROOT=$STELROOT
make -j$(nproc)
make install
```

Installs `libSrtStreamer.so` to `~/.stellarium/modules/SrtStreamer/`

</details>

## Version history

See [ChangeLog](ChangeLog) for details.

## License

[GNU General Public License v2](COPYING) or later.
