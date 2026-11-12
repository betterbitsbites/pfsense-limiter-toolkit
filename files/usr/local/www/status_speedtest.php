<?php
/*
 * status_speedtest.php
 *
 * Periodic speed test of the ShaperClassGen package: settings, manual run
 * and history graphs.
 * Licensed under the Apache License, Version 2.0
 */

##|+PRIV
##|*IDENT=page-status-shaperspeedtest
##|*NAME=Status: Speedtest (ShaperClassGen)
##|*DESCR=Allow access to the periodic speed test page.
##|*MATCH=status_speedtest.php*
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
require_once("/usr/local/pkg/shaperclassgen_speedtest.inc");

function h($v) {
	return htmlspecialchars((string)$v, ENT_QUOTES, 'UTF-8');
}

/* JSON: results of a time range */
if (isset($_GET['data'])) {
	$days = array('1' => 1, '7' => 7, '30' => 30, '90' => 90, '365' => 365);
	$d = $days[$_GET['range'] ?? '7'] ?? 7;
	header('Content-Type: application/json');
	echo json_encode(array('now' => time(), 'range' => $d, 'results' => scg_st_results(time() - $d * 86400)));
	exit;
}
/* JSON: is a test running, and which phase */
if (isset($_GET['status'])) {
	header('Content-Type: application/json');
	$st = json_decode((string)@file_get_contents(SCG_ST_STATUS), true);
	echo json_encode(array('running' => scg_st_running(), 'status' => is_array($st) ? $st : null));
	exit;
}

$a = scg_st_settings();
$input_errors = array();
$msgs = array();

if ($_POST) {
	$act = $_POST['act'] ?? 'save';
	if ($act == 'run') {
		if (scg_st_running()) {
			$msgs[] = scg_t('A speed test is already running.');
		} else {
			@unlink(SCG_ST_STATUS);
			mwexec_bg('/usr/local/bin/php -f ' . SCG_ST_SCRIPT . ' manual' . (!empty($_POST['exclusive']) ? ' exclusive' : ''));
			$msgs[] = scg_t('Speed test started. The result appears below when it is finished.');
		}
	} elseif ($act == 'clear') {
		@unlink(SCG_ST_DB);
		$msgs[] = scg_t('History deleted.');
	} else {
		$a['enable'] = !empty($_POST['enable']);
		$a['pause_autorate'] = !empty($_POST['pause_autorate']);
		foreach (array('interval', 'duration', 'streams', 'keepdays') as $k) {
			$a[$k] = trim($_POST[$k] ?? '');
		}
		$a['pingtarget'] = trim($_POST['pingtarget'] ?? '');
		$a['wanif'] = trim($_POST['wanif'] ?? '');
		scg_st_validate($a, $input_errors);
		if (!$input_errors) {
			foreach (array('interval', 'duration', 'streams', 'keepdays') as $k) {
				$a[$k] = intval($a[$k]);
			}
			$s = scg_load_settings();
			$s['speedtest'] = $a;
			scg_store_settings($s);
			write_config("ShaperClassGen: speed test settings saved");
			scg_st_install_cron($a);
			$msgs[] = $a['enable'] ? scg_t('Saved. The speed test runs every %d minutes.', $a['interval']) :
				scg_t('Saved. The periodic speed test is off.');
		}
	}
}

$last = null;
$all = scg_st_results(time() - 86400 * 365);
for ($i = count($all) - 1; $i >= 0; $i--) {
	if (empty($all[$i]['skipped']) && empty($all[$i]['failed']) && !empty($all[$i]['down'])) {
		$last = $all[$i];
		break;
	}
}

$pgtitle = array(gettext("Status"), "Speedtest");
$pglinks = array("", "@self");
include("head.inc");

if ($input_errors) {
	print_input_errors($input_errors);
}
foreach ($msgs as $m) {
	print_info_box(h($m), 'success');
}

scg_tabs('speedtest');
?>
<style>
	.scg-st { width: 100%; height: 190px; display: block; }
	.scg-st text { fill: currentColor; font-size: 10px; opacity: .7; }
	.scg-st .grid { stroke: #888; stroke-opacity: .2; }
	.scg-st .ln { fill: none; stroke-width: 1.6; }
	.scg-st .skip { fill: #888; opacity: .6; }
	.scg-st .cur { stroke: currentColor; stroke-opacity: .4; stroke-dasharray: 2,2; }
	.scg-kpi { display: inline-block; margin-right: 18px; }
	.scg-kpi b { font-size: 120%; }
	.scg-legend span { display: inline-block; margin-right: 12px; font-size: 85%; }
	.scg-legend i { display: inline-block; width: 10px; height: 3px; vertical-align: middle; margin-right: 4px; }
	.scg-tip { position: absolute; pointer-events: none; display: none; z-index: 10; padding: 6px 8px; font-size: 85%;
		background: rgba(30,30,30,.92); color: #fff; border-radius: 4px; white-space: nowrap; }
	.scg-chart { position: relative; }
	.scg-num { text-align: right; white-space: nowrap; font-variant-numeric: tabular-nums; }
	.scg-hot { color: #ff5c5c !important; font-weight: 600; }
</style>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('Speed test history')?></h2></div>
	<div class="panel-body">
		<form class="form-inline" method="post" action="status_speedtest.php" style="margin-bottom:10px">
			<label><?=scg_t('Range')?>
				<select class="form-control input-sm" id="scg_range">
					<option value="1">24 h</option>
					<option value="7" selected><?=scg_t('7 days')?></option>
					<option value="30"><?=scg_t('30 days')?></option>
					<option value="90"><?=scg_t('90 days')?></option>
					<option value="365"><?=scg_t('1 year')?></option>
				</select>
			</label>
			&nbsp;
			<label><input type="checkbox" id="scg_showcurl"> <?=scg_t('Show curl rate too')?></label>
			&nbsp;
			<input type="hidden" name="act" value="run">
			<label title="<?=h(scg_t('For about 30 seconds all limiters are throttled to a few kbit/s, so the test has the line to itself. Running downloads and calls pause or may drop. Traffic that does not pass a limiter (pfSense itself, VPN tunnels started on pfSense, interfaces without limiter) is not affected. Only for manual tests.'))?>"><input type="checkbox" name="exclusive" value="1"> <?=scg_t('Exclusive (pause all shaped traffic)')?></label>
			<button type="submit" class="btn btn-sm btn-primary" id="scg_run"><i class="fa-solid fa-gauge-high"></i> <?=scg_t('Run test now')?></button>
			<span id="scg_runstate" class="text-muted" style="margin-left:10px"></span>
		</form>
		<div id="scg_kpis"></div>
		<div id="scg_charts"><p class="text-muted"><?=scg_t('Loading data ...')?></p></div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('Latest results')?></h2></div>
	<div class="panel-body table-responsive" id="scg_table"></div>
</div>

<form method="post" action="status_speedtest.php">
<input type="hidden" name="act" value="save">
<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=scg_t('Settings')?></h2></div>
	<div class="panel-body">
		<label><input type="checkbox" name="enable" value="1" <?=!empty($a['enable']) ? 'checked' : ''?>> <b><?=scg_t('Run speed test periodically')?></b></label>
		<p class="help-block"><?=scg_t('pfSense itself downloads from and uploads to speed.cloudflare.com with curl, with several parallel connections. The latency is measured idle and under load (bufferbloat). Because the test runs on pfSense and not in a shaped network, it measures the raw line, not your limiters. Throughput is taken from the WAN counter after the TCP ramp-up, so traffic of other users at that moment counts too; it is the capacity the line delivered. The curl rate (only the test connections) can be shown in addition.')?></p>
		<div class="row">
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Interval')?></label>
				<select class="form-control" name="interval" id="st_interval">
					<?php foreach (scg_st_intervals() as $iv): ?>
						<option value="<?=$iv?>" <?=intval($a['interval']) == $iv ? 'selected' : ''?>><?=$iv < 60 ? scg_t('every %d minutes', $iv) : ($iv == 60 ? scg_t('every hour') : scg_t('every %d hours', $iv / 60))?></option>
					<?php endforeach; ?>
				</select>
				<span class="help-block"><?=scg_t('Runs at minute 7 (and 37, ...) to stay off the full hour, when many devices start backups and updates.')?></span>
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Duration per direction (s)')?></label>
				<input class="form-control" type="text" name="duration" id="st_duration" value="<?=h($a['duration'])?>">
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Parallel connections')?></label>
				<input class="form-control" type="text" name="streams" value="<?=h($a['streams'])?>">
				<span class="help-block"><?=scg_t('4 is enough up to about 1 Gbit/s.')?></span>
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Keep results (days)')?></label>
				<input class="form-control" type="text" name="keepdays" value="<?=h($a['keepdays'])?>">
			</div>
		</div>
		<div class="row">
			<div class="col-sm-3 form-group">
				<label><?=scg_t('WAN')?></label>
				<select class="form-control" name="wanif">
					<option value="" <?=($a['wanif'] ?? '') === '' ? 'selected' : ''?>><?=scg_t('automatic (first WAN)')?></option>
					<?php foreach (get_configured_interface_with_descr() as $wif => $wdescr): if (scg_if_kind($wif, scg_if_real($wif)) !== 'wan') continue; ?>
						<option value="<?=h($wif)?>" <?=($a['wanif'] ?? '') === $wif ? 'selected' : ''?>><?=h($wdescr)?></option>
					<?php endforeach; ?>
				</select>
				<span class="help-block"><?=scg_t('The test only runs if the default route currently goes through this WAN. If it goes through a backup line (e.g. LTE/5G with a data cap), the test is skipped and marked in the graph.')?></span>
			</div>
			<div class="col-sm-3 form-group">
				<label><?=scg_t('Ping target')?></label>
				<input class="form-control" type="text" name="pingtarget" value="<?=h($a['pingtarget'])?>">
			</div>
			<div class="col-sm-6 form-group">
				<label>&nbsp;</label>
				<div class="checkbox" style="margin-top:0">
					<label><input type="checkbox" name="pause_autorate" value="1" <?=!empty($a['pause_autorate']) ? 'checked' : ''?>> <?=scg_t('Pause Autorate during the test (recommended)')?></label>
				</div>
				<span class="help-block"><?=scg_t('Otherwise Autorate sees the test load as congestion and lowers the limiters. During the pause the limiters keep their value and nothing is learned; the latency is still recorded.')?></span>
			</div>
		</div>
		<div class="alert alert-info" id="st_volume" style="margin-bottom:10px"></div>
		<p class="help-block"><?=scg_t('The history is stored in <code>/var/db/scg_speedtest.jsonl</code>. If /var is a RAM disk (System &gt; Advanced &gt; Miscellaneous), it is lost on reboot.')?></p>
	</div>
</div>
<nav class="action-buttons">
	<button type="submit" class="btn btn-primary"><i class="fa-solid fa-save"></i> <?=scg_t('Save')?></button>
</nav>
</form>
<form method="post" action="status_speedtest.php" style="margin-top:8px" onsubmit="return confirm(<?=h(json_encode(scg_t('Delete the complete speed test history?')))?>);">
	<input type="hidden" name="act" value="clear">
	<button type="submit" class="btn btn-sm btn-danger"><i class="fa-solid fa-trash-can"></i> <?=scg_t('Delete history')?></button>
</form>

<script type="text/javascript">
//<![CDATA[
events.push(function() {
	var I18N = <?=json_encode((object)scg_t_dict())?>;
	var LAST = <?=json_encode($last)?>;
	var LANG = <?=json_encode(scg_lang())?>;
	function t(s) {
		var r = I18N[s] !== undefined ? I18N[s] : s;
		for (var i = 1; i < arguments.length; i++) r = r.replace(/%[sd]/, arguments[i]);
		return r;
	}
	function esc(s) { return $('<div>').text(s === null || s === undefined ? '' : String(s)).html(); }
	function store(k, v) { try { localStorage.setItem('scg_st_' + k, v); } catch (e) {} }
	function load(k) { try { return localStorage.getItem('scg_st_' + k); } catch (e) { return null; } }
	function num(v, d) { return (v === null || v === undefined) ? '–' : Number(v).toLocaleString(LANG, { maximumFractionDigits: d, minimumFractionDigits: d }); }
	function fmtTime(ts, withDate) {
		var d = new Date(ts * 1000);
		var o = { hour: '2-digit', minute: '2-digit' };
		if (withDate) { o.day = '2-digit'; o.month = '2-digit'; }
		return d.toLocaleString(LANG, o);
	}

	var COL = { down: '#3b8fd9', up: '#e8892b', idle: '#2f9e75', ldown: '#3b8fd9', lup: '#e8892b' };
	var data = null;

	/* ---------- estimated data volume ---------- */
	function volume() {
		var iv = parseInt($('#st_interval').val(), 10) || 30, dur = parseFloat($('#st_duration').val()) || 10;
		var per = 1440 / iv;
		var txt;
		if (LAST && LAST.down) {
			var mb = ((LAST.down || 0) + (LAST.up || 0)) * dur / 8;
			txt = t('Estimated data volume: about %s MB per test, %s GB per day, %s GB per month (from the last result: %s / %s Mbit/s).',
				num(mb, 0), num(mb * per / 1000, 1), num(mb * per * 30 / 1000, 0), num(LAST.down, 0), num(LAST.up, 1));
			if (mb * per * 30 > 500000) txt += ' ' + t('That is a lot: if your plan has a fair-use limit, choose a longer interval or a shorter duration.');
		} else {
			txt = t('Estimated data volume: about %s MB per test per 100 Mbit/s of line speed. The exact value appears after the first test.', num(100 * dur / 8, 0));
		}
		$('#st_volume').html(txt);
	}
	$('#st_interval, #st_duration').on('change keyup', volume);
	volume();

	/* ---------- chart ---------- */
	/* round axis step (1, 2, 2.5, 5 x 10^n) for about 4 grid lines */
	function niceStep(v) {
		var steps = [1, 2, 2.5, 5, 10];
		var p = Math.pow(10, Math.floor(Math.log10(Math.max(v, 1e-9))));
		for (var i = 0; i < steps.length; i++) if (steps[i] * p >= v) return steps[i] * p;
		return 10 * p;
	}
	/*
	 * series: [{key, label, color, get(r), dash}], rows: results
	 * returns HTML of one chart with hover tooltip
	 */
	function chart(id, title, unit, series, rows, t0, t1, decimals) {
		var W = 900, H = 190, L = 44, R = 8, T = 8, B = 18;
		var ymax = 0;
		rows.forEach(function(r) { series.forEach(function(s) { var v = s.get(r); if (v !== null && v !== undefined && v > ymax) ymax = v; }); });
		var step = niceStep((ymax * 1.08 || 1) / 4);
		ymax = step * Math.ceil((ymax * 1.08 || 1) / step);
		function X(ts) { return L + (W - L - R) * (ts - t0) / Math.max(1, t1 - t0); }
		function Y(v) { return T + (H - T - B) * (1 - Math.min(1, v / ymax)); }
		var g = '';
		for (var v = 0; v <= ymax + step / 2; v += step) {
			var y = Y(v).toFixed(1);
			g += '<line class="grid" x1="' + L + '" x2="' + (W - R) + '" y1="' + y + '" y2="' + y + '"/><text x="' + (L - 4) + '" y="' + (Number(y) + 3) + '" text-anchor="end">' + num(v, step % 1 ? 1 : 0) + '</text>';
		}
		/* time axis: 6 labels */
		var span = t1 - t0;
		for (var j = 0; j <= 6; j++) {
			var ts = t0 + span * j / 6;
			g += '<text x="' + X(ts).toFixed(1) + '" y="' + (H - 4) + '" text-anchor="' + (j == 0 ? 'start' : (j == 6 ? 'end' : 'middle')) + '">' + esc(fmtTime(ts, span > 86400 * 1.5)) + '</text>';
		}
		/* lines, broken at gaps longer than 3 test intervals or at skipped tests */
		var gap = 3 * 60 * Math.max(15, <?=intval($a['interval'])?>);
		var paths = '';
		series.forEach(function(s) {
			var d = '', prev = null, dots = '';
			rows.forEach(function(r) {
				var v = (r.skipped || r.failed) ? null : s.get(r);
				if (v === null || v === undefined) { prev = null; return; }
				var x = X(r.t).toFixed(1), y = Y(v).toFixed(1);
				d += (prev === null || r.t - prev > gap ? ' M' : ' L') + x + ',' + y;
				if (rows.length <= 200) dots += '<circle cx="' + x + '" cy="' + y + '" r="2" fill="' + s.color + '"/>';
				prev = r.t;
			});
			if (d) paths += '<path class="ln" d="' + d + '" stroke="' + s.color + '"' + (s.dash ? ' stroke-dasharray="' + s.dash + '"' : '') + '/>' + dots;
		});
		var skips = '';
		rows.forEach(function(r) {
			if (r.skipped || r.failed) skips += '<rect class="skip" x="' + (X(r.t) - 1.5).toFixed(1) + '" y="' + (H - B - 6) + '" width="3" height="6"/>';
		});
		var legend = '<div class="scg-legend">' + series.map(function(s) {
			return '<span><i style="background:' + s.color + '"></i>' + esc(s.label) + '</span>';
		}).join('') + (skips ? '<span><i style="background:#888;height:6px;width:3px"></i>' + t('skipped / failed') + '</span>' : '') + '</div>';
		return '<div class="scg-chart" data-chart="' + id + '"><h5 style="margin:12px 0 4px">' + esc(title) + ' <small>(' + esc(unit) + ')</small></h5>' +
			'<svg class="scg-st" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none" data-l="' + L + '" data-r="' + R + '" data-w="' + W + '">' +
			g + paths + skips + '<line class="cur" x1="0" x2="0" y1="' + T + '" y2="' + (H - B) + '" style="display:none"/></svg>' + legend +
			'<div class="scg-tip"></div></div>';
	}

	function tipHtml(r) {
		var s = '<b>' + esc(fmtTime(r.t, true)) + '</b>' + (r.manual ? ' (' + (r.exclusive ? t('manual, exclusive') : t('manual')) + ')' : '');
		if (r.skipped) return s + '<br>' + t('skipped') + ': ' + esc(r.skipped);
		if (r.failed) return s + '<br>' + t('failed') + (r.errors ? ': ' + esc(r.errors.join(', ')) : '');
		s += '<br>Download: <b>' + num(r.down, 1) + '</b> Mbit/s &middot; Upload: <b>' + num(r.up, 1) + '</b> Mbit/s';
		if (r.down_curl !== undefined) s += '<br><small>curl: ' + num(r.down_curl, 1) + ' / ' + num(r.up_curl, 1) + ' Mbit/s</small>';
		function pg(p) { return p && p.avg !== null ? num(p.avg, 0) + ' ms (P95 ' + num(p.p95, 0) + ')' : '–'; }
		s += '<br>' + t('Ping idle') + ': ' + pg(r.ping) + '<br>' + t('Ping during download') + ': ' + pg(r.ping_down) +
			'<br>' + t('Ping during upload') + ': ' + pg(r.ping_up);
		if (r.colo) s += '<br><small>Cloudflare ' + esc(r.colo) + (r.wan_mb ? ' &middot; ' + num(r.wan_mb, 0) + ' MB' : '') + '</small>';
		if (r.errors) s += '<br><small class="scg-hot">' + esc(r.errors.join(', ')) + '</small>';
		return s;
	}

	function bindHover(rows, t0, t1) {
		$('#scg_charts .scg-chart').each(function() {
			var box = $(this), svg = box.find('svg'), tip = box.find('.scg-tip'), cur = svg.find('.cur');
			var L = +svg.data('l'), R = +svg.data('r'), W = +svg.data('w');
			svg.on('mousemove', function(e) {
				var rect = this.getBoundingClientRect();
				var vx = (e.clientX - rect.left) / rect.width * W;
				var ts = t0 + (vx - L) / (W - L - R) * (t1 - t0);
				var best = null, bd = Infinity;
				rows.forEach(function(r) { var d = Math.abs(r.t - ts); if (d < bd) { bd = d; best = r; } });
				if (!best) return;
				var x = L + (W - L - R) * (best.t - t0) / Math.max(1, t1 - t0);
				cur.attr({ x1: x, x2: x }).show();
				tip.html(tipHtml(best)).show();
				var px = (x / W) * rect.width, tw = tip.outerWidth();
				tip.css({ left: Math.max(0, Math.min(rect.width - tw, px + 12)) + 'px', top: '30px' });
			}).on('mouseleave', function() { tip.hide(); cur.hide(); });
		});
	}

	function stats(rows, get) {
		var v = [];
		rows.forEach(function(r) { if (!r.skipped && !r.failed) { var x = get(r); if (x !== null && x !== undefined) v.push(x); } });
		if (!v.length) return null;
		var s = v.reduce(function(a, b) { return a + b; }, 0);
		v.sort(function(a, b) { return a - b; });
		return { avg: s / v.length, min: v[0], max: v[v.length - 1], n: v.length };
	}

	function render() {
		if (!data) return;
		var rows = data.results, t1 = data.now, t0 = t1 - data.range * 86400;
		var curl = $('#scg_showcurl').prop('checked');
		/* KPIs of the range */
		var sd = stats(rows, function(r) { return r.down; }), su = stats(rows, function(r) { return r.up; });
		var sp = stats(rows, function(r) { return r.ping ? r.ping.avg : null; });
		var sl = stats(rows, function(r) { return r.ping_down && r.ping_up ? Math.max(r.ping_down.avg || 0, r.ping_up.avg || 0) : null; });
		var nskip = rows.filter(function(r) { return r.skipped || r.failed; }).length;
		function k(label, s, unit, d) {
			return '<span class="scg-kpi">' + label + ' <b>' + (s ? num(s.avg, d) : '–') + '</b> ' + unit +
				(s ? ' <small class="text-muted">(' + num(s.min, d) + '&ndash;' + num(s.max, d) + ')</small>' : '') + '</span>';
		}
		$('#scg_kpis').html('<div>' + t('Average in range (min&ndash;max):') + ' ' + k('Download', sd, 'Mbit/s', 0) + k('Upload', su, 'Mbit/s', 1) +
			k(t('Ping idle'), sp, 'ms', 0) + k(t('Ping under load'), sl, 'ms', 0) +
			'<span class="scg-kpi">' + t('Tests') + ' <b>' + (sd ? sd.n : 0) + '</b>' + (nskip ? ' <small class="text-muted">(' + t('%s skipped/failed', nskip) + ')</small>' : '') + '</span></div>');
		if (!rows.length) {
			$('#scg_charts').html('<p class="text-muted">' + t('No results in this range yet. Enable the periodic test below or run one now.') + '</p>');
		} else {
			var dn = [{ label: 'Download', color: COL.down, get: function(r) { return r.down; } }];
			var up = [{ label: 'Upload', color: COL.up, get: function(r) { return r.up; } }];
			if (curl) {
				dn.push({ label: t('curl rate'), color: COL.down, dash: '3,3', get: function(r) { return r.down_curl; } });
				up.push({ label: t('curl rate'), color: COL.up, dash: '3,3', get: function(r) { return r.up_curl; } });
			}
			var lat = [
				{ label: t('idle'), color: COL.idle, get: function(r) { return r.ping ? r.ping.avg : null; } },
				{ label: t('during download'), color: COL.ldown, get: function(r) { return r.ping_down ? r.ping_down.avg : null; } },
				{ label: t('during upload'), color: COL.lup, get: function(r) { return r.ping_up ? r.ping_up.avg : null; } }
			];
			$('#scg_charts').html(chart('down', 'Download', 'Mbit/s', dn, rows, t0, t1) + chart('up', 'Upload', 'Mbit/s', up, rows, t0, t1) +
				chart('lat', t('Latency (average)'), 'ms', lat, rows, t0, t1));
			bindHover(rows, t0, t1);
		}
		/* table: last 20 */
		var last = rows.slice(-20).reverse();
		function pc(p) { return p && p.avg !== null ? num(p.avg, 0) : '–'; }
		var tb = '<table class="table table-condensed table-striped"><thead><tr><th>' + t('Time') + '</th><th class="scg-num">Download</th><th class="scg-num">Upload</th>' +
			'<th class="scg-num">' + t('Ping idle') + '</th><th class="scg-num">' + t('during download') + '</th><th class="scg-num">' + t('during upload') + '</th>' +
			'<th class="scg-num">' + t('Loss') + '</th><th>Server</th><th class="scg-num">' + t('Data') + '</th><th></th></tr></thead><tbody>';
		last.forEach(function(r) {
			var note = r.skipped ? t('skipped') + ': ' + esc(r.skipped) : (r.failed ? t('failed') : '') + (r.errors ? ' ' + esc(r.errors.join(', ')) : '');
			if (r.manual) note = (r.exclusive ? t('manual, exclusive') : t('manual')) + (note ? ' &middot; ' + note : '');
			var loss = r.ping_down && r.ping_up ? Math.max(r.ping_down.loss || 0, r.ping_up.loss || 0, r.ping ? r.ping.loss || 0 : 0) : null;
			var bad = function(p) { return p && r.ping && r.ping.avg !== null && p.avg !== null && p.avg > r.ping.avg + 30; };
			tb += '<tr' + (r.skipped || r.failed ? ' class="text-muted"' : '') + '><td>' + esc(fmtTime(r.t, true)) + '</td>' +
				'<td class="scg-num">' + (r.skipped ? '' : num(r.down, 1)) + '</td><td class="scg-num">' + (r.skipped ? '' : num(r.up, 1)) + '</td>' +
				'<td class="scg-num">' + pc(r.ping) + '</td><td class="scg-num' + (bad(r.ping_down) ? ' scg-hot' : '') + '">' + pc(r.ping_down) + '</td>' +
				'<td class="scg-num' + (bad(r.ping_up) ? ' scg-hot' : '') + '">' + pc(r.ping_up) + '</td>' +
				'<td class="scg-num' + (loss ? ' scg-hot' : '') + '">' + (loss === null ? '' : num(loss, 1) + ' %') + '</td>' +
				'<td>' + esc(r.colo || '') + '</td><td class="scg-num">' + (r.wan_mb ? num(r.wan_mb, 0) + ' MB' : '') + '</td><td><small>' + note + '</small></td></tr>';
		});
		tb += '</tbody></table><p class="help-block">' + t('Values: Mbit/s and ms. Red: latency under load more than 30 ms above idle (bufferbloat on the raw line), or packet loss.') + '</p>';
		$('#scg_table').html(last.length ? tb : '<p class="text-muted">' + t('No results yet.') + '</p>');
	}

	/* Aeltere Laeufe mit HTTP-Fehler und 0 Mbit/s nachtraeglich als fehlgeschlagen werten */
	function normalize(rows) {
		rows.forEach(function(r) {
			if (r.skipped || r.failed) return;
			var bad = function(v) { return v === null || v === undefined || v < 0.1; };
			if (r.errors && bad(r.down_curl)) r.down = r.down_curl = null;
			if (r.errors && bad(r.up_curl)) r.up = r.up_curl = null;
			if (bad(r.down) && bad(r.up)) r.failed = true;
		});
		return rows;
	}
	function fetchData() {
		$.getJSON('status_speedtest.php', { data: 1, range: $('#scg_range').val() }, function(d) { d.results = normalize(d.results); data = d; render(); });
	}

	/* ---------- manual run: poll the phase ---------- */
	var PH = { prepare: t('preparing'), ping: t('measuring idle latency'), download: t('download running'), upload: t('upload running'), done: t('finished') };
	var polling = false;
	function poll() {
		$.getJSON('status_speedtest.php', { status: 1 }, function(s) {
			if (s.running) {
				polling = true;
				var ph = s.status && s.status.phase ? (PH[s.status.phase] || s.status.phase) : t('starting');
				$('#scg_runstate').html('<i class="fa-solid fa-spinner fa-spin"></i> ' + t('Speed test running:') + ' ' + esc(ph));
				$('#scg_run').prop('disabled', true);
				setTimeout(poll, 1000);
			} else {
				$('#scg_run').prop('disabled', false);
				if (polling) {
					polling = false;
					var r = s.status && s.status.result;
					$('#scg_runstate').html(r ? (r.skipped ? t('skipped') + ': ' + esc(r.skipped) :
						t('Result:') + ' ' + num(r.down, 1) + ' / ' + num(r.up, 1) + ' Mbit/s') : '');
					fetchData();
				} else {
					$('#scg_runstate').html('');
				}
			}
		});
	}

	var rg = load('range');
	if (rg && $('#scg_range option[value="' + rg + '"]').length) $('#scg_range').val(rg);
	$('#scg_showcurl').prop('checked', load('curl') === '1');
	$('#scg_range').on('change', function() { store('range', this.value); fetchData(); });
	$('#scg_run').closest('form').on('submit', function() {
		if ($(this).find('[name=exclusive]').prop('checked')) {
			return confirm(t('Exclusive test: for about 30 seconds all traffic through the limiters is paused. Running downloads and calls may drop. Continue?'));
		}
		return true;
	});
	$('#scg_showcurl').on('change', function() { store('curl', this.checked ? '1' : '0'); render(); });
	fetchData();
	poll();
	/* refresh after scheduled tests */
	setInterval(function() { if (!polling) fetchData(); }, 60000);
});
//]]>
</script>

<?php include("foot.inc"); ?>
