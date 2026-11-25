<?php
/* Failover: backup values on the same limiters, own learned base, pause without values */
require __DIR__ . '/_boot.php';
function fo_mk($bk) {
	$cfg = scg_ar_settings();
	$cfg['enable'] = true;
	$cfg['reflectors'] = '192.0.2.1';
	$cfg['up'] = array('pipe' => 'LimUp', 'min' => 1.5, 'base' => 22, 'max' => 45);
	$cfg['down'] = array('pipe' => 'LimDown', 'min' => 80, 'base' => 800, 'max' => 800);
	$cfg['bk'] = $bk;
	$ar = new SCGAutorate($cfg, array('learned' => array('down' => array('pipe' => 'LimDown', 'kbit' => 500000), 'up' => array('pipe' => 'LimUp', 'kbit' => 18000)),
		'learned_bk' => array('down' => 30000)));
	$ar->resolve(array(3 => array('name' => 'LimDown'), 4 => array('name' => 'LimUp')));
	return $ar;
}
function fo_tick($ar, $t) {
	return $ar->tick($t, array(3 => array('bw' => 1, 'bytes' => 0), 4 => array('bw' => 1, 'bytes' => 0)), array('down' => 0, 'up' => 0));
}
$bk = array('down' => array('min' => 5, 'base' => 40, 'max' => 100), 'up' => array('min' => 1, 'base' => 8, 'max' => 20));
$ar = fo_mk($bk);
check($ar->dirs['down']['shaper'] == 500000, 'main WAN: learned base 500 Mbit/s');
$ar->setFailover('igb2', 'LTE', 100.0);
check($ar->failover['profile'] === 'backup' && $ar->dirs['down']['max'] == 100000 && $ar->dirs['down']['shaper'] == 30000 && $ar->dirs['up']['shaper'] == 8000,
	'failover: backup limits, learned backup base 30 Mbit/s, upload base 8');
$apply = fo_tick($ar, 101.0);
check(($apply[3] ?? null) == 30000 && ($apply[4] ?? null) == 8000, 'backup values applied to the same limiters at once');
$mem = $ar->memory();
check($mem['learned']['down']['kbit'] == 500000 && $mem['learned_bk']['down'] == 30000, 'memory keeps main and backup learned base apart');
$ar->setFailover(null, '', 200.0);
check($ar->failover === null && $ar->dirs['down']['max'] == 800000 && $ar->dirs['down']['shaper'] == 500000, 'back on the main WAN: main limits and learned base');
$apply = fo_tick($ar, 201.0);
check(($apply[3] ?? null) == 500000, 'main value applied again');

$ar = fo_mk(array('down' => array('min' => '', 'base' => '', 'max' => ''), 'up' => array('min' => '', 'base' => '', 'max' => '')));
$ar->setFailover('igb2', 'LTE', 100.0);
$apply = fo_tick($ar, 101.0);
check($ar->failover['profile'] === 'paused' && !$apply && $ar->dirs['down']['state'] === 'failover', 'no backup values: control paused, limiters untouched');
$st = $ar->status('socket');
check($st['failover']['descr'] === 'LTE', 'status shows the failover');
$e = array();
$cfg = scg_ar_settings();
$cfg['bk']['down'] = array('min' => 5, 'base' => '', 'max' => 100);
scg_ar_validate($cfg, $e);
check((bool)array_filter($e, function ($x) { return strpos($x, 'Backup line') !== false; }), 'incomplete backup values rejected');

/* gateway group members count as WAN, static-route gateways on internal interfaces do not */
global $config;
$config['gateways'] = array(
	'gateway_item' => array(array('name' => 'GW_LTE', 'interface' => 'opt2'), array('name' => 'GW_LANROUTER', 'interface' => 'lan')),
	'gateway_group' => array(array('name' => 'GRP_FAILOVER', 'item' => array('GW_WAN|1|address', 'GW_LTE|2|address'))),
);
check(scg_if_is_wan('opt2') && !scg_if_is_wan('lan'), 'gateway group member is WAN, static-route gateway on LAN is not');
unset($config['gateways']);
