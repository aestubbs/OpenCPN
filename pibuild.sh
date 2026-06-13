#!/usr/bin/env bash
#
# pibuild.sh -- build opencpn-qt (the Qt/QtQuick edition of OpenCPN) on a
# Raspberry Pi (64-bit Raspberry Pi OS Bookworm, Pi 4/5, aarch64).
#
# It is idempotent: re-running skips an already-installed Qt and re-uses the
# existing build/ directory (incremental). Steps:
#   1. install the system build dependencies (apt)
#   2. install Qt (prebuilt arm64 binaries via aqtinstall) if not present
#   3. configure + build the opencpn-qt target
#
# The one reason we don't use the distro's apt Qt: Raspberry Pi OS Bookworm
# ships Qt 6.4, but the app needs 6.5+ (QQmlApplicationEngine::loadFromModule).
# We track Qt 6.11 to match the macOS dev build. The native `wayland` QPA
# plugin ships in the base Qt install (qtwayland is a base archive), so no
# extra module is needed for Bookworm's default Wayland session.
#
# See Docs/QT_RASPBERRY_PI_BUILD.md for the full write-up (display stack,
# performance tuning, o-charts on ARM).
#
# Override any of these via the environment, e.g.:
#   QT_VERSION=6.12.0 JOBS=2 ./pibuild.sh
#
set -euo pipefail

# ---- configuration (env-overridable) --------------------------------------
QT_VERSION="${QT_VERSION:-6.11.1}"
QT_HOST="${QT_HOST:-linux_arm64}"
QT_ARCH="${QT_ARCH:-linux_gcc_arm64}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
AQT_VENV="${AQT_VENV:-$HOME/.aqt-venv}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
BUILD_DIR="${BUILD_DIR:-build}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QT_PREFIX="$QT_ROOT/$QT_VERSION/gcc_arm64"

log()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mwarning: %s\033[0m\n' "$*" >&2; }
die()  { printf '\033[1;31merror: %s\033[0m\n'   "$*" >&2; exit 1; }

# ---- 0. sanity -------------------------------------------------------------
arch="$(uname -m)"
if [ "$arch" != "aarch64" ] && [ "$arch" != "arm64" ]; then
  warn "architecture is '$arch', not aarch64 -- this script targets 64-bit"
  warn "Raspberry Pi OS. 32-bit (armhf) is unsupported; carrying on anyway."
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
  else die "not root and sudo not found -- needed to install apt packages"; fi
fi

# ---- 1. system dependencies (apt) -----------------------------------------
log "Installing system build dependencies (apt)"
if ! $SUDO apt-get update; then
  warn "apt-get update failed -- this is almost always a broken THIRD-PARTY"
  warn "apt repo unrelated to OpenCPN (e.g. an old NodeSource 'node_20.x'"
  warn "list whose Release file was retired). Find and remove the stale source:"
  warn "  grep -rl nodesource /etc/apt/sources.list /etc/apt/sources.list.d/"
  warn "  sudo rm /etc/apt/sources.list.d/nodesource.list   # if that's the one"
  warn "then re-run ./pibuild.sh. (Refusing to build against a stale package index.)"
  exit 1
fi
$SUDO apt-get install -y --no-install-recommends \
  cmake ninja-build g++ git python3 python3-venv python3-pip \
  libwxgtk3.2-dev libgdal-dev libarchive-dev libglew-dev \
  libgl1-mesa-dev libglu1-mesa-dev libgles2-mesa-dev \
  libcurl4-openssl-dev libssl-dev liblz4-dev libzstd-dev \
  libmpg123-dev libmp3lame-dev libsndfile1-dev libexif-dev \
  libusb-1.0-0-dev libudev-dev \
  libxkbcommon-dev libxcb-cursor0 mesa-utils

# ---- 2. Qt (prebuilt arm64 via aqtinstall) --------------------------------
if [ -x "$QT_PREFIX/bin/qmake" ]; then
  log "Qt $QT_VERSION already present at $QT_PREFIX -- skipping install"
else
  log "Installing Qt $QT_VERSION ($QT_ARCH) into $QT_ROOT via aqtinstall"
  # A venv keeps aqt off the system Python (Bookworm's pip is PEP 668
  # externally-managed and refuses a bare --user install).
  if [ ! -x "$AQT_VENV/bin/aqt" ]; then
    python3 -m venv "$AQT_VENV"
    "$AQT_VENV/bin/pip" install --upgrade pip aqtinstall
  fi
  "$AQT_VENV/bin/aqt" install-qt "$QT_HOST" desktop "$QT_VERSION" "$QT_ARCH" \
    -m qtserialport qtwebsockets qtshadertools qtmultimedia \
    -O "$QT_ROOT"
  [ -x "$QT_PREFIX/bin/qmake" ] || \
    die "Qt install finished but $QT_PREFIX/bin/qmake is missing"
fi

export CMAKE_PREFIX_PATH="$QT_PREFIX"
export PATH="$QT_PREFIX/bin:$PATH"

# ---- 3. submodules ---------------------------------------------------------
if [ -e "$REPO_ROOT/.git" ]; then
  log "Syncing git submodules"
  git -C "$REPO_ROOT" submodule update --init --recursive
else
  warn "no .git here -- skipping submodule sync (assuming a complete source tree)"
fi

# ---- 4. parallelism (avoid OOM on low-RAM Pis) ----------------------------
ncpu="$(nproc)"
memkb="$(awk '/MemTotal/{print $2}' /proc/meminfo 2>/dev/null || echo 0)"
memgb=$(( memkb / 1024 / 1024 ))
jobs="${JOBS:-$ncpu}"
if [ -z "${JOBS:-}" ] && [ "$memgb" -ge 1 ] && [ "$memgb" -lt 8 ]; then
  # C++ TUs here are memory-hungry; roughly one job per ~1.5 GB keeps a
  # 2-4 GB Pi clear of the OOM killer. Override with JOBS= to force.
  cap=$(( memgb * 2 / 3 )); [ "$cap" -lt 1 ] && cap=1
  if [ "$jobs" -gt "$cap" ]; then
    warn "only ${memgb} GB RAM -- limiting to -j$cap (set JOBS= to override)"
    jobs="$cap"
  fi
fi

# ---- 5. configure + build --------------------------------------------------
log "Configuring (CMAKE_PREFIX_PATH=$QT_PREFIX)"
cmake -S "$REPO_ROOT" -B "$REPO_ROOT/$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX"

log "Building opencpn-qt (-j$jobs) -- the bundled chart libs make the first"
log "build slow on a Pi; later builds are incremental"
cmake --build "$REPO_ROOT/$BUILD_DIR" --target opencpn-qt -j"$jobs"

# ---- done ------------------------------------------------------------------
bin="$REPO_ROOT/$BUILD_DIR/gui/qt/opencpn-qt"
[ -x "$bin" ] || bin="$(find "$REPO_ROOT/$BUILD_DIR" -name opencpn-qt -type f -perm -u+x 2>/dev/null | head -1)"

log "Build complete: ${bin:-(binary not found -- check the build output above)}"
cat <<EOF

Run it (Raspberry Pi OS Bookworm defaults to a Wayland session):

  export LD_LIBRARY_PATH="$QT_PREFIX/lib:\${LD_LIBRARY_PATH:-}"
  QT_QPA_PLATFORM=wayland $bin     # native Wayland (preferred on Bookworm)
  QT_QPA_PLATFORM=xcb     $bin     # X11 / XWayland fallback
  QT_QPA_PLATFORM=eglfs   $bin     # kiosk: direct V3D/KMS, no compositor

Tuning:
  OCPN_QT_MSAA=1   disable multisampling if panning feels heavy on V3D
  QSG_INFO=1       log the chosen RHI backend (expect OpenGL on V3D)
EOF
