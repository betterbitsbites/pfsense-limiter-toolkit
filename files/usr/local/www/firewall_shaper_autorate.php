<?php
/*
 * firewall_shaper_autorate.php
 *
 * Settings for the Autorate service of the Shaper Class Generator.
 * Licensed under the Apache License, Version 2.0
 */

##|+PRIV
##|*IDENT=page-firewall-shaperautorate
##|*NAME=Firewall: Traffic Shaper: Autorate
##|*DESCR=Allow access to the 'Firewall: Traffic Shaper: Autorate' page.
##|*MATCH=firewall_shaper_autorate.php*
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
require_once("/usr/local/pkg/shaperclassgen_autorate.inc");

$pgtitle = array(gettext("Firewall"), gettext("Traffic Shaper"), "Autorate");
$pglinks = array("", "firewall_shaper.php", "@self");
$shortcut_section = "trafficshaper-limiters";

function h($v) {
	return htmlspecialchars((string)$v, ENT_QUOTES, 'UTF-8');
}

$pipes = scg_pipes();
$a = scg_ar_settings();
$input_errors = array();
$msgs = array();

if ($_POST) {
	$act = $_POST['act'] ?? 'save';
	if ($act == 'restart') {
		scg_ar_service('restart');
		$msgs[] = scg_t('Service restarted.');
	} else {
		$a['enable'] = !empty($_POST['enable']);
		$a['learnbase'] = !empty($_POST['learnbase']);
		$a['monitor'] = !empty($_POST['monitor']);
		foreach (array('down', 'up') as $d) {
			foreach (array('pipe', 'min', 'base', 'max') as $k) {
				$a[$d][$k] = trim(str_replace(',', '.', $_POST["{$d}_{$k}"] ?? ''));
			}
		}
		$a['reflectors'] = trim($_POST['reflectors'] ?? '');
		$a['wanif'] = trim($_POST['wanif'] ?? '');
		foreach (array('interval', 'threshold', 'decfactor', 'incstep', 'tick') as $k) {
			$a[$k] = trim(str_replace(',', '.', $_POST[$k] ?? ''));
		}
		scg_ar_validate($a, $input_errors);
		if (!$input_errors) {
			foreach (array('interval', 'tick') as $k) {
				$a[$k] = intval($a[$k]);
			}
			foreach (array('threshold', 'decfactor', 'incstep') as $k) {
				$a[$k] = floatval($a[$k]);
			}
			$s = scg_load_settings();
			$s['autorate'] = $a;
			scg_store_settings($s);
			write_config("ShaperClassGen: Autorate settings saved");
			scg_ar_service('restart');
			if ($a['enable']) {
				$msgs[] = scg_t('Saved, service restarted.');
			} elseif ($a['monitor']) {
				$msgs[] = scg_t('Saved. Limiter bandwidths restored; the service only measures latency.');
			} else {
				$msgs[] = scg_t('Saved, service stopped and limiter bandwidths restored.');
			}
		}
	}
}

$running = scg_ar_running();
$status = scg_ar_status();

include("head.inc");

if ($input_errors) {
	print_input_errors($input_errors);
}
foreach ($msgs as $m) {
	print_info_box(h($m), 'success');
}

scg_tabs('autorate');

function dir_rows($d, $label, $a, $pipes) {
	$c = $a[$d];
	$o = '<tr><td><b>' . h($label) . '</b></td><td><select class="form-control scg-pipe" name="' . $d . '_pipe" data-dir="' . $d . '">';
	$o .= '<option value="">- ' . scg_t('not controlled') . ' -</option>';
	foreach ($pipes as $pn => $pd) {
		$mbit = scg_ar_pipe_cfg_mbit($pn);
		$o .= '<option value="' . h($pn) . '" data-mbit="' . h($mbit) . '"' . ($pn === $c['pipe'] ? ' selected' : '') . '>' .
			h($pn) . ($pd['bw'] ? ' (' . h($pd['bw']) . ')' : '') . '</option>';
	}
	$o .= '</select></td>';
	foreach (array('min', 'base', 'max') as $k) {
		$o .= '<td><input class="form-control" type="text" name="' . $d . '_' . $k . '" id="' . $d . '_' . $k . '" value="' . h($c[$k]) . '"></td>';
	}
	$o .= '<td><button type="button" class="btn btn-sm btn-default scg-fill" data-dir="' . $d . '" title="' .
		h(scg_t('Calculate minimum, base and maximum from the bandwidth of the selected limiter (5 %% but at least 1 Mbit/s / 100 %% / 100 %%, upload base 50 %%)', '')) . '">' .
		'<i class="fa-solid fa-wand-magic-sparkles"></i> ' . scg_t('From limiter') . '</button></td>';
	return $o . '</tr>';
}
?>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title">Status</h2></div>
	<div class="panel-body">
		<?php if ($running): ?>
			<span class="text-success"><i class="fa-solid fa-circle-check"></i> <?=scg_t('Service running')?></span>
		<?php else: ?>
			<span class="text-muted"><i class="fa-solid fa-circle-stop"></i> <?=scg_t('Service stopped')?></span>
		<?php endif; ?>
		<?php if ($status): ?>
			&nbsp;&middot;&nbsp; Pinger: <?=h($status['mode'])?><?=$status['err'] ? ' <small class="text-muted">(' . h($status['err']) . ')</small>' : ''?>
			<?php foreach ($status['dirs'] as $d => $x): ?>
				&nbsp;&middot;&nbsp; <?=$d == 'down' ? 'Download' : 'Upload'?>: <b><?=h(round($x['shaper'] / 1000, 1))?> Mbit/s</b> (<?=h(scg_ar_state_text($x['state']))?>)
			<?php endforeach; ?>
		<?php endif; ?>
		&nbsp;&middot;&nbsp; <a href="status_limiters_live.php"><?=scg_t('Live view')?></a>
		<form method="post" action="firewall_shaper_autorate.php" style="display:inline">
			<input type="hidden" name="act" value="restart">
			<button type="submit" class="btn btn-xs btn-default" style="margin-left:10px"><i class="fa-solid fa-arrows-rotate"></i> <?=scg_t('Restart')?></button>
		</form>
	</div>
</div>

<form method="post" action="firewall_shaper_autorate.php">
<input type="hidden" name="act" value="save">

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title">Autorate</h2></div>
	<div class="panel-body">
		<label><input type="checkbox" name="enable" value="1" <?=!empty($a['enable']) ? 'checked' : ''?>> <?=scg_t('Enable Autorate')?></label>
		&nbsp;&nbsp;
		<label><input type="checkbox" name="monitor" value="1" <?=!empty($a['monitor']) ? 'checked' : ''?>> <?=scg_t('Measure latency even when Autorate is off (for Limiter Live)')?></label>
		<p class="help-block"><?=scg_t('The service continuously measures the ping time to the targets below. If it rises well above the learned base latency under load, traffic is queuing at the provider: the limiter is then lowered just below the throughput currently achieved, so that <i>your</i> limiter is the narrowest point again and the weights take effect. With a free line it is raised step by step up to the maximum; when idle it slowly moves towards the base value. The limiter bandwidth in the configuration stays unchanged; only the running value is changed.')?></p>
		<table class="table table-condensed">
			<thead><tr><th><?=scg_t('Direction')?></th><th>Limiter</th><th><?=scg_t('Minimum (Mbit/s)')?></th><th><?=scg_t('Base (Mbit/s)')?></th><th><?=scg_t('Maximum (Mbit/s)')?></th><th></th></tr></thead>
			<tbody>
				<?=dir_rows('down', 'Download', $a, $pipes)?>
				<?=dir_rows('up', 'Upload', $a, $pipes)?>
			</tbody>
		</table>
		<p class="help-block"><?=scg_t('<b>Minimum</b>: never lowered below this. Do not set it too high: if the line breaks down (e.g. during cable network faults), Autorate must be able to go below the actual rate. <b>Base</b>: start value and target when idle (with a learned base only for the very first run). <b>Maximum</b>: upper limit, typically the value at which the limiter prioritizes cleanly on a good line. Empty fields are prefilled from the limiter bandwidth when a limiter is selected (5 %% but at least 1 Mbit/s / 100 %% / 100 %%, upload base 50 %%); your own values are kept. <b>From limiter</b> recalculates all three values, because the limiter is usually already set to the value that prioritizes cleanly on a good line.', '')?></p>
		<div class="checkbox" style="margin-top:12px">
			<label><input type="checkbox" name="learnbase" value="1" <?=!empty($a['learnbase']) ? 'checked' : ''?>> <b><?=scg_t('Learn base automatically (recommended)')?></b></label>
		</div>
		<p class="help-block"><?=scg_t('Autorate remembers the rate that last ran under load without congestion for a while, and starts the next upload or download there. After a congestion the learned base drops immediately. The base entered above is then only the start value for the very first run.')?></p>
		<div class="alert alert-info" style="margin-bottom:10px"><?=scg_t('<b>Why a cautious start value?</b> If the start value is too high, every new transfer first fills the buffer in the modem until Autorate reacts. These seconds of high ping hit everyone: calls, games, page loads and running downloads. If it is too low, only the one transfer starts a little slower and ramps up within seconds. A few seconds of slower uploading for one is the much smaller price than a few seconds of congestion for all. This is especially true for the upload of cable connections, whose modem buffers are large.')?></div>
		<?=scg_limiter_hint_html()?>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('Measurement and control')?></h2></div>
	<div class="panel-body">
		<div class="row">
			<div class="col-sm-12 form-group">
				<label><?=scg_t('Ping targets (IPv4)')?></label>
				<input class="form-control" type="text" name="reflectors" value="<?=h($a['reflectors'])?>">
				<span class="help-block"><?=scg_t('Two or three reliable targets behind the provider. Congestion is detected when several of the latest samples are above the threshold; a single slow target triggers nothing.')?></span>
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Measure throughput at')?></label>
				<select class="form-control" name="wanif">
					<option value="" <?=($a['wanif'] ?? '') === '' ? 'selected' : ''?>><?=scg_t('automatic (first WAN)')?></option>
					<?php foreach (get_configured_interface_with_descr() as $wif => $wdescr): if (scg_if_kind($wif, scg_if_real($wif)) !== 'wan') continue; ?>
						<option value="<?=h($wif)?>" <?=($a['wanif'] ?? '') === $wif ? 'selected' : ''?>><?=h($wdescr)?></option>
					<?php endforeach; ?>
					<option value="limiter" <?=($a['wanif'] ?? '') === 'limiter' ? 'selected' : ''?>><?=scg_t('limiter counters only')?></option>
				</select>
				<span class="help-block"><?=scg_t('At the WAN, traffic that does not pass through the limiters is counted too. VPN tunnels are not offered: their traffic already runs over the WAN.')?></span>
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Ping interval per target (ms)')?></label>
				<input class="form-control" type="text" name="interval" value="<?=h($a['interval'])?>">
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Congestion threshold (ms)')?></label>
				<input class="form-control" type="text" name="threshold" value="<?=h($a['threshold'])?>">
				<span class="help-block"><?=scg_t('Increase above the base latency.')?></span>
			</div>
		</div>
		<div class="row">
			<div class="col-sm-4 form-group">
				<label><?=scg_t('Decrease on congestion (factor)')?></label>
				<input class="form-control" type="text" name="decfactor" value="<?=h($a['decfactor'])?>">
				<span class="help-block"><?=scg_t('New rate = achieved throughput &times; factor.')?></span>
			</div>
			<div class="col-sm-4 form-group">
				<label><?=scg_t('Increase per tick (%%)', '')?></label>
				<input class="form-control" type="text" name="incstep" value="<?=h($a['incstep'])?>">
			</div>
			<div class="col-sm-4 form-group">
				<label><?=scg_t('Control tick (ms)')?></label>
				<input class="form-control" type="text" name="tick" value="<?=h($a['tick'])?>">
			</div>
		</div>
	</div>
</div>

<nav class="action-buttons">
	<button type="submit" class="btn btn-primary"><i class="fa-solid fa-save"></i> <?=scg_t('Save')?></button>
</nav>
</form>

<script type="text/javascript">
//<![CDATA[
events.push(function() {
	/* Calculate values from the bandwidth of the selected limiter */
	function fill(d, overwrite) {
		var mbit = parseFloat($('select[name="' + d + '_pipe"]').find(':selected').data('mbit')) || 0;
		if (!mbit) return;
		/* Upload: vorsichtiger Startwert (50 %), Download: 100 % */
		var v = { min: 0.05, base: (d === 'up' ? 0.5 : 1.0), max: 1.0 };
		$.each(v, function(k, f) {
			var el = $('#' + d + '_' + k);
			var r = mbit < 10 ? 1000 : 10;
			var x = mbit * f;
			/* Minimum: 5 %, aber mindestens 1 Mbit/s (sofern der Limiter groesser ist) */
			if (k === 'min') x = Math.min(mbit, Math.max(x, 1));
			if (overwrite || $.trim(el.val()) === '') el.val(Math.round(x * r) / r);
		});
	}
	/* On change only fill empty fields, own values are kept */
	$('.scg-pipe').on('change', function() { fill($(this).data('dir'), false); });
	/* Button: always recalculate */
	$('.scg-fill').on('click', function() { fill($(this).data('dir'), true); });
});
//]]>
</script>

<?php include("foot.inc"); ?>
