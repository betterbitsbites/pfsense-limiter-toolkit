<?php
/*
 * shaperclassgen_autorate.php - Autorate-Dienst
 * Aufruf: php -f shaperclassgen_autorate.php [run|restore|enabled|select [list]]
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
	case 'restore':
		scg_ar_restore();
		@unlink(SCG_AR_STATUS);
		exit(0);
	default:
		exit(scg_ar_run());
}
