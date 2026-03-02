#!/usr/bin/env bash
# build_deb.sh — build a Debian (.deb) package for Notepad++ Linux
# Usage: ./packaging/build_deb.sh
#
# Prerequisites:
#   dpkg-deb (part of the dpkg package on Debian/Ubuntu)
#   The application must already be built (run build.sh first)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LINUX_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${LINUX_DIR}/build"
VERSION="8.7.5"
ARCH="amd64"
PKG_NAME="notepad++"
PKGDIR="${LINUX_DIR}/${PKG_NAME}_${VERSION}_${ARCH}"

# ---- Check prerequisites ----
if ! command -v dpkg-deb &>/dev/null; then
    echo "ERROR: dpkg-deb not found. Install with: sudo apt install dpkg"
    exit 1
fi
if [[ ! -f "${BUILD_DIR}/notepad++" ]]; then
    echo "ERROR: ${BUILD_DIR}/notepad++ not found. Run build.sh first."
    exit 1
fi

# ---- Populate staging tree ----
echo "==> Building staging directory ${PKGDIR}..."
rm -rf "${PKGDIR}"
mkdir -p "${PKGDIR}/DEBIAN"
mkdir -p "${PKGDIR}/usr/bin"
mkdir -p "${PKGDIR}/usr/lib/notepad++/plugins"
mkdir -p "${PKGDIR}/usr/share/applications"
mkdir -p "${PKGDIR}/usr/share/man/man1"
mkdir -p "${PKGDIR}/usr/share/doc/notepad++"

# Binary
install -m 0755 "${BUILD_DIR}/notepad++" "${PKGDIR}/usr/bin/"

# Plugins
if ls "${BUILD_DIR}/plugins/"*.so 2>/dev/null | head -1 | grep -q .; then
    install -m 0644 "${BUILD_DIR}/plugins/"*.so \
        "${PKGDIR}/usr/lib/notepad++/plugins/"
fi

# Desktop integration
install -m 0644 "${LINUX_DIR}/resources/notepad++.desktop" \
    "${PKGDIR}/usr/share/applications/"

# Man page
gzip -9 -c "${LINUX_DIR}/resources/notepad++.1" \
    > "${PKGDIR}/usr/share/man/man1/notepad++.1.gz"

# Copyright / changelog
cat > "${PKGDIR}/usr/share/doc/notepad++/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: notepad++
Upstream-Contact: notepad-plus-plus@github.com
Source: https://github.com/notepad-plus-plus/notepad-plus-plus

Files: *
Copyright: 2003-$(date +%Y) Don Ho and contributors
License: GPL-2+
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
EOF

gzip -9 -c /dev/stdin > "${PKGDIR}/usr/share/doc/notepad++/changelog.gz" <<CHANGE
notepad++ (${VERSION}) unstable; urgency=low
  * Linux GTK3 port Phase 6: Preferences dialog, session management,
    Zenburn theme, AppImage and Debian packaging.
 -- Notepad++ Team <notepad-plus-plus@github.com>  $(date -R)
CHANGE

# ---- DEBIAN/control ----
cat > "${PKGDIR}/DEBIAN/control" <<EOF
Package: notepad++
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: Notepad++ Team <notepad-plus-plus@github.com>
Installed-Size: $(du -sk "${PKGDIR}/usr" | cut -f1)
Depends: libgtk-3-0 (>= 3.20), libglib2.0-0 (>= 2.44), libstdc++6
Recommends: fonts-dejavu-core
Section: editors
Priority: optional
Homepage: https://notepad-plus-plus.org
Description: Powerful GTK3 text editor (Linux port of Notepad++)
 Notepad++ is a feature-rich text editor that supports syntax highlighting
 for 30+ programming languages, tabbed editing, find-in-files, macro
 recording, a plugin system, and much more.
 .
 This package is the Linux/GTK3 community port.
EOF

# ---- DEBIAN/postinst ----
cat > "${PKGDIR}/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database &>/dev/null; then
    update-desktop-database -q
fi
if command -v mandb &>/dev/null; then
    mandb -q 2>/dev/null || true
fi
EOF
chmod 0755 "${PKGDIR}/DEBIAN/postinst"

# ---- Build .deb ----
OUTPUT="${LINUX_DIR}/${PKG_NAME}_${VERSION}_${ARCH}.deb"
echo "==> Building ${OUTPUT}..."
dpkg-deb --build --root-owner-group "${PKGDIR}" "${OUTPUT}"
rm -rf "${PKGDIR}"

echo ""
echo "Done: ${OUTPUT}"
echo "Install with: sudo dpkg -i $(basename "${OUTPUT}")"
echo "Remove  with: sudo apt remove notepad++"
