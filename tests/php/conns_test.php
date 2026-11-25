<?php
/* Connections that may bypass the limiters: pfctl parsing, groups, kill, reset per interface */
require __DIR__ . '/_boot.php';
$log = dirname(SCG_CONNS_SNAP) . '/pfctl.log';
@unlink($log);
@unlink(SCG_CONNS_SNAP);
file_put_contents(SCG_APPLIED, (string)(time() - 3600));
$lines = array();
exec(SCG_PFCTL . ' -vvss', $lines);
$st = scg_states_parse($lines);
check(count($st) == 6 && $st[0]['dir'] === 'in' && $st[1]['lnat'] === '10.0.1.5:40000' && $st[1]['id'] === 'bbbb000000000002/0000abcd', 'pfctl -vvss parsed (' . count($st) . ' states)');
check($st[0]['age'] == 18000 && $st[0]['bytes'] == 2001000 && $st[0]['rule'] === 'Default allow LAN', 'age, bytes and rule description');
$r = scg_conn_list();
$g = array();
foreach ($r['list'] as $c) {
	$g[$c['grp']][] = $c;
}
check(count($g['old'] ?? array()) == 1 && $g['old'][0]['client'] === '10.0.1.5:40000' && $g['old'][0]['ids'] === 'aaaa000000000001/0000abcd,bbbb000000000002/0000abcd',
	'old LAN connection listed with its NAT state; new one not listed');
check(count($g['portfwd'] ?? array()) == 1 && $g['portfwd'][0]['client'] === '10.0.3.20:27015' && $g['portfwd'][0]['ids'] === 'cccc000000000004/0000abcd,dddd000000000005/0000abcd',
	'port forward listed with the inside state');
check(count($g['self'] ?? array()) == 1 && $g['self'][0]['remote'] === '192.0.2.99:1194', 'pfSense itself listed');
check($r['list'][0]['rate'] === null, 'first query: no rate yet');
$r2 = scg_conn_list(null, microtime(true) + 2);
check($r2['list'][0]['rate'] === 0.0 || $r2['list'][0]['rate'] === 0, 'second query: rate from the byte difference');
check(scg_conn_kill('aaaa000000000001/0000abcd,bad id;rm -rf /,bbbb000000000002/0000abcd') == 2, 'kill only well-formed state IDs');
$l = (string)@file_get_contents($log);
check(strpos($l, '-k id -k aaaa000000000001/0000abcd') !== false && strpos($l, 'rm -rf') === false, 'pfctl called with the state ID');
@unlink($log);
check(scg_if_reset_states('lan') === 3, 'reset LAN: 2 LAN states + 1 NAT state at the WAN');
$l = (string)@file_get_contents($log);
check(strpos($l, '-i igb1 -Fs') !== false && strpos($l, 'bbbb000000000002/0000abcd') !== false, 'interface states flushed, NAT state killed');
check(scg_if_reset_states('wan') === null && scg_if_reset_states('nope') === null, 'WAN and unknown interfaces are refused');

/* gateway per interface (policy routing) from route-to in the states */
@unlink(SCG_CONNS_SNAP);
$mk = function ($b1, $b2) {
	return array(
		'igb1 tcp 192.0.2.10:443 <- 10.0.1.5:40000       ESTABLISHED:ESTABLISHED',
		"   age 00:10:00, expires in 23:59:59, 10:20 pkts, 1000:{$b1} bytes, anchor 0, rule 300",
		'   id: aaaa000000000001 creatorid: 0000abcd route-to: 198.51.100.1@ovpnc1',
		'igb1 tcp 192.0.2.11:443 <- 10.0.1.6:40001       ESTABLISHED:ESTABLISHED',
		"   age 00:10:00, expires in 23:59:59, 10:20 pkts, 1000:{$b2} bytes, anchor 0, rule 300",
		'   id: aaaa000000000002 creatorid: 0000abcd route-to: 203.0.113.1@igb0',
	);
};
$t0 = microtime(true);
scg_conn_list($mk(0, 0), $t0);
$r = scg_conn_list($mk(3000000, 1000000), $t0 + 1);
$rt = (array)$r['routes'];
check(isset($rt['igb1']) && $rt['igb1'][0][0] === 'ovpnc1' && $rt['igb1'][0][1] === 'VPN_PROVIDER' && $rt['igb1'][0][2] == 24000000 && $rt['igb1'][1][0] === 'igb0',
	'LAN traffic split by gateway: ' . json_encode($rt['igb1'] ?? null));
