#!/usr/bin/env bash
# build_appimage.sh — build a portable AppImage for Notepad++ Linux
# Usage: ./packaging/build_appimage.sh [--clean]
#
# Prerequisites:
#   appimagetool-x86_64.AppImage in PATH or in the repo root
#   The application must already be built (run build.sh first)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${LINUX_DIR}/build"
APPDIR="${LINUX_DIR}/AppDir"

# ---- Locate appimagetool ----
APPIMAGETOOL=""
for candidate in \
    "$(command -v appimagetool 2>/dev/null || true)" \
    "${LINUX_DIR}/../appimagetool-x86_64.AppImage" \
    "${LINUX_DIR}/appimagetool-x86_64.AppImage"; do
    if [[ -x "${candidate}" ]]; then
        APPIMAGETOOL="${candidate}"
        break
    fi
done
if [[ -z "${APPIMAGETOOL}" ]]; then
    echo "ERROR: appimagetool not found. Download it from:"
    echo "  https://github.com/AppImage/AppImageKit/releases/latest"
    exit 1
fi

# ---- Check that the binary exists ----
if [[ ! -f "${BUILD_DIR}/notepad++" ]]; then
    echo "ERROR: ${BUILD_DIR}/notepad++ not found. Run build.sh first."
    exit 1
fi

# ---- Clean previous AppDir ----
if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "${APPDIR}"
fi

# ---- Populate AppDir ----
echo "==> Populating AppDir..."
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib/notepad++/plugins"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/man/man1"

cp "${BUILD_DIR}/notepad++" "${APPDIR}/usr/bin/"

# Copy plugins if any were built
if ls "${BUILD_DIR}/plugins/"*.so 2>/dev/null | head -1 | grep -q .; then
    cp "${BUILD_DIR}/plugins/"*.so "${APPDIR}/usr/lib/notepad++/plugins/"
fi

# Desktop file
cp "${LINUX_DIR}/resources/notepad++.desktop" "${APPDIR}/usr/share/applications/"
cp "${LINUX_DIR}/resources/notepad++.desktop" "${APPDIR}/"

# Man page
cp "${LINUX_DIR}/resources/notepad++.1"       "${APPDIR}/usr/share/man/man1/"

# Icon — use a placeholder if no icon file is present
ICON_SRC="${LINUX_DIR}/resources/notepad++.png"
if [[ -f "${ICON_SRC}" ]]; then
    cp "${ICON_SRC}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/notepad++.png"
    cp "${ICON_SRC}" "${APPDIR}/notepad++.png"
else
    # Create a minimal 1x1 placeholder so appimagetool does not abort
    if command -v convert &>/dev/null; then
        convert -size 256x256 xc:#3c6eb4 \
                -fill white -font DejaVu-Sans-Bold -pointsize 48 \
                -gravity Center -annotate 0 "N++" \
                "${APPDIR}/notepad++.png"
        cp "${APPDIR}/notepad++.png" \
           "${APPDIR}/usr/share/icons/hicolor/256x256/apps/notepad++.png"
    else
        echo "WARNING: No icon file found and 'convert' (ImageMagick) is not available."
        echo "         The AppImage will be created without an icon."
        touch "${APPDIR}/notepad++.png"
    fi
fi

# AppRun entry point
cat > "${APPDIR}/AppRun" <<'EOF'
#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
export NPP_PLUGINS_DIR="${HERE}/usr/lib/notepad++/plugins"
exec "${HERE}/usr/bin/notepad++" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# ---- Build AppImage ----
OUTPUT="${LINUX_DIR}/notepad++-linux-x86_64.AppImage"
echo "==> Building AppImage -> ${OUTPUT}"
ARCH=x86_64 "${APPIMAGETOOL}" "${APPDIR}" "${OUTPUT}"

echo ""
echo "Done: ${OUTPUT}"
echo "Run with: ./$(basename "${OUTPUT}")"
