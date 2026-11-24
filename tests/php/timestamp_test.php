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
