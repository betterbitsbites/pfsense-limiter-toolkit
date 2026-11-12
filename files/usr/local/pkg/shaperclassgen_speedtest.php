<?php
/*
 * shaperclassgen_speedtest.php - periodic speed test (called by cron)
 * Usage: php -f shaperclassgen_speedtest.php [run|manual]
 * Licensed under the Apache License, Version 2.0
 */
/* Waehrend eines Paket-Updates fehlen die Bibliotheken kurz: dann still beenden */
foreach (array('shaperclassgen.inc', 'shaperclassgen_i18n.inc', 'shaperclassgen_autorate.inc', 'shaperclassgen_speedtest.inc') as $scg_f) {
	if (!is_readable("/usr/local/pkg/{$scg_f}")) {
		exit(0);
	}
}
require_once("/usr/local/pkg/shaperclassgen_speedtest.inc");

set_time_limit(0);
$cmd = $argv[1] ?? 'run';

switch ($cmd) {
	case 'manual':
		exit(scg_st_run(true, ($argv[2] ?? '') === 'exclusive'));
	default:
		$a = scg_st_settings();
		if (empty($a['enable'])) {
			exit(0);
		}
		exit(scg_st_run(false));
}
