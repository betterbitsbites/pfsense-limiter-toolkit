<?php
/* Latency per interface: probe rules in the plan, channels from the applied rules, per-interface status */
require __DIR__ . '/_boot.php';

$s = scg_default_settings('simple');
$s['vlans'] = array(
	array('if' => 'lan', 'enable' => true, 'tag' => 'LAN', 'prio' => 'high', 'factor' => 1, 'down' => 'LimDown', 'up' => 'LimUp'),
	array('if' => 'opt2', 'enable' => true, 'tag' => 'GUEST', 'prio' => 'low', 'factor' => 0.2, 'down' => 'LimDown', 'up' => 'LimUp'),
	array('if' => 'opt1', 'enable' => false, 'tag' => 'IOT', 'prio' => 'mid', 'factor' => 0.5, 'down' => 'LimDown', 'up' => 'LimUp'),
);

function scg_test_probes($rules) {
	return array_values(array_filter($rules, function($r) { return !empty($r['dscp']); }));
}

$p = scg_build_plan($s);
check(!scg_test_probes($p['rules']) && !$p['probes'], 'option off: no probe rules');

$s['latprobe'] = true;
$p = scg_build_plan($s);
$pr = scg_test_probes($p['rules']);
check(count($pr) == 2, 'one probe rule per enabled interface (' . count($pr) . ')');
$r = $pr[0];
check($r['direction'] == 'out' && $r['protocol'] == 'icmp' && $r['icmptype'] == 'echoreq' && $r['ipprotocol'] == 'inet',
	'probe rule: ICMP echo request, direction out');
check($r['interface'] == 'wan,opt5', 'probe rule on the interfaces with a gateway (' . $r['interface'] . ')');
check(($r['source']['network'] ?? '') == '(self)' && isset($r['destination']['any']), 'source = pfSense itself, destination any');
check($r['dnpipe'] == 'U_LAN_qInteractive' && $r['pdnpipe'] == 'D_LAN_qInteractive', 'default: probe goes through the interactive class of its interface');
check(scg_is_generated_rule($r) && strpos($r['descr'], 'SCG Pipe U/D_LAN_qInteractive') === 0 && strlen($r['descr']) <= SCG_DESCR_MAX,
	'probe rule is marked: ' . $r['descr']);
check($pr[0]['dscp'] !== $pr[1]['dscp'], 'each interface has its own DSCP value');
$last = array_slice($p['rules'], -2);
check($last == $pr, 'probe rules at the end of the generated block');
$pool = scg_latprobe_tos_pool();
$ok = count($pool) == 16 && count(array_unique($pool)) == 16;
foreach ($pool as $t) {
	$ok = $ok && (hexdec($t) & 3) == 0 && ((hexdec($t) >> 2) & 3) == 3;
}
check($ok, 'DSCP pool: 16 values from pool 2 (xxxx11), ECN bits zero');
check(scg_rule_identity($pr[0]) !== scg_rule_identity(array_merge($pr[0], array('dscp' => $pr[1]['dscp']))), 'DSCP is part of the rule identity');

/* selectable class; unknown class falls back to the interactive class */
$sel = $s;
$sel['latclass'] = 'qRealtime';
$q = scg_test_probes(scg_build_plan($sel)['rules']);
check($q[0]['dnpipe'] == 'U_LAN_qRealtime' && $q[1]['pdnpipe'] == 'D_GUEST_qRealtime', 'selected class is used: ' . $q[0]['dnpipe']);
$sel['latclass'] = 'qVoIP';
check(scg_latprobe_class($sel) == 'qInteractive', 'class from another scheme falls back to the interactive class');
$sel['latclass'] = 'qRealtime';
$cl = scg_switch_scheme($sel, 'classic');
check($cl['latclass'] == 'qControl' && scg_latprobe_class($cl) == 'qControl', 'scheme switch converts the class (' . $cl['latclass'] . ')');
$cl['latclass'] = '';
check(scg_latprobe_class($cl) == 'qOthersHigh', 'classic default: qOthersHigh');

/* more interfaces than DSCP values */
$many = $s;
$many['vlans'] = array();
for ($i = 0; $i < 18; $i++) {
	$many['vlans'][] = array('if' => 'lan', 'enable' => true, 'tag' => 'T' . $i, 'factor' => 1, 'down' => 'LimDown', 'up' => 'LimUp');
}
$p = scg_build_plan($many);
check(count($p['probes']) == 16 && count($p['notes']) == 1, 'at most 16 probes, with a note');

/* apply: channels come from the generated rules */
ob_start();
$e = array(); $m = array();
$ok = scg_apply($s, $e, $m);
ob_end_clean();
check($ok, 'apply with probes: ' . implode(' | ', $e));
$ch = scg_latprobe_channels();
check(count($ch) == 2 && $ch[0]['tag'] == 'LAN' && $ch[0]['if'] == 'lan' && $ch[0]['tos'] == hexdec($pool[0]) && $ch[1]['tag'] == 'GUEST' && $ch[0]['class'] == 'qInteractive',
	'channels from applied rules: ' . json_encode($ch));
$fl = array_values(array_filter(scg_cfg_get('filter/rule'), function($r) { return !empty($r['floating']); }));
$ng = count(array_filter($fl, 'scg_is_generated_rule'));
check($ng > 0 && scg_leading_generated_floating($fl) == $ng, 'all generated rules incl. probes at the top of the floating list');

/* Autorate: per-interface statistics in the status */
$cfg = scg_ar_settings();
$cfg['reflectors'] = '192.0.2.1 192.0.2.2';
$ar = new SCGAutorate($cfg, array());
$ar->setLatChannels($ch);
$t = 5000.0;
for ($i = 0; $i < 50; $i++) {
	$ar->onRtt('192.0.2.1', 12, $t);
	$ar->onLatRtt('LAN', 14 + ($i % 5), $t);
	if ($i % 10 == 9) {
		$ar->onLatLost('GUEST', $t);
	} else {
		$ar->onLatRtt('GUEST', 60 + ($i % 7), $t);
	}
	$t += 0.2;
}
$st = $ar->latStatus($t + 1);
$by = array();
foreach ($st as $x) {
	$by[$x['tag']] = $x;
}
check(isset($by['LAN'], $by['GUEST']) && $by['LAN']['tos'] == $pool[0], 'status per interface with DSCP value');
check($by['LAN']['stats']['avg'] > 14 && $by['LAN']['stats']['avg'] < 18 && $by['GUEST']['stats']['avg'] > 60, 'average per interface (LAN ' .
	$by['LAN']['stats']['avg'] . ' ms, GUEST ' . $by['GUEST']['stats']['avg'] . ' ms)');
check($by['GUEST']['stats']['loss'] > 0 && $by['LAN']['stats']['loss'] == 0, 'loss per interface (' . $by['GUEST']['stats']['loss'] . ' %)');
check(count($by['LAN']['hist']) >= 9 && $by['LAN']['hist'][0][1] !== null, 'per-second history (' . count($by['LAN']['hist']) . ' s)');
$full = $ar->status('socket');
check(isset($full['iflat']) && count($full['iflat']) == 2 && $full['iflat_class'] == 'qInteractive' && $full['iflat'][0]['class'] == 'qInteractive', 'iflat in the service status');
$ar->setLatChannels(array($ch[0]));
check(count($ar->lat) == 1 && count($ar->lat['LAN']['hist']) >= 9, 'channel change keeps the history of unchanged interfaces');
$ar->setLatChannels(array(array_merge($ch[0], array('class' => 'qRealtime'))));
check(count($ar->lat['LAN']['hist']) == 0 && $ar->lat['LAN']['class'] == 'qRealtime', 'class change starts a new history');
