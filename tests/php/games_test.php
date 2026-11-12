<?php
/* Game recognition: vendor ports, ASN aliases (UDP, no 443), own game servers, validation */
require __DIR__ . '/_boot.php';
$s = scg_default_settings('simple');
$s['vlans'] = array(array('if' => 'lan', 'enable' => true, 'tag' => 'LAN', 'prio' => 'high', 'factor' => 1, 'down' => 'LimDown', 'up' => 'LimUp'));
foreach ($s['others'] as $k => $v) {
	$s['others'][$k] = '';
}
$s['p2p'] = array();
$s['games'] = array('minecraft', 'xboxnet');
$s['gamenets'] = array('valve');
$s['gamenets_extra'] = 'AS64500';
$s['gameservers'] = 'mc.example.net 203.0.113.10';
$p = scg_build_plan($s);
check(isset($p['aliases']['SCG_AS32590']) && isset($p['aliases']['SCG_AS64500']), 'one URL table alias per AS number');
check(strpos($p['aliases']['SCG_AS32590']['url'], '32590') !== false, 'alias URL contains the AS number');
check(isset($p['aliases']['SCG_GAMESERVERS']) && $p['aliases']['SCG_GAMESERVERS']['address'] == 'mc.example.net 203.0.113.10', 'game server alias');
$asn = array_values(array_filter($p['rules'], function($r) { return ($r['destination']['address'] ?? '') == 'SCG_AS32590'; }));
check(count($asn) == 2 && $asn[0]['protocol'] == 'udp' && $asn[0]['destination']['port'] == '1-442' && $asn[1]['destination']['port'] == '444-65535', 'ASN rules: UDP without 443');
$gs = array_values(array_filter($p['rules'], function($r) { return ($r['destination']['address'] ?? '') == 'SCG_GAMESERVERS'; }));
check(count($gs) == 1 && !isset($gs[0]['protocol']), 'game servers: all protocols');
$mc = array_filter($p['rules'], function($r) { return ($r['destination']['port'] ?? '') == '25565-25600'; });
check(count($mc) == 1, 'Minecraft Java ports');
$xs = array_filter($p['rules'], function($r) { return ($r['source']['port'] ?? '') == '3074'; });
check(count($xs) >= 1, 'Xbox 3074 also as source port');
$s['gamenets_extra'] = 'ASx';
$s['gameservers'] = 'bad_host!';
$e = array();
scg_validate($s, $e);
check(count($e) == 2, 'invalid AS number and game server rejected');
