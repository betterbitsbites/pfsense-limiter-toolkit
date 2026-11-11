<?php
/*
 * shaperclassgen_autorate.inc
 *
 * Autorate: passt die Bandbreite von Limitern laufend an die tatsaechlich
 * verfuegbare Leitungskapazitaet an. Signal ist der Anstieg der Ping-Zeit
 * (RTT) ueber die gelernte Grundlatenz unter Last.
 *
 *  - Latenz steigt und Richtung ist ausgelastet  -> Limiter auf knapp unter
 *    den gerade erreichten Durchsatz senken
 *  - Latenz normal und Richtung ausgelastet       -> Limiter langsam anheben
 *  - Leerlauf                                     -> langsam Richtung Basiswert
 *
 * Idee nach cake-autorate (OpenWrt), eigenstaendige Umsetzung fuer dummynet.
 * Licensed under the Apache License, Version 2.0
 */

require_once("/usr/local/pkg/shaperclassgen.inc");

define('SCG_AR_STATUS', '/var/run/scg_autorate.json');
define('SCG_AR_PID', '/var/run/scg_autorate.pid');
define('SCG_AR_RC', '/usr/local/etc/rc.d/scg_autorate.sh');
define('SCG_AR_RULES', '/tmp/rules.limiter');
define('SCG_AR_BASELINE', '/var/db/scg_autorate_baseline.json');
define('SCG_AR_PAUSE', '/var/run/scg_autorate.pause');   /* Speedtest: Regelung pausiert bis Zeitstempel */

/* ------------------------------------------------------------------ */
/* Einstellungen                                                       */
/* ------------------------------------------------------------------ */

function scg_ar_defaults() {
	return array(
		'enable' => false,
		'down' => array('pipe' => '', 'min' => '', 'base' => '', 'max' => ''),
		'up' => array('pipe' => '', 'min' => '', 'base' => '', 'max' => ''),
		'reflectors' => '1.1.1.1 9.9.9.9 8.8.8.8',
		'interval' => 200,      /* ms zwischen Pings je Ziel */
		'threshold' => 30,      /* ms Anstieg ueber Grundlatenz = Stau */
		'decfactor' => 0.9,     /* bei Stau: neue Rate = erreichter Durchsatz * Faktor */
		'incstep' => 2,         /* % Anhebung je Takt bei freier Leitung */
		'tick' => 500,          /* ms Regeltakt */
		'wanif' => '',          /* Durchsatz messen an: '' = automatisch (erstes WAN), 'limiter' = Limiter-Zaehler, sonst Interface */
		'learnbase' => true,    /* Basis lernen: Leerlauf-Ziel und Startwert = zuletzt stabil laufende Rate */
		'monitor' => true,      /* Latenz auch messen, wenn Autorate aus ist (fuer Limiter Live) */
	);
}

function scg_ar_settings() {
	$d = scg_ar_defaults();
	$s = scg_load_settings();
	$a = (isset($s['autorate']) && is_array($s['autorate'])) ? $s['autorate'] : array();
	foreach ($d as $k => $v) {
		if (is_array($v)) {
			$d[$k] = array_merge($v, (isset($a[$k]) && is_array($a[$k])) ? $a[$k] : array());
		} elseif (array_key_exists($k, $a)) {
			$d[$k] = $a[$k];
		}
	}
	return $d;
}

/* Konfigurierte Bandbreite eines Limiters in Mbit/s (erste Bandbreitenzeile) */
function scg_ar_pipe_cfg_mbit($name) {
	foreach ((array)scg_cfg_get('dnshaper/queue', array()) as $p) {
		if (!is_array($p) || ($p['name'] ?? '') !== $name) {
			continue;
		}
		$it = $p['bandwidth']['item'][0] ?? null;
		if (!$it) {
			return 0;
		}
		$f = array('b' => 0.000001, 'Kb' => 0.001, 'Mb' => 1, 'Gb' => 1000);
		return floatval($it['bw']) * ($f[$it['bwscale'] ?? 'Mb'] ?? 1);
	}
	return 0;
}

function scg_ar_validate($a, &$errors) {
	$pipes = scg_pipes();
	$any = false;
	foreach (array('down' => 'Download', 'up' => 'Upload') as $d => $lbl) {
		$c = $a[$d];
		if ($c['pipe'] === '') {
			continue;
		}
		$any = true;
		if (!isset($pipes[$c['pipe']])) {
			$errors[] = scg_t('%s: limiter "%s" does not exist.', $lbl, $c['pipe']);
			continue;
		}
		foreach (array('min', 'base', 'max') as $k) {
			if (!is_numeric($c[$k]) || floatval($c[$k]) <= 0) {
				$errors[] = scg_t('%s: %s must be a positive number (Mbit/s).', $lbl, $k);
			}
		}
		if (is_numeric($c['min']) && is_numeric($c['base']) && is_numeric($c['max']) &&
		    !(floatval($c['min']) <= floatval($c['base']) && floatval($c['base']) <= floatval($c['max']))) {
			$errors[] = scg_t('%s: minimum <= base <= maximum must hold.', $lbl);
		}
	}
	$wif = $a['wanif'] ?? '';
	if ($wif !== '' && $wif !== 'limiter' && !isset(get_configured_interface_with_descr()[$wif])) {
		$errors[] = scg_t('Measurement interface "%s" does not exist.', $wif);
	}
	if (!empty($a['down']['pipe']) && $a['down']['pipe'] === $a['up']['pipe']) {
		$errors[] = scg_t('Download and upload must not use the same limiter.');
	}
	if (!empty($a['enable']) && !$any) {
		$errors[] = scg_t('At least one direction needs a limiter.');
	}
	$refl = preg_split('/[\s,]+/', trim($a['reflectors']));
	if (empty($refl) || $refl[0] === '') {
		$errors[] = scg_t('Specify at least one ping target.');
	}
	foreach ($refl as $r) {
		if ($r !== '' && !filter_var($r, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
			$errors[] = scg_t('Ping target "%s" is not an IPv4 address.', $r);
		}
	}
	$num = array('interval' => array(50, 2000), 'threshold' => array(5, 500), 'decfactor' => array(0.5, 0.99),
		'incstep' => array(0.2, 20), 'tick' => array(200, 2000));
	foreach ($num as $k => $r) {
		if (!is_numeric($a[$k]) || $a[$k] < $r[0] || $a[$k] > $r[1]) {
			$errors[] = scg_t('Value "%s" must be between %s and %s.', $k, $r[0], $r[1]);
		}
	}
}

/* Anzeigetexte der Autorate-Zustaende */
function scg_ar_state_texts() {
	$t = array(
		'start' => 'starting',
		'no_limiter' => 'no limiter',
		'decrease' => 'decreasing',
		'decrease_low' => 'decreasing (line below limit)',
		'bloat_wait' => 'congestion, waiting',
		'bloat_min' => 'congestion, minimum reached',
		'bloat_idle' => 'congestion without own load',
		'bloat_lowload' => 'congestion, direction hardly loaded',
		'increase' => 'increasing',
		'max' => 'maximum',
		'hold' => 'holding',
		'idle' => 'idle',
		'monitor' => 'measuring only',
		'paused' => 'paused (speed test running)',
	);
	foreach ($t as $k => $v) {
		$t[$k] = scg_t($v);
	}
	return $t;
}

function scg_ar_state_text($code) {
	$t = scg_ar_state_texts();
	return $t[$code] ?? $code;
}

/* Interface, an dem Autorate den Durchsatz misst: array(if, real, descr) oder null */
function scg_ar_wan($a) {
	if (($a['wanif'] ?? '') === 'limiter') {
		return null;
	}
	$list = get_configured_interface_with_descr();
	if (($a['wanif'] ?? '') !== '' && isset($list[$a['wanif']])) {
		return array($a['wanif'], scg_if_real($a['wanif']), $list[$a['wanif']]);
	}
	/* automatisch: erstes physisches WAN, keine VPN-Tunnel (die laufen ueber das WAN) */
	foreach ($list as $if => $descr) {
		$real = scg_if_real($if);
		if (scg_if_kind($if, $real) === 'wan') {
			return array($if, $real, $descr);
		}
	}
	return null;
}

/* ------------------------------------------------------------------ */
/* dnctl                                                               */
/* ------------------------------------------------------------------ */

/* Originalzeile "pipe N config ..." aus den von pfSense erzeugten Regeln */
function scg_ar_pipe_line($num) {
	$rules = @file_get_contents(SCG_AR_RULES);
	if ($rules !== false && preg_match('/^pipe ' . intval($num) . ' config.*$/m', $rules, $m)) {
		return trim($m[0]);
	}
	return null;
}

function scg_ar_dnctl_args($line) {
	$out = array();
	foreach (preg_split('/\s+/', trim($line)) as $t) {
		$out[] = escapeshellarg($t);
	}
	return implode(' ', $out);
}

/* Setzt die Bandbreite eines laufenden Limiters, alle anderen Parameter bleiben */
function scg_ar_set_bw($num, $kbit) {
	$kbit = max(1, (int)round($kbit));
	$line = scg_ar_pipe_line($num);
	if ($line === null) {
		$line = "pipe " . intval($num) . " config bw {$kbit}Kb";
	} elseif (preg_match('/\sbw\s+\S+/', $line)) {
		$line = preg_replace('/\sbw\s+\S+/', " bw {$kbit}Kb", $line, 1);
	} else {
		$line .= " bw {$kbit}Kb";
	}
	exec('/sbin/dnctl ' . scg_ar_dnctl_args($line) . ' 2>&1', $o, $rc);
	return $rc == 0;
}

/* Stellt die konfigurierte Bandbreite der verwalteten Limiter wieder her */
function scg_ar_restore() {
	$a = scg_ar_settings();
	list($pname) = scg_dn_names();
	$want = array();
	foreach (array('down', 'up') as $d) {
		if ($a[$d]['pipe'] !== '') {
			$want[$a[$d]['pipe']] = true;
		}
	}
	foreach ($pname as $num => $p) {
		if (isset($want[$p['name']])) {
			$line = scg_ar_pipe_line($num);
			if ($line !== null) {
				exec('/sbin/dnctl ' . scg_ar_dnctl_args($line) . ' 2>&1');
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Dienst                                                              */
/* ------------------------------------------------------------------ */

function scg_ar_running() {
	$pid = intval(@file_get_contents(SCG_AR_PID));
	if ($pid <= 0) {
		return false;
	}
	exec('/bin/kill -0 ' . $pid . ' 2>/dev/null', $o, $rc);
	return $rc == 0;
}

function scg_ar_service($action) {
	if (!file_exists(SCG_AR_RC)) {
		return;
	}
	/* Ausgabe verwerfen: ein offener Ausgabekanal wuerde den Aufrufer blockieren */
	mwexec('/bin/sh ' . SCG_AR_RC . ' ' . escapeshellarg($action) . ' >/dev/null 2>&1 </dev/null');
}

/* Status, wenn der Dienst ihn in den letzten 5 s geschrieben hat */
function scg_ar_status() {
	if (!file_exists(SCG_AR_STATUS) || filemtime(SCG_AR_STATUS) < time() - 5) {
		return null;
	}
	$j = json_decode(@file_get_contents(SCG_AR_STATUS), true);
	return is_array($j) ? $j : null;
}

/* ------------------------------------------------------------------ */
/* Pinger: Raw-ICMP-Socket, Fallback auf /sbin/ping                    */
/* ------------------------------------------------------------------ */

class SCGPingerSocket {
	public $ok = false;
	public $err = '';
	public $mode = 'socket';
	private $sock;
	private $id;
	private $seq = 0;
	private $pending = array();

	function __construct() {
		if (!function_exists('socket_create')) {
			$this->err = 'PHP-Erweiterung sockets fehlt';
			return;
		}
		$this->sock = @socket_create(AF_INET, SOCK_RAW, 1);
		if ($this->sock === false) {
			$this->err = 'socket_create: ' . socket_strerror(socket_last_error());
			return;
		}
		socket_set_nonblock($this->sock);
		$this->id = getmypid() & 0xffff;
		$this->ok = true;
	}

	static function cksum($d) {
		if (strlen($d) % 2) {
			$d .= "\0";
		}
		$s = array_sum(unpack('n*', $d));
		while ($s >> 16) {
			$s = ($s & 0xffff) + ($s >> 16);
		}
		return ~$s & 0xffff;
	}

	function send($ip) {
		$this->seq = ($this->seq + 1) & 0xffff;
		$payload = pack('J', hrtime(true)) . str_repeat("\0", 8);
		$hdr = pack('CCnnn', 8, 0, 0, $this->id, $this->seq);
		$pkt = pack('CCnnn', 8, 0, self::cksum($hdr . $payload), $this->id, $this->seq) . $payload;
		$this->pending[$ip . '|' . $this->seq] = hrtime(true);
		@socket_sendto($this->sock, $pkt, strlen($pkt), 0, $ip, 0);
	}

	/* Wartet hoechstens $us Mikrosekunden; liefert array(array(ip, rtt_ms), ...) */
	function receive($us) {
		$out = array();
		$r = array($this->sock);
		$w = $e = null;
		$n = @socket_select($r, $w, $e, intdiv($us, 1000000), $us % 1000000);
		if (!$n) {
			return $out;
		}
		while (true) {
			$buf = '';
			$from = '';
			$port = 0;
			$len = @socket_recvfrom($this->sock, $buf, 2048, 0, $from, $port);
			if ($len === false || $len <= 0) {
				break;
			}
			$now = hrtime(true);
			$ihl = (ord($buf[0]) & 0x0f) * 4;
			if (strlen($buf) < $ihl + 8) {
				continue;
			}
			$h = unpack('Ctype/Ccode/ncks/nid/nseq', substr($buf, $ihl, 8));
			if ($h['type'] != 0 || $h['id'] != $this->id) {
				continue;
			}
			$k = $from . '|' . $h['seq'];
			if (isset($this->pending[$k])) {
				/* ip, rtt_ms, Sendezeitpunkt (ns) */
				$out[] = array($from, ($now - $this->pending[$k]) / 1e6, $this->pending[$k]);
				unset($this->pending[$k]);
			}
		}
		return $out;
	}

	/* Unbeantwortete Pings nach $ms als verloren werten */
	function expire($ms) {
		$lost = array();
		$lim = hrtime(true) - $ms * 1e6;
		foreach ($this->pending as $k => $t) {
			if ($t < $lim) {
				$lost[] = substr($k, 0, strpos($k, '|'));
				unset($this->pending[$k]);
			}
		}
		return $lost;
	}
}

class SCGPingerProc {
	public $ok = false;
	public $err = '';
	public $mode = 'ping';
	private $procs = array();

	function __construct($ips, $interval_ms) {
		$base = array('/sbin/ping', '-n', '-i', sprintf('%.2f', max(0.05, $interval_ms / 1000)));
		if (file_exists('/usr/bin/stdbuf')) {
			$base = array_merge(array('/usr/bin/stdbuf', '-oL'), $base);
		}
		foreach ($ips as $ip) {
			$p = @proc_open(array_merge($base, array($ip)), array(1 => array('pipe', 'w'), 2 => array('file', '/dev/null', 'w')), $pipes);
			if (is_resource($p)) {
				stream_set_blocking($pipes[1], false);
				$this->procs[$ip] = array($p, $pipes[1], '');
			}
		}
		$this->ok = count($this->procs) > 0;
		if (!$this->ok) {
			$this->err = 'ping konnte nicht gestartet werden';
		}
	}

	function send($ip) {
		/* ping sendet selbststaendig */
	}

	function receive($us) {
		$out = array();
		$r = array();
		foreach ($this->procs as $ip => $p) {
			$r[] = $p[1];
		}
		$w = $e = null;
		if (!$r || !@stream_select($r, $w, $e, intdiv($us, 1000000), $us % 1000000)) {
			return $out;
		}
		foreach ($this->procs as $ip => &$p) {
			$chunk = @fread($p[1], 65536);
			if ($chunk === false || $chunk === '') {
				continue;
			}
			$p[2] .= $chunk;
			while (($pos = strpos($p[2], "\n")) !== false) {
				$line = substr($p[2], 0, $pos);
				$p[2] = substr($p[2], $pos + 1);
				if (preg_match('/from ([\d.]+):.*time=([\d.]+) ms/', $line, $m)) {
					/* ping misst selbst; Verzoegerungen beim Auslesen verfaelschen nichts */
					$out[] = array($m[1], floatval($m[2]), null);
				}
			}
		}
		unset($p);
		return $out;
	}

	function expire($ms) {
		return array();
	}

	function close() {
		foreach ($this->procs as $p) {
			@proc_terminate($p[0]);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Regelung                                                            */
/* ------------------------------------------------------------------ */

class SCGAutorate {
	public $cfg;
	public $dirs = array();
	public $refl = array();
	public $window = array();
	public $winsize = 6;      /* letzte n RTT-Proben ... */
	public $winbloat = 3;     /* ... davon mindestens m ueber Schwelle = Stau */
	public $bloat = false;
	public $bloatSince = null;    /* seit wann der Stau ununterbrochen anhaelt */
	public $idle = false;         /* keine Richtung nennenswert genutzt (< 2 % bzw. < 0,5 Mbit/s) */
	public $idleBloatSince = null;/* seit wann Stau ohne eigene Last anhaelt */
	public $decRefractory = 1.0;  /* s zwischen zwei Absenkungen */
	public $incHold = 3.0;        /* s keine Anhebung nach einer Absenkung */
	public $lowLoadDelay = 2.0;   /* s anhaltender Stau, bevor bei geringer Last gesenkt wird */
	public $lowLoadShare = 0.02;  /* Mindestlast (Anteil am Limit), damit eine Richtung als genutzt gilt */
	public $lowLoadKbit = 500;    /* und mindestens 0,5 Mbit/s Durchsatz */
	public $resets = 0;
	public $discarded = 0;        /* wegen Verarbeitungspause verworfene Proben */
	public $source = array('kind' => 'limiter');   /* woher "erreicht" kommt */

	public $learnHold = 10.0;     /* s ohne Stau unter Last, bevor die gelernte Basis steigt */
	public $control = true;       /* false = nur messen, Limiter nie veraendern */
	public $paused = false;       /* Speedtest laeuft: nicht regeln, nichts lernen */
	/* Latenzverlauf: je Sekunde min/avg/max/Anzahl/Verluste ueber alle Ziele */
	public $hist = array();
	public $histLen = 120;
	/* Limit/erreicht je Richtung und Sekunde, damit Limiter Live den Verlauf beim Oeffnen sofort zeigt */
	public $rhist = array();
	public $rhistLen = 300;
	public $cur = null;           /* laufende Sekunde */
	public $samples = array();    /* letzte Rohproben array(t, rtt, delta) fuer Kennzahlen */

	/*
	 * $saved = Inhalt der Gedaechtnisdatei: array('refl' => ip => Grundlatenz,
	 * 'learned' => richtung => array('pipe' => Name, 'kbit' => Rate)).
	 * Aeltere Dateien enthalten nur ip => Grundlatenz.
	 */
	function __construct($cfg, $saved = array()) {
		$this->cfg = $cfg;
		$this->control = !empty($cfg['enable']);
		$baselines = isset($saved['refl']) && is_array($saved['refl']) ? $saved['refl'] : (isset($saved['learned']) ? array() : $saved);
		$learned = isset($saved['learned']) && is_array($saved['learned']) ? $saved['learned'] : array();
		foreach (array('down', 'up') as $d) {
			$c = $cfg[$d];
			if (($c['pipe'] ?? '') === '') {
				continue;
			}
			$this->dirs[$d] = array(
				'pipe' => $c['pipe'], 'num' => null,
				'min' => floatval($c['min']) * 1000, 'base' => floatval($c['base']) * 1000, 'max' => floatval($c['max']) * 1000,
				'shaper' => floatval($c['base']) * 1000, 'applied' => null,
				'achieved' => 0.0, 'lastBytes' => null, 'lastT' => null, 'load' => 0.0,
				'lastDec' => -1e9, 'state' => 'start', 'cleanSince' => null,
				'learned' => floatval($c['base']) * 1000,
			);
			/* Gelernte Basis vom letzten Lauf uebernehmen, wenn es derselbe Limiter ist */
			$x = &$this->dirs[$d];
			if (!empty($cfg['learnbase']) && isset($learned[$d]['pipe'], $learned[$d]['kbit']) &&
			    $learned[$d]['pipe'] === $c['pipe'] && is_numeric($learned[$d]['kbit'])) {
				$x['learned'] = max($x['min'], min($x['max'], floatval($learned[$d]['kbit'])));
				$x['shaper'] = $x['learned'];
			}
			unset($x);
		}
		foreach (preg_split('/[\s,]+/', trim($cfg['reflectors'])) as $ip) {
			if ($ip !== '') {
				/* Gemerkte Grundlatenz verhindert, dass ein Start waehrend Last den Stau als normal lernt */
				$b = isset($baselines[$ip]) && is_numeric($baselines[$ip]) ? floatval($baselines[$ip]) : null;
				$this->refl[$ip] = array('baseline' => $b, 'rtt' => null, 'delta' => null, 'last' => 0.0, 'lost' => 0);
			}
		}
	}

	/* Neue RTT-Probe */
	function onRtt($ip, $rtt, $now) {
		if (!isset($this->refl[$ip])) {
			return;
		}
		$thr = floatval($this->cfg['threshold']);
		$r = &$this->refl[$ip];
		if ($r['baseline'] === null) {
			$r['baseline'] = $rtt;
		}
		$delta = $rtt - $r['baseline'];
		if ($this->paused) {
			/* Speedtest: Latenz nur aufzeichnen, Grundlatenz und Stau-Fenster bleiben unberuehrt */
			$r['rtt'] = $rtt;
			$r['delta'] = $delta;
			$r['last'] = $now;
			$r['lost'] = 0;
			$this->record($now, $rtt, $delta, false);
			unset($r);
			return;
		}
		if ($delta < 0) {
			/* Grundlatenz faellt schnell nach */
			$r['baseline'] += 0.5 * $delta;
		} elseif ($delta < $thr) {
			/* ... steigt nur sehr langsam und nur ohne Stau */
			$r['baseline'] += 0.002 * $delta;
		} elseif ($this->idle && $this->idleBloatSince !== null && $now - $this->idleBloatSince >= 60) {
			/*
			 * Hohe Latenz seit ueber 60 s, obwohl wir kaum Verkehr haben: Das
			 * ist nicht unser Stau, oder die Grundlatenz hat sich dauerhaft
			 * veraendert (anderer Weg, anderer Anschluss). Sehr langsam
			 * nachziehen. Kurze Staus beim Provider bleiben so sichtbar.
			 */
			$r['baseline'] += 0.001 * $delta;
		}
		$r['rtt'] = $rtt;
		$r['delta'] = $rtt - $r['baseline'];
		$r['last'] = $now;
		$r['lost'] = 0;
		$this->record($now, $rtt, $r['delta'], false);
		$this->window[] = ($r['delta'] > $thr);
		if (count($this->window) > $this->winsize) {
			array_shift($this->window);
		}
		unset($r);
	}

	function onLost($ip, $now = null) {
		if (isset($this->refl[$ip])) {
			$this->refl[$ip]['lost']++;
			$this->record($now ?? microtime(true), null, null, true);
		}
	}

	/* Probe in den Sekundenverlauf eintragen */
	function record($now, $rtt, $delta, $lost) {
		$sec = (int)floor($now);
		if ($this->cur === null || $this->cur['t'] != $sec) {
			$this->flushSecond();
			$this->cur = array('t' => $sec, 'min' => null, 'max' => null, 'sum' => 0.0, 'n' => 0, 'lost' => 0);
		}
		if ($lost) {
			$this->cur['lost']++;
			$this->samples[] = array($now, null, null);
		} else {
			$c = &$this->cur;
			$c['min'] = ($c['min'] === null) ? $rtt : min($c['min'], $rtt);
			$c['max'] = ($c['max'] === null) ? $rtt : max($c['max'], $rtt);
			$c['sum'] += $rtt;
			$c['n']++;
			unset($c);
			$this->samples[] = array($now, $rtt, $delta);
		}
		/* Rohproben der letzten 10 s behalten */
		while ($this->samples && $this->samples[0][0] < $now - 10) {
			array_shift($this->samples);
		}
	}

	function flushSecond() {
		if ($this->cur === null) {
			return;
		}
		$c = $this->cur;
		$base = array();
		foreach ($this->refl as $r) {
			if ($r['baseline'] !== null) {
				$base[] = $r['baseline'];
			}
		}
		$this->hist[] = array($c['t'], $c['min'] === null ? null : round($c['min'], 1),
			$c['n'] ? round($c['sum'] / $c['n'], 1) : null, $c['max'] === null ? null : round($c['max'], 1),
			$c['n'], $c['lost'], $base ? round(array_sum($base) / count($base), 1) : null);
		while (count($this->hist) > $this->histLen) {
			array_shift($this->hist);
		}
	}

	/* Limit und erreichten Durchsatz je Sekunde festhalten (letzter Wert der Sekunde zaehlt) */
	function recordRates($now) {
		$sec = (int)floor($now);
		foreach ($this->dirs as $d => $x) {
			$e = array($sec, (int)round($x['shaper']), (int)round($x['achieved']));
			$n = count($this->rhist[$d] ?? array());
			if ($n && $this->rhist[$d][$n - 1][0] == $sec) {
				$this->rhist[$d][$n - 1] = $e;
			} else {
				$this->rhist[$d][] = $e;
			}
			while (count($this->rhist[$d]) > $this->rhistLen) {
				array_shift($this->rhist[$d]);
			}
		}
	}

	/* Kennzahlen der letzten 10 s: Mittel, 95-%-Wert, Jitter, Verlust */
	function stats() {
		$r = array();
		$lost = 0;
		$jit = 0.0;
		foreach ($this->samples as $s) {
			if ($s[1] === null) {
				$lost++;
				continue;
			}
			$r[] = $s[1];
		}
		/* Jitter als mittlere Abweichung vom Mittelwert ueber alle Ziele */
		$n = count($r);
		if ($n == 0) {
			return array('n' => 0, 'avg' => null, 'p95' => null, 'min' => null, 'max' => null, 'jitter' => null,
				'loss' => ($lost ? 100.0 : 0.0));
		}
		$avg = array_sum($r) / $n;
		foreach ($r as $v) {
			$jit += abs($v - $avg);
		}
		sort($r);
		return array('n' => $n, 'avg' => round($avg, 1), 'p95' => round($r[(int)floor(0.95 * ($n - 1))], 1),
			'min' => round($r[0], 1), 'max' => round($r[$n - 1], 1), 'jitter' => round($jit / $n, 1),
			'loss' => round(100.0 * $lost / ($n + $lost), 1));
	}

	/* Inhalt der Gedaechtnisdatei: Grundlatenzen und gelernte Basis */
	function memory() {
		$b = array();
		foreach ($this->refl as $ip => $r) {
			if ($r['baseline'] !== null) {
				$b[$ip] = round($r['baseline'], 2);
			}
		}
		$l = array();
		foreach ($this->dirs as $d => $x) {
			$l[$d] = array('pipe' => $x['pipe'], 'kbit' => round($x['learned']));
		}
		return array('refl' => $b, 'learned' => $l);
	}

	/* Ziel im Leerlauf: gelernte oder feste Basis */
	function idleTarget($x) {
		return !empty($this->cfg['learnbase']) ? $x['learned'] : $x['base'];
	}

	/* Zuordnung Limitername -> dummynet-Nummer */
	function resolve($pname) {
		foreach ($this->dirs as $d => &$x) {
			$x['num'] = null;
			foreach ($pname as $num => $p) {
				if ($p['name'] === $x['pipe']) {
					$x['num'] = $num;
				}
			}
		}
		unset($x);
	}

	/*
	 * Ein Regeltakt.
	 *  $pipes   = array(num => array('bw' => bit/s, 'bytes' => ...)) der Limiter
	 *  $ifbytes = array('down' => Bytes rein, 'up' => Bytes raus) am WAN-Interface
	 *             oder null; dann zaehlt der Durchsatz der Limiter.
	 * Liefert array(num => kbit) der zu setzenden Bandbreiten.
	 */
	function tick($now, $pipes, $ifbytes = null) {
		$apply = array();
		if ($this->paused) {
			/* Stau-Fenster leeren: nach dem Test beginnt die Bewertung frisch */
			$this->window = array();
			$this->bloatSince = null;
			$this->idleBloatSince = null;
		}
		$n = 0;
		foreach ($this->window as $b) {
			$n += $b ? 1 : 0;
		}
		$this->bloat = ($n >= $this->winbloat);
		if ($this->bloat) {
			if ($this->bloatSince === null) {
				$this->bloatSince = $now;
			}
		} else {
			$this->bloatSince = null;
		}
		$dec = floatval($this->cfg['decfactor']);
		$inc = floatval($this->cfg['incstep']) / 100;

		/* 1. Durchsatz und Last je Richtung */
		$active = array();
		foreach ($this->dirs as $d => &$x) {
			if ($x['num'] === null || !isset($pipes[$x['num']])) {
				$x['state'] = 'no_limiter';
				continue;
			}
			$p = $pipes[$x['num']];
			$bytes = ($ifbytes !== null && isset($ifbytes[$d])) ? $ifbytes[$d] : $p['bytes'];
			/* Erreichter Durchsatz (kbit/s), leicht geglaettet */
			if ($x['lastBytes'] !== null && $bytes >= $x['lastBytes'] && $now > $x['lastT']) {
				$rate = ($bytes - $x['lastBytes']) * 8 / 1000 / ($now - $x['lastT']);
				$x['achieved'] = 0.5 * $x['achieved'] + 0.5 * $rate;
			}
			$x['lastBytes'] = $bytes;
			$x['lastT'] = $now;

			/* Hat jemand (Filter Reload) die Bandbreite zurueckgesetzt? */
			$kernel = $p['bw'] / 1000;
			if ($x['applied'] !== null && abs($kernel - $x['applied']) > 0.02 * $x['applied']) {
				$this->resets++;
				$x['applied'] = null;
			}
			$x['load'] = $x['shaper'] > 0 ? $x['achieved'] / $x['shaper'] : 0;
			$active[] = $d;
		}
		unset($x);

		/*
		 * 2. Wer wird bei Stau gesenkt?
		 * Normal: jede Richtung mit mindestens 50 % Last.
		 * Liegt die Leitung weit unter dem Limit (z. B. Upload schafft nur
		 * 4 von 47 Mbit/s), erreicht keine Richtung 50 %. Haelt der Stau dann
		 * an, wird die Richtung mit der hoechsten Last gesenkt, sofern sie
		 * ueberhaupt genutzt wird: mindestens 2 % des Limits und 0,5 Mbit/s.
		 * Darunter gilt die Leitung als unbelastet (Stau kommt dann nicht von uns).
		 */
		$anyHigh = false;
		$pick = null;
		$best = -1;
		$this->idle = true;
		foreach ($active as $d) {
			$x = $this->dirs[$d];
			$l = $x['load'];
			if ($l >= 0.5) {
				$anyHigh = true;
			}
			$used = ($l >= $this->lowLoadShare && $x['achieved'] >= $this->lowLoadKbit);
			if ($used) {
				$this->idle = false;
				if ($l > $best) {
					$best = $l;
					$pick = $d;
				}
			}
		}
		$lowLoadCut = $this->bloat && !$anyHigh && $pick !== null && ($now - $this->bloatSince) >= $this->lowLoadDelay;
		/* Ohne Limiter (reine Messung ohne Richtungen) gilt die Leitung als unbelastet */
		if (!$active) {
			$this->idle = true;
		}
		if ($this->bloat && $this->idle) {
			if ($this->idleBloatSince === null) {
				$this->idleBloatSince = $now;
			}
		} else {
			$this->idleBloatSince = null;
		}

		/* Nur messen: Durchsatz und Last sind bestimmt, Limiter bleiben unveraendert */
		if (!$this->control || $this->paused) {
			foreach ($active as $d) {
				$this->dirs[$d]['state'] = $this->paused ? 'paused' : 'monitor';
				$this->dirs[$d]['cleanSince'] = null;
			}
			return $apply;
		}

		/* 3. Regeln */
		foreach ($active as $d) {
			$x = &$this->dirs[$d];
			$load = $x['load'];
			$new = $x['shaper'];
			$cut = $this->bloat && ($load >= 0.5 || ($lowLoadCut && $d === $pick));
			if ($cut) {
				if ($now - $x['lastDec'] >= $this->decRefractory) {
					$new = max($x['min'], min($x['shaper'], $x['achieved']) * $dec);
					$x['lastDec'] = $now;
					$x['state'] = ($load >= 0.5) ? 'decrease' : 'decrease_low';
					/* Stau: die gelernte Basis faellt sofort mit, der naechste Start beginnt darunter */
					$x['learned'] = min($x['learned'], $new);
				} else {
					$x['state'] = 'bloat_wait';
				}
				if ($new <= $x['min'] && $x['shaper'] <= $x['min']) {
					$x['state'] = 'bloat_min';
				}
			} elseif ($this->bloat) {
				$x['state'] = ($this->idle) ? 'bloat_idle' : 'bloat_lowload';
			} elseif ($load >= 0.75) {
				if ($now - $x['lastDec'] >= $this->incHold) {
					$new = min($x['max'], $x['shaper'] * (1 + $inc));
					$x['state'] = $new > $x['shaper'] ? 'increase' : 'max';
				} else {
					$x['state'] = 'hold';
				}
			} else {
				$new = $x['shaper'] + ($this->idleTarget($x) - $x['shaper']) * 0.05;
				$x['state'] = 'idle';
			}
			$x['shaper'] = max($x['min'], min($x['max'], $new));

			/*
			 * Basis lernen: Laeuft die Richtung ausgelastet und seit mindestens
			 * learnHold Sekunden ohne Stau, naehert sich die gelernte Basis dem
			 * aktuellen Limit an. So startet der naechste Upload/Download dort,
			 * wo die Leitung zuletzt sicher lief.
			 */
			if (!$this->bloat && $load >= 0.75) {
				if ($x['cleanSince'] === null) {
					$x['cleanSince'] = $now;
				}
				if ($now - $x['cleanSince'] >= $this->learnHold && $now - $x['lastDec'] >= $this->learnHold) {
					$x['learned'] += 0.05 * ($x['shaper'] - $x['learned']);
				}
			} else {
				$x['cleanSince'] = null;
			}
			$x['learned'] = max($x['min'], min($x['max'], $x['learned']));

			if ($x['applied'] === null || abs($x['shaper'] - $x['applied']) > 0.005 * $x['applied']) {
				$apply[$x['num']] = $x['shaper'];
				$x['applied'] = $x['shaper'];
			}
			unset($x);
		}
		return $apply;
	}

	function status($mode, $err = '') {
		$dirs = array();
		foreach ($this->dirs as $d => $x) {
			$dirs[$d] = array('pipe' => $x['pipe'], 'num' => $x['num'], 'shaper' => round($x['shaper']),
				'achieved' => round($x['achieved']), 'min' => $x['min'], 'base' => $x['base'], 'max' => $x['max'],
				'learned' => round($x['learned']), 'learnbase' => !empty($this->cfg['learnbase']),
				'state' => $x['state']);
		}
		$refl = array();
		foreach ($this->refl as $ip => $r) {
			$refl[$ip] = array('rtt' => $r['rtt'] === null ? null : round($r['rtt'], 1),
				'baseline' => $r['baseline'] === null ? null : round($r['baseline'], 1),
				'delta' => $r['delta'] === null ? null : round($r['delta'], 1), 'lost' => $r['lost']);
		}
		$n = 0;
		foreach ($this->window as $b) {
			$n += $b ? 1 : 0;
		}
		return array('t' => microtime(true), 'dirs' => $dirs, 'refl' => $refl, 'bloat' => $this->bloat,
			'window' => "{$n}/" . count($this->window), 'threshold' => floatval($this->cfg['threshold']),
			'mode' => $mode, 'err' => $err, 'resets' => $this->resets, 'discarded' => $this->discarded,
			'source' => $this->source, 'pid' => getmypid(), 'control' => $this->control, 'paused' => $this->paused,
			'hist' => $this->hist, 'rhist' => $this->rhist, 'stats' => $this->stats());
	}
}

/* ------------------------------------------------------------------ */
/* Hauptschleife des Dienstes                                          */
/* ------------------------------------------------------------------ */

function scg_ar_run() {
	$cfg = scg_ar_settings();
	if (empty($cfg['enable']) && empty($cfg['monitor'])) {
		return 0;
	}
	$saved = array();
	if (file_exists(SCG_AR_BASELINE) && filemtime(SCG_AR_BASELINE) > time() - 7 * 86400) {
		$saved = json_decode(@file_get_contents(SCG_AR_BASELINE), true) ?: array();
	}
	$ar = new SCGAutorate($cfg, $saved);
	$ips = array_keys($ar->refl);
	$wan = null;
	$next_baseline = microtime(true) + 60;
	$pinger = new SCGPingerSocket();
	if (!$pinger->ok) {
		$err = $pinger->err;
		$pinger = new SCGPingerProc($ips, intval($cfg['interval']));
		$pinger->err = $err;
	}
	syslog(LOG_NOTICE, "ShaperClassGen Autorate gestartet (Pinger: {$pinger->mode})");

	$interval = intval($cfg['interval']) / 1000;
	$tick = intval($cfg['tick']) / 1000;
	$next_ping = array();
	$i = 0;
	foreach ($ips as $ip) {
		/* Ziele zeitlich versetzt anpingen */
		$next_ping[$ip] = microtime(true) + $interval * $i++ / max(1, count($ips));
	}
	$next_tick = microtime(true) + $tick;
	$next_resolve = 0;
	$next_status = 0;
	$last_rx_end = null;
	$stall_end = null;
	$cfg_mtime = null;

	while (true) {
		$now = microtime(true);
		if ($now >= $next_resolve) {
			/*
			 * Konfiguration nur neu einlesen, wenn config.xml sich geaendert hat
			 * (Geruest-Aenderungen verschieben die Limiter-Nummern). Das Einlesen
			 * einer grossen Konfiguration kann ueber eine Sekunde dauern.
			 */
			global $g;
			$cfile = (isset($g['conf_path']) ? $g['conf_path'] : '/cf/conf') . '/config.xml';
			clearstatcache(true, $cfile);
			$mt = @filemtime($cfile);
			if ($mt !== false && $cfg_mtime !== null && $mt != $cfg_mtime && function_exists('parse_config')) {
				global $config;
				$fresh = @parse_config(true);
				if (is_array($fresh) && !empty($fresh)) {
					$config = $fresh;
				}
			}
			if ($mt !== false) {
				$cfg_mtime = $mt;
			}
			list($pname) = scg_dn_names();
			$ar->resolve($pname);
			$wan = scg_ar_wan($cfg);
			$ar->source = $wan ? array('kind' => 'iface', 'descr' => $wan[2], 'real' => $wan[1]) : array('kind' => 'limiter');
			$next_resolve = $now + 30;
		}
		/*
		 * Schutz vor Messfehlern: Die RTT wird beim Verarbeiten der Antwort
		 * gemessen. War der Dienst seit dem letzten Empfang laenger als 20 ms
		 * mit anderem beschaeftigt (dnctl, Konfiguration lesen ...), koennen
		 * Antworten im Socket gewartet haben. Betroffen sind nur Pings, die
		 * vor dem Ende dieser Pause gesendet wurden; die werden verworfen.
		 */
		$busy_end = hrtime(true);
		if ($last_rx_end !== null && $busy_end - $last_rx_end > 20e6) {
			$stall_end = $busy_end;
		}
		foreach ($next_ping as $ip => $t) {
			if ($now >= $t) {
				$pinger->send($ip);
				$next_ping[$ip] = max($t + $interval, $now);
			}
		}
		$wake = min(min($next_ping), $next_tick);
		$wait = max(0, (int)(($wake - microtime(true)) * 1e6));
		foreach ($pinger->receive($wait) as $res) {
			if ($res[2] !== null && $stall_end !== null && $res[2] < $stall_end) {
				$ar->discarded++;
				continue;
			}
			$ar->onRtt($res[0], $res[1], microtime(true));
		}
		$last_rx_end = hrtime(true);
		foreach ($pinger->expire(2000) as $ip) {
			$ar->onLost($ip, microtime(true));
		}
		$now = microtime(true);
		if ($now >= $next_tick) {
			/* Speedtest-Pause: Datei mit Ablaufzeit, verwaiste Dateien laufen von selbst ab */
			clearstatcache(true, SCG_AR_PAUSE);
			$pz = file_exists(SCG_AR_PAUSE) ? intval(@file_get_contents(SCG_AR_PAUSE)) : 0;
			$ar->paused = ($pz > time());
			$out = array();
			exec('/sbin/dnctl sched show 2>/dev/null', $out);
			$sched = scg_dn_parse_pipes($out);
			$out = array();
			exec('/sbin/dnctl pipe show 2>/dev/null', $out);
			$pipe = scg_dn_parse_pipes($out);
			$pipes = array();
			foreach ($ar->dirs as $x) {
				$n = $x['num'];
				if ($n === null) {
					continue;
				}
				$s = $sched[$n] ?? null;
				$p = $pipe[$n] ?? null;
				$src = ($s && $s['lines'] > 0) ? $s : $p;
				if ($src) {
					$pipes[$n] = array('bw' => ($s['bw'] ?? $p['bw'] ?? 0), 'bytes' => $src['bytes']);
				}
			}
			$ifbytes = null;
			if ($wan) {
				$st = scg_if_bytes($wan[1]);
				if ($st !== null) {
					$ifbytes = array('down' => $st['in'], 'up' => $st['out']);
				}
			}
			foreach ($ar->tick($now, $pipes, $ifbytes) as $num => $kbit) {
				scg_ar_set_bw($num, $kbit);
			}
			$ar->recordRates($now);
			$next_tick += $tick;
			if ($next_tick < $now) {
				$next_tick = $now + $tick;
			}
		}
		if ($now >= $next_baseline) {
			@file_put_contents(SCG_AR_BASELINE, json_encode($ar->memory()));
			$next_baseline = $now + 60;
		}
		if ($now >= $next_status) {
			@file_put_contents(SCG_AR_STATUS . '.tmp', json_encode($ar->status($pinger->mode, $pinger->err)));
			@rename(SCG_AR_STATUS . '.tmp', SCG_AR_STATUS);
			$next_status = $now + 1;
		}
	}
}
