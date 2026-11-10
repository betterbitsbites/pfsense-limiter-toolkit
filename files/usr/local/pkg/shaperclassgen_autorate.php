<?php
/*
 * shaperclassgen_autorate.php - Autorate-Dienst
 * Aufruf: php -f shaperclassgen_autorate.php [run|restore|enabled]
 * Licensed under the Apache License, Version 2.0
 */
require_once("/usr/local/pkg/shaperclassgen_autorate.inc");

set_time_limit(0);
$cmd = $argv[1] ?? 'run';

switch ($cmd) {
	case 'enabled':
		/* Exit-Code 0 = aktiviert (fuer das rc-Skript) */
		$a = scg_ar_settings();
		/* Dienst laeuft bei Regelung oder reiner Latenzmessung */
		exit((empty($a['enable']) && empty($a['monitor'])) ? 1 : 0);
	case 'restore':
		scg_ar_restore();
		@unlink(SCG_AR_STATUS);
		exit(0);
	default:
		exit(scg_ar_run());
}
