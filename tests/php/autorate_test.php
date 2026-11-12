<?php
/* Autorate: cut when the line is far below the limit, speed test pause, rate and minute history */
require __DIR__ . '/_boot.php';
$cfg = scg_ar_settings();
$cfg['enable'] = true;
$cfg['reflectors'] = '192.0.2.1';
$cfg['up'] = array('pipe' => 'LimUp', 'min' => 1, 'base' => 47, 'max' => 47);
$cfg['down'] = array('pipe' => 'LimDown', 'min' => 80, 'base' => 800, 'max' => 800);
$ar = new SCGAutorate($cfg, array('refl' => array('192.0.2.1' => 12)));
$ar->resolve(array(3 => array('name' => 'LimDown'), 4 => array('name' => 'LimUp')));
$t = 1000.0;
$bd = $bu = 0.0;
/* upstream delivers only 3 Mbit/s while the limit is 47 -> bloat, load ~6 % */
for ($i = 0; $i < 40; $i++) {
	$up = min(3000, $ar->dirs['up']['shaper']);
	$rtt = ($ar->dirs['up']['shaper'] > 3200) ? 400 : 14;
	for ($k = 0; $k < 5; $k++) {
		$ar->onRtt('192.0.2.1', $rtt, $t + $k * 0.1);
	}
	$t += 0.5;
	$bu += $up / 8 * 500;
	$bd += 200;
	foreach ($ar->tick($t, array(3 => array('bw' => $ar->dirs['down']['shaper'] * 1000, 'bytes' => 0), 4 => array('bw' => $ar->dirs['up']['shaper'] * 1000, 'bytes' => 0)),
		array('down' => $bd, 'up' => $bu)) as $num => $kbit) {
		/* applied */
	}
	$ar->recordRates($t);
}
check($ar->dirs['up']['shaper'] < 5000, 'upload limit cut from 47 Mbit/s to ' . round($ar->dirs['up']['shaper'] / 1000, 1) . ' Mbit/s');
$before = $ar->dirs['up']['shaper'];
$ar->paused = true;
for ($i = 0; $i < 20; $i++) {
	$ar->onRtt('192.0.2.1', 500, $t);
	$t += 0.5;
	$ar->tick($t, array(3 => array('bw' => 1, 'bytes' => 0), 4 => array('bw' => 1, 'bytes' => 0)), array('down' => $bd, 'up' => $bu));
	$ar->recordRates($t);
}
check($ar->dirs['up']['shaper'] == $before && $ar->dirs['up']['state'] == 'paused', 'speed test pause: no change');
check(count($ar->rhist['up']) > 20, 'rate history per second kept');
$rec = $ar->minuteRecord((int)(floor(($t - 30) / 60) * 60));
check($rec && isset($rec['l']) && isset($rec['u']) && isset($rec['mx']['up']), 'minute record with latency, rates and maximum');

/* long-term series downsampling */
@unlink(SCG_AR_LONG);
$now = (int)(floor(time() / 60) * 60);
$lines = '';
for ($i = 48 * 60; $i > 0; $i--) {
	$lines .= json_encode(array('t' => $now - $i * 60, 'l' => array(8, 12, 30, 20, 0, 300, 8), 'd' => array(800000, 100000, 200000),
		'u' => array(40000, 5000, 9000), 'mx' => array('down' => 800000, 'up' => 47000))) . "\n";
}
file_put_contents(SCG_AR_LONG, $lines);
foreach (array(1 => 60, 24 => 120, 48 => 240) as $h => $bucket) {
	$sr = scg_ar_long_series($h);
	check($sr['bucket'] == $bucket && count($sr['rows']) <= 721, "{$h} h: bucket {$sr['bucket']} s, " . count($sr['rows']) . ' points');
}
