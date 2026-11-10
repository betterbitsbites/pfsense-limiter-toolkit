<?php
/*
 * shaperclassgen_speedtest.php - periodic speed test (called by cron)
 * Usage: php -f shaperclassgen_speedtest.php [run|manual]
 * Licensed under the Apache License, Version 2.0
 */
require_once("/usr/local/pkg/shaperclassgen_speedtest.inc");

set_time_limit(0);
$cmd = $argv[1] ?? 'run';

switch ($cmd) {
	case 'manual':
		exit(scg_st_run(true));
	default:
		$a = scg_st_settings();
		if (empty($a['enable'])) {
			exit(0);
		}
		exit(scg_st_run(false));
}
