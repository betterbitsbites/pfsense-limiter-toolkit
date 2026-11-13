<?php
/*
 * firewall_shaper_classgen.php
 *
 * Shaper Class Generator - GUI
 * Licensed under the Apache License, Version 2.0
 */

##|+PRIV
##|*IDENT=page-firewall-shaperclassgen
##|*NAME=Firewall: Traffic Shaper: Class Generator
##|*DESCR=Allow access to the 'Firewall: Traffic Shaper: Class Generator' page.
##|*MATCH=firewall_shaper_classgen.php*
##|-PRIV

require_once("guiconfig.inc");
/* Waehrend eines Paket-Updates fehlen die Bibliotheken kurz: ruhig abbrechen statt PHP-Absturzbericht */
foreach (array('shaperclassgen.inc', 'shaperclassgen_i18n.inc', 'shaperclassgen_autorate.inc', 'shaperclassgen_speedtest.inc') as $scg_f) {
	if (!is_readable("/usr/local/pkg/{$scg_f}")) {
		http_response_code(503);
		header('Retry-After: 10');
		echo 'ShaperClassGen is being updated, please reload in a few seconds.';
		exit;
	}
}
require_once("/usr/local/pkg/shaperclassgen.inc");

$pgtitle = array(gettext("Firewall"), gettext("Traffic Shaper"), scg_t('Class Generator'));
$pglinks = array("", "firewall_shaper.php", "@self");
$shortcut_section = "trafficshaper-limiters";

function h($v) {
	return htmlspecialchars((string)$v, ENT_QUOTES, 'UTF-8');
}

$ifaces = scg_local_interfaces();
$pipes = scg_pipes();
$presets = scg_prio_presets();
$s = scg_load_settings();
$classes = scg_classes($s);

/* Rows for all local interfaces */
$vrows = array();
foreach ($s['vlans'] as $v) {
	if (isset($v['if'])) {
		$vrows[$v['if']] = $v;
	}
}
$pipenames = array_keys($pipes);
foreach ($ifaces as $if => $descr) {
	if (!isset($vrows[$if])) {
		$vrows[$if] = array(
			'if' => $if, 'enable' => false,
			'tag' => substr(preg_replace('/[^A-Za-z0-9]/', '', strtoupper($descr)), 0, 10),
			'prio' => 'mid', 'factor' => 0.5,
			'down' => $pipenames[0] ?? '', 'up' => $pipenames[1] ?? ($pipenames[0] ?? ''),
		);
	}
}

$input_errors = array();
$msgs = array();
$preview = null;

if ($_POST) {
	$act = $_POST['act'] ?? 'save';

	if ($act == 'remove') {
		scg_remove_all($msgs);
		$pipes = scg_pipes();
	} else {
		$vl = array();
		foreach ($ifaces as $if => $descr) {
			$prio = $_POST['vl_prio'][$if] ?? 'mid';
			$factor = isset($presets[$prio]) ? $presets[$prio] : floatval(str_replace(',', '.', $_POST['vl_factor'][$if] ?? '0.5'));
			$vl[] = array(
				'if' => $if,
				'enable' => !empty($_POST['vl_enable'][$if]),
				'tag' => trim($_POST['vl_tag'][$if] ?? ''),
				'prio' => $prio,
				'factor' => $factor,
				'down' => $_POST['vl_down'][$if] ?? '',
				'up' => $_POST['vl_up'][$if] ?? '',
			);
		}
		$s['vlans'] = $vl;
		foreach ($classes as $c => $d) {
			$s['classes'][$c] = array(
				'weight' => trim($_POST['cl_weight'][$c] ?? $d['weight']),
				'aqm' => $_POST['cl_aqm'][$c] ?? 'codel',
				'qlimit' => trim($_POST['cl_qlimit'][$c] ?? ''),
			);
		}
		$s['voip'] = array(
			'enable' => !empty($_POST['voip_enable']),
			'provider' => $_POST['voip_provider'] ?? 'Asterisk',
			'address' => trim($_POST['voip_address'] ?? ''),
		);
		$s['penalty'] = trim($_POST['penalty'] ?? '');
		$s['devudp'] = !empty($_POST['devudp']);
		$s['gamenets'] = array_values(array_intersect(array_keys(scg_gamenet_presets()), (array)($_POST['gamenets'] ?? array())));
		$s['gamenets_extra'] = trim($_POST['gamenets_extra'] ?? '');
		$s['gameservers'] = trim($_POST['gameservers'] ?? '');
		$s['asn_url'] = trim($_POST['asn_url'] ?? $s['asn_url']);
		$s['games'] = array_values(array_intersect(array_keys(scg_games_list()), (array)($_POST['games'] ?? array())));
		$s['p2p'] = array_values(array_intersect(array_keys(scg_p2p_list()), (array)($_POST['p2p'] ?? array())));
		foreach (scg_others_list() as $k => $d) {
			$val = $_POST['others'][$k] ?? '';
			$s['others'][$k] = ($val === '' || isset($classes[$val])) ? $val : '';
		}
		$s['srcports'] = !empty($_POST['srcports']);
		$s['ecn'] = !empty($_POST['ecn']);
		$s['latprobe'] = !empty($_POST['latprobe']);
		$s['aliasname'] = trim($_POST['aliasname'] ?? 'SCG_LOCALNETS');
		$s['localnets'] = trim($_POST['localnets'] ?? '');

		/* Schema gewechselt: Klassen umschluesseln, speichern und zur Kontrolle anzeigen */
		$newscheme = $_POST['scheme'] ?? scg_scheme($s);
		$switched = isset(scg_schemes()[$newscheme]) && $newscheme !== scg_scheme($s);
		if ($switched) {
			$s = scg_switch_scheme($s, $newscheme);
			$classes = scg_classes($s);
		}

		$active = scg_validate($s, $input_errors);

		if (!$input_errors) {
			if ($switched && $act != 'preview') {
				scg_store_settings($s);
				write_config("ShaperClassGen: class scheme changed to " . $newscheme);
				$msgs[] = scg_t('Class scheme changed. Classes and application assignments were converted automatically; check them below, then click "Generate / Update". Nothing has been generated yet.');
			} elseif ($act == 'generate') {
				if ($active == 0) {
					$input_errors[] = scg_t('Enable at least one interface.');
				} else {
					scg_apply($s, $input_errors, $msgs);
					$pipes = scg_pipes();
				}
			} elseif ($act == 'preview') {
				$preview = scg_build_plan($s);
			} else {
				scg_store_settings($s);
				write_config("ShaperClassGen: settings saved");
				$msgs[] = scg_t('Settings saved (nothing generated yet).');
			}
		}
		foreach ($s['vlans'] as $v) {
			$vrows[$v['if']] = $v;
		}
	}
}

include("head.inc");

if ($input_errors) {
	print_input_errors($input_errors);
}
foreach ($msgs as $m) {
	print_info_box(h($m), 'info');
}

scg_tabs('classgen');
$simple = (scg_scheme($s) == 'simple');

if (empty($pipes)) {
	print_info_box(scg_t('No limiters exist yet. First create one download and one upload limiter under Firewall &gt; Traffic Shaper &gt; Limiters (scheduler WF2Q+).'), 'warning', false);
}

function pipe_select($name, $sel, $pipes) {
	$o = '<select class="form-control" name="' . h($name) . '">';
	$o .= '<option value="">-</option>';
	foreach ($pipes as $pn => $pd) {
		$o .= '<option value="' . h($pn) . '"' . ($pn === $sel ? ' selected' : '') . '>' . h($pn) . ($pd['bw'] ? ' (' . h($pd['bw']) . ')' : '') . '</option>';
	}
	return $o . '</select>';
}

function class_select($name, $sel, $classes, $allowEmpty = true) {
	$o = '<select class="form-control input-sm" name="' . h($name) . '">';
	if ($allowEmpty) {
		$o .= '<option value="">- (qDefault)</option>';
	}
	foreach ($classes as $c => $d) {
		if ($c == 'qDefault') {
			continue;
		}
		$o .= '<option value="' . h($c) . '"' . ($c === $sel ? ' selected' : '') . '>' . h($c) . '</option>';
	}
	return $o . '</select>';
}
?>

<?php if ($preview !== null): ?>
<div class="panel panel-info">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('Preview (not applied yet)')?></h2></div>
	<div class="panel-body">
		<p><?=scg_t('%d queues, %d floating match rules.', count($preview['queues']), count($preview['rules']))?>
		<?=scg_t('Rules per interface:')?>
		<?php foreach ($preview['perif'] as $if => $n): ?>
			<b><?=h($ifaces[$if] ?? $if)?></b>: <?=$n?> &nbsp;
		<?php endforeach; ?>
		<?php if ($preview['probes']): ?>
			<?=scg_t('Latency probes:')?> <?=count($preview['probes'])?>
		<?php endif; ?></p>
		<?php foreach ($preview['notes'] as $n): ?>
			<p class="text-warning"><?=h($n)?></p>
		<?php endforeach; ?>
		<table class="table table-condensed table-striped">
			<thead><tr><th>Limiter</th><th>Queue</th><th><?=scg_t('Weight')?></th><th>AQM</th><th><?=scg_t('Description')?></th></tr></thead>
			<tbody>
			<?php foreach ($preview['queues'] as $q): ?>
				<tr><td><?=h($q['pipe'])?></td><td><?=h($q['name'])?></td><td><?=h($q['weight'])?></td><td><?=h($q['aqm'])?></td><td><?=h($q['description'])?></td></tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		<details><summary><?=scg_t('Show rules')?></summary>
		<table class="table table-condensed">
			<thead><tr><th>Interface</th><th>Proto</th><th><?=scg_t('Source')?></th><th><?=scg_t('Destination')?></th><th>In (Upload)</th><th>Out (Download)</th><th><?=scg_t('Description')?></th></tr></thead>
			<tbody>
			<?php $allifs = get_configured_interface_with_descr(); foreach ($preview['rules'] as $r):
				$src = isset($r['source']['any']) ? 'any' : ($r['source']['address'] ?? ($r['source']['network'] ?? ''));
				if (isset($r['source']['port'])) { $src .= ':' . $r['source']['port']; }
				if (isset($r['destination']['any'])) {
					$dst = 'any';
				} else {
					$dst = (isset($r['destination']['not']) ? '!' : '') . $r['destination']['address'] . (isset($r['destination']['port']) ? ':' . $r['destination']['port'] : '');
				}
				$rif = implode(', ', array_map(function ($x) use ($allifs) { return $allifs[$x] ?? $x; }, explode(',', $r['interface'])));
				$proto = ($r['protocol'] ?? 'any') . ' ' . $r['ipprotocol'] . (($r['direction'] ?? 'in') == 'out' ? ' out' : '') . (!empty($r['dscp']) ? ' tos ' . $r['dscp'] : '');
			?>
				<tr><td><?=h($rif)?></td><td><?=h($proto)?></td><td><?=h($src)?></td><td><?=h($dst)?></td><td><?=h($r['dnpipe'])?></td><td><?=h($r['pdnpipe'])?></td><td><?=h($r['descr'])?></td></tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		</details>
	</div>
</div>
<?php endif; ?>

<form method="post" action="firewall_shaper_classgen.php">

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('1. Framework: interfaces and limiters')?></h2></div>
	<div class="panel-body">
		<p class="help-block"><?=scg_t('For every enabled interface, %d child queues are created under the selected download and upload limiter. Queue weight = class weight &times; priority factor (1&ndash;100). The limiter must use the <b>WF2Q+</b> scheduler. If only one interface is active, it still gets the full bandwidth.', count($classes))?></p>
		<?=scg_limiter_hint_html()?>
		<table class="table table-condensed table-striped">
			<thead><tr><th><?=scg_t('Active')?></th><th>Interface</th><th><?=scg_t('Tag')?></th><th><?=scg_t('Priority')?></th><th><?=scg_t('Factor (for "Custom")')?></th><th><?=scg_t('Download limiter')?></th><th><?=scg_t('Upload limiter')?></th></tr></thead>
			<tbody>
			<?php foreach ($ifaces as $if => $descr): $v = $vrows[$if]; ?>
				<tr>
					<td><input type="checkbox" name="vl_enable[<?=h($if)?>]" value="1" <?=!empty($v['enable']) ? 'checked' : ''?>></td>
					<td><?=h($descr)?> <small class="text-muted">(<?=h($if)?>)</small></td>
					<td><input class="form-control" type="text" maxlength="10" name="vl_tag[<?=h($if)?>]" value="<?=h($v['tag'])?>"></td>
					<td><select class="form-control" name="vl_prio[<?=h($if)?>]">
						<?php foreach (array('high' => scg_t('High (1.0)'), 'mid' => scg_t('Medium (0.5)'), 'low' => scg_t('Low (0.2)'), 'custom' => scg_t('Custom')) as $pk => $pl): ?>
							<option value="<?=$pk?>" <?=($v['prio'] ?? 'mid') == $pk ? 'selected' : ''?>><?=$pl?></option>
						<?php endforeach; ?>
					</select></td>
					<td><input class="form-control" type="text" name="vl_factor[<?=h($if)?>]" value="<?=h($v['factor'])?>"></td>
					<td><?=pipe_select("vl_down[{$if}]", $v['down'] ?? '', $pipes)?></td>
					<td><?=pipe_select("vl_up[{$if}]", $v['up'] ?? '', $pipes)?></td>
				</tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		<?php if ($pipes):
			$usedp = array();
			foreach ($vrows as $vr) {
				if (!empty($vr['enable'])) {
					$usedp[$vr['down'] ?? ''] = true;
					$usedp[$vr['up'] ?? ''] = true;
				}
			}
		?>
		<p class="help-block"><?=scg_t('Existing limiters:')?>
		<?php foreach ($pipes as $pn => $pd): ?>
			<b><?=h($pn)?></b> <?=h($pd['bw'])?>, Scheduler <?=h($pd['sched'] ?: '?')?>, <?=h($pd['nq'])?> Queues<?php
			if (isset($usedp[$pn]) && $pd['sched'] !== '' && stripos($pd['sched'], 'wf2q') === false && stripos($pd['sched'], 'qfq') === false) {
				echo ' <span class="scg-hot">(' . scg_t('not WF2Q+!') . ')</span>';
			} ?>; &nbsp;
		<?php endforeach; ?></p>
		<?php endif; ?>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('2. Classes')?></h2></div>
	<div class="panel-body">
		<div class="row" style="margin-bottom:8px">
			<div class="col-sm-3"><label for="scg_scheme"><?=scg_t('Class scheme')?></label></div>
			<div class="col-sm-9">
				<select class="form-control" name="scheme" id="scg_scheme">
					<?php foreach (scg_schemes() as $sk => $sl): ?>
						<option value="<?=h($sk)?>" <?=scg_scheme($s) == $sk ? 'selected' : ''?>><?=h(scg_t($sl))?></option>
					<?php endforeach; ?>
				</select>
				<span class="help-block"><?=scg_t('<b>Four classes</b> sort traffic by behaviour and are easier to tune: real-time (telephony, games, DNS &ndash; small flows that must never wait), interactive (remote sessions), default and bulk (downloads, backups, P2P, which may take all free bandwidth but always yield). <b>Seven classes</b> follow the pfSense wizard. When you switch, classes and application assignments are converted automatically and shown for review; existing rules and queues are only replaced with "Generate / Update". Afterwards reset the firewall states once.')?></span>
			</div>
		</div>
		<table class="table table-condensed table-striped">
			<thead><tr><th><?=scg_t('Class')?></th><th><?=scg_t('Base weight (1&ndash;100)')?></th><th>AQM</th><th><?=scg_t('Queue length')?></th><th><?=scg_t('Contents')?></th></tr></thead>
			<tbody>
			<?php foreach ($classes as $c => $d): $cs = $s['classes'][$c]; ?>
				<tr>
					<td><b><?=h($c)?></b></td>
					<td><input class="form-control" type="number" min="1" max="100" name="cl_weight[<?=h($c)?>]" value="<?=h($cs['weight'])?>"></td>
					<td><select class="form-control" name="cl_aqm[<?=h($c)?>]">
						<option value="codel" <?=$cs['aqm'] == 'codel' ? 'selected' : ''?>>CoDel</option>
						<option value="droptail" <?=$cs['aqm'] == 'droptail' ? 'selected' : ''?>>Tail Drop</option>
					</select></td>
					<td><input class="form-control" type="text" placeholder="<?=h(scg_t('Default'))?>" name="cl_qlimit[<?=h($c)?>]" value="<?=h($cs['qlimit'])?>"></td>
					<td><small><?=h(scg_t($d['hint']))?></small></td>
				</tr>
			<?php endforeach; ?>
			</tbody>
		</table>
		<label><input type="checkbox" name="ecn" value="1" <?=!empty($s['ecn']) ? 'checked' : ''?>> <?=scg_t('Enable ECN on the queues')?></label>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t($simple ? '3. Matching: devices and VoIP' : '3. Matching: VoIP and penalty box')?></h2></div>
	<div class="panel-body">
		<div class="row">
			<div class="col-sm-3"><label><input type="checkbox" name="voip_enable" value="1" <?=!empty($s['voip']['enable']) ? 'checked' : ''?>> <?=scg_t('Prioritize VoIP')?></label></div>
			<div class="col-sm-4"><select class="form-control" name="voip_provider">
				<?php foreach (scg_voip_list() as $k => $d): ?>
					<option value="<?=h($k)?>" <?=$s['voip']['provider'] == $k ? 'selected' : ''?>><?=h(scg_t($d['label']))?></option>
				<?php endforeach; ?>
			</select></div>
			<?php if (!$simple): ?>
			<div class="col-sm-5"><input class="form-control" type="text" name="voip_address" placeholder="<?=h(scg_t('Optional: IP/alias of the VoIP device (replaces the port list)'))?>" value="<?=h($s['voip']['address'])?>"></div>
			<?php endif; ?>
		</div>
		<?php if ($simple): ?>
		<br>
		<div class="row">
			<div class="col-sm-3"><b><?=scg_t('Real-time devices')?></b> &rarr; qRealtime</div>
			<div class="col-sm-9"><input class="form-control" type="text" name="voip_address" placeholder="<?=h(scg_t('Optional: IP/alias of VoIP phones, game consoles, gaming PCs'))?>" value="<?=h($s['voip']['address'])?>"></div>
		</div>
		<?php endif; ?>
		<div class="row">
			<div class="col-sm-9 col-sm-offset-3">
				<label style="font-weight:normal"><input type="checkbox" name="devudp" value="1" <?=!empty($s['devudp']) ? 'checked' : ''?>> <?=scg_t('Only UDP traffic of these devices (recommended)')?></label>
				<span class="help-block"><?=scg_t('Game and voice traffic is almost always UDP, while game downloads, updates and stores use TCP or QUIC (UDP 443). With this option a 60 GB console update stays in qDefault instead of running at real-time weight. Without it, the entire traffic of the device is prioritized.')?></span>
			</div>
		</div>
		<br>
		<div class="row">
			<div class="col-sm-3"><b><?=scg_t($simple ? 'Bulk devices / penalty box' : 'Penalty box')?></b> &rarr; <?=h(scg_role_class($s, 'low'))?></div>
			<div class="col-sm-9"><input class="form-control" type="text" name="penalty" placeholder="<?=h(scg_t($simple ? 'Optional: IP/alias whose entire traffic goes to qBulk (backup server, download box, penalty box)' : 'Optional: IP/alias whose entire traffic goes to qOthersLow'))?>" value="<?=h($s['penalty'])?>"></div>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('4. Matching: games')?> &rarr; <?=h(scg_role_class($s, 'games'))?>
		<label class="pull-right" style="font-weight:normal;margin:0"><input type="checkbox" class="scg-all" data-target="games[]"> <?=scg_t('All')?></label></h2></div>
	<div class="panel-body">
		<p class="help-block"><?=scg_t('Current platforms with the ports documented by the vendors (game traffic only, no download ports). Many modern games use changing UDP ports on cloud servers; for those, use the game networks below or the real-time devices.')?></p>
		<div class="row">
		<?php foreach (scg_games_list() as $k => $d): if (empty($d['m'])) continue; ?>
			<div class="col-sm-4"><label><input type="checkbox" name="games[]" value="<?=h($k)?>" <?=in_array($k, (array)$s['games']) ? 'checked' : ''?>> <?=h(scg_t($d['label']))?></label></div>
		<?php endforeach; ?>
		</div>
		<?php $legacy_on = count(array_filter((array)$s['games'], function($k) { $l = scg_games_list(); return isset($l[$k]) && empty($l[$k]['m']); })); ?>
		<details <?=$legacy_on ? 'open' : ''?> style="margin-top:8px"><summary><?=scg_t('Legacy list of the pfSense wizard (games from 2004&ndash;2010)')?><?=$legacy_on ? ' (' . $legacy_on . ')' : ''?></summary>
		<div class="row" style="margin-top:6px">
		<?php foreach (scg_games_list() as $k => $d): if (!empty($d['m'])) continue; ?>
			<div class="col-sm-3"><label><input type="checkbox" name="games[]" value="<?=h($k)?>" <?=in_array($k, (array)$s['games']) ? 'checked' : ''?>> <?=h(scg_label($d))?></label></div>
		<?php endforeach; ?>
		</div>
		</details>
		<div class="row" style="margin-top:10px">
			<div class="col-sm-3"><label><?=scg_t('Game servers')?></label></div>
			<div class="col-sm-9"><input class="form-control" type="text" name="gameservers" placeholder="<?=h(scg_t('e.g. mc.example.net 203.0.113.10 or an alias'))?>" value="<?=h($s['gameservers'])?>">
			<span class="help-block"><?=scg_t('The servers you play on (IP, network, host name or one existing alias, separated by spaces). All traffic to them, TCP and UDP, goes to the class above &ndash; for games that use TCP or changing ports, e.g. Minecraft Java. Host names are resolved by pfSense regularly. Only enter game servers: a server that also delivers downloads would get them prioritized as well.')?></span></div>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('4b. Matching: game networks (by publisher)')?> &rarr; <?=h(scg_role_class($s, 'games'))?></h2></div>
	<div class="panel-body">
		<p class="help-block"><?=scg_t('Publishers that run their game servers in their own network (autonomous system). The address ranges are loaded as a URL table alias and refreshed daily. Only UDP except port 443 is matched, because downloads use TCP or QUIC &ndash; Valve, for example, also delivers Steam downloads from the same network.')?></p>
		<div class="row">
		<?php foreach (scg_gamenet_presets() as $k => $d): ?>
			<div class="col-sm-4"><label><input type="checkbox" name="gamenets[]" value="<?=h($k)?>" <?=in_array($k, (array)$s['gamenets']) ? 'checked' : ''?>> <?=h($d['label'])?> <small class="text-muted">AS<?=h(implode(', AS', $d['asn']))?></small></label></div>
		<?php endforeach; ?>
		</div>
		<div class="row" style="margin-top:8px">
			<div class="col-sm-3"><label><?=scg_t('Additional AS numbers')?></label></div>
			<div class="col-sm-9"><input class="form-control" type="text" name="gamenets_extra" placeholder="<?=h(scg_t('e.g. AS12345 AS67890'))?>" value="<?=h($s['gamenets_extra'])?>"></div>
		</div>
		<div class="row" style="margin-top:8px">
			<div class="col-sm-3"><label><?=scg_t('Source of the address lists')?></label></div>
			<div class="col-sm-9"><input class="form-control" type="text" name="asn_url" value="<?=h($s['asn_url'])?>">
			<span class="help-block"><?=scg_t('URL that returns one network (CIDR) per line for an AS number; %%s is replaced by the number. pfSense must be able to reach it.', '')?></span></div>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('5. Matching: peer-to-peer')?> &rarr; <?=h(scg_role_class($s, 'p2p'))?>
		<label class="pull-right" style="font-weight:normal;margin:0"><input type="checkbox" class="scg-all" data-target="p2p[]"> <?=scg_t('All')?></label></h2></div>
	<div class="panel-body"><div class="row">
		<?php foreach (scg_p2p_list() as $k => $d): ?>
			<div class="col-sm-3"><label><input type="checkbox" name="p2p[]" value="<?=h($k)?>" <?=in_array($k, (array)$s['p2p']) ? 'checked' : ''?>> <?=h(scg_label($d))?></label></div>
		<?php endforeach; ?>
	</div></div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('6. Matching: other applications')?></h2></div>
	<div class="panel-body">
		<p class="help-block"><?=scg_t('As in the wizard (there H/D/L), freely selectable here. "- (qDefault)" creates no rule of its own.')?></p>
		<div class="row" style="margin-bottom:10px">
			<div class="col-sm-4"><select class="form-control input-sm" id="scg_bulk_class">
				<option value="">- (qDefault)</option>
				<?php foreach ($classes as $c => $d): if ($c == 'qDefault') continue; ?>
					<option value="<?=h($c)?>"><?=h($c)?></option>
				<?php endforeach; ?>
			</select></div>
			<div class="col-sm-8">
				<button type="button" class="btn btn-sm btn-default" id="scg_bulk_set"><?=scg_t('Set all to this class')?></button>
				<button type="button" class="btn btn-sm btn-default" id="scg_bulk_def"><?=scg_t('Defaults')?></button>
			</div>
		</div>
		<div class="row">
		<?php foreach (scg_others_list() as $k => $d): ?>
			<div class="col-sm-4" style="margin-bottom:6px">
				<div class="row">
					<div class="col-xs-6"><?=h(scg_label($d))?></div>
					<div class="col-xs-6"><?=class_select("others[{$k}]", $s['others'][$k] ?? '', $classes)?></div>
				</div>
			</div>
		<?php endforeach; ?>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('7. Options')?></h2></div>
	<div class="panel-body">
		<div class="form-group">
			<label><?=scg_t('Local networks (never shaped, inter-VLAN traffic stays free)')?></label>
			<textarea class="form-control" rows="2" name="localnets"><?=h($s['localnets'])?></textarea>
			<span class="help-block"><?=scg_t('CIDR, separated by spaces. Add your own public IPv6 prefixes here, otherwise inter-VLAN IPv6 is shaped as well.')?></span>
		</div>
		<div class="form-group">
			<label><?=scg_t('Alias name')?></label>
			<input class="form-control" type="text" name="aliasname" value="<?=h($s['aliasname'])?>">
		</div>
		<label><input type="checkbox" name="srcports" value="1" <?=!empty($s['srcports']) ? 'checked' : ''?>> <?=scg_t('Also match source port (like the wizard\'s "both", doubles the number of rules)')?></label>
		<br>
		<label><input type="checkbox" name="latprobe" value="1" <?=!empty($s['latprobe']) ? 'checked' : ''?>> <?=scg_t('Measure latency per interface through its qDefault queue (Limiter Live)')?></label>
		<span class="help-block"><?=scg_t('Shows whether interfaces with a higher weight really get a lower latency under load. Adds one floating match rule per interface on the interfaces with a gateway (ICMP from pfSense itself, marked with its own DSCP value). The Autorate service then sends about 5 pings per second per interface to the Autorate ping targets. Needs Autorate or its latency measurement to be enabled.')?></span>
	</div>
</div>

<input type="hidden" name="act" id="scg_act" value="save">
<nav class="action-buttons">
	<button type="submit" class="btn btn-default" onclick="document.getElementById('scg_act').value='save'"><i class="fa-solid fa-save"></i> <?=scg_t('Save')?></button>
	<button type="submit" class="btn btn-info" onclick="document.getElementById('scg_act').value='preview'"><i class="fa-solid fa-eye"></i> <?=scg_t('Preview')?></button>
	<button type="submit" class="btn btn-primary" onclick="document.getElementById('scg_act').value='generate'"><i class="fa-solid fa-play"></i> <?=scg_t('Generate / Update')?></button>
	<button type="submit" class="btn btn-danger" onclick="if(!confirm(<?=h(json_encode(scg_t('Remove all queues, rules and the alias created by the generator?')))?>))return false;document.getElementById('scg_act').value='remove'"><i class="fa-solid fa-trash-can"></i> <?=scg_t('Remove generated objects')?></button>
</nav>
</form>

<?php
$scg_defaults = array();
foreach (scg_others_list() as $k => $d) {
	$scg_defaults[$k] = $d['def'] === '' ? '' : scg_role_class($s, $d['def']);
}
?>
<style>.scg-hot { color: #ff5c5c !important; font-weight: 600; }</style>
<script type="text/javascript">
//<![CDATA[
events.push(function() {
	/* Alias autocomplete as in the firewall rules */
	var addressarray = <?=json_encode(scg_alias_names())?>;
	if ($.fn.autocomplete) {
		$('input[name="voip_address"], input[name="penalty"]').autocomplete({ source: addressarray });
	}

	/* "All" toggles for games / P2P */
	function syncAll(box) {
		var t = $('input[name="' + $(box).data('target') + '"]');
		$(box).prop('checked', t.length > 0 && t.filter(':checked').length == t.length);
	}
	$('.scg-all').each(function() {
		var box = this;
		syncAll(box);
		$('input[name="' + $(box).data('target') + '"]').on('change', function() { syncAll(box); });
		$(box).on('click', function(e) {
			e.stopPropagation();
			$('input[name="' + $(box).data('target') + '"]').prop('checked', $(box).prop('checked'));
		});
	});

	/* Schema-Wechsel: sofort speichern, damit die umgerechneten Klassen angezeigt werden */
	$('#scg_scheme').on('change', function() {
		if (confirm(<?=json_encode(scg_t('Switch the class scheme? Classes and application assignments are converted and saved; nothing is generated until you click "Generate / Update".'))?>)) {
			$('#scg_act').val('save');
			this.form.submit();
		} else {
			$(this).val(<?=json_encode(scg_scheme($s))?>);
		}
	});

	/* Bulk actions for other applications */
	var scgDefaults = <?=json_encode($scg_defaults)?>;
	$('#scg_bulk_set').on('click', function() {
		var v = $('#scg_bulk_class').val();
		$('select[name^="others["]').val(v);
	});
	$('#scg_bulk_def').on('click', function() {
		$.each(scgDefaults, function(k, v) {
			$('select[name="others[' + k + ']"]').val(v);
		});
	});
});
//]]>
</script>
<?php include("foot.inc"); ?>
