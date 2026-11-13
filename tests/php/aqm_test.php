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

$rules = "pipe 3 config bw 800Mb type wf2q+ droptail\nsched 3 config pipe 3 type wf2q+\n";
for ($i = 1; $i <= 28; $i++) {
	$rules .= "queue $i config pipe 3 weight 20 queue 1000 codel target 5ms interval 100ms noecn\n";
}
$rules .= "pipe 4 config bw 45Mb type wf2q+\nsched 4 config pipe 4 type wf2q+\n";
for ($i = 29; $i <= 56; $i++) {
	$rules .= "queue $i config pipe 4 weight 20 queue 1000 codel target 5ms interval 100ms noecn\n";
}
file_put_contents("$W/run/rules.limiter", $rules);
$GLOBALS['mwexec_log'] = array();
check(scg_dn_recreate() === array(0, 0) && !$GLOBALS['mwexec_log'], 'all queues with CoDel: nothing re-created');

/* Queues of limiter 3 lost their AQM; capture the files dnctl loads */
touch("$W/run/dn_noaqm");
$GLOBALS['mwexec_log'] = array();
$GLOBALS['mwexec_hook'] = function ($cmd) {
	if (preg_match("/dnctl '([^']+\\.scg_[qp])'/", $cmd, $m)) {
		$GLOBALS['dn_loaded'][] = file_get_contents($m[1]);
	}
};
$GLOBALS['dn_loaded'] = array();
$rc = scg_dn_recreate();
unset($GLOBALS['mwexec_hook']);
$log = implode("\n", $GLOBALS['mwexec_log']);
$qdel = preg_match_all('/dnctl queue delete \'?(\d+)/', $log, $m);
check($rc[0] == 28, 'counts affected queues (' . $rc[0] . ')');
check($qdel == 28 && max($m[1]) == 28, 'deletes only the queues of the affected limiter (' . $qdel . ')');
check(strpos($log, 'dnctl pipe delete 3') !== false && strpos($log, 'dnctl pipe delete 4') === false, 'deletes only the affected limiter');
$posQ = strpos($log, ".scg_q'");
$posP = strpos($log, ".scg_p'");
check($posQ !== false && $posP !== false && $posQ > strpos($log, 'dnctl pipe delete 3') && $posQ < $posP,
	'loads the queues first, then the limiter');
check(strpos($log, "rules.limiter'") === false, 'does not reload the whole rules file');
$ql = $GLOBALS['dn_loaded'][0] ?? '';
$pl = $GLOBALS['dn_loaded'][1] ?? '';
check(substr_count($ql, 'config pipe 3 ') == 28 && strpos($ql, 'pipe 4') === false, 'queue file: only the 28 queues of limiter 3');
check(strpos($pl, "pipe 3 config bw 950000Kb type wf2q+ droptail") !== false && strpos($pl, 'sched 3 config pipe 3') !== false
	&& strpos($pl, 'pipe 4') === false, 'limiter file: pipe 3 with the running bandwidth, sched 3');
check(!glob("$W/run/rules.limiter.scg_*"), 'temporary files removed');
@unlink("$W/run/dn_noaqm");
@unlink("$W/run/rules.limiter");
