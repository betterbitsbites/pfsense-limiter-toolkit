<?php
/* AQM state of the queues (dnctl queue show) and re-creating limiters whose queues lost CoDel */
require __DIR__ . '/_boot.php';

$W = getenv('SCG_W');

/* Line endings as printed by dnctl (sbin/ipfw/dummynet.c, print_flowset_parms) */
check(scg_dn_aqm_state('q00001 1000 sl. 1 flows (1 buckets) sched 3 weight 70 lmax 0 pri 0  AQM CoDel target 5ms interval 100ms NoECN') === 'codel',
	'AQM CoDel at the end of the line = codel');
check(scg_dn_aqm_state('q00001 1000 sl. 1 flows (1 buckets) sched 3 weight 70 lmax 0 pri 0 droptail') === 'droptail', 'droptail');
check(scg_dn_aqm_state('q00001 1000 sl. 1 flows (1 buckets) sched 3 weight 70 lmax 0 pri 0 ') === 'missing',
	'empty end = AQM flag without attached AQM');
check(scg_dn_aqm_state('q00001  50 sl. 0 flows (1 buckets) sched 3 weight 1 lmax 0 pri 0  AQM type PIE target 15ms tupdate 15ms') === 'pie', 'PIE');
$q = scg_dn_parse_queues(array(
	'q00001 1000 sl. 0 flows (1 buckets) sched 3 weight 70 lmax 0 pri 0 ',
	'	  RED w_q 0.002000 min_th 5 max_th 15 max_p 0.100000',
	'q00002 1000 sl. 1 flows (1 buckets) sched 3 weight 35 lmax 0 pri 0 ',
	'  0 ip           0.0.0.0/0             0.0.0.0/0      149    45675  0    0   0',
));
check($q[1]['aqm'] === 'red' && $q[2]['aqm'] === 'missing' && $q[2]['bytes'] == 45675, 'RED on the next line; bucket lines still counted');

/* Snapshot carries the state per queue */
@unlink("$W/run/dn_noaqm");
$snap = scg_dn_snapshot();
$all = array();
foreach ($snap['pipes'] as $p) {
	foreach ($p['queues'] as $qq) {
		$all[] = $qq['aqm'];
	}
}
check($all && count(array_unique($all)) == 1 && $all[0] === 'codel', 'snapshot: all queues CoDel (' . count($all) . ')');

/* No rules file: nothing to do */
@unlink("$W/run/rules.limiter");
check(scg_dn_recreate() === null, 'no rules file: no action');

file_put_contents("$W/run/rules.limiter", "pipe 3 config bw 800Mb type wf2q+ droptail\nsched 3 config pipe 3 type wf2q+\npipe 4 config bw 45Mb type wf2q+\n");
$GLOBALS['mwexec_log'] = array();
check(scg_dn_recreate() === array(0, 0) && !$GLOBALS['mwexec_log'], 'all queues with CoDel: nothing re-created');

/* Queues of limiter 3 lost their AQM */
touch("$W/run/dn_noaqm");
$GLOBALS['mwexec_log'] = array();
$rc = scg_dn_recreate();
$log = implode("\n", $GLOBALS['mwexec_log']);
$qdel = preg_match_all('/dnctl queue delete \'?(\d+)/', $log, $m);
check($rc[0] == 28, 'counts affected queues (' . $rc[0] . ')');
check($qdel == 28 && max($m[1]) == 28, 'deletes only the queues of the affected limiter (' . $qdel . ')');
check(strpos($log, 'dnctl pipe delete 3') !== false && strpos($log, 'dnctl pipe delete 4') === false, 'deletes only the affected limiter');
$posLoad = strpos($log, "rules.limiter'");
check($posLoad !== false && $posLoad > strpos($log, 'dnctl pipe delete 3'), 'reloads the rules file after deleting');
check(preg_match("/dnctl 'pipe' '3' 'config' 'bw' '950000Kb' 'type' 'wf2q\\+' 'droptail'/", $log) === 1 && strrpos($log, 'bw') > $posLoad,
	'restores the running bandwidth after reloading');
@unlink("$W/run/dn_noaqm");
@unlink("$W/run/rules.limiter");
