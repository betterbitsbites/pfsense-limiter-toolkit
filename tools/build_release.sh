#!/bin/sh
# Builds the release assets into dist/ (run on FreeBSD, e.g. in CI):
#  - one package per pfSense version listed in tools/release_targets.txt
#  - pfSense-pkg-ShaperClassGen-<version>.tar.gz (sources for install.sh)
set -e

HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "${HERE}"
NAME="pfSense-pkg-ShaperClassGen"
VERSION=$(sed -n 's/^VERSION="\(.*\)"$/\1/p' install.sh)
DIST="${HERE}/dist"
rm -rf "${DIST}"
mkdir -p "${DIST}"

grep -v '^[[:space:]]*#' tools/release_targets.txt | while read -r PFV ABI; do
	[ -n "${PFV}" ] || continue
	sh install.sh --build "${ABI}" "${DIST}/pfSense-${PFV}-pkg-ShaperClassGen.pkg"
done

SRC="${NAME}-${VERSION}"
TMP=$(mktemp -d /tmp/scgsrc.XXXXXX)
mkdir -p "${TMP}/${SRC}"
cp -Rp files install.sh uninstall.sh README.md README.de.md LICENSE "${TMP}/${SRC}/"
tar -C "${TMP}" -czf "${DIST}/${SRC}.tar.gz" "${SRC}"
rm -rf "${TMP}"

(
	cd "${DIST}"
	if command -v sha256 >/dev/null 2>&1; then sha256 -r -- *; else sha256sum -- *; fi > "${HERE}/.SHA256SUMS"
)
mv "${HERE}/.SHA256SUMS" "${DIST}/SHA256SUMS"
ls -l "${DIST}"
