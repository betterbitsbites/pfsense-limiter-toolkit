<?php
/* Service config re-read (pfSense 2.9 has no parse_config) and the reload signal (issue #14) */
require __DIR__ . '/_boot.php';
global $config;
check(!function_exists('parse_config') && !scg_cfg_reload(), 'no reader function: reload reports failure');
/* defined only now (a top-level function would exist before the first check) */
if (true) {
	function config_read_file($parse = false, $restore = true) {
		global $config;
		$config['scg_reload_marker'] = $parse && !$restore ? 'fresh' : 'wrong args';
		return true;
	}
}
check(scg_cfg_reload() && ($config['scg_reload_marker'] ?? '') === 'fresh', 'pfSense 2.7+: config_read_file(true, false) re-reads the global config');
@unlink(SCG_AR_RELOAD);
scg_ar_signal();
check(is_readable(SCG_AR_RELOAD), 'signal file written');
$src = file_get_contents(__DIR__ . '/../../files/usr/local/pkg/shaperclassgen.inc');
check(substr_count($src, 'scg_ar_signal();') >= 3, 'apply, remove and limiter re-create signal the service');
