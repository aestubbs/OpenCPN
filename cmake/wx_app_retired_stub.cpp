/*
 * P3.11 wx-app retirement stub.
 *
 * The legacy wxWidgets application (the former gui/src + gui/include trees)
 * has been removed from the build. The top-level CMakeLists.txt still defines
 * the `OpenCPN` target so the ~50 scattered
 * `target_link_libraries(${PACKAGE_NAME} ...)` calls and the `_opencpn`
 * plugin-link alias remain valid, but the target is EXCLUDE_FROM_ALL and is
 * never built in a default (Qt) build — so this stub is the only translation
 * unit it would compile.
 *
 * Restoring the wx app (OCPN_BUILD_WX_APP=ON) is no longer supported: the
 * sources are gone. This file exists purely to keep the target well-formed
 * during the remainder of the Qt migration; the target itself is removed in a
 * later cleanup pass.
 */
int main() { return 0; }
