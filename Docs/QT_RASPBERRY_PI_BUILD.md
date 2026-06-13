# Building & running opencpn-qt on Raspberry Pi

The Qt/QtQuick edition (`opencpn-qt`) builds and runs on a Raspberry Pi.
A Pi is **aarch64 Linux** rendering through **OpenGL ES** (Mesa's V3D
driver on the Pi 4/5 VideoCore), which is just another RHI backend for the
Qt Quick scene graph — the same code path as desktop GL/Metal, with the
GLES shader variants selected at run time.

## Why it "just works"

The migration code carries no x86 assumptions, verified against the tree:

- **Shaders** — the custom `.qsb` (aaline / coastshade / areapattern) are
  baked with a **`GLSL 100 es`** variant (alongside SPIR-V / GLSL 120/150 /
  HLSL / MSL), so the GLES backend has a shader to load. `qsb -d` on any
  baked shader shows the `es` variant.
- **No x86 intrinsics** anywhere in `gui/qt/`, `libs/s52plib/`, or
  `libs/s57-charts/` (no SSE/AVX/`_mm_*`).
- **No forced desktop-GL profile** — `main.cpp` only sets MSAA samples on
  `QSurfaceFormat::defaultFormat()`; it never requests a core profile or a
  specific GL version, so RHI auto-selects GLES on the Pi.
- **Fusion** QtQuick Controls style on Linux (consistent chrome without a
  platform theme dependency).
- All system dependencies (wxWidgets 3.2, GDAL, libarchive, OpenSSL,
  libsndfile, …) are in Raspberry Pi OS / Ubuntu arm64 apt.

CI builds the identical aarch64 target on GitHub's free `ubuntu-24.04-arm`
runner (`build-linux-arm64` in `.github/workflows/opencpn-qt.yml`). A green
run proves the **compile + link**; the **GLES runtime** can only be
verified on real hardware (CI has no GPU).

## Target

- Raspberry Pi **4 or 5**, 64-bit **Raspberry Pi OS (Bookworm)** or Ubuntu
  arm64. (A Pi 5's V3D supports GLES 3.1; the renderer's floor is GLES 2.0.)
- Pi 3 / 32-bit (armhf) is untested and not recommended — too little GPU
  headroom for the vector scene graph.

## 1. Qt 6.5+ (the one real prerequisite)

The app uses `QQmlApplicationEngine::loadFromModule()` (Qt 6.5). Raspberry
Pi OS Bookworm's apt Qt is **6.4**, which is too old, so install a newer Qt
rather than the distro packages. We track **Qt 6.11** to match the macOS
dev build. Easiest is `aqtinstall`, which pulls the official prebuilt
**linux_arm64** desktop binaries (Qt ships these from 6.7+; arm64 is
available up to 6.12) — the same path CI uses:

```bash
python3 -m pip install --user aqtinstall
# host=linux_arm64  target=desktop  arch=linux_gcc_arm64
# The native `wayland` QPA plugin is part of the BASE install (qtwayland is
# a base archive, not an add-on module), so it needs no -m entry.
aqt install-qt linux_arm64 desktop 6.11.1 linux_gcc_arm64 \
    -m qtserialport qtwebsockets qtshadertools qtmultimedia \
    -O "$HOME/Qt"
export CMAKE_PREFIX_PATH="$HOME/Qt/6.11.1/gcc_arm64"
export PATH="$CMAKE_PREFIX_PATH/bin:$PATH"
```

Alternatives: build Qt 6.8 from source on the Pi (hours, but fully native),
or cross-compile from an x86 host with a Pi sysroot (fast, advanced).
Building against the distro **6.4** is *not* supported as-is (it needs a
`loadFromModule` fallback that has not been written/verified).

## 2. System dependencies (apt)

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  cmake ninja-build g++ git \
  libwxgtk3.2-dev libgdal-dev libarchive-dev libglew-dev \
  libgl1-mesa-dev libglu1-mesa-dev libgles2-mesa-dev \
  libgtk-3-dev libcurl4-openssl-dev libssl-dev liblz4-dev libzstd-dev \
  libmpg123-dev libmp3lame-dev libsndfile1-dev libexif-dev \
  libusb-1.0-0-dev libudev-dev gettext \
  libxkbcommon-dev libxcb-cursor0 mesa-utils
```

## 3. Build

```bash
git clone --recurse-submodules https://github.com/aestubbs/OpenCPN.git
cd OpenCPN
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"
cmake --build build --target opencpn-qt -j"$(nproc)"
```

Binary: `build/gui/qt/opencpn-qt`.

## 4. Running — display stack

Raspberry Pi OS Bookworm **defaults to Wayland** (the `labwc` compositor on
Pi 5, `wayfire` on Pi 4), so Wayland is the primary path, not a fallback.
Qt picks the platform plugin automatically, but you can pin it:

- **Native Wayland (preferred on Bookworm)** — the `wayland` QPA plugin
  ships in the base Qt install (qtwayland is a base archive), so it is
  always available:
  ```bash
  QT_QPA_PLATFORM=wayland ./build/gui/qt/opencpn-qt
  ```
- **XWayland (fallback)** — the X11 (`xcb`) plugin runs transparently under
  the compositor's X compatibility layer:
  ```bash
  QT_QPA_PLATFORM=xcb ./build/gui/qt/opencpn-qt
  ```
- **eglfs (kiosk / appliance, no desktop)** — drive the V3D GPU directly via
  KMS/DRM with no compositor; ideal for a dedicated chartplotter that boots
  straight into OpenCPN:
  ```bash
  QT_QPA_PLATFORM=eglfs ./build/gui/qt/opencpn-qt
  ```

If startup is blank or falls back to software, force the GL RHI backend and
check the V3D driver is active (`glxinfo -B` / `eglinfo` should report
"V3D"):
```bash
QSG_RHI_BACKEND=opengl QSG_INFO=1 ./build/gui/qt/opencpn-qt
```

## 5. Performance tuning on V3D

- **MSAA** — default is 2× (`main.cpp`). V3D multisample resolve is the
  most expensive per-frame cost on a Pi; drop it if pans feel heavy:
  ```bash
  OCPN_QT_MSAA=1 ./build/gui/qt/opencpn-qt   # off; 2 = default; 4/8 = prettier
  ```
- The scene-graph perf instrumentation works the same as on desktop:
  `QSG_RENDER_TIMING=1`, `OCPN_QT_SG_STATS=1`, `OCPN_QT_PAN_TEST=<s>`.
- Persisted viewport, tide data, and config behave identically to desktop.

## 6. o-charts (encrypted) on ARM

ENC viewing of **NOAA `.000`** and other unencrypted vector charts needs
nothing extra. The **o-charts** encrypted sets are decrypted by `oexserverd`,
a closed prebuilt daemon — the copy bundled/installed for macOS/x86 will not
run on ARM. To use o-charts on a Pi you need the **ARM build of
`oexserverd`** from o-charts' Raspberry Pi distribution of `o-charts_pi`;
point the service at it (it is discovered the same way — see
`gui/qt/ocharts_service.cpp::daemonPath()`). Login to the o-charts shop is a
plain network call and works regardless of CPU.
