<?php
/*
 * shaperclassgen_speedtest.inc
 *
 * Periodic speed test against Cloudflare (speed.cloudflare.com) with curl,
 * run directly on pfSense. Measures idle latency, download and upload with
 * parallel connections, and latency under load. Results are appended as JSON
 * lines to /var/db/scg_speedtest.jsonl.
 *
 * Safety: the test only runs if the default route goes through the selected
 * WAN (never via a backup/volume-limited gateway). While it runs, Autorate is
 * paused so it does not lower the limiters because of the test load.
 * Licensed under the Apache License, Version 2.0
 */

require_once("/usr/local/pkg/shaperclassgen_autorate.inc");

define('SCG_ST_DB', '/var/db/scg_speedtest.jsonl');
define('SCG_ST_LOCK', '/var/run/scg_speedtest.lock');
define('SCG_ST_STATUS', '/var/run/scg_speedtest.json');
define('SCG_ST_FILE', '/var/tmp/scg_speedtest.bin');
define('SCG_ST_SCRIPT', '/usr/local/pkg/shaperclassgen_speedtest.php');
define('SCG_ST_CURL', '/usr/local/bin/curl');

function scg_st_defaults() {
	return array(
		'enable' => false,
		'interval' => 30,          /* minutes */
		'duration' => 10,          /* seconds per direction */
		'streams' => 4,            /* parallel connections */
		'pingtarget' => '1.1.1.1',
		'wanif' => '',             /* '' = first physical WAN */
		'keepdays' => 90,
		'pause_autorate' => true,
		'down_url' => 'https://speed.cloudflare.com/__down?bytes=',
		'up_url' => 'https://speed.cloudflare.com/__up',
	);
}

function scg_st_settings() {
	$d = scg_st_defaults();
	$s = scg_load_settings();
	$a = (isset($s['speedtest']) && is_array($s['speedtest'])) ? $s['speedtest'] : array();
	foreach ($d as $k => $v) {
		if (array_key_exists($k, $a)) {
			$d[$k] = $a[$k];
		}
	}
	return $d;
}

function scg_st_intervals() {
	return array(15, 30, 60, 120, 240, 360);
}

function scg_st_validate($a, &$errors) {
	if (!in_array(intval($a['interval']), scg_st_intervals())) {
		$errors[] = scg_t('Invalid interval.');
	}
	if (!is_numeric($a['duration']) || $a['duration'] < 3 || $a['duration'] > 30) {
		$errors[] = scg_t('Test duration must be between 3 and 30 seconds.');
	}
	if (!is_numeric($a['streams']) || $a['streams'] < 1 || $a['streams'] > 16) {
		$errors[] = scg_t('Parallel connections must be between 1 and 16.');
	}
	if (!filter_var($a['pingtarget'], FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
		$errors[] = scg_t('Ping target must be an IPv4 address.');
	}
	if (!is_numeric($a['keepdays']) || $a['keepdays'] < 1 || $a['keepdays'] > 730) {
		$errors[] = scg_t('Retention must be between 1 and 730 days.');
	}
	$wif = $a['wanif'] ?? '';
	if ($wif !== '' && !isset(get_configured_interface_with_descr()[$wif])) {
		$errors[] = scg_t('Measurement interface "%s" does not exist.', $wif);
	}
}

/* ------------------------------------------------------------------ */
/* Schedule (cron)                                                     */
/* ------------------------------------------------------------------ */

function scg_st_cron_command() {
	return '/usr/local/bin/php -f ' . SCG_ST_SCRIPT . ' run';
}

/* Minute/hour fields; a fixed offset keeps tests off the full/half hour */
function scg_st_cron_fields($interval) {
	$off = 7;
	switch (intval($interval)) {
		case 15: return array("{$off}," . ($off + 15) . "," . ($off + 30) . "," . ($off + 45), '*');
		case 30: return array("{$off}," . ($off + 30), '*');
		case 60: return array("{$off}", '*');
		case 120: return array("{$off}", '*/2');
		case 240: return array("{$off}", '*/4');
		default: return array("{$off}", '*/6');
	}
}

function scg_st_install_cron($a) {
	if (!function_exists('install_cron_job')) {
		require_once("services.inc");
	}
	if (!function_exists('install_cron_job')) {
		return false;
	}
	list($min, $hour) = scg_st_cron_fields($a['interval']);
	install_cron_job(scg_st_cron_command(), !empty($a['enable']), $min, $hour, '*', '*', '*', 'root', true);
	return true;
}

/* ------------------------------------------------------------------ */
/* Results                                                             */
/* ------------------------------------------------------------------ */

function scg_st_results($since = 0) {
	$out = array();
	$fh = @fopen(SCG_ST_DB, 'r');
	if (!$fh) {
		return $out;
	}
	while (($l = fgets($fh)) !== false) {
		$r = json_decode($l, true);
		if (is_array($r) && isset($r['t']) && $r['t'] >= $since) {
			$out[] = $r;
		}
	}
	fclose($fh);
	return $out;
}

function scg_st_append($r, $keepdays) {
	$lim = time() - intval($keepdays) * 86400;
	$keep = array();
	foreach (scg_st_results($lim) as $old) {
		$keep[] = json_encode($old);
	}
	$keep[] = json_encode($r);
	@file_put_contents(SCG_ST_DB . '.tmp', implode("\n", $keep) . "\n");
	@rename(SCG_ST_DB . '.tmp', SCG_ST_DB);
}

function scg_st_status($phase, $extra = array()) {
	@file_put_contents(SCG_ST_STATUS, json_encode(array_merge(array('phase' => $phase, 't' => time()), $extra)));
}

function scg_st_running() {
	$fh = @fopen(SCG_ST_LOCK, 'c');
	if (!$fh) {
		return false;
	}
	$free = flock($fh, LOCK_EX | LOCK_NB);
	if ($free) {
		flock($fh, LOCK_UN);
	}
	fclose($fh);
	return !$free;
}

/* ------------------------------------------------------------------ */
/* Measurement                                                         */
/* ------------------------------------------------------------------ */

/* Interface of the default IPv4 route */
function scg_st_default_iface() {
	$o = array();
	exec('/sbin/route -n get default 2>/dev/null', $o);
	foreach ($o as $l) {
		if (preg_match('/^\s*interface:\s*(\S+)/', $l, $m)) {
			return $m[1];
		}
	}
	return null;
}

/* Start a background ping; returns process handle */
function scg_st_ping_start($target, $count, $interval = 0.2) {
	$cmd = array('/sbin/ping', '-n', '-i', sprintf('%.1f', $interval), '-c', (string)$count, $target);
	$p = proc_open($cmd, array(1 => array('pipe', 'w'), 2 => array('file', '/dev/null', 'w')), $pipes);
	return is_resource($p) ? array($p, $pipes[1]) : null;
}

/* Collect ping results: avg, p95, min, max, jitter, loss */
function scg_st_ping_collect($h, $sent_expected = null) {
	if (!$h) {
		return null;
	}
	list($p, $out) = $h;
	$txt = stream_get_contents($out);
	fclose($out);
	proc_close($p);
	$t = array();
	if (preg_match_all('/time=([\d.]+) ms/', $txt, $m)) {
		$t = array_map('floatval', $m[1]);
	}
	$sent = $sent_expected;
	if (preg_match('/(\d+) packets transmitted/', $txt, $m)) {
		$sent = intval($m[1]);
	}
	return scg_st_ping_stats($t, $sent);
}

function scg_st_ping_stats($t, $sent) {
	$n = count($t);
	$loss = ($sent && $sent > 0) ? round(100.0 * max(0, $sent - $n) / $sent, 1) : null;
	if ($n == 0) {
		return array('avg' => null, 'p95' => null, 'min' => null, 'max' => null, 'jitter' => null, 'loss' => $loss);
	}
	$avg = array_sum($t) / $n;
	$jit = 0.0;
	for ($i = 1; $i < $n; $i++) {
		$jit += abs($t[$i] - $t[$i - 1]);
	}
	$s = $t;
	sort($s);
	return array('avg' => round($avg, 1), 'p95' => round($s[(int)floor(0.95 * ($n - 1))], 1),
		'min' => round($s[0], 1), 'max' => round($s[$n - 1], 1),
		'jitter' => $n > 1 ? round($jit / ($n - 1), 1) : 0.0, 'loss' => $loss);
}

/* Start one curl transfer with at most $secs seconds; returns array(proc, stdout) */
function scg_st_curl_start($dir, $a, $real, $secs) {
	$cmd = array(SCG_ST_CURL, '-s', '-o', '/dev/null', '-D', '-', '-m', sprintf('%.1f', $secs), '--connect-timeout', '5');
	if ($real) {
		$cmd[] = '--interface';
		$cmd[] = $real;
	}
	/*
	 * Cloudflare answers large downloads (>~100 MB) without browser user
	 * agent and referer with 403. "Expect:" skips the 100-continue round
	 * trip (up to 1 s per upload chunk).
	 */
	$cmd = array_merge($cmd, array('-A', 'Mozilla/5.0', '-e', 'https://speed.cloudflare.com/'));
	if ($dir == 'down') {
		$cmd = array_merge($cmd, array('-w', "\nSCG %{size_download} %{http_code}\n", $a['down_url'] . '1000000000'));
	} else {
		$cmd = array_merge($cmd, array('-X', 'POST', '-H', 'Expect:', '-H', 'Content-Type: application/octet-stream',
			'-T', SCG_ST_FILE, '-w', "\nSCG %{size_upload} %{http_code}\n", $a['up_url']));
	}
	$p = proc_open($cmd, array(1 => array('pipe', 'w'), 2 => array('file', '/dev/null', 'w')), $pipes);
	if (!is_resource($p)) {
		return null;
	}
	stream_set_blocking($pipes[1], false);
	return array('p' => $p, 'out' => $pipes[1], 'buf' => '');
}

/*
 * Parallel curl transfers for $dur seconds. Streams that finish early
 * (upload chunks) are restarted until the time is up.
 * Rate: WAN counter in the steady part (after the TCP ramp-up, includes
 * other traffic on the line) and curl bytes over the whole phase.
 * Returns array('mbps', 'curl_mbps', 'bytes', 'errors', 'colo').
 */
function scg_st_transfer($dir, $a, $real) {
	$dur = floatval($a['duration']);
	$t0 = microtime(true);
	$end = $t0 + $dur;
	$ramp = $t0 + max(1.0, $dur * 0.3);
	$procs = array();
	for ($i = 0; $i < intval($a['streams']); $i++) {
		$h = scg_st_curl_start($dir, $a, $real, $dur);
		if ($h) {
			$procs[] = $h;
		}
	}
	$bytes = 0.0;
	$errors = array();
	$colo = null;
	$c_ramp = null;
	$t_ramp = null;
	$key = ($dir == 'down') ? 'in' : 'out';
	while ($procs) {
		usleep(100000);
		$now = microtime(true);
		if ($c_ramp === null && $now >= $ramp && $real) {
			$c_ramp = scg_if_bytes($real);
			$t_ramp = microtime(true);
		}
		foreach ($procs as $i => $h) {
			$procs[$i]['buf'] .= (string)stream_get_contents($h['out']);
			$st = proc_get_status($h['p']);
			if ($st['running']) {
				continue;
			}
			$procs[$i]['buf'] .= (string)stream_get_contents($h['out']);
			fclose($h['out']);
			proc_close($h['p']);
			$txt = $procs[$i]['buf'];
			unset($procs[$i]);
			if (preg_match('/^cf-ray:\s*\S+-([A-Z]{3})/mi', $txt, $m)) {
				$colo = strtoupper($m[1]);
			}
			if (preg_match('/SCG (\d+(?:\.\d+)?) (\d+)/', $txt, $m)) {
				$code = intval($m[2]);
				$bytes += floatval($m[1]);
				if ($code >= 400 || ($code == 0 && floatval($m[1]) == 0)) {
					$errors[] = "HTTP {$code}";
				}
			} else {
				$errors[] = 'curl';
			}
			/* restart while time is left (upload chunks, early aborts) */
			$left = $end - microtime(true);
			if ($left > 0.5 && count($errors) < 8) {
				$n = scg_st_curl_start($dir, $a, $real, $left);
				if ($n) {
					$procs[] = $n;
				}
			}
		}
	}
	$wall = max(0.1, microtime(true) - $t0);
	$curl = $bytes * 8 / $wall / 1e6;
	$mbps = $curl;
	if ($c_ramp !== null) {
		$c1 = scg_if_bytes($real);
		$dt = microtime(true) - $t_ramp;
		if ($c1 && $dt > 0.5 && $c1[$key] >= $c_ramp[$key]) {
			$mbps = ($c1[$key] - $c_ramp[$key]) * 8 / $dt / 1e6;
		}
	}
	return array('mbps' => round($mbps, 2), 'curl_mbps' => round($curl, 2), 'bytes' => $bytes,
		'errors' => array_values(array_unique($errors)), 'colo' => $colo);
}

/* Upload chunk (random data, created once; Cloudflare limits the body size) */
function scg_st_prepare_file($a) {
	$need = 25 * 1024 * 1024;
	if (!file_exists(SCG_ST_FILE) || filesize(SCG_ST_FILE) != $need) {
		exec('/bin/dd if=/dev/urandom of=' . escapeshellarg(SCG_ST_FILE) . ' bs=1048576 count=25 2>/dev/null');
	}
}

/* Pause Autorate until $until (unix time) */
function scg_st_pause_autorate($until) {
	@file_put_contents(SCG_AR_PAUSE, (string)intval($until));
}

function scg_st_resume_autorate() {
	@unlink(SCG_AR_PAUSE);
}

/*
 * Run one complete test and store the result. $manual = started from the GUI.
 */
function scg_st_run($manual = false) {
	$a = scg_st_settings();
	$fh = fopen(SCG_ST_LOCK, 'c');
	if (!$fh || !flock($fh, LOCK_EX | LOCK_NB)) {
		return 1;
	}
	$r = array('t' => time(), 'manual' => $manual);
	$wan = scg_ar_wan($a);
	$real = $wan ? $wan[1] : null;
	$r['iface'] = $wan ? $wan[2] : null;

	/* Safety: only when the default route goes through the selected WAN */
	$def = scg_st_default_iface();
	if (!$real || $def !== $real) {
		$r['skipped'] = $real ? scg_t('default route via %s instead of %s', (string)$def, $real) : scg_t('no WAN found');
		scg_st_append($r, $a['keepdays']);
		scg_st_status('done', array('result' => $r));
		flock($fh, LOCK_UN);
		return 0;
	}

	$dur = intval($a['duration']);
	scg_st_status('prepare');
	scg_st_prepare_file($a);
	if (!empty($a['pause_autorate'])) {
		scg_st_pause_autorate(time() + 2 * $dur + 30);
		/* resume even if the run aborts */
		register_shutdown_function('scg_st_resume_autorate');
	}
	$c0 = scg_if_bytes($real);

	/* 1. Idle latency */
	scg_st_status('ping');
	$r['ping'] = scg_st_ping_collect(scg_st_ping_start($a['pingtarget'], 10), 10);

	/* 2. Download with latency under load */
	scg_st_status('download');
	$n = $dur * 5;
	$ph = scg_st_ping_start($a['pingtarget'], $n);
	$d = scg_st_transfer('down', $a, $real);
	$r['ping_down'] = scg_st_ping_collect($ph, $n);

	/* 3. Upload with latency under load */
	scg_st_status('upload', array('down' => $d['mbps']));
	$ph = scg_st_ping_start($a['pingtarget'], $n);
	$u = scg_st_transfer('up', $a, $real);
	$r['ping_up'] = scg_st_ping_collect($ph, $n);

	if (!empty($a['pause_autorate'])) {
		scg_st_resume_autorate();
	}
	$r['down'] = $d['mbps'];
	$r['up'] = $u['mbps'];
	$r['down_curl'] = $d['curl_mbps'];
	$r['up_curl'] = $u['curl_mbps'];
	$c1 = scg_if_bytes($real);
	$r['mb'] = round(($d['bytes'] + $u['bytes']) / 1e6, 1);
	if ($c0 && $c1) {
		$r['wan_mb'] = round(($c1['in'] - $c0['in'] + $c1['out'] - $c0['out']) / 1e6, 1);
	}
	$r['colo'] = $d['colo'] ?: $u['colo'];
	$err = array_values(array_unique(array_merge($d['errors'], $u['errors'])));
	if ($err) {
		$r['errors'] = $err;
	}
	/*
	 * Eine Richtung zaehlt nur, wenn curl wirklich Daten uebertragen hat
	 * (eine 403-Antwort hat 1 Byte). Sonst kein Wert statt 0 Mbit/s, damit
	 * der WAN-Zaehler (anderer Verkehr) keine Scheinrate liefert.
	 */
	$okd = $d['bytes'] >= 100000;
	$oku = $u['bytes'] >= 100000;
	if (!$okd) {
		$r['down'] = $r['down_curl'] = null;
	}
	if (!$oku) {
		$r['up'] = $r['up_curl'] = null;
	}
	if (!$okd && !$oku) {
		$r['failed'] = true;
	}
	scg_st_append($r, $a['keepdays']);
	scg_st_status('done', array('result' => $r));
	flock($fh, LOCK_UN);
	return 0;
}
