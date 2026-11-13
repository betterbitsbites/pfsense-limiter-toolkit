#!/bin/sh
# Baut auf der pfSense ein echtes pkg-Paket und installiert es.
# Aufruf als root auf der pfSense:  sh install.sh
set -e

NAME="pfSense-pkg-ShaperClassGen"
VERSION="0.6.0"
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d /tmp/scgbuild.XXXXXX)
ROOT="${WORK}/root"

mkdir -p "${ROOT}"
cp -Rp "${HERE}/files/" "${ROOT}/"
sed -i '' "s/%%PKGVERSION%%/${VERSION}/g" \
	"${ROOT}/usr/local/pkg/shaperclassgen.xml" \
	"${ROOT}/usr/local/share/${NAME}/info.xml"
chmod 644 $(find "${ROOT}" -type f)
chmod 755 "${ROOT}/usr/local/etc/rc.d/scg_autorate.sh"

ABI=$(pkg config abi)

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

pkg create -M "${WORK}/+MANIFEST" -p "${WORK}/plist" -r "${ROOT}" -o "${HERE}"
PKGFILE=$(ls -1t "${HERE}/${NAME}-${VERSION}".* | head -1)

if pkg info -e "${NAME}"; then
	echo "Entferne vorhandene Version ..."
	pkg delete -y "${NAME}"
fi
pkg add "${PKGFILE}"
rm -rf "${WORK}"

echo
echo "Fertig. Paket: ${PKGFILE}"
echo "GUI: Firewall > Shaper Klassen-Generator  (bzw. Tab im Traffic Shaper)"
