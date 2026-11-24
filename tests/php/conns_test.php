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
