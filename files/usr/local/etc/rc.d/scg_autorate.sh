#!/bin/sh
# ShaperClassGen Autorate - passt Limiter an die verfuegbare Bandbreite an
PIDF=/var/run/scg_autorate.pid
PHP=/usr/local/bin/php
SCRIPT=/usr/local/pkg/shaperclassgen_autorate.php

running() {
	[ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null
}

start() {
	if running; then
		echo "scg_autorate laeuft bereits"
		return 0
	fi
	if ! "$PHP" -f "$SCRIPT" enabled; then
		echo "scg_autorate ist deaktiviert"
		return 0
	fi
	# -f: stdin/stdout/stderr nach /dev/null, damit der Aufrufer (Weboberflaeche) nicht haengt
	/usr/sbin/daemon -f -r -R 5 -P "$PIDF" -t scg_autorate "$PHP" -f "$SCRIPT" run
	echo "scg_autorate gestartet"
}

stop() {
	if running; then
		kill "$(cat "$PIDF")" 2>/dev/null
		i=0
		while running && [ $i -lt 20 ]; do sleep 0.2; i=$((i+1)); done
	fi
	rm -f "$PIDF"
	"$PHP" -f "$SCRIPT" restore
	echo "scg_autorate gestoppt, Limiter-Bandbreiten wiederhergestellt"
}

case "$1" in
	start) start ;;
	stop) stop ;;
	restart) stop; start ;;
	status) if running; then echo "laeuft (PID $(cat "$PIDF"))"; else echo "gestoppt"; exit 1; fi ;;
	*) echo "Aufruf: $0 {start|stop|restart|status}"; exit 1 ;;
esac
