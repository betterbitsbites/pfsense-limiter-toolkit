<?php
/* ICMP timestamp: wrap-around, list filter, ranking, per-direction control */
require __DIR__ . '/_boot.php';

/* midnight wrap */
check(abs(scg_ts_diff(5, SCG_AR_TS_DAY - 5) - 10) < 1e-9, 'diff across midnight = +10 ms');
check(abs(scg_ts_diff(SCG_AR_TS_DAY - 5, 5) + 10) < 1e-9, 'diff across midnight = -10 ms');
list($u, $d) = scg_ts_owd(array(1000.0, 1520.0, 1521.0, 1040.0));
check($u == 520 && $d == -481, 'one-way times include the clock offset');

/* CSV filter: country, sub-region fallback, world */
function csvfh($rows) {
	$fh = fopen('php://memory', 'w+');
	fwrite($fh, "reflector_ip,ip_version,offset,description,country_code,region,sub_region,country_name,sub_division_1,sub_division_2,city\n");
	foreach ($rows as $r) {
		fwrite($fh, $r . "\n");
	}
	rewind($fh);
	return $fh;
}
$rows = array();
for ($i = 1; $i <= 80; $i++) {
	$rows[] = "203.0.{$i}.1,4,-3,host{$i}.example,AA,Europe,Western Europe,Aland,,,Town";
}
for ($i = 1; $i <= 60; $i++) {
	$rows[] = "198.51.{$i}.7,4,2,\"comma, host\",BB,Europe,Western Europe,Bland,,,City";
}
$rows[] = "10.1.2.3,4,0,private,AA,Europe,Western Europe,Aland,,,Town";
$rows[] = "2001:db8::1,6,0,v6,AA,Europe,Western Europe,Aland,,,Town";
$rows[] = "192.0.2.9,4,0,few,CC,Asia,Eastern Asia,Cland,,,X";
$f = scg_ts_filter_csv(csvfh($rows), 'aa');
check($f['src'] === 'country' && count($f['rows']) == 80, 'country rows kept, private and IPv6 skipped (' . count($f['rows']) . ')');
$f = scg_ts_filter_csv(csvfh($rows), 'BB');
check($f['src'] === 'country' && $f['rows'][0][2] === 'comma, host', 'quoted description with comma parsed');
$f = scg_ts_filter_csv(csvfh($rows), 'CC');
check($f['src'] === 'world' && count($f['rows']) == 141, 'too few in country and sub-region: worldwide');
$few = array_merge(array_slice($rows, 0, 70), array("203.0.200.1,4,0,x,DD,Europe,Western Europe,Dland,,,Y"));
$f = scg_ts_filter_csv(csvfh($few), 'DD');
check($f['src'] === 'subregion' && count($f['rows']) == 71, 'too few in country: sub-region added');

/* ranking */
$res = array(
	'203.0.1.1' => array('rtt' => array(10, 11, 10), 'valid' => 3, 'up' => array(500, 501, 500)),
	'203.0.2.1' => array('rtt' => array(9, 9, 9), 'valid' => 3, 'up' => array(500, 500, 500)),      /* same /16 as above */
	'198.51.1.7' => array('rtt' => array(12, 12, 13), 'valid' => 3, 'up' => array(4, 4, 5)),
	'192.0.2.9' => array('rtt' => array(8, 8, 8), 'valid' => 3, 'up' => array(0, 400, 900)),      /* clock jumps */
	'198.18.0.1' => array('rtt' => array(7), 'valid' => 1, 'up' => array(1)),                    /* one answer only */
	'100.64.0.1' => array('rtt' => array(30, 31, 30), 'valid' => 0, 'up' => array()),           /* no valid time */
);
$rk = scg_ts_rank($res, 2);
check($rk['chosen'] === array('203.0.2.1', '198.51.1.7'), 'best per /16 first: ' . implode(' ', $rk['chosen']));
check(!isset($rk['score']['192.0.2.9']) && !isset($rk['score']['198.18.0.1']) && !isset($rk['score']['100.64.0.1']),
	'unstable clock, single answer and invalid time are dropped');
check($rk['spare'] === array('203.0.1.1'), 'spare list');
check(scg_ts_valid(array(1.0, 5.0, 5.0, 9.0)) && !scg_ts_valid(array(1.0, 5.0, 4.0, 9.0)) && !scg_ts_valid(null), 'reply check');

/* control: ICMP timestamp mode */
function mk($mode) {
	$cfg = scg_ar_settings();
	$cfg['enable'] = true;
	$cfg['pingmode'] = $mode;
	$cfg['reflectors'] = '192.0.2.1';
	$cfg['up'] = array('pipe' => 'LimUp', 'min' => 1, 'base' => 40, 'max' => 40);
	$cfg['down'] = array('pipe' => 'LimDown', 'min' => 80, 'base' => 800, 'max' => 800);
	$ar = new SCGAutorate($cfg, array());
	$ar->resolve(array(3 => array('name' => 'LimDown'), 4 => array('name' => 'LimUp')));
	if ($mode === 'ts') {
		$ar->setTsReflectors(array('203.0.113.5', '203.0.113.6'));
	}
	return $ar;
}
/* both directions fully loaded; $xu / $xd = extra upload / download delay in ms */
function run($ar, $secs, $xu, $xd, &$t, $ts = true) {
	static $bd = 0, $bu = 0;
	for ($i = 0; $i < $secs * 2; $i++) {
		foreach (array('203.0.113.5' => 700.0, '203.0.113.6' => -300.0) as $ip => $off) {
			$t1 = fmod($t * 1000, SCG_AR_TS_DAY);
			$rtt = 15 + $xu + $xd;
			$t2 = fmod($t1 + $off + 7 + $xu, SCG_AR_TS_DAY);
			$ar->onRtt($ip, $rtt, $t, $ts ? array($t1, floor($t2), floor($t2), $t1 + $rtt) : null);
		}
		$ar->onRtt('192.0.2.1', 15 + $xu + $xd, $t);
		$t += 0.5;
		$bd += $ar->dirs['down']['shaper'] * 0.95 / 8 * 500;
		$bu += $ar->dirs['up']['shaper'] * 0.95 / 8 * 500;
		$ar->tick($t, array(3 => array('bw' => $ar->dirs['down']['shaper'] * 1000, 'bytes' => 0),
			4 => array('bw' => $ar->dirs['up']['shaper'] * 1000, 'bytes' => 0)), array('down' => $bd, 'up' => $bu));
	}
}
$t = 50000.0;
$ar = mk('ts');
run($ar, 20, 0, 0, $t);
check(!$ar->bloat && $ar->tsActive($t), 'clean line, reflectors active');
$d0 = $ar->dirs['down']['shaper'];
run($ar, 3, 120, 0, $t);
check($ar->bloatDir === array('up' => true, 'down' => false), 'upload congestion detected as upload');
check($ar->dirs['up']['shaper'] < 40000 * 0.95 && $ar->dirs['down']['shaper'] >= $d0, 'only upload lowered (up ' .
	round($ar->dirs['up']['shaper'] / 1000, 1) . ', down ' . round($ar->dirs['down']['shaper'] / 1000) . ' Mbit/s)');
check($ar->dirs['down']['state'] === 'bloat_other', 'download state: congestion in the other direction');
$st = $ar->status('socket');
check($st['stats']['up'] > 20 && abs($st['stats']['down']) < 5 && $st['refl']['203.0.113.5']['up'] > 100, 'status shows upload delay rise');
check($st['refl']['192.0.2.1']['role'] === 'ref', 'echo target is reference in timestamp mode');
$lc = end($st['cuts']);
check($lc && $lc['dir'] === 'up' && $lc['why'] === 'owd', 'cut log in the status: ' . json_encode($lc));

$t += 30;
$ar = mk('ts');
run($ar, 20, 0, 0, $t);
$u0 = $ar->dirs['up']['shaper'];
run($ar, 3, 0, 120, $t);
check($ar->bloatDir === array('up' => false, 'down' => true), 'download congestion detected as download');
check($ar->dirs['down']['shaper'] < 800000 * 0.95 && $ar->dirs['up']['shaper'] >= $u0, 'only download lowered');

/* slow clock drift on a clean line must not look like congestion */
$t += 30;
$ar = mk('ts');
run($ar, 20, 0, 0, $t);
$drift = 0;
for ($i = 0; $i < 120; $i++) {
	$drift += 0.05;
	$t1 = fmod($t * 1000, SCG_AR_TS_DAY);
	$t2 = $t1 + 700 + 7 + $drift;
	$ar->onRtt('203.0.113.5', 15, $t, array($t1, floor($t2), floor($t2), $t1 + 15));
	$t += 0.25;
}
check(abs($ar->refl['203.0.113.5']['dup']) < 3, 'clock drift absorbed (up rise ' . round($ar->refl['203.0.113.5']['dup'], 1) . ' ms)');

/* without timestamps: old rule, both loaded directions are lowered */
$t += 30;
$ar = mk('echo');
run($ar, 20, 0, 0, $t, false);
run($ar, 3, 120, 0, $t, false);
check($ar->bloatDir === null && $ar->dirs['up']['shaper'] < 40000 * 0.95 && $ar->dirs['down']['shaper'] < 800000 * 0.95,
	'echo mode: both loaded directions lowered');

/* timestamp mode, reflectors silent: echo target takes over control */
$t += 30;
$ar = mk('ts');
for ($i = 0; $i < 40; $i++) {
	$ar->onRtt('192.0.2.1', $i < 30 ? 15 : 150, $t);
	$t += 0.5;
	$ar->tick($t, array(3 => array('bw' => 800000000, 'bytes' => 0), 4 => array('bw' => 40000000, 'bytes' => 0)), array('down' => 0, 'up' => 0));
}
check(!$ar->tsActive($t) && $ar->bloat, 'no reflector answers: echo target detects congestion');
$mem = $ar->memory();
check(isset($mem['tsup']) && is_array($mem['tsup']), 'memory contains upload baselines');

/* direction from the median over reflectors: one outlier does not tip it, small rises do not count */
function mk4() {
	$cfg = scg_ar_settings();
	$cfg['enable'] = true;
	$cfg['pingmode'] = 'ts';
	$cfg['reflectors'] = '192.0.2.1';
	$cfg['up'] = array('pipe' => 'LimUp', 'min' => 1, 'base' => 40, 'max' => 40);
	$cfg['down'] = array('pipe' => 'LimDown', 'min' => 80, 'base' => 800, 'max' => 800);
	$ar = new SCGAutorate($cfg, array());
	$ar->resolve(array(3 => array('name' => 'LimDown'), 4 => array('name' => 'LimUp')));
	$ar->setTsReflectors(array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4'));
	return $ar;
}
/* $rise = ip => array(upload rise, download rise) */
function run4($ar, $secs, $rise, &$t) {
	$verdicts = array();
	for ($i = 0; $i < $secs * 2; $i++) {
		for ($k = 0; $k < 2; $k++) {
			foreach (array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4') as $j => $ip) {
				list($xu, $xd) = $rise[$ip] ?? array(0, 0);
				$t1 = fmod($t * 1000, SCG_AR_TS_DAY);
				$rtt = 15 + $xu + $xd;
				$t2 = fmod($t1 + 100 * $j + 7 + $xu, SCG_AR_TS_DAY);
				$ar->onRtt($ip, $rtt, $t, array($t1, floor($t2), floor($t2), $t1 + $rtt));
				$t += 0.06;
			}
		}
		$ar->tick($t, array(3 => array('bw' => $ar->dirs['down']['shaper'] * 1000, 'bytes' => 0),
			4 => array('bw' => $ar->dirs['up']['shaper'] * 1000, 'bytes' => 0)), array('down' => 0, 'up' => 0));
		if ($ar->bloat) {
			$verdicts[] = json_encode($ar->bloatDir);
		}
	}
	return array_count_values($verdicts);
}
$t += 30;
$ar = mk4();
run4($ar, 15, array(), $t);
$v = run4($ar, 4, array('203.0.113.1' => array(45, 4), '203.0.113.2' => array(44, 4), '203.0.113.3' => array(46, 2), '203.0.113.4' => array(25, 40)), $t);
check(array_keys($v) === array('{"up":true,"down":false}'), 'outlier reflector does not tip the verdict: ' . json_encode($v));
$t += 30;
$ar = mk4();
run4($ar, 15, array(), $t);
$all = array();
foreach (array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4') as $ip) {
	$all[$ip] = array(40, 9);
}
$v = run4($ar, 4, $all, $t);
check(array_keys($v) === array('{"up":true,"down":false}'), 'download rise below half the threshold is not congestion: ' . json_encode($v));
$st = $ar->status('socket');
check(is_array($st['owdmed']) && $st['owdmed'][0] > 30 && isset($st['late']), 'status has median rise and late counter');

/* replies without timestamps during heavy congestion: last direction is held, download not cut */
$t += 30;
$ar = mk4();
run4($ar, 15, array(), $t);
$all = array();
foreach (array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4') as $ip) {
	$all[$ip] = array(80, 2);
}
run4($ar, 2, $all, $t);
$down0 = $ar->dirs['down']['shaper'];
for ($i = 0; $i < 8; $i++) {
	foreach (array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4') as $ip) {
		$ar->onRtt($ip, 900, $t);   /* no timestamp data */
		$t += 0.12;
	}
	$ar->tick($t, array(3 => array('bw' => $ar->dirs['down']['shaper'] * 1000, 'bytes' => 0),
		4 => array('bw' => $ar->dirs['up']['shaper'] * 1000, 'bytes' => 0)), array('down' => 0, 'up' => 0));
}
check($ar->dirHeld && $ar->bloatDir === array('up' => true, 'down' => false), 'direction held while timestamps are missing');
check($ar->dirs['down']['shaper'] >= $down0, 'download not cut during the hold');

/* short spike on half of the reflectors: RTT says congestion, median says no direction -> no cut, no hold */
$t += 30;
$ar = mk4();
run4($ar, 15, array(), $t);
$all = array();
foreach (array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4') as $ip) {
	$all[$ip] = array(80, 2);
}
run4($ar, 2, $all, $t);   /* sets the last known direction (upload) */
$v = run4($ar, 2, array('203.0.113.3' => array(25, 25), '203.0.113.4' => array(25, 25)), $t);
check($ar->bloat && $ar->bloatDir === array('up' => false, 'down' => false) && !$ar->dirHeld,
	'spike without clear direction: no direction, not held (' . json_encode($v) . ')');
check($ar->dirs['down']['state'] === 'bloat_unclear', 'state: short spike, no direction clearly congested');

/* lossy reflectors: replaced only when clearly worse than the other targets */
$cand = array('203.0.113.1', '203.0.113.2', '203.0.113.3', '203.0.113.4');
$win = array('203.0.113.1' => array(150, 147), '203.0.113.2' => array(150, 117), '203.0.113.3' => array(150, 133),
	'203.0.113.4' => array(150, 148), '192.0.2.1' => array(150, 149), '192.0.2.2' => array(150, 146));
$l = scg_ar_lossy($win, $cand);
check(array_keys($l) === array('203.0.113.2', '203.0.113.3'), 'rate-limiting reflectors replaced (22 %, 11 % vs. ~2 %): ' . json_encode($l));
$win = array('203.0.113.1' => array(150, 122), '203.0.113.2' => array(150, 112), '203.0.113.3' => array(150, 135),
	'203.0.113.4' => array(150, 130), '192.0.2.1' => array(150, 136), '192.0.2.2' => array(150, 137));
check(scg_ar_lossy($win, $cand) === array(), 'general loss during congestion (9–25 %): nothing replaced');
check(scg_ar_lossy(array('203.0.113.1' => array(50, 10)), $cand) === array(), 'too few pings: nothing replaced');

/* base latency outliers */
$o = scg_ar_base_outliers(array('203.0.113.1' => 11.0, '203.0.113.2' => 14.0, '203.0.113.3' => 42.0, '203.0.113.4' => 210.0));
check(array_keys($o) === array('203.0.113.4'), 'reflector with base 210 ms replaced, 42 ms kept: ' . json_encode($o));
check(scg_ar_base_outliers(array('203.0.113.1' => 11.0, '203.0.113.2' => 300.0)) === array(), 'fewer than 3 reflectors: no outlier decision');

/* replaced reflectors are stored and skipped for 7 days */
@unlink(SCG_AR_TS_SEL);
file_put_contents(SCG_AR_TS_SEL, json_encode(array('t' => time(), 'country' => 'AA', 'src' => 'country', 'tested' => 100, 'answered' => 90,
	'chosen' => array(array('ip' => '203.0.113.1', 'rtt' => 10, 'descr' => 'a', 'city' => ''), array('ip' => '203.0.113.2', 'rtt' => 11, 'descr' => 'b', 'city' => '')),
	'spare' => array(array('ip' => '203.0.113.3', 'rtt' => 12, 'descr' => 'c', 'city' => '')), 'err' => '',
	'exclude' => array('198.51.100.9' => time() - 8 * 86400))));
scg_ts_selection_store(array('203.0.113.1', '203.0.113.3'), array(), array('203.0.113.2'));
$sel = scg_ts_selection();
check(array_column($sel['chosen'], 'ip') === array('203.0.113.1', '203.0.113.3') && $sel['chosen'][1]['descr'] === 'c', 'selection file updated with the replacement');
check(scg_ts_excluded($sel) === array('203.0.113.2') && !isset($sel['exclude']['198.51.100.9']), 'replaced reflector excluded, old exclusions expire after 7 days');

/* own ICMP ID per Autorate target, never equal to a probe channel ID */
$pg = new SCGPingerSocket();
$a1 = $pg->icmpId0('203.0.113.1');
$a2 = $pg->icmpId0('203.0.113.2');
$pg->rotate0();
$a1b = $pg->icmpId0('203.0.113.1');
$probeIds = array();
for ($ch = 1; $ch <= 16; $ch++) {
	$probeIds[] = $pg->icmpId($ch) & 63;
}
check($a1 != $a2 && $a1b != $a1 && ($a1 & 63) >= 32 && max($probeIds) < 32, 'per-target IDs, rotate, disjoint from probe IDs');
