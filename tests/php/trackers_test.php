<?php
/* Rule trackers: unchanged rules keep tracker and creation time when applying again */
require __DIR__ . '/_boot.php';

$s = scg_default_settings('simple');
$s['vlans'] = array(array('if' => 'lan', 'enable' => true, 'tag' => 'LAN', 'prio' => 'high', 'factor' => 1, 'down' => 'LimDown', 'up' => 'LimUp'));

function scg_test_generated() {
	$out = array();
	foreach (scg_cfg_get('filter/rule', array()) as $r) {
		if (is_array($r) && scg_is_generated_rule($r)) {
			$out[] = $r;
		}
	}
	return $out;
}

ob_start();
$e = array(); $m = array();
$ok1 = scg_apply($s, $e, $m);
/* pretend the first apply happened a day ago (trackers are time based) */
$all = scg_cfg_get('filter/rule', array());
foreach ($all as $i => $r) {
	if (is_array($r) && scg_is_generated_rule($r)) {
		$all[$i]['tracker'] = (string)((int)$r['tracker'] - 86400);
		$all[$i]['created']['time'] -= 86400;
	}
}
scg_cfg_set('filter/rule', $all);
$first = scg_test_generated();
$e = array(); $m = array();
$ok2 = scg_apply($s, $e, $m);
$second = scg_test_generated();
ob_end_clean();

check($ok1 && $ok2, 'apply twice: ' . implode(' | ', $e));
check(count($first) > 0 && count($first) == count($second), 'same number of generated rules (' . count($first) . ')');
$t1 = array_column($first, 'tracker');
$t2 = array_column($second, 'tracker');
check($t1 === $t2, 'unchanged rules keep their trackers');
check(array_column($first, 'created') == array_column($second, 'created'), 'unchanged rules keep their creation time');
check(count(array_unique($t2)) == count($t2), 'trackers stay unique');

/* change the class of one application: only its rules get new trackers */
$app = null;
foreach ($s['others'] as $k => $cls) {
	if ($cls != 'qBulk') { $app = $k; break; }
}
$s['others'][$app] = 'qBulk';
ob_start();
$e = array(); $m = array();
scg_apply($s, $e, $m);
$ok3 = !$e;
$third = scg_test_generated();
ob_end_clean();
$t3 = array_column($third, 'tracker');
check($ok3, "apply after change: " . implode(" | ", $e) . " app=$app");
$changed = count(array_diff($t3, $t2));
check($changed > 0 && $changed < count($t3), "changed application: $changed of " . count($t3) . ' rules get a new tracker');
check(!array_intersect(array_diff($t3, $t2), $t2), 'changed rules never inherit a previous tracker');
check(count(array_unique($t3)) == count($t3), 'trackers stay unique after a change');

/* a non-generated rule keeps its tracker and is never reused */
$user = array_values(array_filter(scg_cfg_get('filter/rule', array()), function($r) { return is_array($r) && !scg_is_generated_rule($r); }));
check(count($user) > 0 && !in_array($user[0]['tracker'], $t3, true), 'user rule tracker not taken by generated rules');
