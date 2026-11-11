<?php
/*
 * status_limiters_live.php
 *
 * Live view of all limiters (dummynet), their child queues, the interfaces
 * and the Autorate service. Works with any limiters, not only with the
 * class generator.
 * Licensed under the Apache License, Version 2.0
 */

##|+PRIV
##|*IDENT=page-status-limiterslive
##|*NAME=Status: Limiters Live
##|*DESCR=Allow access to the 'Status: Limiters Live' page.
##|*MATCH=status_limiters_live.php*
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
require_once("/usr/local/pkg/shaperclassgen_autorate.inc");

if (isset($_GET['getstats'])) {
	header("Content-Type: application/json");
	header("Cache-Control: no-store");
	$snap = scg_dn_snapshot();
	$snap['autorate'] = scg_ar_status();
	$snap['ifaces'] = scg_if_counters();
	echo json_encode($snap);
	exit;
}

/* Langzeitverlauf (Minutenwerte des Autorate-Dienstes) */
if (isset($_GET['longhist'])) {
	header("Content-Type: application/json");
	header("Cache-Control: no-store");
	$r = scg_ar_long_series((int)($_GET['hours'] ?? 24));
	$r['now'] = time();
	$st = scg_ar_status();
	$r['threshold'] = $st ? floatval($st['threshold']) : 30;
	echo json_encode($r);
	exit;
}

$fixmsg = '';
if ($_POST && ($_POST['act'] ?? '') == 'fixexpire') {
	scg_dn_fix_expire();
	$fixmsg = scg_t('net.inet.ip.dummynet.expire=0 set (active immediately and saved as system tunable).');
}
$expire = scg_dn_expire();

$pgtitle = array(gettext("Status"), "Limiter Live");
$shortcut_section = "trafficshaper-limiters";

/* Priority factors (tag -> factor), shaped interfaces and limiters per direction */
$scg_factors = array();
$scg_shaped = array();
$scg_downpipes = array();
$scg_uppipes = array();
$scg_set = scg_load_settings();
foreach ((array)($scg_set['vlans'] ?? array()) as $v) {
	if (!empty($v['enable']) && !empty($v['tag'])) {
		$scg_factors[$v['tag']] = floatval($v['factor']);
		$scg_shaped[$v['if']] = $v['tag'];
		if (!empty($v['down'])) {
			$scg_downpipes[$v['down']] = true;
		}
		if (!empty($v['up'])) {
			$scg_uppipes[$v['up']] = true;
		}
	}
}
foreach (array('down' => 'scg_downpipes', 'up' => 'scg_uppipes') as $d => $var) {
	if (!empty($scg_set['autorate'][$d]['pipe'])) {
		${$var}[$scg_set['autorate'][$d]['pipe']] = true;
	}
}

include("head.inc");

scg_tabs('live');

if ($fixmsg) {
	print_info_box(htmlspecialchars($fixmsg), 'success');
}
if ($expire !== null && $expire !== 0) {
?>
<div class="alert alert-warning">
	<form method="post" action="status_limiters_live.php" style="margin:0">
		<?=scg_t('<b>Counters are unstable.</b> dummynet deletes a queue as soon as it is briefly empty (<code>net.inet.ip.dummynet.expire=%d</code>) and resets its counters. Queue rates jump as a result; limiter totals are not affected. With <code>expire=0</code> empty queues are kept. This does not change shaping and costs little memory (for limiters with a mask, one entry per address stays until the next reload).', intval($expire))?>
		<input type="hidden" name="act" value="fixexpire">
		<button type="submit" class="btn btn-sm btn-warning" style="margin-left:8px"><i class="fa-solid fa-wrench"></i> <?=scg_t('Set expire=0')?></button>
	</form>
</div>
<?php
}
?>
<style>
	.scg-bar { position: relative; height: 16px; min-width: 120px; background: rgba(128,128,128,.18); border-radius: 3px; overflow: hidden; }
	.scg-bar > span { position: absolute; left: 0; top: 0; bottom: 0; background: #2f9e75; transition: width .4s; }
	.scg-bar.scg-high > span { background: #d89b2b; }
	.scg-bar.scg-full > span { background: #ff5c5c; }
	.scg-num { text-align: right; white-space: nowrap; font-variant-numeric: tabular-nums; }
	.scg-idle { opacity: .45; }
	.scg-matrix td, .scg-matrix th { text-align: right; white-space: nowrap; }
	.scg-matrix td:first-child, .scg-matrix th:first-child { text-align: left; }
	.scg-matrix .scg-dir { font-size: 85%; opacity: .8; }
	.scg-spark { width: 100%; height: 60px; display: block; }
	.scg-spark .lim { fill: none; stroke: #d89b2b; stroke-width: 1.5; }
	.scg-spark .ach { fill: rgba(47,158,117,.35); stroke: #2f9e75; stroke-width: 1; }
	.scg-spark .dly { fill: none; stroke: #ff5c5c; stroke-width: 1.5; }
	/* Red that is readable on dark and light backgrounds */
	.scg-hot { color: #ff5c5c !important; font-weight: 600; }
	.scg-legend span { display: inline-block; margin-right: 12px; font-size: 85%; }
	.scg-legend i { display: inline-block; width: 10px; height: 3px; vertical-align: middle; margin-right: 4px; }
	.scg-group td { font-size: 85%; opacity: .75; text-transform: uppercase; letter-spacing: .04em; padding-top: 10px !important; }
	#scg_status { margin-left: 10px; }
	.scg-lat { width: 100%; height: 140px; display: block; }
	.scg-comb-wrap { position: relative; }
	.scg-comb { width: 100%; height: 240px; display: block; }
	.scg-comb text { fill: currentColor; font-size: 10px; opacity: .7; }
	.scg-comb .band { fill: rgba(255,92,92,.22); stroke: none; }
	.scg-comb .lat { fill: none; stroke: #ff5c5c; stroke-width: 1.8; }
	.scg-comb .thr { fill: none; stroke: #9aa4b2; stroke-width: 1; stroke-dasharray: 2,3; opacity: .9; }
	.scg-comb .dfill { fill: rgba(59,143,217,.14); stroke: none; }
	.scg-comb .dach { fill: none; stroke: #3b8fd9; stroke-width: 1.4; }
	.scg-comb .dlim { fill: none; stroke: #3b8fd9; stroke-width: 1.2; stroke-dasharray: 5,3; }
	.scg-comb .uach { fill: none; stroke: #e8892b; stroke-width: 1.6; }
	.scg-comb .ulim { fill: none; stroke: #e8892b; stroke-width: 1.2; stroke-dasharray: 5,3; }
	.scg-comb .cur { stroke: currentColor; stroke-opacity: .5; stroke-dasharray: 2,2; }
	.scg-comb-tip { position: absolute; top: 6px; pointer-events: none; z-index: 5; padding: 6px 8px; font-size: 85%;
		background: rgba(25,25,25,.92); color: #fff; border-radius: 4px; white-space: nowrap; }
	.scg-lat .band { fill: rgba(255,92,92,.28); stroke: none; }
	.scg-lat .avg { fill: none; stroke: #ff5c5c; stroke-width: 1.6; }
	.scg-lat .base { fill: none; stroke: #2f9e75; stroke-width: 1.2; stroke-dasharray: 4,3; }
	.scg-lat .thr { fill: none; stroke: #d89b2b; stroke-width: 1; stroke-dasharray: 2,3; }
	.scg-lat .loss { fill: #ff5c5c; }
	.scg-lat text { fill: currentColor; font-size: 10px; opacity: .7; }
	.scg-kpi { display: inline-block; margin-right: 18px; }
	.scg-kpi b { font-size: 120%; }
	.scg-toggle { font-weight: normal; margin: 0; font-size: 85%; }
</style>

<div class="panel panel-default">
	<div class="panel-body">
		<form class="form-inline">
			<label><?=scg_t('Refresh')?>
				<select class="form-control input-sm" id="scg_interval">
					<option value="1000">1 s</option>
					<option value="2000" selected>2 s</option>
					<option value="5000">5 s</option>
					<option value="10000">10 s</option>
				</select>
			</label>
			&nbsp;
			<label><input type="checkbox" id="scg_hideidle"> <?=scg_t('Hide idle queues')?></label>
			&nbsp;
			<label><input type="checkbox" id="scg_idleifs"> <?=scg_t('Show idle interfaces')?></label>
			&nbsp;
			<label><input type="checkbox" id="scg_showvpn"> <?=scg_t('Show VPN tunnels')?></label>
			&nbsp;
			<label><?=scg_t('Unit')?>
				<select class="form-control input-sm" id="scg_unit">
					<option value="bit" selected>bit/s</option>
					<option value="byte">Byte/s</option>
				</select>
			</label>
			<button type="button" class="btn btn-sm btn-default" id="scg_pause"><i class="fa-solid fa-pause"></i> <?=scg_t('Pause')?></button>
			<span id="scg_status" class="text-muted"></span>
		</form>
		<p class="help-block" style="margin-bottom:0"><?=scg_t('Rates are calculated from the difference between two queries of <code>dnctl</code> and the interface counters. Bar = share of the limiter bandwidth.<br><b>Limiter set correctly?</b> Check with downloads in two interfaces whether the split matches the weights. If both share about equally despite different weights, the limiter is still too high.')?></p>
	</div>
</div>

<div id="scg_combined"></div>
<div id="scg_long"></div>
<div id="scg_latency"></div>
<div id="scg_autorate"></div>
<div id="scg_ifaces"></div>
<div id="scg_matrix"></div>
<div id="scg_pipes"><div class="panel panel-default"><div class="panel-body"><?=scg_t('Loading data ...')?></div></div></div>

<script type="text/javascript">
//<![CDATA[
events.push(function() {
	var I18N = <?=json_encode((object)scg_t_dict())?>;
	var STATES = <?=json_encode((object)scg_ar_state_texts())?>;
	/* Translate; %s placeholders are replaced in order */
	function t(s) {
		var r = I18N[s] !== undefined ? I18N[s] : s;
		for (var i = 1; i < arguments.length; i++) r = r.replace('%s', arguments[i]);
		return r;
	}

	var prev = null, timer = null, paused = false;

	/* Remember settings per browser */
	function store(k, v) { try { localStorage.setItem('scg_live_' + k, v); } catch (e) {} }
	function load(k) { try { return localStorage.getItem('scg_live_' + k); } catch (e) { return null; } }
	var sv;
	if ((sv = load('interval')) !== null) $('#scg_interval').val(sv);
	if ((sv = load('unit')) !== null) $('#scg_unit').val(sv);
	$('#scg_hideidle').prop('checked', load('hideidle') !== '0');
	$('#scg_idleifs').prop('checked', load('idleifs') === '1');
	$('#scg_showvpn').prop('checked', load('showvpn') !== '0');
	var showMatrix = load('matrix') !== '0';
	$('#scg_interval, #scg_unit').on('change', function() { store(this.id.replace('scg_', ''), $(this).val()); });
	$('#scg_hideidle, #scg_idleifs, #scg_showvpn').on('change', function() { store(this.id.replace('scg_', ''), this.checked ? '1' : '0'); });

	var CLASS_ORDER = ['qControl', 'qACK', 'qRealtime', 'qVoIP', 'qGames', 'qInteractive', 'qOthersHigh', 'qDefault', 'qOthersLow', 'qBulk', 'qP2P'];
	var FACTORS = <?=json_encode((object)$scg_factors)?>;
	var SHAPED = <?=json_encode((object)$scg_shaped)?>;
	var DOWNPIPES = <?=json_encode((object)$scg_downpipes)?>;
	var UPPIPES = <?=json_encode((object)$scg_uppipes)?>;

	/* Queues by weight descending, ties in configuration order */
	function byWeight(list) {
		return list.map(function(q, i) { return [q, i]; })
			.sort(function(a, b) { return (b[0].weight - a[0].weight) || (a[1] - b[1]); })
			.map(function(x) { return x[0]; });
	}

	function esc(s) {
		return String(s).replace(/[&<>"']/g, function(c) {
			return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c];
		});
	}
	function fmtRate(bps) {
		if (bps === null || bps === undefined || isNaN(bps)) return '–';
		var byte = $('#scg_unit').val() == 'byte';
		var v = byte ? bps / 8 : bps;
		var u = byte ? ['B/s', 'KB/s', 'MB/s', 'GB/s'] : ['bit/s', 'Kbit/s', 'Mbit/s', 'Gbit/s'];
		var i = 0;
		while (v >= 1000 && i < u.length - 1) { v /= 1000; i++; }
		return (v >= 100 || i == 0 ? v.toFixed(0) : v.toFixed(v >= 10 ? 1 : 2)) + ' ' + u[i];
	}
	function fmtNum(n, d) {
		if (n === null || n === undefined || isNaN(n)) return '–';
		return n.toFixed(d || 0);
	}
	function bar(rate, bw) {
		if (!bw || rate === null) return '<div class="scg-bar"><span style="width:0"></span></div>';
		var pct = Math.min(100, rate / bw * 100);
		var cls = pct >= 90 ? ' scg-full' : (pct >= 70 ? ' scg-high' : '');
		return '<div class="scg-bar' + cls + '" title="' + pct.toFixed(1) + ' %"><span style="width:' + pct.toFixed(1) + '%"></span></div>';
	}
	/*
	 * Difference with counter reset detection: if the counter drops, the
	 * queue was recreated; at most dt seconds have passed since, so cur/dt
	 * is a lower bound instead of a gap.
	 */
	function delta(cur, old, dt) {
		if (old === undefined || old === null || dt <= 0) return null;
		if (cur < old) return cur / dt;
		return (cur - old) / dt;
	}

	function compute(data) {
		var dt = prev ? (data.t - prev.t) : 0;
		var pmap = {}, qmap = {};
		if (prev) {
			prev.pipes.forEach(function(p) {
				pmap[p.num] = p;
				p.queues.forEach(function(q) { qmap[q.num] = q; });
			});
		}
		data.pipes.forEach(function(p) {
			var op = pmap[p.num];
			p.rate = op ? delta(p.bytes, op.bytes, dt) : null;
			if (p.rate !== null) p.rate *= 8;
			p.pps = op ? delta(p.pkts, op.pkts, dt) : null;
			p.dps = op ? delta(p.drops, op.drops, dt) : null;
			var qsum = 0, qany = false;
			p.queues.forEach(function(q) {
				var oq = qmap[q.num] || (prev ? { bytes: 0, pkts: 0, drops: 0 } : null);
				q.rate = oq ? delta(q.bytes, oq.bytes, dt) : null;
				if (q.rate !== null) { q.rate *= 8; qsum += q.rate; qany = true; }
				q.pps = oq ? delta(q.pkts, oq.pkts, dt) : null;
				q.dps = oq ? delta(q.drops, oq.drops, dt) : null;
			});
			/* Scheduler counters may restart when idle: at least the sum of the queues */
			if (qany && (p.rate === null || qsum > p.rate)) p.rate = qsum;
		});
		/* Interfaces: download/upload as seen from the internet */
		var dti = prev ? (data.t - prev.t) : 0, old = {};
		if (prev && prev.ifaces) prev.ifaces.forEach(function(i) { old[i.if] = i; });
		(data.ifaces || []).forEach(function(i) {
			var o = old[i.if];
			var rin = o ? delta(i.in, o.in, dti) : null, rout = o ? delta(i.out, o.out, dti) : null;
			if (rin !== null) rin *= 8;
			if (rout !== null) rout *= 8;
			/* WAN and VPN: in = download. Internal: out = download to the clients */
			var ext = (i.kind === 'wan' || i.kind === 'vpn');
			i.down = ext ? rin : rout;
			i.up = ext ? rout : rin;
		});
	}

	function arBadge(data, name) {
		if (!data.autorate) return '';
		var b = '';
		$.each(data.autorate.dirs, function(d, x) {
			if (x.pipe === name) b = ' <span class="label label-info" title="' + esc(t('Bandwidth is controlled by Autorate')) + '">Autorate</span>';
		});
		return b;
	}

	function renderPipes(data) {
		var hide = $('#scg_hideidle').prop('checked');
		var h = '';
		data.pipes.forEach(function(p) {
			h += '<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">' +
				esc(p.name) + arBadge(data, p.name) + ' <small>' + (p.bw ? fmtRate(p.bw) : t('unlimited')) + (p.type ? ' &middot; ' + esc(p.type) : '') +
				(p.loaded ? '' : ' &middot; <span class="scg-hot">' + t('not loaded') + '</span>') + '</small>' +
				'<span class="pull-right">' + fmtRate(p.rate) + ' &nbsp; ' + (p.dps ? '<span class="scg-hot">' + fmtNum(p.dps, 1) + ' Drops/s</span> &nbsp; ' : '') +
				t('Drops total') + ' ' + fmtNum(p.drops) + '</span></h2></div>';
			h += '<div class="panel-body" style="padding-bottom:0">' + bar(p.rate, p.bw) + '</div>';
			if (p.queues.length) {
				h += '<div class="table-responsive panel-body"><table class="table table-condensed table-hover" style="margin-bottom:0"><thead><tr>' +
					'<th>Queue</th><th>' + t('Description') + '</th><th class="scg-num">' + t('Weight') + '</th><th class="scg-num">Flows</th>' +
					'<th class="scg-num">Rate</th><th style="width:30%">' + t('Share of limiter') + '</th><th class="scg-num">' + t('Packets/s') + '</th>' +
					'<th class="scg-num">Drops/s</th><th class="scg-num">Drops</th></tr></thead><tbody>';
				byWeight(p.queues).forEach(function(q) {
					var idle = !q.flows && !q.rate;
					if (hide && idle) return;
					h += '<tr class="' + (idle ? 'scg-idle' : '') + '"><td>' + esc(q.name) + '</td><td><small>' + esc(q.descr) + '</small></td>' +
						'<td class="scg-num">' + q.weight + '</td><td class="scg-num">' + q.flows + '</td>' +
						'<td class="scg-num">' + fmtRate(q.rate) + '</td><td>' + bar(q.rate, p.bw) + '</td>' +
						'<td class="scg-num">' + fmtNum(q.pps) + '</td>' +
						'<td class="scg-num' + (q.dps ? ' scg-hot' : '') + '">' + fmtNum(q.dps, 1) + '</td>' +
						'<td class="scg-num">' + fmtNum(q.drops) + '</td></tr>';
				});
				h += '</tbody></table></div>';
			}
			h += '</div>';
		});
		if (!data.pipes.length) {
			h = '<div class="panel panel-default"><div class="panel-body">' + t('No limiters configured.') + '</div></div>';
		}
		$('#scg_pipes').html(h);
	}

	/* Matrix tag x class for queues named D_<tag>_<class> / U_<tag>_<class> */
	function renderMatrix(data) {
		var rows = {}, cols = {}, n = 0;
		data.pipes.forEach(function(p) {
			p.queues.forEach(function(q) {
				var m = /^([DU])_([A-Za-z0-9]+)_([A-Za-z0-9]+)$/.exec(q.name);
				if (!m) return;
				n++;
				rows[m[2]] = rows[m[2]] || {};
				rows[m[2]][m[3]] = rows[m[2]][m[3]] || {};
				rows[m[2]][m[3]][m[1]] = q.rate;
				cols[m[3]] = true;
			});
		});
		if (n == 0) { $('#scg_matrix').html(''); return; }
		var head = '<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">' + t('Classes per interface') + ' ' +
			'<small>&darr; Download &middot; &uarr; Upload</small>' +
			'<label class="pull-right scg-toggle"><input type="checkbox" id="scg_matrix_show"' + (showMatrix ? ' checked' : '') + '> ' + t('Show') + '</label></h2></div>';
		if (!showMatrix) { $('#scg_matrix').html(head + '</div>'); return; }
		var cl = Object.keys(cols).sort(function(a, b) {
			var ia = CLASS_ORDER.indexOf(a), ib = CLASS_ORDER.indexOf(b);
			if (ia < 0) ia = 99;
			if (ib < 0) ib = 99;
			return ia - ib || a.localeCompare(b);
		});
		var h = head + '<div class="table-responsive panel-body">' +
			'<table class="table table-condensed scg-matrix" style="margin-bottom:0"><thead><tr><th>' + t('Tag') + '</th><th>' + t('Priority') + '</th>';
		cl.forEach(function(c) { h += '<th>' + esc(c) + '</th>'; });
		h += '<th>' + t('Total') + '</th></tr></thead><tbody>';
		/* Interfaces by priority factor descending, unknown last */
		Object.keys(rows).sort(function(a, b) {
			var fa = FACTORS[a], fb = FACTORS[b];
			if (fa === undefined) fa = -1;
			if (fb === undefined) fb = -1;
			return (fb - fa) || a.localeCompare(b);
		}).forEach(function(tag) {
			var sd = 0, su = 0;
			h += '<tr><td><b>' + esc(tag) + '</b></td><td>' + (FACTORS[tag] !== undefined ? FACTORS[tag].toFixed(2) : '–') + '</td>';
			cl.forEach(function(c) {
				var cell = rows[tag][c] || {};
				var d = cell.D, u = cell.U;
				if (d) sd += d;
				if (u) su += u;
				var idle = !d && !u;
				h += '<td class="' + (idle ? 'scg-idle' : '') + '"><span class="scg-dir">&darr;</span> ' + fmtRate(d === undefined ? null : d) +
					'<br><span class="scg-dir">&uarr;</span> ' + fmtRate(u === undefined ? null : u) + '</td>';
			});
			h += '<td><b><span class="scg-dir">&darr;</span> ' + fmtRate(prev ? sd : null) + '<br><span class="scg-dir">&uarr;</span> ' + fmtRate(prev ? su : null) + '</b></td></tr>';
		});
		h += '</tbody></table></div></div>';
		$('#scg_matrix').html(h);
	}

	/* Autorate: history from the service (last 300 s); browser history only as fallback for older services */
	var arHist = { down: [], up: [], delay: [] };
	function sparkPath(vals, max, w, h, close) {
		if (!vals.length || !max) return '';
		var step = w / Math.max(1, 299), x0 = w - (vals.length - 1) * step;
		var pts = vals.map(function(v, i) {
			return (x0 + i * step).toFixed(1) + ',' + (h - Math.min(1, v / max) * (h - 2) - 1).toFixed(1);
		});
		var d = 'M' + pts.join(' L');
		if (close) d += ' L' + (x0 + (vals.length - 1) * step).toFixed(1) + ',' + h + ' L' + x0.toFixed(1) + ',' + h + ' Z';
		return d;
	}
	function sourceText(src) {
		if (!src) return '';
		if (src.kind === 'iface') return 'Interface ' + src.descr + ' (' + src.real + ')';
		return t('limiter counters');
	}
	/*
	 * Latenz: Verlauf der letzten 120 s aus dem Dienst. Band = min..max je
	 * Sekunde (breites Band = starke Schwankung), Linie = Mittel, gruen
	 * gestrichelt = Grundlatenz, orange = Stau-Schwelle, rote Striche unten =
	 * Paketverlust.
	 */
	function renderLatency(data) {
		var a = data.autorate;
		if (!a || !a.hist) {
			$('#scg_latency').html('<div class="panel panel-default"><div class="panel-body text-muted">' +
				t('Latency measurement is off. Enable it on the Autorate page (it also works without Autorate control).') + '</div></div>');
			return;
		}
		var hs = a.hist.slice(-120), W = 600, H = 140, L = 34, B = 14, N = 120;
		var now = hs.length ? hs[hs.length - 1][0] : 0;
		var ymax = 0;
		hs.forEach(function(r) { if (r[3] !== null && r[3] > ymax) ymax = r[3]; if (r[6] !== null && r[6] + a.threshold > ymax) ymax = r[6] + a.threshold; });
		ymax = Math.max(50, ymax * 1.1);
		/* Achse in runden Schritten */
		var steps = [10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000], st = steps[steps.length - 1];
		for (var k = 0; k < steps.length; k++) if (ymax / steps[k] <= 5) { st = steps[k]; break; }
		ymax = Math.ceil(ymax / st) * st;
		function X(ts) { return L + (W - L) * (1 - (now - ts) / (N - 1)); }
		function Y(v) { return (H - B) - Math.min(1, v / ymax) * (H - B - 4); }
		var up = [], dn = [], avg = [], base = [], thr = [], loss = '';
		hs.forEach(function(r) {
			var x = X(r[0]).toFixed(1);
			if (r[1] !== null) { up.push(x + ',' + Y(r[3]).toFixed(1)); dn.unshift(x + ',' + Y(r[1]).toFixed(1)); avg.push(x + ',' + Y(r[2]).toFixed(1)); }
			if (r[6] !== null) { base.push(x + ',' + Y(r[6]).toFixed(1)); thr.push(x + ',' + Y(r[6] + a.threshold).toFixed(1)); }
			if (r[5]) loss += '<rect class="loss" x="' + (X(r[0]) - 1.5).toFixed(1) + '" y="' + (H - B + 2) + '" width="3" height="' + (Math.min(10, 2 + r[5] * 2)) + '"/>';
		});
		var grid = '';
		for (var v = 0; v <= ymax; v += st) grid += '<line x1="' + L + '" x2="' + W + '" y1="' + Y(v).toFixed(1) + '" y2="' + Y(v).toFixed(1) + '" stroke="#888" stroke-opacity=".2"/>' +
			'<text x="' + (L - 4) + '" y="' + (Y(v) + 3).toFixed(1) + '" text-anchor="end">' + v + '</text>';
		grid += '<text x="' + L + '" y="' + (H - 1) + '">-120 s</text><text x="' + W + '" y="' + (H - 1) + '" text-anchor="end">' + t('now') + '</text>';
		var svg = '<svg class="scg-lat" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none">' + grid +
			(up.length ? '<path class="band" d="M' + up.concat(dn).join(' L') + ' Z"/>' : '') +
			(thr.length ? '<path class="thr" d="M' + thr.join(' L') + '"/>' : '') +
			(base.length ? '<path class="base" d="M' + base.join(' L') + '"/>' : '') +
			(avg.length ? '<path class="avg" d="M' + avg.join(' L') + '"/>' : '') + loss + '</svg>';
		var s = a.stats || {};
		function kpi(label, v, unit, hot) {
			return '<span class="scg-kpi">' + label + ' <b' + (hot ? ' class="scg-hot"' : '') + '>' + (v === null || v === undefined ? '–' : v + unit) + '</b></span>';
		}
		var baseAvg = hs.length && hs[hs.length - 1][6] !== null ? hs[hs.length - 1][6] : null;
		var hotAvg = baseAvg !== null && s.avg !== null && s.avg > baseAvg + a.threshold;
		var refl = '';
		$.each(a.refl || {}, function(ip, r) {
			var hot = r.delta !== null && r.delta > a.threshold;
			refl += '<span class="scg-kpi"><small' + (hot ? ' class="scg-hot"' : '') + '>' + esc(ip) + ': ' + (r.rtt === null ? '–' : r.rtt + ' ms') +
				' (' + t('Base') + ' ' + (r.baseline === null ? '–' : r.baseline) + ')' + (r.lost ? ' &middot; ' + t('%s lost', r.lost) : '') + '</small></span>';
		});
		var title = t('Latency') + ' ' + (a.paused ? '<span class="label label-info">' + t('Speed test running &ndash; Autorate paused') + '</span>' :
			(a.bloat ? '<span class="label label-danger">' + t('Congestion detected') + '</span>' : '<span class="label label-success">' + t('Line clear') + '</span>')) +
			' <small>Pinger ' + esc(a.mode) + ' &middot; ' + t('Threshold') + ' +' + a.threshold + ' ms' + (a.control ? '' : ' &middot; ' + t('measuring only, Autorate control is off')) + '</small>';
		$('#scg_latency').html('<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">' + title + '</h2></div><div class="panel-body">' +
			'<div>' + t('Last 10 s:') + ' ' + kpi(t('average'), s.avg, ' ms', hotAvg) + kpi('P95', s.p95, ' ms', baseAvg !== null && s.p95 > baseAvg + a.threshold) +
			kpi('Min/Max', s.min === null ? null : s.min + ' / ' + s.max, ' ms') + kpi('Jitter', s.jitter, ' ms', s.jitter > a.threshold / 2) +
			kpi(t('Loss'), s.loss, ' %', s.loss > 0) + '</div>' + svg +
			'<div class="scg-legend"><span><i style="background:rgba(255,92,92,.5);height:8px"></i>' + t('Min&ndash;max per second (wide = fluctuating)') + '</span>' +
			'<span><i style="background:#ff5c5c"></i>' + t('average') + '</span><span><i style="background:#2f9e75"></i>' + t('Base latency') + '</span>' +
			'<span><i style="background:#d89b2b"></i>' + t('Congestion threshold') + '</span><span><i style="background:#ff5c5c;height:8px;width:3px"></i>' + t('Loss') + '</span></div>' +
			'<div style="margin-top:4px">' + refl + '</div></div></div>');
	}

	/*
	 * Kombinierter Graph (Test): Latenz (links, ms) und Durchsatz/Limit je
	 * Richtung (rechts, % des Autorate-Maximums) ueber die letzten 300 s.
	 * Download und Upload liegen in Mbit/s weit auseinander, deshalb in %.
	 * Tooltip zeigt die echten Werte.
	 */
	var combHover = null;
	function renderCombined(data) {
		var a = data.autorate;
		if (!a || !a.hist || !a.hist.length) { $('#scg_combined').html(''); return; }
		var W = 900, H = 240, L = 36, R = 40, T = 8, B = 16, N = 300;
		var hs = a.hist, rh = a.rhist || {}, dirs = a.dirs || {};
		var now = hs[hs.length - 1][0];
		['down', 'up'].forEach(function(d) { if (rh[d] && rh[d].length) now = Math.max(now, rh[d][rh[d].length - 1][0]); });
		var t0 = now - N + 1;
		var ymax = 0;
		hs.forEach(function(r) { if (r[0] >= t0 && r[3] !== null && r[3] > ymax) ymax = r[3]; if (r[6] !== null && r[6] + a.threshold > ymax) ymax = r[6] + a.threshold; });
		ymax = Math.max(50, ymax * 1.1);
		var steps = [10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000], st = steps[steps.length - 1];
		for (var k = 0; k < steps.length; k++) if (ymax / steps[k] <= 5) { st = steps[k]; break; }
		ymax = Math.ceil(ymax / st) * st;
		function X(ts) { return L + (W - L - R) * (ts - t0) / (N - 1); }
		function Y(v) { return (H - B) - Math.min(1, v / ymax) * (H - B - T); }
		function P(pct) { return (H - B) - Math.min(1.05, Math.max(0, pct)) * (H - B - T) / 1.05; }
		var g = '';
		for (var v = 0; v <= ymax; v += st) {
			g += '<line x1="' + L + '" x2="' + (W - R) + '" y1="' + Y(v).toFixed(1) + '" y2="' + Y(v).toFixed(1) + '" stroke="#888" stroke-opacity=".18"/>' +
				'<text x="' + (L - 4) + '" y="' + (Y(v) + 3).toFixed(1) + '" text-anchor="end">' + v + '</text>';
		}
		[0, 0.25, 0.5, 0.75, 1].forEach(function(p) {
			g += '<text x="' + (W - R + 4) + '" y="' + (P(p) + 3).toFixed(1) + '">' + (p * 100) + '%</text>';
		});
		g += '<text x="' + L + '" y="' + (H - 3) + '">-5 min</text><text x="' + ((L + W - R) / 2) + '" y="' + (H - 3) + '" text-anchor="middle">-2.5 min</text>' +
			'<text x="' + (W - R) + '" y="' + (H - 3) + '" text-anchor="end">' + t('now') + '</text>';
		/* Durchsatz: Luecken (Dienst neu gestartet) bleiben Luecken */
		function path(arr, idx, max, close) {
			var segs = [], seg = null, prev = null;
			arr.forEach(function(r) {
				if (r[0] < t0) return;
				if (prev === null || r[0] - prev > 3) { seg = []; segs.push(seg); }
				seg.push([X(r[0]).toFixed(1), P(r[idx] / max).toFixed(1)]);
				prev = r[0];
			});
			return segs.map(function(sg) {
				var d = 'M' + sg.map(function(p) { return p[0] + ',' + p[1]; }).join(' L');
				if (close) d += ' L' + sg[sg.length - 1][0] + ',' + (H - B) + ' L' + sg[0][0] + ',' + (H - B) + ' Z';
				return d;
			}).join(' ');
		}
		var lines = '';
		if (rh.down && dirs.down) {
			var mx = dirs.down.max;
			lines += '<path class="dfill" d="' + path(rh.down, 2, mx, true) + '"/><path class="dach" d="' + path(rh.down, 2, mx, false) + '"/>' +
				'<path class="dlim" d="' + path(rh.down, 1, mx, false) + '"/>';
		}
		if (rh.up && dirs.up) {
			var mu = dirs.up.max;
			lines += '<path class="uach" d="' + path(rh.up, 2, mu, false) + '"/><path class="ulim" d="' + path(rh.up, 1, mu, false) + '"/>';
		}
		var upp = [], dnp = [], avg = '', thr = '', prevT = null;
		hs.forEach(function(r) {
			if (r[0] < t0) return;
			var x = X(r[0]).toFixed(1);
			if (r[1] !== null) {
				upp.push(x + ',' + Y(r[3]).toFixed(1)); dnp.unshift(x + ',' + Y(r[1]).toFixed(1));
				avg += (prevT === null || r[0] - prevT > 3 ? ' M' : ' L') + x + ',' + Y(r[2]).toFixed(1);
				prevT = r[0];
			}
			if (r[6] !== null) thr += (thr ? ' L' : 'M') + x + ',' + Y(r[6] + a.threshold).toFixed(1);
		});
		var svg = '<svg class="scg-comb" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none">' + g + lines +
			(upp.length ? '<path class="band" d="M' + upp.concat(dnp).join(' L') + ' Z"/>' : '') +
			(thr ? '<path class="thr" d="' + thr + '"/>' : '') + (avg ? '<path class="lat" d="' + avg + '"/>' : '') +
			'<line class="cur" x1="0" x2="0" y1="' + T + '" y2="' + (H - B) + '" style="display:none"/></svg>';
		var leg = '<div class="scg-legend">' +
			'<span><i style="background:#ff5c5c"></i>' + t('Latency (ms, left)') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#9aa4b2 0 2px,transparent 2px 4px)"></i>' + t('Congestion threshold') + '</span>' +
			'<span><i style="background:#3b8fd9"></i>' + t('Download achieved') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#3b8fd9 0 4px,transparent 4px 6px)"></i>' + t('Download limit') + '</span>' +
			'<span><i style="background:#e8892b"></i>' + t('Upload achieved') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#e8892b 0 4px,transparent 4px 6px)"></i>' + t('Upload limit') + '</span>' +
			'<span class="text-muted">' + t('Throughput in % of the Autorate maximum (right axis)') + '</span></div>';
		$('#scg_combined').html('<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">' +
			t('Latency and throughput') + ' <span class="label label-info">' + t('test') + '</span> <small>' + t('last 5 minutes') + '</small></h2></div>' +
			'<div class="panel-body"><div class="scg-comb-wrap">' + svg + '<div class="scg-comb-tip" style="display:none"></div></div>' + leg + '</div></div>');

		/* Tooltip: echte Werte zur Sekunde unter dem Mauszeiger, bleibt beim Neuzeichnen stehen */
		var wrap = $('#scg_combined .scg-comb-wrap'), el = wrap.find('svg'), tip = wrap.find('.scg-comb-tip'), cur = el.find('.cur');
		function find(arr, ts) {
			var best = null, bd = 3;
			(arr || []).forEach(function(r) { var dd = Math.abs(r[0] - ts); if (dd <= bd) { bd = dd; best = r; } });
			return best;
		}
		function show(frac) {
			var ts = Math.round(t0 + (frac * W - L) / (W - L - R) * (N - 1));
			if (ts < t0 || ts > now) { tip.hide(); cur.hide(); return; }
			var x = X(ts), lr = find(hs, ts), dr = find(rh.down, ts), ur = find(rh.up, ts);
			var h = '<b>' + (now - ts == 0 ? t('now') : '-' + (now - ts) + ' s') + '</b>';
			if (lr && lr[2] !== null) h += '<br>' + t('Latency') + ': <b>' + lr[2] + ' ms</b> <small>(' + lr[1] + '&ndash;' + lr[3] + ')</small>';
			else if (lr && lr[5]) h += '<br>' + t('Latency') + ': ' + t('lost');
			if (dr) h += '<br>&darr; ' + fmtRate(dr[2] * 1000) + ' <small>/ Limit ' + fmtRate(dr[1] * 1000) + '</small>';
			if (ur) h += '<br>&uarr; ' + fmtRate(ur[2] * 1000) + ' <small>/ Limit ' + fmtRate(ur[1] * 1000) + '</small>';
			cur.attr({ x1: x, x2: x }).show();
			tip.html(h).show();
			var rect = el[0].getBoundingClientRect(), px = x / W * rect.width, tw = tip.outerWidth();
			tip.css('left', Math.max(0, Math.min(rect.width - tw, px + 12)) + 'px');
		}
		el.on('mousemove', function(e) {
			var rect = this.getBoundingClientRect();
			combHover = (e.clientX - rect.left) / rect.width;
			show(combHover);
		}).on('mouseleave', function() { combHover = null; tip.hide(); cur.hide(); });
		if (combHover !== null) show(combHover);
	}

	/*
	 * Langzeitgraph (Test): wie der kombinierte Graph, aber aus den
	 * Minutenwerten des Dienstes ueber 1-48 h. Eigene Abfrage alle 60 s.
	 */
	var longHover = null, longData = null;
	var LONG_HOURS = [1, 2, 3, 6, 12, 24, 48];
	function longHours() {
		var h = parseInt(load('longh'), 10);
		return LONG_HOURS.indexOf(h) >= 0 ? h : 24;
	}
	function fetchLong() {
		$.ajax({ url: 'status_limiters_live.php', data: { longhist: 1, hours: longHours() }, dataType: 'json', cache: false })
			.done(function(d) { longData = d; renderLong(); });
	}
	function renderLong() {
		var d = longData, hours = longHours();
		var sel = '<select class="form-control input-sm" id="scg_longh" style="display:inline-block;width:auto;margin-left:8px">' +
			LONG_HOURS.map(function(h) { return '<option value="' + h + '"' + (h == hours ? ' selected' : '') + '>' + h + ' h</option>'; }).join('') + '</select>';
		var head = '<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">' + t('Latency and throughput') +
			' <span class="label label-info">' + t('test') + '</span> <small>' + t('history') + '</small>' + sel + '</h2></div><div class="panel-body">';
		if (!d || !d.rows || !d.rows.length) {
			$('#scg_long').html(head + '<p class="text-muted">' + t('No history yet. The Autorate service records one value per minute; the first point appears after one minute.') + '</p></div></div>');
			bindLongSel();
			return;
		}
		var W = 900, H = 240, L = 36, R = 40, T = 8, B = 16;
		var t1 = d.now, t0 = t1 - hours * 3600, rows = d.rows, bucket = d.bucket || 60, thrv = d.threshold || 30;
		var ymax = 0;
		rows.forEach(function(r) {
			if (r.l && r.l[2] !== null && r.l[2] > ymax) ymax = r.l[2];
			if (r.l && r.l[6] !== null && r.l[6] + thrv > ymax) ymax = r.l[6] + thrv;
		});
		/* Maximum-Ausreisser kappen: Achse nach P95 der Minuten, nicht nach dem einen Extremwert */
		var p95s = rows.filter(function(r) { return r.l && r.l[3] !== null; }).map(function(r) { return r.l[3]; }).sort(function(a, b) { return a - b; });
		var cap = p95s.length ? p95s[Math.floor(0.98 * (p95s.length - 1))] * 1.5 : ymax;
		ymax = Math.max(50, Math.min(ymax, Math.max(cap, 50)) * 1.1);
		var steps = [10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 5000], st = steps[steps.length - 1];
		for (var k = 0; k < steps.length; k++) if (ymax / steps[k] <= 5) { st = steps[k]; break; }
		ymax = Math.ceil(ymax / st) * st;
		function X(ts) { return L + (W - L - R) * (ts - t0) / (t1 - t0); }
		function Y(v) { return (H - B) - Math.min(1, v / ymax) * (H - B - T); }
		function P(p) { return (H - B) - Math.min(1.05, Math.max(0, p)) * (H - B - T) / 1.05; }
		function hhmm(ts) { var dt = new Date(ts * 1000); return ('0' + dt.getHours()).slice(-2) + ':' + ('0' + dt.getMinutes()).slice(-2); }
		var g = '';
		for (var v = 0; v <= ymax; v += st) {
			g += '<line x1="' + L + '" x2="' + (W - R) + '" y1="' + Y(v).toFixed(1) + '" y2="' + Y(v).toFixed(1) + '" stroke="#888" stroke-opacity=".18"/>' +
				'<text x="' + (L - 4) + '" y="' + (Y(v) + 3).toFixed(1) + '" text-anchor="end">' + v + '</text>';
		}
		[0, 0.25, 0.5, 0.75, 1].forEach(function(p) { g += '<text x="' + (W - R + 4) + '" y="' + (P(p) + 3).toFixed(1) + '">' + (p * 100) + '%</text>'; });
		for (var j = 0; j <= 6; j++) {
			var ts = t0 + (t1 - t0) * j / 6;
			g += '<text x="' + X(ts).toFixed(1) + '" y="' + (H - 3) + '" text-anchor="' + (j == 0 ? 'start' : (j == 6 ? 'end' : 'middle')) + '">' + hhmm(ts) + '</text>';
		}
		var gap = bucket * 3;
		/* Linien je Groesse; Luecken (Dienst aus, pfSense neu gestartet) bleiben Luecken */
		function segs(get) {
			var out = [], sg = null, prev = null;
			rows.forEach(function(r) {
				var y = get(r);
				if (y === null || y === undefined || isNaN(y)) { prev = null; return; }
				if (prev === null || r.t - prev > gap) { sg = []; out.push(sg); }
				sg.push([X(r.t + bucket / 2).toFixed(1), y.toFixed(1)]);
				prev = r.t;
			});
			return out;
		}
		function line(get) { return segs(get).map(function(sg) { return 'M' + sg.map(function(p) { return p[0] + ',' + p[1]; }).join(' L'); }).join(' '); }
		function area(get) {
			return segs(get).map(function(sg) {
				return 'M' + sg.map(function(p) { return p[0] + ',' + p[1]; }).join(' L') + ' L' + sg[sg.length - 1][0] + ',' + (H - B) + ' L' + sg[0][0] + ',' + (H - B) + ' Z';
			}).join(' ');
		}
		function band(getHi, getLo) {
			var hi = segs(getHi), lo = segs(getLo);
			return hi.map(function(sg, i) {
				var l = (lo[i] || []).slice().reverse();
				return 'M' + sg.concat(l).map(function(p) { return p[0] + ',' + p[1]; }).join(' L') + ' Z';
			}).join(' ');
		}
		function pct(k, idx) { return function(r) { return r[k] && r.mx && r.mx[k == 'd' ? 'down' : 'up'] ? P(r[k][idx] / r.mx[k == 'd' ? 'down' : 'up']) : null; }; }
		var lat = function(i) { return function(r) { return r.l && r.l[i] !== null ? Y(r.l[i]) : null; }; };
		var pauseRects = '';
		rows.forEach(function(r) { if (r.p) pauseRects += '<rect x="' + X(r.t).toFixed(1) + '" y="' + T + '" width="' + Math.max(1, (X(r.t + bucket) - X(r.t))).toFixed(1) + '" height="' + (H - B - T) + '" fill="#5bc0de" fill-opacity=".12"/>'; });
		var loss = '';
		rows.forEach(function(r) { if (r.l && r.l[4]) loss += '<rect x="' + (X(r.t + bucket / 2) - 1).toFixed(1) + '" y="' + (H - B - 8) + '" width="2" height="8" fill="#ff5c5c"/>'; });
		var svg = '<svg class="scg-comb" viewBox="0 0 ' + W + ' ' + H + '" preserveAspectRatio="none">' + g + pauseRects +
			'<path class="dfill" d="' + area(pct('d', 1)) + '"/><path class="dach" d="' + line(pct('d', 1)) + '"/><path class="dlim" d="' + line(pct('d', 0)) + '"/>' +
			'<path class="uach" d="' + line(pct('u', 1)) + '"/><path class="ulim" d="' + line(pct('u', 0)) + '"/>' +
			'<path class="band" d="' + band(lat(3), lat(0)) + '"/>' +
			'<path class="thr" d="' + line(function(r) { return r.l && r.l[6] !== null ? Y(r.l[6] + thrv) : null; }) + '"/>' +
			'<path class="lat" d="' + line(lat(1)) + '"/>' + loss +
			'<line class="cur" x1="0" x2="0" y1="' + T + '" y2="' + (H - B) + '" style="display:none"/></svg>';
		var leg = '<div class="scg-legend">' +
			'<span><i style="background:#ff5c5c"></i>' + t('Latency average (ms, left)') + '</span>' +
			'<span><i style="background:rgba(255,92,92,.4);height:8px"></i>' + t('min to P95') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#9aa4b2 0 2px,transparent 2px 4px)"></i>' + t('Congestion threshold') + '</span>' +
			'<span><i style="background:#3b8fd9"></i>' + t('Download achieved') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#3b8fd9 0 4px,transparent 4px 6px)"></i>' + t('Download limit') + '</span>' +
			'<span><i style="background:#e8892b"></i>' + t('Upload achieved') + '</span>' +
			'<span><i style="background:repeating-linear-gradient(90deg,#e8892b 0 4px,transparent 4px 6px)"></i>' + t('Upload limit') + '</span>' +
			'<span><i style="background:rgba(91,192,222,.4);height:8px"></i>' + t('Speed test (Autorate paused)') + '</span>' +
			'<span class="text-muted">' + (bucket == 60 ? t('1 point per minute') : t('1 point per %s minutes', bucket / 60)) + ' &middot; ' +
			t('Throughput in % of the Autorate maximum (right axis)') + '</span></div>';
		$('#scg_long').html(head + '<div class="scg-comb-wrap">' + svg + '<div class="scg-comb-tip" style="display:none"></div></div>' + leg + '</div></div>');
		bindLongSel();

		var wrap = $('#scg_long .scg-comb-wrap'), el = wrap.find('svg'), tip = wrap.find('.scg-comb-tip'), cur = el.find('.cur');
		function show(frac) {
			var ts = t0 + (frac * W - L) / (W - L - R) * (t1 - t0), best = null, bd = bucket * 1.5;
			rows.forEach(function(r) { var dd = Math.abs(r.t + bucket / 2 - ts); if (dd < bd) { bd = dd; best = r; } });
			if (!best) { tip.hide(); cur.hide(); return; }
			var x = X(best.t + bucket / 2);
			var h = '<b>' + hhmm(best.t) + (bucket > 60 ? '&ndash;' + hhmm(best.t + bucket) : '') + '</b>' + (best.p ? ' <small>(' + t('speed test') + ')</small>' : '');
			if (best.l && best.l[1] !== null) h += '<br>' + t('Latency') + ': <b>' + best.l[1] + ' ms</b> <small>(' + best.l[0] + '&ndash;' + best.l[2] + ', P95 ' + best.l[3] + ')</small>';
			if (best.l && best.l[4]) h += '<br><span class="scg-hot">' + t('%s lost', best.l[4]) + '</span>';
			if (best.d) h += '<br>&darr; ' + fmtRate(best.d[1] * 1000) + ' <small>(max ' + fmtRate(best.d[2] * 1000) + ') / Limit ' + fmtRate(best.d[0] * 1000) + '</small>';
			if (best.u) h += '<br>&uarr; ' + fmtRate(best.u[1] * 1000) + ' <small>(max ' + fmtRate(best.u[2] * 1000) + ') / Limit ' + fmtRate(best.u[0] * 1000) + '</small>';
			cur.attr({ x1: x, x2: x }).show();
			tip.html(h).show();
			var rect = el[0].getBoundingClientRect(), px = x / W * rect.width, tw = tip.outerWidth();
			tip.css('left', Math.max(0, Math.min(rect.width - tw, px + 12)) + 'px');
		}
		el.on('mousemove', function(e) {
			var rect = this.getBoundingClientRect();
			longHover = (e.clientX - rect.left) / rect.width;
			show(longHover);
		}).on('mouseleave', function() { longHover = null; tip.hide(); cur.hide(); });
		if (longHover !== null) show(longHover);
	}
	function bindLongSel() {
		$('#scg_longh').on('change', function() { store('longh', this.value); longData = null; fetchLong(); });
	}

	function renderAutorate(data) {
		var a = data.autorate;
		if (!a || !a.control || !a.dirs || !Object.keys(a.dirs).length) { $('#scg_autorate').html(''); return; }
		var maxDelay = 0;
		$.each(a.refl || {}, function(ip, r) { if (r.delta !== null && r.delta > maxDelay) maxDelay = r.delta; });
		['down', 'up'].forEach(function(d) {
			if (!a.dirs[d]) return;
			arHist[d].push([a.dirs[d].shaper * 1000, a.dirs[d].achieved * 1000]);
			if (arHist[d].length > 300) arHist[d].shift();
		});
		arHist.delay.push(Math.max(0, maxDelay));
		if (arHist.delay.length > 300) arHist.delay.shift();

		var h = '<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">Autorate ' +
			' <small>' + (a.source ? t('measured at') + ' ' + esc(sourceText(a.source)) : '') +
			(a.resets ? ' &middot; ' + t('%s&times; reapplied after filter reload', a.resets) : '') +
			(a.discarded ? ' &middot; ' + t('%s samples discarded due to processing pause', a.discarded) : '') + '</small></h2></div><div class="panel-body"><div class="row">';
		['down', 'up'].forEach(function(d) {
			var x = a.dirs[d];
			if (!x) return;
			var hist = arHist[d], mx = x.max * 1000;
			if (a.rhist && a.rhist[d] && a.rhist[d].length) {
				/* [t, limit kbit, achieved kbit] per second, gaps (service paused) stay as gaps in time */
				var rh = a.rhist[d], tEnd = rh[rh.length - 1][0];
				hist = [];
				var byT = {};
				rh.forEach(function(v) { byT[v[0]] = v; });
				for (var tt = tEnd - 299; tt <= tEnd; tt++) {
					if (byT[tt]) hist.push([byT[tt][1] * 1000, byT[tt][2] * 1000]);
					else if (hist.length) hist.push(hist[hist.length - 1]);
				}
			}
			h += '<div class="col-md-4"><b>' + (d == 'down' ? '&darr; Download' : '&uarr; Upload') + '</b> <small>' + esc(x.pipe) + '</small><br>' +
				'Limit <b>' + fmtRate(x.shaper * 1000) + '</b> &middot; ' + t('achieved') + ' ' + fmtRate(x.achieved * 1000) + '<br>' +
				'<small class="text-muted">' + fmtRate(x.min * 1000) + ' &hellip; ' + fmtRate(x.max * 1000) + ', ' +
				(x.learnbase ? '<span title="' + esc(t('Start value of the next transfer: the rate that last ran under load without congestion')) + '">' + t('learned base') + ' ' + fmtRate(x.learned * 1000) + '</span>'
					: t('Base') + ' ' + fmtRate(x.base * 1000)) + ' &middot; ' +
				esc(STATES[x.state] !== undefined ? STATES[x.state] : x.state) + '</small>' +
				'<svg class="scg-spark" viewBox="0 0 300 60" preserveAspectRatio="none">' +
				'<path class="ach" d="' + sparkPath(hist.map(function(v) { return v[1]; }), mx, 300, 60, true) + '"/>' +
				'<path class="lim" d="' + sparkPath(hist.map(function(v) { return v[0]; }), mx, 300, 60, false) + '"/></svg></div>';
		});
		if (a.discarded || a.resets) {
			h += '<div class="col-md-4"><small class="text-muted">' +
				(a.resets ? t('%s&times; reapplied after filter reload', a.resets) + '<br>' : '') +
				(a.discarded ? t('%s samples discarded due to processing pause', a.discarded) : '') + '</small></div>';
		}
		h += '</div><div class="scg-legend"><span><i style="background:#d89b2b"></i>Limit</span><span><i style="background:#2f9e75"></i>' + t('achieved') + '</span></div></div></div>';
		$('#scg_autorate').html(h);
	}

	/*
	 * Interfaces in fixed groups: WAN, VPN, shaped, not shaped; sorted by
	 * name within each group so the list does not jump in live mode.
	 * VPN tunnels run encrypted over the WAN and are therefore not part of
	 * the WAN total.
	 */
	function renderIfaces(data) {
		if (!data.ifaces || !data.ifaces.length) { $('#scg_ifaces').html(''); return; }
		var showIdle = $('#scg_idleifs').prop('checked'), showVpn = $('#scg_showvpn').prop('checked');
		var groups = { wan: [], vpn: [], shaped: [], unshaped: [] };
		var wanDown = 0, wanUp = 0, haveWan = false;
		data.ifaces.forEach(function(i) {
			if (i.kind === 'wan') { haveWan = true; wanDown += i.down || 0; wanUp += i.up || 0; groups.wan.push(i); }
			else if (i.kind === 'vpn') groups.vpn.push(i);
			else if (SHAPED[i.if]) groups.shaped.push(i);
			else groups.unshaped.push(i);
		});
		var labels = {
			wan: '<span class="label label-primary">WAN</span>',
			vpn: '<span class="label label-info">VPN</span>',
			shaped: null,
			unshaped: '<span class="label label-default">' + t('not shaped') + '</span>'
		};
		var titles = { wan: 'WAN', vpn: t('VPN tunnels (included in WAN)'), shaped: t('Shaped'), unshaped: t('Not shaped') };
		var rows = '';
		['wan', 'vpn', 'shaped', 'unshaped'].forEach(function(g) {
			var list = groups[g].slice().sort(function(a, b) { return a.descr.localeCompare(b.descr); });
			var body = '';
			list.forEach(function(i) {
				var idle = (i.down || 0) < 1000 && (i.up || 0) < 1000;
				if (!showIdle && idle && g !== 'wan') return;
				if (g === 'vpn' && !showVpn) return;
				var typ = g === 'shaped' ? '<span class="label label-success">' + t('shaped') + ' (' + esc(SHAPED[i.if]) + ')</span>' : labels[g];
				body += '<tr class="' + (idle ? 'scg-idle' : '') + '"><td>' + esc(i.descr) + ' <small class="text-muted">' + esc(i.real) + '</small></td><td>' + typ + '</td>' +
					'<td class="scg-num">' + fmtRate(i.down) + '</td><td class="scg-num">' + fmtRate(i.up) + '</td></tr>';
			});
			if (body) rows += '<tr class="scg-group"><td colspan="4">' + titles[g] + '</td></tr>' + body;
		});
		/* WAN against the sum of the limiters */
		var limDown = 0, limUp = 0;
		data.pipes.forEach(function(p) {
			if (DOWNPIPES[p.name] && p.rate) limDown += p.rate;
			if (UPPIPES[p.name] && p.rate) limUp += p.rate;
		});
		/* Summenzeilen immer anzeigen; ohne Vorwert oder WAN als "–" */
		var summary = '';
		{
			var ok = haveWan && prev;
			if (!ok) { wanDown = wanUp = limDown = limUp = null; }
			var unDown = ok ? Math.max(0, wanDown - limDown) : null, unUp = ok ? Math.max(0, wanUp - limUp) : null;
			summary = '<tr class="scg-group"><td colspan="4">' + t('Total') + '</td></tr>' +
				'<tr><td colspan="2"><b>' + t('WAN total') + '</b></td><td class="scg-num"><b>' + fmtRate(wanDown) + '</b></td><td class="scg-num"><b>' + fmtRate(wanUp) + '</b></td></tr>' +
				'<tr><td colspan="2">' + t('of which via limiters') + '</td><td class="scg-num">' + fmtRate(limDown) + '</td><td class="scg-num">' + fmtRate(limUp) + '</td></tr>' +
				'<tr><td colspan="2">' + t('not shaped') + ' <small class="text-muted">' + t('(VPN overhead, pfSense itself, local services, interfaces without limiter)') + '</small></td>' +
				'<td class="scg-num' + (unDown > 0.1 * wanDown && unDown > 1e6 ? ' scg-hot' : '') + '">' + fmtRate(unDown) + '</td>' +
				'<td class="scg-num' + (unUp > 0.1 * wanUp && unUp > 1e6 ? ' scg-hot' : '') + '">' + fmtRate(unUp) + '</td></tr>';
		}
		$('#scg_ifaces').html('<div class="panel panel-default"><div class="panel-heading"><h2 class="panel-title">Interfaces ' +
			'<small>' + t('Download / upload as seen from the internet') + '</small></h2></div><div class="table-responsive panel-body">' +
			'<table class="table table-condensed" style="margin-bottom:0"><thead><tr><th>Interface</th><th>' + t('Type') + '</th>' +
			'<th class="scg-num">&darr; Download</th><th class="scg-num">&uarr; Upload</th></tr></thead><tbody>' + rows + summary +
			'</tbody></table></div></div>');
	}

	function renderAll(data) {
		renderCombined(data);
		renderLatency(data);
		renderAutorate(data);
		renderIfaces(data);
		renderMatrix(data);
		renderPipes(data);
	}

	function poll() {
		if (paused) return;
		$.ajax({ url: 'status_limiters_live.php', data: { getstats: 1 }, dataType: 'json', cache: false })
			.done(function(data) {
				compute(data);
				renderAll(data);
				prev = data;
				$('#scg_status').text(t('As of') + ': ' + new Date().toLocaleTimeString());
			})
			.fail(function() { $('#scg_status').text(t('Query failed')); })
			.always(function() { schedule(); });
	}
	function schedule() {
		clearTimeout(timer);
		if (!paused) timer = setTimeout(poll, parseInt($('#scg_interval').val(), 10));
	}
	$('#scg_interval').on('change', schedule);
	$('#scg_hideidle, #scg_unit, #scg_idleifs, #scg_showvpn').on('change', function() { if (prev) { renderIfaces(prev); renderMatrix(prev); renderPipes(prev); } });
	/* Matrix ein-/ausklappen (Checkbox im Panel-Kopf, wird neu gezeichnet) */
	$(document).on('change', '#scg_matrix_show', function() {
		showMatrix = this.checked;
		store('matrix', showMatrix ? '1' : '0');
		if (prev) renderMatrix(prev);
	});
	$('#scg_pause').on('click', function() {
		paused = !paused;
		$(this).html(paused ? '<i class="fa-solid fa-play"></i> ' + t('Resume') : '<i class="fa-solid fa-pause"></i> ' + t('Pause'));
		if (!paused) { prev = null; poll(); }
	});
	poll();
	/* Langzeitgraph: eigene Abfrage, neue Minutenwerte gibt es nur einmal pro Minute */
	fetchLong();
	setInterval(function() { if (!paused) fetchLong(); }, 60000);
});
//]]>
</script>

<?php include("foot.inc"); ?>
