#!/bin/bash
# Builds tests/.work: test copies of the package with paths pointing to stubs and fakes.
# Usage: tests/setup.sh   (run again after every code change)
set -e
T=$(cd "$(dirname "$0")" && pwd)
F=$T/../files
W=$T/.work
mkdir -p "$W/run" "$W/db"
cp "$T"/stubs/* "$W/"
cp -r "$T"/fakes/* "$W/"
R="s#is_readable(\"/usr/local/pkg/{\$scg_f}\")#true#; s#/usr/local/pkg/shaperclassgen_i18n.inc#$W/scg_i18n.inc#; s#/usr/local/pkg/shaperclassgen_autorate.inc#$W/scg_ar.inc#; s#/usr/local/pkg/shaperclassgen_speedtest.inc#$W/scg_st.inc#; s#/usr/local/pkg/shaperclassgen.inc#$W/scg.inc#; s#/sbin/dnctl#$W/fakednctl#g; s#/sbin/sysctl#$W/fakesysctl#g; s#/var/run/#$W/run/#g; s#/var/db/#$W/db/#g; s#/var/tmp/#$W/run/#g; s#/usr/local/bin/curl#$W/fakecurl#; s#'/sbin/ping'#'$W/fakeping'#; s#/sbin/route#$W/fakeroute#; s#/usr/local/pkg/shaperclassgen_speedtest.php#$W/st_cli.php#; s#/conf/shaperclassgen.json#$W/run/backup.json#"
sed "$R" "$F/usr/local/pkg/shaperclassgen.inc" > "$W/scg.inc"
sed "$R" "$F/usr/local/pkg/shaperclassgen_autorate.inc" > "$W/scg_ar.inc"
sed "$R" "$F/usr/local/pkg/shaperclassgen_speedtest.inc" > "$W/scg_st.inc"
sed "$R" "$F/usr/local/pkg/shaperclassgen_i18n.inc" > "$W/scg_i18n.inc"
sed "$R" "$F/usr/local/pkg/shaperclassgen_speedtest.php" | sed "2i require_once(\"$W/guiconfig.inc\");" > "$W/st_cli.php"
for p in firewall_shaper_classgen.php firewall_shaper_autorate.php status_limiters_live.php status_speedtest.php; do
	sed "$R" "$F/usr/local/www/$p" > "$W/$p"
done
echo "Test copies built in $W"
