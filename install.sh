#!/bin/sh
# Baut auf der pfSense ein echtes pkg-Paket und installiert es.
# Aufruf als root auf der pfSense:  sh install.sh
#
# Nur bauen (z. B. fuer Release-Pakete auf einem FreeBSD-System):
#   sh install.sh --build <ABI> <Ausgabedatei>
#   sh install.sh --build FreeBSD:15:amd64 dist/pfSense-2.8.1-pkg-ShaperClassGen.pkg
set -e

BUILD_ONLY=""
if [ "$1" = "--build" ]; then
	BUILD_ONLY=1
	ABI="$2"
	OUT="$3"
	if [ -z "${ABI}" ] || [ -z "${OUT}" ]; then
		echo "Usage: sh install.sh --build <ABI> <output file>" >&2
		exit 1
	fi
fi

NAME="pfSense-pkg-ShaperClassGen"
VERSION="0.6.11"
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d /tmp/scgbuild.XXXXXX)
ROOT="${WORK}/root"

mkdir -p "${ROOT}"
cp -Rp "${HERE}/files/." "${ROOT}/"
sed -i '' "s/%%PKGVERSION%%/${VERSION}/g" \
	"${ROOT}/usr/local/pkg/shaperclassgen.xml" \
	"${ROOT}/usr/local/share/${NAME}/info.xml"
chmod 644 $(find "${ROOT}" -type f)
chmod 755 "${ROOT}/usr/local/etc/rc.d/scg_autorate.sh"

[ -n "${ABI}" ] || ABI=$(pkg config abi)

cat > "${WORK}/+MANIFEST" <<MANIFEST
name: "${NAME}"
version: "${VERSION}"
origin: "net/${NAME}"
comment: "Limiter toolkit: class generator, Autorate, live view, speed test"
desc: "Creates traffic classes as limiter child queues with floating match rules, adapts limiter bandwidth to the line by latency (Autorate), shows limiters live and runs a periodic speed test."
maintainer: "local@localhost"
www: "https://github.com/betterbitsbites/pfsense-limiter-toolkit"
prefix: "/usr/local"
abi: "${ABI}"
categories: [ "net" ]
licenselogic: "single"
licenses: [ "APACHE20" ]
scripts: {
  post-install: "/usr/local/bin/php -f /etc/rc.packages ${NAME} POST-INSTALL",
  pre-deinstall: "/usr/local/bin/php -f /etc/rc.packages ${NAME} DEINSTALL"
}
MANIFEST

( cd "${ROOT}" && find . -type f | sed 's|^\.||' ) > "${WORK}/plist"

OUTDIR="${HERE}"
[ -z "${BUILD_ONLY}" ] || OUTDIR="${WORK}/out"
mkdir -p "${OUTDIR}"
pkg create -M "${WORK}/+MANIFEST" -p "${WORK}/plist" -r "${ROOT}" -o "${OUTDIR}"
PKGFILE=$(ls -1t "${OUTDIR}/${NAME}-${VERSION}".* | head -1)

if [ -n "${BUILD_ONLY}" ]; then
	mkdir -p "$(dirname "${OUT}")"
	cp "${PKGFILE}" "${OUT}"
	rm -rf "${WORK}"
	echo "Built ${OUT} (${ABI})"
	exit 0
fi

if pkg info -e "${NAME}"; then
	echo "Entferne vorhandene Version ..."
	pkg delete -y "${NAME}"
fi
pkg add "${PKGFILE}"
rm -rf "${WORK}"

echo
echo "Fertig. Paket: ${PKGFILE}"
echo "GUI: Firewall > Shaper Klassen-Generator  (bzw. Tab im Traffic Shaper)"
