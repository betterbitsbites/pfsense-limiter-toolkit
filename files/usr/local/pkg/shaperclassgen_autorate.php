<?php
/*
 * shaperclassgen_autorate.php - Autorate-Dienst
 * Aufruf: php -f shaperclassgen_autorate.php [run|restore|enabled|select [list]|probe <ip> [ts|echo] [n] [ms]]
 * Licensed under the Apache License, Version 2.0
 */
/* Waehrend eines Paket-Updates fehlen die Bibliotheken kurz: dann still beenden */
foreach (array('shaperclassgen.inc', 'shaperclassgen_i18n.inc', 'shaperclassgen_autorate.inc', 'shaperclassgen_speedtest.inc') as $scg_f) {
	if (!is_readable("/usr/local/pkg/{$scg_f}")) {
		exit(0);
	}
}
require_once("/usr/local/pkg/shaperclassgen_autorate.inc");

set_time_limit(0);
$cmd = $argv[1] ?? 'run';

switch ($cmd) {
	case 'enabled':
		/* Exit-Code 0 = aktiviert (fuer das rc-Skript) */
		$a = scg_ar_settings();
		/* Dienst laeuft bei Regelung oder reiner Latenzmessung */
		exit((empty($a['enable']) && empty($a['monitor'])) ? 1 : 0);
	case 'select':
		/* ICMP-Timestamp-Reflektoren auswaehlen ("select list" = Liste neu laden) */
		$r = scg_ts_select(($argv[2] ?? '') === 'list');
		if ($r === null) {
			echo "selection already running\n";
			exit(0);
		}
		echo "country " . ($r['country'] ?: '-') . " (" . ($r['src'] ?: '-') . "), tested {$r['tested']}, answered {$r['answered']}" .
			($r['err'] ? ", error: {$r['err']}" : '') . "\n";
		foreach ($r['chosen'] as $c) {
			echo "  {$c['ip']}  {$c['rtt']} ms  {$c['descr']} {$c['city']}\n";
		}
		exit($r['chosen'] ? 0 : 1);
	case 'probe':
		/*
		 * Diagnose: gleicher Pinger wie der Dienst, eigener Prozess, ein Ziel.
		 * probe <ip> [ts|echo] [Anzahl] [Abstand ms]
		 */
		$ip = $argv[2] ?? '';
		$kind = ($argv[3] ?? 'ts') === 'echo' ? 'echo' : 'ts';
		$n = max(1, intval($argv[4] ?? 100));
		$iv = max(10, intval($argv[5] ?? 200)) / 1000;
		if (!filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
			echo "usage: probe <ipv4> [ts|echo] [count] [interval ms]\n";
			exit(2);
		}
		$p = new SCGPingerSocket();
		if (!$p->ok) {
			echo "pinger: {$p->err}\n";
			exit(1);
		}
		$got = 0;
		$valid = 0;
		$rtts = array();
		$next = microtime(true);
		$sent = 0;
		$end = null;
		while (true) {
			$now = microtime(true);
			if ($sent < $n && $now >= $next) {
				$kind === 'ts' ? $p->sendTs($ip) : $p->send($ip);
				$sent++;
				$next += $iv;
				if ($sent == $n) {
					$end = $now + 4;
				}
			}
			foreach ($p->receive(10000) as $a) {
				if ($a[0] === $ip) {
					$got++;
					$rtts[] = $a[1];
					$valid += ($a[4] ?? null) !== null ? 1 : 0;
				}
			}
			if ($end !== null && microtime(true) >= $end) {
				break;
			}
		}
		sort($rtts);
		$c = $p->count[$ip][0] ?? array(0, 0, 0, 0, 0);
		printf("%s %s: sent %d, answered %d (%.1f %% loss), valid timestamps %d, send errors %d %s, stray %d, rtt min/median/max %.1f/%.1f/%.1f ms\n",
			$ip, $kind, $sent, $got, 100 * ($sent - $got) / max(1, $sent), $valid, $c[3], json_encode($p->sendErr), $c[4],
			$rtts ? $rtts[0] : 0, $rtts ? $rtts[intdiv(count($rtts), 2)] : 0, $rtts ? end($rtts) : 0);
		exit(0);
	case 'restore':
		scg_ar_restore();
		@unlink(SCG_AR_STATUS);
		exit(0);
	default:
		exit(scg_ar_run());
}
