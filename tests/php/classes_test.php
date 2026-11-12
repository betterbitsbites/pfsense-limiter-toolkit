<?php
/* Class schemes: migration qACK -> qControl, scheme switch, plan size, device rules */
require __DIR__ . '/_boot.php';
$vl = array(array('if' => 'lan', 'enable' => true, 'tag' => 'LAN', 'prio' => 'high', 'factor' => 1, 'down' => 'LimDown', 'up' => 'LimUp'));

/* settings saved by a version before 0.5 */
$legacy = scg_default_settings('classic');
unset($legacy['scheme'], $legacy['classes']['qControl'], $legacy['devudp']);
$legacy['classes']['qACK'] = array('weight' => 77, 'aqm' => 'codel', 'qlimit' => '500');
$legacy['others']['dns'] = 'qACK';
$legacy['vlans'] = $vl;
scg_cfg_set(SCG_CFG, base64_encode(json_encode($legacy)));
$s = scg_load_settings();
check($s['scheme'] == 'classic', 'legacy settings stay on the 7-class scheme');
check(isset($s['classes']['qControl']) && $s['classes']['qControl']['weight'] == 77, 'qACK migrated to qControl with its weight');
check($s['others']['dns'] == 'qControl', 'app assignment qACK -> qControl');
check(empty($s['devudp']), 'legacy device rule keeps whole-device behaviour');
$p = scg_build_plan($s);
check(count($p['queues']) == 14, '7 classes x 2 directions = 14 queues');

$s2 = scg_switch_scheme($s, 'simple');
check(array_keys($s2['classes']) == array('qRealtime', 'qInteractive', 'qDefault', 'qBulk'), 'switch to 4 classes');
check($s2['others']['dns'] == 'qRealtime' && $s2['others']['ssh'] == 'qInteractive', 'apps remapped');
$s2['voip']['address'] = '10.0.0.50';
$s2['devudp'] = true;
$p = scg_build_plan($s2);
check(count($p['queues']) == 8, '4 classes x 2 directions = 8 queues');
$dev = array_values(array_filter($p['rules'], function($r) { return strpos($r['descr'], 'Devices UDP') !== false; }));
check(count($dev) == 2 && $dev[0]['protocol'] == 'udp', 'real-time devices: 2 UDP rules (port 443 excluded)');
$e = array();
scg_validate($s2, $e);
check(!$e, 'validation passes: ' . implode(' | ', $e));

scg_cfg_set(SCG_CFG, '');
@unlink(dirname(SCG_AR_STATUS) . '/backup.json');
check(scg_load_settings()['scheme'] == 'simple', 'new installation starts with 4 classes');
