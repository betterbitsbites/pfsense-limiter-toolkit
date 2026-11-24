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
define('SCG_AR_PAUSE', '/var/run/scg_autorate.pause');
define('SCG_AR_LONG', '/var/db/scg_autorate_history.jsonl');   /* Minutenwerte fuer den Langzeitgraphen */
define('SCG_AR_LONG_HOURS', 48);
/* Exklusiver Speedtest: urspruengliche Limiter-Bandbreiten waehrend des Tests */
define('SCG_ST_SQUEEZE', '/var/run/scg_speedtest_squeeze.json');
define('SCG_ST_SQUEEZE_KBIT', 8);   /* Speedtest: Regelung pausiert bis Zeitstempel */
define('SCG_AR_LAT_MS', 200);       /* Latenz je Interface: ms zwischen zwei Pings eines Interfaces (5/s) */
define('SCG_AR_LAT_ROTATE', 300);   /* s: neue ICMP-ID je Interface, damit ein alter State ohne Queue nicht bleibt */
/* ICMP-Timestamp-Reflektoren (Liste: github.com/tievolu/timestamp-reflectors, wird nicht mitgeliefert) */
define('SCG_AR_TS_URL', 'https://raw.githubusercontent.com/tievolu/timestamp-reflectors/main/reflectors.csv');
define('SCG_AR_TS_TRACE', 'https://speed.cloudflare.com/cdn-cgi/trace');
define('SCG_AR_CURL', '/usr/local/bin/curl');
define('SCG_AR_TS_CAND', '/var/db/scg_ts_candidates.json');   /* gefilterte Kandidaten des Landes */
define('SCG_AR_TS_SEL', '/var/db/scg_ts_reflectors.json');    /* Auswahl: gewaehlte Reflektoren und Ersatz */
define('SCG_AR_TS_LOCK', '/var/run/scg_ts_select.lock');
define('SCG_AR_TS_CAND_AGE', 7 * 86400);   /* Liste woechentlich neu laden */
define('SCG_AR_TS_SEL_AGE', 86400);        /* Auswahl taeglich neu testen */
define('SCG_AR_TS_COUNT', 4);              /* so viele Reflektoren gleichzeitig */
define('SCG_AR_TS_PROBE', 100);            /* so viele Kandidaten je Auswahl testen */
define('SCG_AR_TS_DAY', 86400000);         /* ms je Tag: Timestamps laufen um Mitternacht UTC ueber */
define('SCG_AR_TIMEOUT_MS', 4000);  /* Ping ohne Antwort nach dieser Zeit = Verlust (Upload-Stau erreicht ueber 2 s) */
define('SCG_AR_DIR_HOLD', 10);      /* s: letzte bekannte Stau-Richtung gilt weiter, wenn Timestamp-Antworten fehlen */

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
		'pingmode' => 'echo',   /* 'echo' = RTT, 'ts' = ICMP-Timestamp: Verzoegerung je Richtung */
		'ts_country' => '',     /* Land der Reflektoren (ISO, 2 Buchstaben), '' = automatisch */
		'ts_manual' => '',      /* feste Reflektoren statt automatischer Auswahl */
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
	if (!in_array($a['pingmode'] ?? 'echo', array('echo', 'ts'), true)) {
		$errors[] = scg_t('Unknown ping type.');
	}
	if (($a['ts_country'] ?? '') !== '' && !preg_match('/^[A-Za-z]{2}$/', $a['ts_country'])) {
		$errors[] = scg_t('Country must be a two-letter code (e.g. NL) or empty.');
	}
	foreach (scg_ar_iplist($a['ts_manual'] ?? '') as $r) {
		if (!filter_var($r, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
			$errors[] = scg_t('Reflector "%s" is not an IPv4 address.', $r);
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

/* Liste von IP-Adressen aus einem Eingabefeld */
function scg_ar_iplist($str) {
	$out = array();
	foreach (preg_split('/[\s,;]+/', trim((string)$str)) as $ip) {
		if ($ip !== '') {
			$out[] = $ip;
		}
	}
	return array_values(array_unique($out));
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
		'bloat_other' => 'congestion in the other direction',
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

/*
 * Exklusiver Speedtest: alle Limiter auf wenige kbit/s drosseln, damit der
 * Test die Leitung fuer sich hat. Die laufenden Bandbreiten werden vorher
 * gesichert. Wirkt sofort auch auf bestehende Verbindungen; Verkehr, der
 * nicht durch Limiter laeuft, ist nicht betroffen.
 */
function scg_st_squeeze($until) {
	$o = array();
	exec('/sbin/dnctl pipe show 2>/dev/null', $o);
	$save = array();
	foreach (scg_dn_parse_pipes($o) as $num => $p) {
		if ($p['bw'] > 0) {
			$save[$num] = (int)round($p['bw'] / 1000);
		}
	}
	if (!$save) {
		return 0;
	}
	@file_put_contents(SCG_ST_SQUEEZE, json_encode(array('until' => (int)$until, 'pipes' => $save)));
	foreach ($save as $num => $kbit) {
		scg_ar_set_bw($num, SCG_ST_SQUEEZE_KBIT);
	}
	return count($save);
}

/* Gedrosselte Limiter zuruecksetzen (nach dem Test, beim Abbruch oder durch den Dienst nach Ablauf) */
function scg_st_unsqueeze() {
	$j = json_decode((string)@file_get_contents(SCG_ST_SQUEEZE), true);
	if (!is_array($j)) {
		return;
	}
	foreach ((array)($j['pipes'] ?? array()) as $num => $kbit) {
		scg_ar_set_bw((int)$num, (int)$kbit);
	}
	@unlink(SCG_ST_SQUEEZE);
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

/*
 * Kanaele: 0 = Autorate-Pings (ohne Markierung, an keiner Queue vorbei),
 * 1..n = Latenz je Interface mit eigenem TOS-Wert. Jeder Kanal hat eine
 * eigene ICMP-ID: pf ordnet ICMP-States nach ID zu, mit gleicher ID wuerden
 * die markierten Pings im State der unmarkierten landen (ohne Queue).
 */
/* Jetzt in ms seit Mitternacht UTC (mit Nachkommastellen) */
function scg_ts_now() {
	return fmod(microtime(true) * 1000, SCG_AR_TS_DAY);
}

/* Differenz a - b zweier Tageszeiten in ms, Ueberlauf um Mitternacht beruecksichtigt */
function scg_ts_diff($a, $b) {
	$d = fmod($a - $b, SCG_AR_TS_DAY);
	if ($d > SCG_AR_TS_DAY / 2) {
		$d -= SCG_AR_TS_DAY;
	} elseif ($d <= -SCG_AR_TS_DAY / 2) {
		$d += SCG_AR_TS_DAY;
	}
	return $d;
}

/* Einweg-Zeiten aus array(t1, t2, t3, t4): array(hin, zurueck) inkl. unbekanntem Uhrenversatz */
function scg_ts_owd($ts) {
	return array(scg_ts_diff($ts[1], $ts[0]), scg_ts_diff($ts[3], $ts[2]));
}

class SCGPingerSocket {
	public $ok = false;
	public $err = '';
	public $mode = 'socket';
	private $sock;
	private $id;
	private $seq = 0;
	private $pending = array();
	private $gen = array();       /* Kanal => Generation (neue ICMP-ID) */
	private $tos = 0;             /* aktuell am Socket gesetzter TOS-Wert */
	private $expired = array();   /* als verloren gewertete Pings der letzten 10 s: Schluessel => Kanal */
	public $late = array(0, 0);   /* Antworten nach Ablauf (als verloren gezaehlt): Kanal 0 / Kanaele 1..n */
	/* Zaehler je Ziel: ip => array(Art => array(gesendet, beantwortet, verspaetet)); Art 0 = Autorate, 1 = Interface-Proben */
	public $count = array();

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
		/*
		 * Grosser Empfangspuffer: Der Raw-Socket bekommt jede ICMP-Antwort des
		 * Systems. Haengt der Dienst kurz (dnctl, Konfiguration lesen), liefe
		 * ein kleiner Puffer ueber und Antworten gingen als "Verlust" verloren.
		 */
		@socket_set_option($this->sock, SOL_SOCKET, SO_RCVBUF, 1048576);
		$this->id = getmypid() & 0xffff;
		$this->ok = true;
	}

	/* ICMP-ID je Kanal und Generation; Kanal 0 behaelt immer dieselbe ID */
	function icmpId($ch) {
		return ($this->id + $ch + 64 * ($this->gen[$ch] ?? 0)) & 0xffff;
	}

	/*
	 * Neue Kanalliste (Kanal => TOS). Alle Kanaele bekommen eine neue ICMP-ID,
	 * damit pf neue States anlegt, die die aktuellen Regeln durchlaufen.
	 */
	function channels($tos, $ips, $interval_ms) {
		foreach ($this->pending as $k => $p) {
			if ($p[1] > 0) {
				unset($this->pending[$k]);
			}
		}
		foreach (array_keys($tos) as $ch) {
			$this->gen[$ch] = ($this->gen[$ch] ?? 0) + 1;
		}
	}

	private function setTos($tos) {
		if ($this->tos === $tos) {
			return;
		}
		$lvl = defined('IPPROTO_IP') ? IPPROTO_IP : 0;
		$opt = defined('IP_TOS') ? IP_TOS : (PHP_OS_FAMILY === 'Linux' ? 1 : 3);
		if (@socket_set_option($this->sock, $lvl, $opt, $tos)) {
			$this->tos = $tos;
		}
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

	function send($ip, $ch = 0, $tos = 0) {
		$this->setTos($tos);
		if ($this->tos !== $tos && $ch > 0) {
			/* TOS laesst sich nicht setzen: unmarkiert senden wuerde an der Queue vorbei messen */
			return;
		}
		$id = $this->icmpId($ch);
		$this->seq = ($this->seq + 1) & 0xffff;
		$payload = pack('J', hrtime(true)) . str_repeat("\0", 8);
		$hdr = pack('CCnnn', 8, 0, 0, $id, $this->seq);
		$pkt = pack('CCnnn', 8, 0, self::cksum($hdr . $payload), $id, $this->seq) . $payload;
		$this->pending[$id . '|' . $ip . '|' . $this->seq] = array(hrtime(true), $ch, $ip);
		$this->tally($ip, $ch, 0);
		@socket_sendto($this->sock, $pkt, strlen($pkt), 0, $ip, 0);
	}

	/*
	 * ICMP-Timestamp-Anfrage (Typ 13, nur Kanal 0). Die Antwort (Typ 14) traegt
	 * die Empfangs- und Sendezeit des Reflektors: daraus Verzoegerung je Richtung.
	 */
	function sendTs($ip) {
		$this->setTos(0);
		$id = $this->icmpId(0);
		$this->seq = ($this->seq + 1) & 0xffff;
		$t1 = scg_ts_now();
		$body = pack('NNN', ((int)floor($t1)) % SCG_AR_TS_DAY, 0, 0);
		$hdr = pack('CCnnn', 13, 0, 0, $id, $this->seq);
		$pkt = pack('CCnnn', 13, 0, self::cksum($hdr . $body), $id, $this->seq) . $body;
		$this->pending[$id . '|' . $ip . '|' . $this->seq] = array(hrtime(true), 0, $ip, $t1);
		$this->tally($ip, 0, 0);
		@socket_sendto($this->sock, $pkt, strlen($pkt), 0, $ip, 0);
	}

	/*
	 * Wartet hoechstens $us Mikrosekunden; liefert array(array(ip, rtt_ms, Sendezeit ns, Kanal, ts), ...).
	 * ts = array(t1, t2, t3, t4) in ms seit Mitternacht UTC bei Timestamp-Antworten, sonst null;
	 * null auch, wenn der Reflektor keine Standardzeit liefert (hoechstes Bit gesetzt).
	 */
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
			if ($h['type'] != 0 && $h['type'] != 14) {
				continue;
			}
			$k = $h['id'] . '|' . $from . '|' . $h['seq'];
			if (!isset($this->pending[$k]) && isset($this->expired[$k])) {
				/* Antwort kam, aber erst nach Ablauf: war als Verlust gezaehlt */
				$this->late[$this->expired[$k][1] > 0 ? 1 : 0]++;
				$this->tally($from, $this->expired[$k][1], 2);
				unset($this->expired[$k]);
				continue;
			}
			if (isset($this->pending[$k])) {
				/* ip, rtt_ms, Sendezeitpunkt (ns), Kanal, Timestamps */
				$p = $this->pending[$k];
				$this->tally($from, $p[1], 1);
				$ts = null;
				if ($h['type'] == 14 && isset($p[3]) && strlen($buf) >= $ihl + 20) {
					$t = unpack('Norig/Nrecv/Nxmit', substr($buf, $ihl + 8, 12));
					if ($t['recv'] < SCG_AR_TS_DAY && $t['xmit'] < SCG_AR_TS_DAY) {
						$ts = array($p[3], (float)$t['recv'], (float)$t['xmit'], $p[3] + ($now - $p[0]) / 1e6);
					}
				}
				$out[] = array($from, ($now - $p[0]) / 1e6, $p[0], $p[1], $ts);
				unset($this->pending[$k]);
			}
		}
		return $out;
	}

	private function tally($ip, $ch, $what) {
		$kind = $ch > 0 ? 1 : 0;
		if (!isset($this->count[$ip][$kind])) {
			$this->count[$ip][$kind] = array(0, 0, 0);
		}
		$this->count[$ip][$kind][$what]++;
	}

	/* Unbeantwortete Pings nach $ms als verloren werten: array(array(ip, Kanal), ...) */
	function expire($ms) {
		$lost = array();
		$now = hrtime(true);
		$lim = $now - $ms * 1e6;
		foreach ($this->pending as $k => $p) {
			if ($p[0] < $lim) {
				$lost[] = array($p[2], $p[1]);
				$this->expired[$k] = array($now, $p[1]);
				unset($this->pending[$k]);
			}
		}
		foreach ($this->expired as $k => $e) {
			if ($e[0] < $now - 10e9) {
				unset($this->expired[$k]);
			}
		}
		return $lost;
	}
}

class SCGPingerProc {
	public $ok = false;
	public $err = '';
	public $mode = 'ping';
	private $procs = array();     /* "Kanal|ip" => array(Prozess, Pipe, Puffer, Kanal) */

	function __construct($ips, $interval_ms) {
		foreach ($ips as $ip) {
			$this->spawn(0, $ip, 0, $interval_ms);
		}
		$this->ok = count($this->procs) > 0;
		if (!$this->ok) {
			$this->err = 'ping konnte nicht gestartet werden';
		}
	}

	private function spawn($ch, $ip, $tos, $interval_ms) {
		$base = array('/sbin/ping', '-n', '-i', sprintf('%.2f', max(0.05, $interval_ms / 1000)));
		if ($tos) {
			$base[] = '-z';
			$base[] = (string)$tos;
		}
		if (file_exists('/usr/bin/stdbuf')) {
			$base = array_merge(array('/usr/bin/stdbuf', '-oL'), $base);
		}
		$p = @proc_open(array_merge($base, array($ip)), array(1 => array('pipe', 'w'), 2 => array('file', '/dev/null', 'w')), $pipes);
		if (is_resource($p)) {
			stream_set_blocking($pipes[1], false);
			$this->procs[$ch . '|' . $ip] = array($p, $pipes[1], '', $ch);
		}
	}

	/* Kanaele 1..n neu starten (neuer Prozess = neue ICMP-ID); je Ziel ein ping, zusammen 1 Ping je $interval_ms */
	function channels($tos, $ips, $interval_ms) {
		foreach ($this->procs as $k => $p) {
			if ($p[3] > 0) {
				@proc_terminate($p[0]);
				unset($this->procs[$k]);
			}
		}
		foreach ($tos as $ch => $t) {
			foreach ($ips as $ip) {
				$this->spawn($ch, $ip, $t, $interval_ms * max(1, count($ips)));
			}
		}
	}

	function send($ip, $ch = 0, $tos = 0) {
		/* ping sendet selbststaendig */
	}

	function sendTs($ip) {
		/* ohne Raw-Socket keine Timestamps; der Dienst nutzt dann Echo */
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
					$out[] = array($m[1], floatval($m[2]), null, $p[3]);
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
	public $late = array(0, 0);   /* verspaetete Antworten (vom Pinger): Kanal 0 / Interface-Kanaele */
	public $lastReset = null;     /* letzte Abweichung Kernel <-> gesetzter Wert (Diagnose) */
	/*
	 * ICMP-Timestamp: Reflektoren mit Rolle 'ctl' regeln, Echo-Ziele haben dann
	 * die Rolle 'ref' (Ausweichziel und Referenz fuer die Latenz je Interface).
	 * bloatDir = array('up' => bool, 'down' => bool), wenn der Stau einer
	 * Richtung zugeordnet werden konnte, sonst null (dann gelten die Lastregeln).
	 */
	public $tsmode = false;
	public $echoIps = array();
	public $bloatDir = null;
	public $refSamples = array();   /* Echo-Referenz im Timestamp-Betrieb: array(t, rtt) */
	public $owdRecent = array();    /* Anstieg je Richtung der letzten 2 s: array(t, ip, hin, zurueck) */
	public $owdMed = null;          /* Median ueber die Reflektoren im letzten Takt: array(hin, zurueck) */
	public $lastDir = null;         /* letzte aus Timestamps bestimmte Richtung: array(t, array('up','down')) */
	public $dirHeld = false;        /* bloatDir stammt aus lastDir (Antworten fehlten) */
	public $cuts = array();         /* letzte Absenkungen (Diagnose) */
	public $pcount = array();       /* Zaehler je Ziel vom Pinger */
	public $tsInfo = array();       /* Herkunft der Reflektoren fuer die Anzeige */
	private $savedBase = array();
	private $savedUp = array();
	public $discarded = 0;        /* wegen Verarbeitungspause verworfene Proben */
	public $source = array('kind' => 'limiter');   /* woher "erreicht" kommt */

	public $learnHold = 10.0;     /* s ohne Stau unter Last, bevor die gelernte Basis steigt */
	public $control = true;       /* false = nur messen, Limiter nie veraendern */
	public $paused = false;       /* Speedtest laeuft: nicht regeln, nichts lernen */
	/* Latenzverlauf: je Sekunde min/avg/max/Anzahl/Verluste ueber alle Ziele */
	public $hist = array();
	public $histLen = 300;        /* Sekunden Latenzverlauf (kombinierter Graph: 5 min) */
	/* Limit/erreicht je Richtung und Sekunde, damit Limiter Live den Verlauf beim Oeffnen sofort zeigt */
	public $rhist = array();
	public $rhistLen = 300;
	public $cur = null;           /* laufende Sekunde */
	public $samples = array();    /* letzte Rohproben array(t, rtt, delta) fuer Kennzahlen */
	/* Latenz je Interface (durch die gewaehlte Queue des Interfaces): Tag => Kanal */
	public $lat = array();
	public $latLen = 120;         /* Sekunden Verlauf je Interface */

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
		$this->tsmode = (($cfg['pingmode'] ?? 'echo') === 'ts');
		$this->savedBase = is_array($baselines) ? $baselines : array();
		$this->savedUp = isset($saved['tsup']) && is_array($saved['tsup']) ? $saved['tsup'] : array();
		$this->echoIps = scg_ar_iplist($cfg['reflectors']);
		foreach ($this->echoIps as $ip) {
			$this->refl[$ip] = $this->newRefl($ip, $this->tsmode ? 'ref' : 'ctl', false);
		}
	}

	private function newRefl($ip, $role, $ts) {
		/* Gemerkte Grundlatenz verhindert, dass ein Start waehrend Last den Stau als normal lernt */
		$b = isset($this->savedBase[$ip]) && is_numeric($this->savedBase[$ip]) ? floatval($this->savedBase[$ip]) : null;
		$u = ($ts && isset($this->savedUp[$ip]) && is_numeric($this->savedUp[$ip])) ? floatval($this->savedUp[$ip]) : null;
		return array('baseline' => $b, 'rtt' => null, 'delta' => null, 'last' => 0.0, 'lost' => 0,
			'role' => $role, 'ts' => $ts, 'upbase' => $u, 'dup' => null, 'ddown' => null, 'tslast' => 0.0);
	}

	/*
	 * Timestamp-Reflektoren setzen (Rolle 'ctl'). Bekannte behalten ihre
	 * Grundlatenzen; Echo-Ziele bleiben als Rolle 'ref'.
	 */
	function setTsReflectors($ips) {
		$new = array();
		foreach ($ips as $ip) {
			$old = $this->refl[$ip] ?? null;
			if ($old && $old['role'] === 'ctl' && $old['ts']) {
				$new[$ip] = $old;
			} else {
				$new[$ip] = $this->newRefl($ip, 'ctl', true);
			}
		}
		foreach ($this->echoIps as $ip) {
			if (!isset($new[$ip])) {
				$new[$ip] = $this->refl[$ip] ?? $this->newRefl($ip, 'ref', false);
				$new[$ip]['role'] = $this->tsmode ? 'ref' : 'ctl';
			}
		}
		foreach ($this->refl as $ip => $r) {
			if ($r['baseline'] !== null) {
				$this->savedBase[$ip] = $r['baseline'];
			}
			if ($r['upbase'] !== null) {
				$this->savedUp[$ip] = $r['upbase'];
			}
		}
		$this->refl = $new;
	}

	/*
	 * Median der Richtungs-Anstiege ueber die Reflektoren: je Reflektor das
	 * Mittel der letzten Sekunde. Braucht mindestens 2 Reflektoren (oder alle,
	 * wenn nur einer eingetragen ist). Liefert array(hin, zurueck) oder null.
	 */
	function owdMedian($now) {
		$per = array();
		foreach ($this->owdRecent as $o) {
			if ($o[0] >= $now - 1.0) {
				$per[$o[1]][] = array($o[2], $o[3]);
			}
		}
		$nts = 0;
		foreach ($this->refl as $r) {
			$nts += ($r['role'] === 'ctl' && $r['ts']) ? 1 : 0;
		}
		if (count($per) < min(2, max(1, $nts))) {
			return null;
		}
		$us = $ds = array();
		foreach ($per as $list) {
			$us[] = array_sum(array_column($list, 0)) / count($list);
			$ds[] = array_sum(array_column($list, 1)) / count($list);
		}
		$med = function ($v) {
			sort($v);
			$n = count($v);
			return ($n % 2) ? $v[intdiv($n, 2)] : ($v[$n / 2 - 1] + $v[$n / 2]) / 2;
		};
		return array($med($us), $med($ds));
	}

	/* Liefert mindestens ein Timestamp-Reflektor Antworten (letzte 5 s)? */
	function tsActive($now) {
		foreach ($this->refl as $r) {
			if ($r['role'] === 'ctl' && $r['ts'] && $r['last'] >= $now - 5) {
				return true;
			}
		}
		return false;
	}

	/* Reflektoren, die seit $secs keine Antwort mehr geschickt haben (nur Rolle ctl, Timestamp) */
	function deadTs($now, $secs = 30) {
		$out = array();
		foreach ($this->refl as $ip => $r) {
			if ($r['role'] === 'ctl' && $r['ts'] && $r['lost'] >= 10 && $r['last'] < $now - $secs) {
				$out[] = $ip;
			}
		}
		return $out;
	}

	/*
	 * Neue RTT-Probe; $ts = array(t1, t2, t3, t4) bei einer Timestamp-Antwort.
	 * Die Einweg-Zeiten enthalten einen unbekannten, festen Uhrenversatz. Er
	 * faellt heraus, weil nur der Anstieg ueber die Grundwerte zaehlt:
	 * Anstieg Upload = hin - Grundwert hin, Anstieg Download = RTT-Anstieg - Anstieg Upload.
	 */
	function onRtt($ip, $rtt, $now, $ts = null) {
		if (!isset($this->refl[$ip])) {
			return;
		}
		$thr = floatval($this->cfg['threshold']);
		$r = &$this->refl[$ip];
		if ($r['baseline'] === null) {
			$r['baseline'] = $rtt;
		}
		$delta = $rtt - $r['baseline'];
		/* Echo-Ziel im Timestamp-Betrieb: nur Referenz, solange Reflektoren antworten */
		$ctl = ($r['role'] === 'ctl') || !$this->tsActive($now);
		if ($r['role'] === 'ref') {
			$this->refSamples[] = array($now, $rtt);
			while ($this->refSamples && $this->refSamples[0][0] < $now - 10) {
				array_shift($this->refSamples);
			}
		}
		if ($this->paused) {
			/* Speedtest: Latenz nur aufzeichnen, Grundlatenz und Stau-Fenster bleiben unberuehrt */
			$r['rtt'] = $rtt;
			$r['delta'] = $delta;
			$r['last'] = $now;
			$r['lost'] = 0;
			if ($ctl) {
				$this->record($now, $rtt, $delta, false);
			}
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
		if (!$ctl) {
			unset($r);
			return;
		}
		$dr = $r['delta'];
		$du = $dd = null;
		if ($ts !== null && $r['ts']) {
			list($up) = scg_ts_owd($ts);
			/* Erster Wert oder Uhrsprung des Reflektors (bei freier Leitung): neu aufsetzen */
			if ($r['upbase'] === null || (abs($up - $r['upbase']) > 2000 && $dr < $thr / 2)) {
				$r['upbase'] = $up;
			}
			$x = $up - $r['upbase'];
			if ($dr < $thr / 2) {
				/* Freie Leitung: Aenderungen der Aufteilung sind Uhrendrift, nachziehen */
				$r['upbase'] += ($x < 0 ? 0.1 : 0.02) * $x;
			}
			$du = $up - $r['upbase'];
			$dd = $dr - $du;
			$r['dup'] = $du;
			$r['ddown'] = $dd;
			$r['tslast'] = $now;
			$this->owdRecent[] = array($now, $ip, $du, $dd);
			while ($this->owdRecent && $this->owdRecent[0][0] < $now - 2) {
				array_shift($this->owdRecent);
			}
		}
		$this->record($now, $rtt, $dr, false, $du, $dd);
		/* Fenster: array(Stau, Upload betroffen, Download betroffen, Richtung bekannt) */
		$bl = ($dr > $thr);
		$e = array($bl, false, false, false);
		if ($bl && $du !== null) {
			/* Richtung betroffen: eigener Anstieg mindestens halbe Schwelle und mindestens ein Viertel des Ganzen */
			$share = max(0.0, min($dr, $du)) / $dr;
			$e = array(true, $du >= $thr / 2 && $share >= 0.25, $dd >= $thr / 2 && $share <= 0.75, true);
		}
		$this->window[] = $e;
		if (count($this->window) > $this->winsize) {
			array_shift($this->window);
		}
		unset($r);
	}

	function onLost($ip, $now = null) {
		if (isset($this->refl[$ip])) {
			$now = $now ?? microtime(true);
			$this->refl[$ip]['lost']++;
			if ($this->refl[$ip]['role'] === 'ref') {
				$this->refSamples[] = array($now, null);
			}
			if ($this->refl[$ip]['role'] === 'ctl' || !$this->tsActive($now)) {
				$this->record($now, null, null, true);
			}
		}
	}

	/* Probe in den Sekundenverlauf eintragen */
	function record($now, $rtt, $delta, $lost, $dup = null, $ddown = null) {
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
			$this->samples[] = array($now, $rtt, $delta, $dup, $ddown);
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
		$tsa = $this->tsActive((float)$c['t'] + 1);
		foreach ($this->refl as $r) {
			/* Grundlatenz der Ziele, die gerade den Verlauf bilden */
			if ($r['baseline'] !== null && ($r['role'] === 'ctl') === ($tsa || !$this->tsmode)) {
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

	/*
	 * Minutenwert fuer den Langzeitgraphen aus den Sekundenverlaeufen:
	 * l = [min, Mittel, max, P95, verloren, Proben, Grundlatenz] in ms,
	 * d/u = [Limit Mittel, erreicht Mittel, erreicht max] in kbit/s, mx = Autorate-Maximum.
	 */
	function minuteRecord($m0) {
		$m1 = $m0 + 60;
		$avgs = array();
		$mn = null;
		$mx = null;
		$lost = 0;
		$n = 0;
		$base = null;
		foreach ($this->hist as $r) {
			if ($r[0] < $m0 || $r[0] >= $m1) {
				continue;
			}
			$lost += $r[5];
			$n += $r[4];
			if ($r[2] !== null) {
				$avgs[] = $r[2];
				$mn = ($mn === null) ? $r[1] : min($mn, $r[1]);
				$mx = ($mx === null) ? $r[3] : max($mx, $r[3]);
			}
			if ($r[6] !== null) {
				$base = $r[6];
			}
		}
		$rec = array('t' => $m0);
		if ($avgs || $lost) {
			$p95 = null;
			if ($avgs) {
				$sv = $avgs;
				sort($sv);
				$p95 = $sv[(int)floor(0.95 * (count($sv) - 1))];
			}
			$rec['l'] = array($mn, $avgs ? round(array_sum($avgs) / count($avgs), 1) : null, $mx, $p95, $lost, $n, $base);
		}
		foreach ($this->dirs as $d => $x) {
			$lim = $ach = array();
			foreach ($this->rhist[$d] ?? array() as $r) {
				if ($r[0] >= $m0 && $r[0] < $m1) {
					$lim[] = $r[1];
					$ach[] = $r[2];
				}
			}
			if ($lim) {
				$rec[$d == 'down' ? 'd' : 'u'] = array((int)round(array_sum($lim) / count($lim)),
					(int)round(array_sum($ach) / count($ach)), max($ach));
				$rec['mx'][$d] = (int)$x['max'];
			}
		}
		if ($this->paused) {
			$rec['p'] = 1;
		}
		return (count($rec) > 1) ? $rec : null;
	}

	/*
	 * Kanaele der Latenzmessung je Interface setzen: array(array('tag', 'if', 'tos', 'class'), ...).
	 * Bleibt ein Interface mit demselben TOS-Wert und derselben Klasse, bleibt auch sein Verlauf.
	 */
	function setLatChannels($list) {
		$new = array();
		foreach ($list as $c) {
			$old = $this->lat[$c['tag']] ?? null;
			$cls = (string)($c['class'] ?? '');
			if ($old && $old['tos'] == $c['tos'] && $old['class'] === $cls) {
				$old['if'] = $c['if'];
				$new[$c['tag']] = $old;
			} else {
				$new[$c['tag']] = array('tag' => $c['tag'], 'if' => $c['if'], 'tos' => (int)$c['tos'], 'class' => $cls,
					'samples' => array(), 'hist' => array(), 'cur' => null);
			}
		}
		$this->lat = $new;
	}

	function onLatRtt($tag, $rtt, $now) {
		$this->latRecord($tag, $now, $rtt);
	}

	function onLatLost($tag, $now) {
		$this->latRecord($tag, $now, null);
	}

	/* Probe eines Interfaces: Rohproben 10 s, Sekundenmittel fuer den Verlauf */
	function latRecord($tag, $now, $rtt) {
		if (!isset($this->lat[$tag])) {
			return;
		}
		$c = &$this->lat[$tag];
		$sec = (int)floor($now);
		if ($c['cur'] === null || $c['cur'][0] != $sec) {
			$this->latFlush($c);
			$c['cur'] = array($sec, 0.0, 0, 0);
		}
		if ($rtt === null) {
			$c['cur'][3]++;
		} else {
			$c['cur'][1] += $rtt;
			$c['cur'][2]++;
		}
		$c['samples'][] = array($now, $rtt);
		while ($c['samples'] && $c['samples'][0][0] < $now - 10) {
			array_shift($c['samples']);
		}
		unset($c);
	}

	/* Sekundenwert [t, Mittel ms oder null, verloren] in den Verlauf */
	private function latFlush(&$c) {
		if ($c['cur'] === null) {
			return;
		}
		$k = $c['cur'];
		$c['hist'][] = array($k[0], $k[2] ? round($k[1] / $k[2], 1) : null, $k[3]);
		while (count($c['hist']) > $this->latLen) {
			array_shift($c['hist']);
		}
		$c['cur'] = null;
	}

	/* Status je Interface; vergangene Sekunden werden dabei abgeschlossen */
	function latStatus($now = null) {
		$now = $now ?? microtime(true);
		$out = array();
		foreach ($this->lat as $tag => &$c) {
			if ($c['cur'] !== null && $c['cur'][0] < (int)floor($now)) {
				$this->latFlush($c);
			}
			while ($c['samples'] && $c['samples'][0][0] < $now - 10) {
				array_shift($c['samples']);
			}
			/* WAN-Referenz ueber denselben Zeitraum wie die Proben des Interfaces
			 * (nach einer Pause, z. B. Speedtest, enthielte sie sonst noch alte Werte) */
			$from = $c['samples'] ? $c['samples'][0][0] : $now;
			$ref = array();
			/* Im Timestamp-Betrieb pingen die Interface-Proben die Echo-Ziele: Referenz von dort */
			foreach (($this->tsmode && $this->refSamples ? $this->refSamples : $this->samples) as $s) {
				if ($s[0] >= $from) {
					$ref[] = $s;
				}
			}
			$out[] = array('tag' => (string)$tag, 'if' => $c['if'], 'tos' => sprintf('0x%02x', $c['tos']), 'class' => $c['class'],
				'stats' => self::sampleStats($c['samples']), 'ref' => self::sampleStats($ref), 'hist' => $c['hist']);
		}
		unset($c);
		return $out;
	}

	/* Kennzahlen der letzten 10 s: Mittel, 95-%-Wert, Jitter, Verlust */
	function stats() {
		$st = self::sampleStats($this->samples);
		/* Mittlerer Anstieg je Richtung (nur Timestamp-Proben) */
		$u = $d = array();
		foreach ($this->samples as $s) {
			if (isset($s[3]) && $s[3] !== null) {
				$u[] = $s[3];
				$d[] = $s[4];
			}
		}
		$st['up'] = $u ? round(array_sum($u) / count($u), 1) : null;
		$st['down'] = $d ? round(array_sum($d) / count($d), 1) : null;
		return $st;
	}

	/* $samples = array(array(t, rtt oder null bei Verlust, ...), ...) */
	static function sampleStats($samples) {
		$r = array();
		$lost = 0;
		$jit = 0.0;
		foreach ($samples as $s) {
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
		$u = array();
		foreach ($this->refl as $ip => $r) {
			if ($r['upbase'] !== null) {
				$u[$ip] = round($r['upbase'], 2);
			}
		}
		$l = array();
		foreach ($this->dirs as $d => $x) {
			$l[$d] = array('pipe' => $x['pipe'], 'kbit' => round($x['learned']));
		}
		return array('refl' => $b, 'tsup' => $u, 'learned' => $l);
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
		$n = $nd = $nu = $ndn = 0;
		foreach ($this->window as $e) {
			if (!$e[0]) {
				continue;
			}
			$n++;
			if ($e[3]) {
				$nd++;
				$nu += $e[1] ? 1 : 0;
				$ndn += $e[2] ? 1 : 0;
			}
		}
		$this->bloat = ($n >= $this->winbloat);
		/*
		 * Richtung: Mittel je Reflektor ueber die letzte Sekunde, davon der
		 * Median ueber alle Reflektoren. Ein einzelner Ausreisser kippt so die
		 * Entscheidung nicht. Betroffen ist eine Richtung, deren Anstieg
		 * mindestens die halbe Schwelle und ein Viertel des Ganzen ausmacht.
		 * Ohne genug Timestamp-Daten: Mehrheit der Stau-Proben im Fenster.
		 */
		$this->owdMed = $this->owdMedian($now);
		$this->bloatDir = null;
		if ($this->bloat) {
			$half = floatval($this->cfg['threshold']) / 2;
			if ($this->owdMed !== null) {
				list($mu, $md) = $this->owdMed;
				$sum = max(0.0, $mu) + max(0.0, $md);
				$up = $mu >= $half && $mu >= 0.25 * $sum;
				$down = $md >= $half && $md >= 0.25 * $sum;
				if ($up || $down) {
					$this->bloatDir = array('up' => $up, 'down' => $down);
				}
			} elseif ($nd >= $this->winbloat && ($nu || $ndn)) {
				$this->bloatDir = array('up' => 2 * $nu >= $nd, 'down' => 2 * $ndn >= $nd);
			}
		}
		/*
		 * Grosser Stau: Antworten kommen spaet oder gar nicht, dann fehlen die
		 * Richtungsdaten. Statt auf "alles Ausgelastete senken" zurueckzufallen,
		 * gilt die zuletzt bestimmte Richtung noch SCG_AR_DIR_HOLD Sekunden.
		 */
		$this->dirHeld = false;
		if ($this->bloatDir !== null) {
			$this->lastDir = array($now, $this->bloatDir);
		} elseif ($this->bloat && $this->tsmode && $this->lastDir !== null && $now - $this->lastDir[0] <= SCG_AR_DIR_HOLD) {
			$this->bloatDir = $this->lastDir[1];
			$this->dirHeld = true;
		}
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
				$this->lastReset = array('t' => round($now), 'dir' => $d, 'num' => $x['num'], 'kernel' => round($kernel), 'expected' => round($x['applied']));
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
			if ($this->bloatDir !== null) {
				/*
				 * Richtung bekannt: nur die gestaute Richtung senken. Bei geringer
				 * Last wie bisher erst nach lowLoadDelay und nur, wenn sie genutzt wird.
				 */
				$used = ($load >= $this->lowLoadShare && $x['achieved'] >= $this->lowLoadKbit);
				$cut = !empty($this->bloatDir[$d]) && ($load >= 0.5 || ($used && ($now - $this->bloatSince) >= $this->lowLoadDelay));
			} else {
				$cut = $this->bloat && ($load >= 0.5 || ($lowLoadCut && $d === $pick));
			}
			if ($cut) {
				if ($now - $x['lastDec'] >= $this->decRefractory) {
					$new = max($x['min'], min($x['shaper'], $x['achieved']) * $dec);
					$this->cuts[] = array('t' => round($now, 1), 'dir' => $d, 'from' => round($x['shaper']), 'to' => round($new),
						'load' => round($load, 2), 'why' => $this->bloatDir === null ? ($load >= 0.5 ? 'load' : 'lowload') : ($this->dirHeld ? 'held' : 'owd'),
						'owd' => $this->owdMed === null ? null : array(round($this->owdMed[0], 1), round($this->owdMed[1], 1)));
					if (count($this->cuts) > 20) {
						array_shift($this->cuts);
					}
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
				if ($this->bloatDir !== null && empty($this->bloatDir[$d])) {
					$x['state'] = 'bloat_other';
				} else {
					$x['state'] = ($this->idle) ? 'bloat_idle' : 'bloat_lowload';
				}
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
		$iflat = $this->latStatus();
		$dirs = array();
		foreach ($this->dirs as $d => $x) {
			$dirs[$d] = array('pipe' => $x['pipe'], 'num' => $x['num'], 'shaper' => round($x['shaper']),
				'achieved' => round($x['achieved']), 'min' => $x['min'], 'base' => $x['base'], 'max' => $x['max'],
				'learned' => round($x['learned']), 'learnbase' => !empty($this->cfg['learnbase']),
				'state' => $x['state']);
		}
		$refl = array();
		$now = microtime(true);
		foreach ($this->refl as $ip => $r) {
			/* Richtungswerte nur, solange der Reflektor mit Timestamps antwortet */
			$fresh = $r['tslast'] > 0 && $r['tslast'] >= $r['last'] - 5;
			$refl[$ip] = array('rtt' => $r['rtt'] === null ? null : round($r['rtt'], 1),
				'baseline' => $r['baseline'] === null ? null : round($r['baseline'], 1),
				'delta' => $r['delta'] === null ? null : round($r['delta'], 1), 'lost' => $r['lost'],
				'role' => $r['role'], 'ts' => $r['ts'],
				'up' => ($fresh && $r['dup'] !== null) ? round($r['dup'], 1) : null,
				'down' => ($fresh && $r['ddown'] !== null) ? round($r['ddown'], 1) : null);
		}
		$n = 0;
		foreach ($this->window as $e) {
			$n += $e[0] ? 1 : 0;
		}
		return array('t' => microtime(true), 'dirs' => $dirs, 'refl' => $refl, 'bloat' => $this->bloat,
			'window' => "{$n}/" . count($this->window), 'threshold' => floatval($this->cfg['threshold']),
			'mode' => $mode, 'err' => $err, 'resets' => $this->resets, 'lastreset' => $this->lastReset, 'discarded' => $this->discarded,
			'source' => $this->source, 'pid' => getmypid(), 'control' => $this->control, 'paused' => $this->paused,
			'hist' => $this->hist, 'rhist' => $this->rhist, 'stats' => $this->stats(),
			'iflat' => $iflat, 'iflat_class' => $iflat ? $iflat[0]['class'] : '',
			'pingmode' => $this->tsmode ? 'ts' : 'echo', 'tsactive' => $this->tsActive($now),
			'bloatdir' => $this->bloatDir, 'ts' => $this->tsInfo,
			'owdmed' => $this->owdMed === null ? null : array(round($this->owdMed[0], 1), round($this->owdMed[1], 1)),
			'late' => $this->late, 'dirheld' => $this->dirHeld, 'cuts' => $this->cuts, 'pcount' => $this->pcount);
	}
}

/* ------------------------------------------------------------------ */
/* ICMP-Timestamp-Reflektoren: Liste laden, testen, auswaehlen          */
/* ------------------------------------------------------------------ */

/* Land des WAN-Anschlusses aus dem Cloudflare-Trace (loc=XX), '' wenn unbekannt */
function scg_ts_trace_country() {
	$o = array();
	exec(escapeshellarg(SCG_AR_CURL) . ' -s -m 10 ' . escapeshellarg(SCG_AR_TS_TRACE) . ' 2>/dev/null', $o);
	foreach ($o as $l) {
		if (preg_match('/^loc=([A-Z]{2})\s*$/', $l, $m)) {
			return $m[1];
		}
	}
	return '';
}

/* Oeffentliche IPv4-Adresse? */
function scg_ts_public_ip($ip) {
	return filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4 | FILTER_FLAG_NO_PRIV_RANGE | FILTER_FLAG_NO_RES_RANGE) !== false;
}

/*
 * Liest die CSV-Liste aus $fh und behaelt eine Zufallsauswahl: bevorzugt das
 * Land $country; gibt es dort zu wenige, kommt dieselbe Teilregion dazu,
 * notfalls die ganze Welt. Liefert array('src' => country|subregion|world,
 * 'total' => Zeilen gelesen, 'rows' => array(array(ip, offset, beschreibung, ort), ...)).
 */
function scg_ts_filter_csv($fh, $country, $keep = 3000) {
	$country = strtoupper((string)$country);
	$col = array('reflector_ip' => 0, 'ip_version' => 1, 'offset' => 2, 'description' => 3, 'country_code' => 4,
		'sub_region' => 6, 'city' => 10);
	$hdr = fgetcsv($fh, 0, ',', '"', '');
	if (is_array($hdr)) {
		foreach ($hdr as $i => $name) {
			$name = trim((string)$name);
			if (isset($col[$name])) {
				$col[$name] = $i;
			}
		}
	}
	$own = array();
	$sub = array();      /* Teilregion => Zufallsauswahl */
	$seenSub = array();
	$world = array();
	$nOwn = $nWorld = $total = 0;
	$mySub = null;
	$res = function (&$list, &$n, $row, $k) {
		$n++;
		if (count($list) < $k) {
			$list[] = $row;
		} else {
			$j = mt_rand(0, $n - 1);
			if ($j < $k) {
				$list[$j] = $row;
			}
		}
	};
	while (($r = fgetcsv($fh, 0, ',', '"', '')) !== false) {
		if (!is_array($r) || count($r) < 5) {
			continue;
		}
		$total++;
		$ip = trim((string)($r[$col['reflector_ip']] ?? ''));
		if (($r[$col['ip_version']] ?? '4') !== '4' || !scg_ts_public_ip($ip)) {
			continue;
		}
		$row = array($ip, is_numeric($r[$col['offset']] ?? '') ? (int)$r[$col['offset']] : 0,
			substr((string)($r[$col['description']] ?? ''), 0, 60), substr((string)($r[$col['city']] ?? ''), 0, 40));
		$cc = strtoupper(trim((string)($r[$col['country_code']] ?? '')));
		$sr = (string)($r[$col['sub_region']] ?? '');
		$res($world, $nWorld, $row, $keep);
		if (!isset($sub[$sr])) {
			$sub[$sr] = array();
			$seenSub[$sr] = 0;
		}
		$res($sub[$sr], $seenSub[$sr], $row, 500);
		if ($country !== '' && $cc === $country) {
			$mySub = $sr;
			$res($own, $nOwn, $row, $keep);
		}
	}
	if (count($own) >= 50) {
		return array('src' => 'country', 'total' => $total, 'rows' => $own);
	}
	if ($mySub !== null && count($sub[$mySub]) >= 50) {
		$ips = array();
		foreach ($own as $row) {
			$ips[$row[0]] = true;
		}
		foreach ($sub[$mySub] as $row) {
			if (!isset($ips[$row[0]])) {
				$own[] = $row;
			}
		}
		return array('src' => 'subregion', 'total' => $total, 'rows' => $own);
	}
	return array('src' => 'world', 'total' => $total, 'rows' => $world);
}

/* Kandidaten fuer $country: aus dem Zwischenspeicher oder frisch geladen (woechentlich) */
function scg_ts_candidates($country, $force = false) {
	$old = json_decode((string)@file_get_contents(SCG_AR_TS_CAND), true);
	$fresh = is_array($old) && ($old['country'] ?? null) === $country && !empty($old['rows']) &&
		($old['t'] ?? 0) > time() - SCG_AR_TS_CAND_AGE;
	if ($fresh && !$force) {
		return $old;
	}
	$cmd = escapeshellarg(SCG_AR_CURL) . ' -s -f -L -m 300 --max-filesize 100000000 ' . escapeshellarg(SCG_AR_TS_URL) . ' 2>/dev/null';
	$fh = @popen($cmd, 'r');
	$data = null;
	if ($fh) {
		$f = scg_ts_filter_csv($fh, $country);
		pclose($fh);
		if (!empty($f['rows'])) {
			$data = array('t' => time(), 'country' => $country, 'src' => $f['src'], 'total' => $f['total'], 'rows' => $f['rows']);
			@file_put_contents(SCG_AR_TS_CAND . '.tmp', json_encode($data));
			@rename(SCG_AR_TS_CAND . '.tmp', SCG_AR_TS_CAND);
		}
	}
	/* Download gescheitert: alte Liste weiter verwenden */
	return $data ?? (is_array($old) && !empty($old['rows']) ? $old : null);
}

/*
 * Bewertung der Testantworten: $res = ip => array('rtt' => array(ms, ...), 'valid' => n gueltige
 * Timestamps, 'up' => array(Hinweg inkl. Uhrenversatz, ...)).
 * Gut ist, wer mindestens 2 Antworten mit gueltiger Zeit lieferte und dessen
 * Uhr dabei gleichmaessig lief (Hinweg schwankt nicht staerker als die RTT).
 * Rang nach Median-RTT plus Schwankung. Bevorzugt verschiedene /16-Netze.
 * Liefert array('chosen' => array(ip, ...), 'spare' => array(ip, ...), 'score' => ip => ms).
 */
function scg_ts_rank($res, $count = SCG_AR_TS_COUNT, $spares = 16) {
	$score = array();
	foreach ($res as $ip => $r) {
		$rt = $r['rtt'] ?? array();
		if (count($rt) < 2 || ($r['valid'] ?? 0) < 2) {
			continue;
		}
		$up = $r['up'] ?? array();
		if (count($up) >= 2 && (max($up) - min($up)) > (max($rt) - min($rt)) + 5) {
			continue;
		}
		sort($rt);
		$med = $rt[intdiv(count($rt), 2)];
		$score[$ip] = round($med + ($rt[count($rt) - 1] - $rt[0]), 2);
	}
	asort($score);
	$chosen = array();
	$net = array();
	foreach (array_keys($score) as $ip) {
		$n16 = implode('.', array_slice(explode('.', $ip), 0, 2));
		if (count($chosen) < $count && !isset($net[$n16])) {
			$chosen[] = $ip;
			$net[$n16] = true;
		}
	}
	foreach (array_keys($score) as $ip) {
		if (count($chosen) < $count && !in_array($ip, $chosen, true)) {
			$chosen[] = $ip;
		}
	}
	$spare = array();
	foreach (array_keys($score) as $ip) {
		if (count($spare) < $spares && !in_array($ip, $chosen, true)) {
			$spare[] = $ip;
		}
	}
	return array('chosen' => $chosen, 'spare' => $spare, 'score' => $score);
}

/* Timestamp-Antwort brauchbar? Standardzeit (geprueft im Pinger) und Sendezeit nicht vor Empfangszeit */
function scg_ts_valid($ts) {
	if ($ts === null) {
		return false;
	}
	$hold = scg_ts_diff($ts[2], $ts[1]);
	return $hold >= 0 && $hold <= 1000;
}

function scg_ts_select_running() {
	$pid = intval(@file_get_contents(SCG_AR_TS_LOCK));
	if ($pid <= 0) {
		return false;
	}
	exec('/bin/kill -0 ' . $pid . ' 2>/dev/null', $o, $rc);
	return $rc == 0;
}

/* Auswahl im Hintergrund starten (Dienst, Einstellungsseite) */
function scg_ts_spawn_select() {
	if (scg_ts_select_running()) {
		return;
	}
	exec('/usr/local/bin/php -f /usr/local/pkg/shaperclassgen_autorate.php select > /dev/null 2>&1 &');
}

/*
 * Reflektoren auswaehlen: Land bestimmen, Liste laden, SCG_AR_TS_PROBE
 * zufaellige Kandidaten dreimal anfragen, die besten behalten.
 * Ergebnis in SCG_AR_TS_SEL; liefert es auch zurueck.
 */
function scg_ts_select($force_list = false) {
	if (scg_ts_select_running()) {
		return null;
	}
	@file_put_contents(SCG_AR_TS_LOCK, (string)getmypid());
	$cfg = scg_ar_settings();
	$out = array('t' => time(), 'country' => '', 'src' => '', 'tested' => 0, 'answered' => 0,
		'chosen' => array(), 'spare' => array(), 'err' => '');
	$country = strtoupper(trim((string)$cfg['ts_country']));
	if ($country === '') {
		$country = scg_ts_trace_country();
		$out['country_auto'] = true;
	}
	$out['country'] = $country;
	$cand = scg_ts_candidates($country, $force_list);
	$pinger = new SCGPingerSocket();
	if (!$cand) {
		$out['err'] = 'list download failed';
	} elseif (!$pinger->ok) {
		$out['err'] = $pinger->err;
	} else {
		$out['src'] = $cand['src'];
		$rows = $cand['rows'];
		shuffle($rows);
		$rows = array_slice($rows, 0, SCG_AR_TS_PROBE);
		$info = array();
		$res = array();
		foreach ($rows as $row) {
			$info[$row[0]] = $row;
			$res[$row[0]] = array('rtt' => array(), 'valid' => 0, 'up' => array());
		}
		$collect = function ($us) use ($pinger, &$res, $info) {
			foreach ($pinger->receive($us) as $a) {
				if (!isset($res[$a[0]])) {
					continue;
				}
				$res[$a[0]]['rtt'][] = $a[1];
				if (scg_ts_valid($a[4] ?? null)) {
					$res[$a[0]]['valid']++;
					$res[$a[0]]['up'][] = scg_ts_owd($a[4])[0];
				}
			}
		};
		for ($round = 0; $round < 3; $round++) {
			foreach (array_keys($res) as $ip) {
				$pinger->sendTs($ip);
				$collect(10000);
			}
		}
		$end = microtime(true) + 1.5;
		while (microtime(true) < $end) {
			$collect(100000);
		}
		$out['tested'] = count($res);
		foreach ($res as $r) {
			$out['answered'] += $r['valid'] > 0 ? 1 : 0;
		}
		$rk = scg_ts_rank($res);
		$desc = function ($ip) use ($info, $rk) {
			return array('ip' => $ip, 'rtt' => $rk['score'][$ip] ?? null, 'descr' => $info[$ip][2] ?? '', 'city' => $info[$ip][3] ?? '');
		};
		$out['chosen'] = array_map($desc, $rk['chosen']);
		$out['spare'] = array_map($desc, $rk['spare']);
		if (!$out['chosen']) {
			$out['err'] = 'no reflector answered';
		}
	}
	/* Keine brauchbare Auswahl: alte behalten, nur Zeit und Fehler vermerken */
	$old = json_decode((string)@file_get_contents(SCG_AR_TS_SEL), true);
	if (!$out['chosen'] && is_array($old) && !empty($old['chosen'])) {
		$old['err'] = $out['err'];
		$old['t'] = $out['t'];
		$out = $old;
	}
	@file_put_contents(SCG_AR_TS_SEL . '.tmp', json_encode($out));
	@rename(SCG_AR_TS_SEL . '.tmp', SCG_AR_TS_SEL);
	@unlink(SCG_AR_TS_LOCK);
	return $out;
}

/* Letzte Auswahl (fuer Dienst und Einstellungsseite) */
function scg_ts_selection() {
	$j = json_decode((string)@file_get_contents(SCG_AR_TS_SEL), true);
	return is_array($j) ? $j : null;
}

/* Langzeitverlauf auf SCG_AR_LONG_HOURS kuerzen */
function scg_ar_long_prune() {
	$lim = time() - SCG_AR_LONG_HOURS * 3600;
	$keep = array();
	foreach (scg_ar_long_read($lim) as $r) {
		$keep[] = json_encode($r);
	}
	@file_put_contents(SCG_AR_LONG . '.tmp', $keep ? implode("\n", $keep) . "\n" : '');
	@rename(SCG_AR_LONG . '.tmp', SCG_AR_LONG);
}

function scg_ar_long_read($since) {
	$out = array();
	$fh = @fopen(SCG_AR_LONG, 'r');
	if (!$fh) {
		return $out;
	}
	while (($l = fgets($fh)) !== false) {
		$r = json_decode($l, true);
		if (is_array($r) && isset($r['t']) && $r['t'] >= $since) {
			$out[] = $r;
		}
	}
	fclose($fh);
	return $out;
}

/*
 * Verlauf fuer den Graphen: hoechstens ~720 Punkte, laengere Zeitraeume
 * werden zu Bloecken zusammengefasst (Mittel der Mittel, Min/Max/P95 der Extreme).
 */
function scg_ar_long_series($hours) {
	$hours = max(1, min(SCG_AR_LONG_HOURS, (int)$hours));
	$rows = scg_ar_long_read(time() - $hours * 3600);
	$bucket = max(60, (int)(ceil($hours * 60 / 720) * 60));
	if ($bucket == 60) {
		return array('bucket' => 60, 'rows' => $rows);
	}
	$out = array();
	$cur = null;
	$flush = function() use (&$cur, &$out) {
		if ($cur === null) {
			return;
		}
		$r = array('t' => $cur['t']);
		if ($cur['la']) {
			$r['l'] = array(min($cur['lmin']), round(array_sum($cur['la']) / count($cur['la']), 1), max($cur['lmax']),
				max($cur['lp']), $cur['lost'], $cur['n'], $cur['base']);
		} elseif ($cur['lost']) {
			$r['l'] = array(null, null, null, null, $cur['lost'], $cur['n'], $cur['base']);
		}
		foreach (array('d', 'u') as $k) {
			if ($cur[$k]) {
				$c = count($cur[$k]);
				$r[$k] = array((int)round(array_sum(array_column($cur[$k], 0)) / $c), (int)round(array_sum(array_column($cur[$k], 1)) / $c),
					max(array_column($cur[$k], 2)));
			}
		}
		$r['mx'] = $cur['mx'];
		if ($cur['p']) {
			$r['p'] = 1;
		}
		$out[] = $r;
		$cur = null;
	};
	foreach ($rows as $r) {
		$b = (int)(floor($r['t'] / $bucket) * $bucket);
		if ($cur !== null && $cur['t'] != $b) {
			$flush();
		}
		if ($cur === null) {
			$cur = array('t' => $b, 'la' => array(), 'lmin' => array(), 'lmax' => array(), 'lp' => array(), 'lost' => 0, 'n' => 0,
				'base' => null, 'd' => array(), 'u' => array(), 'mx' => array(), 'p' => false);
		}
		if (isset($r['l'])) {
			$l = $r['l'];
			if ($l[1] !== null) {
				$cur['la'][] = $l[1];
				$cur['lmin'][] = $l[0];
				$cur['lmax'][] = $l[2];
				$cur['lp'][] = $l[3];
			}
			$cur['lost'] += $l[4];
			$cur['n'] += $l[5];
			if ($l[6] !== null) {
				$cur['base'] = $l[6];
			}
		}
		foreach (array('d', 'u') as $k) {
			if (isset($r[$k])) {
				$cur[$k][] = $r[$k];
			}
		}
		if (isset($r['mx'])) {
			$cur['mx'] = array_merge($cur['mx'], $r['mx']);
		}
		if (!empty($r['p'])) {
			$cur['p'] = true;
		}
	}
	$flush();
	return array('bucket' => $bucket, 'rows' => $out);
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
	/* Echo-Ziele: Latenz je Interface pingt immer diese (Probe-Regeln passen auf Echo) */
	$echo_ips = $ar->echoIps;
	$wan = null;
	$next_baseline = microtime(true) + 60;
	$pinger = new SCGPingerSocket();
	if (!$pinger->ok) {
		$err = $pinger->err;
		$pinger = new SCGPingerProc($ips, intval($cfg['interval']));
		$pinger->err = $err;
	}
	syslog(LOG_NOTICE, "ShaperClassGen Autorate gestartet (Pinger: {$pinger->mode})");
	/* ICMP-Timestamp: braucht den Raw-Socket; feste Liste oder automatische Auswahl */
	$ts_on = $ar->tsmode && $pinger->mode === 'socket';
	$ts_manual = scg_ar_iplist($cfg['ts_manual'] ?? '');
	$ts_used = array();
	$ts_spare = array();
	$ts_sel_mtime = null;
	$ts_next_spawn = 0;
	if ($ar->tsmode && !$ts_on) {
		$ar->tsInfo = array('src' => 'none', 'err' => 'ICMP timestamps need the PHP sockets extension');
	}

	$interval = intval($cfg['interval']) / 1000;
	/* Ziele neu setzen: neue bekommen einen Ping-Termin, entfernte keinen mehr */
	$set_ts = function ($list) use ($ar, &$next_ping, &$interval) {
		$ar->setTsReflectors($list);
		$i = 0;
		foreach (array_keys($ar->refl) as $ip) {
			if (!isset($next_ping[$ip])) {
				$next_ping[$ip] = microtime(true) + $interval * $i++ / max(1, count($ar->refl));
			}
		}
		foreach (array_keys($next_ping) as $ip) {
			if (!isset($ar->refl[$ip])) {
				unset($next_ping[$ip]);
			}
		}
	};
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
	$last_minute = null;
	scg_ar_long_prune();
	$last_rx_end = null;
	$stall_end = null;
	$cfg_mtime = null;
	$cfg_err = false;
	/* Signal von Anwenden/Limiter-Neuanlage: alles sofort neu einlesen */
	clearstatcache(true, SCG_AR_RELOAD);
	$sig_mtime = @filemtime(SCG_AR_RELOAD);
	$sig_force = false;
	/* Latenz je Interface: Kanal (1..n) => array('tag', 'tos', 'next', 'i') */
	$latch = array();
	$latsig = null;
	$next_rotate = 0;
	$lat_iv = SCG_AR_LAT_MS / 1000;

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
			$cfg_changed = $sig_force || ($mt !== false && $cfg_mtime !== null && $mt != $cfg_mtime);
			$sig_force = false;
			if ($cfg_changed) {
				if (scg_cfg_reload()) {
					$cfg_err = false;
				} else {
					/* Nicht eingelesen: beim naechsten Durchlauf erneut versuchen */
					if (!$cfg_err) {
						syslog(LOG_WARNING, 'ShaperClassGen Autorate: configuration could not be re-read');
					}
					$cfg_err = true;
					$mt = $cfg_mtime;
					$sig_force = true;
				}
			}
			if ($mt !== false && $mt !== null) {
				$cfg_mtime = $mt;
			}
			list($pname) = scg_dn_names();
			$ar->resolve($pname);
			$wan = scg_ar_wan($cfg);
			$ar->source = $wan ? array('kind' => 'iface', 'descr' => $wan[2], 'real' => $wan[1]) : array('kind' => 'limiter');
			/*
			 * Latenz je Interface: Kanaele aus den erzeugten Regeln. Bei jeder
			 * Konfigurationsaenderung und alle 5 min neue ICMP-IDs, damit ein
			 * State, der vor der Regel entstand (ohne Queue), nicht bestehen bleibt.
			 */
			$cl = scg_latprobe_channels();
			$sig = json_encode($cl);
			if ($sig !== $latsig || $cfg_changed || $now >= $next_rotate) {
				$latch = array();
				$tos = array();
				$ch = 1;
				foreach ($cl as $c) {
					$latch[$ch] = array('tag' => $c['tag'], 'tos' => $c['tos'], 'next' => $now + $lat_iv * $ch / (count($cl) + 1), 'i' => $ch);
					$tos[$ch] = $c['tos'];
					$ch++;
				}
				$pinger->channels($tos, $echo_ips, SCG_AR_LAT_MS);
				$ar->setLatChannels($cl);
				$latsig = $sig;
				$next_rotate = $now + SCG_AR_LAT_ROTATE;
			}
			/* Timestamp-Reflektoren: Auswahl uebernehmen, ausgefallene ersetzen, neu auswaehlen */
			if ($ts_on && $ts_manual) {
				if (!$ts_used) {
					$ts_used = $ts_manual;
					$set_ts($ts_used);
					$ar->tsInfo = array('src' => 'manual');
				}
			} elseif ($ts_on) {
				clearstatcache(true, SCG_AR_TS_SEL);
				$smt = @filemtime(SCG_AR_TS_SEL);
				if ($smt !== false && $smt !== $ts_sel_mtime) {
					$ts_sel_mtime = $smt;
					$sel = scg_ts_selection();
					if ($sel && !empty($sel['chosen'])) {
						$ts_used = array_column($sel['chosen'], 'ip');
						$ts_spare = array_column($sel['spare'] ?? array(), 'ip');
						$set_ts($ts_used);
					}
					$ar->tsInfo = array('src' => 'auto', 'country' => $sel['country'] ?? '', 'list' => $sel['src'] ?? '',
						't' => $sel['t'] ?? null, 'err' => $sel['err'] ?? '');
				}
				$dead = $ar->deadTs($now);
				if ($dead) {
					foreach ($dead as $ip) {
						$rep = null;
						while ($ts_spare && ($rep === null || in_array($rep, $ts_used, true))) {
							$rep = array_shift($ts_spare);
						}
						$k = array_search($ip, $ts_used, true);
						if ($k !== false) {
							if ($rep !== null && !in_array($rep, $ts_used, true)) {
								$ts_used[$k] = $rep;
							} else {
								unset($ts_used[$k]);
							}
						}
					}
					$ts_used = array_values($ts_used);
					$set_ts($ts_used);
				}
				$need = ($smt === false || $smt < time() - SCG_AR_TS_SEL_AGE || count($ts_used) < 2);
				if ($need && $now >= $ts_next_spawn) {
					scg_ts_spawn_select();
					$ts_next_spawn = $now + 600;
				}
			}
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
				if ($ts_on && !empty($ar->refl[$ip]['ts'])) {
					$pinger->sendTs($ip);
				} else {
					$pinger->send($ip);
				}
				$next_ping[$ip] = max($t + $interval, $now);
			}
		}
		/* Latenz je Interface; waehrend eines Speedtests nicht (Limiter evtl. gedrosselt) */
		$lat_wake = $next_tick;
		if (!$ar->paused) {
			foreach ($latch as $ch => &$c) {
				if ($now >= $c['next']) {
					$pinger->send($echo_ips[$c['i']++ % count($echo_ips)], $ch, $c['tos']);
					$c['next'] = max($c['next'] + $lat_iv, $now);
				}
				$lat_wake = min($lat_wake, $c['next']);
			}
			unset($c);
		}
		$wake = min(min($next_ping), $next_tick, $lat_wake);
		$wait = max(0, (int)(($wake - microtime(true)) * 1e6));
		foreach ($pinger->receive($wait) as $res) {
			if ($res[2] !== null && $stall_end !== null && $res[2] < $stall_end) {
				$ar->discarded++;
				continue;
			}
			$ch = $res[3] ?? 0;
			if ($ch == 0) {
				$ar->onRtt($res[0], $res[1], microtime(true), $res[4] ?? null);
			} elseif (isset($latch[$ch])) {
				$ar->onLatRtt($latch[$ch]['tag'], $res[1], microtime(true));
			}
		}
		$last_rx_end = hrtime(true);
		foreach ($pinger->expire(SCG_AR_TIMEOUT_MS) as $l) {
			if ($l[1] == 0) {
				$ar->onLost($l[0], microtime(true));
			} elseif (isset($latch[$l[1]])) {
				$ar->onLatLost($latch[$l[1]]['tag'], microtime(true));
			}
		}
		$now = microtime(true);
		if ($now >= $next_tick) {
			clearstatcache(true, SCG_AR_RELOAD);
			$smt = @filemtime(SCG_AR_RELOAD);
			if ($smt !== false && $smt !== $sig_mtime) {
				$sig_mtime = $smt;
				$sig_force = true;
				$next_resolve = 0;
			}
			/* Speedtest-Pause: Datei mit Ablaufzeit, verwaiste Dateien laufen von selbst ab */
			clearstatcache(true, SCG_AR_PAUSE);
			$pz = file_exists(SCG_AR_PAUSE) ? intval(@file_get_contents(SCG_AR_PAUSE)) : 0;
			$ar->paused = ($pz > time());
			/* Sicherheitsnetz: gedrosselte Limiter eines abgebrochenen exklusiven Tests zuruecksetzen */
			if (!$ar->paused && file_exists(SCG_ST_SQUEEZE)) {
				$sq = json_decode((string)@file_get_contents(SCG_ST_SQUEEZE), true);
				if (!is_array($sq) || ($sq['until'] ?? 0) < time()) {
					scg_st_unsqueeze();
				}
			}
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
		/* Langzeitverlauf: jede volle Minute anhaengen, stuendlich auf 48 h kuerzen */
		$minute = (int)(floor($now / 60) * 60);
		if ($last_minute === null) {
			$last_minute = $minute;
		} elseif ($minute > $last_minute) {
			$rec = $ar->minuteRecord($last_minute);
			if ($rec) {
				@file_put_contents(SCG_AR_LONG, json_encode($rec) . "\n", FILE_APPEND | LOCK_EX);
			}
			$last_minute = $minute;
			if ($minute % 3600 == 0) {
				scg_ar_long_prune();
			}
		}
		if ($now >= $next_baseline) {
			@file_put_contents(SCG_AR_BASELINE, json_encode($ar->memory()));
			$next_baseline = $now + 60;
		}
		if ($now >= $next_status) {
			$ar->late = isset($pinger->late) ? $pinger->late : array(0, 0);
			$ar->pcount = isset($pinger->count) ? $pinger->count : array();
			@file_put_contents(SCG_AR_STATUS . '.tmp', json_encode($ar->status($pinger->mode, $pinger->err)));
			@rename(SCG_AR_STATUS . '.tmp', SCG_AR_STATUS);
			$next_status = $now + 1;
		}
	}
}
