<?php
/*
 * shaperclassgen.inc
 *
 * Shaper Class Generator for pfSense
 * Legt die 7 Klassen des pfSense-Traffic-Shaper-Wizards als Limiter-Child-Queues
 * (dummynet, WF2Q+) unter manuell angelegten Limitern an und erzeugt die
 * Floating-Match-Regeln mit den Port-Listen des Wizards.
 *
 * Die Matching-Listen (Spiele, P2P, VoIP, sonstige Anwendungen) stammen aus
 * src/usr/local/www/wizards/traffic_shaper_wizard_multi_all.inc des
 * pfSense-Projekts (Apache License 2.0, Copyright (c) Rubicon Communications,
 * LLC (Netgate) und Mitwirkende). Offensichtliche Fehler der Vorlage wurden
 * korrigiert (z. B. SMB-Portbereich, doppelte Battle.net-Eintraege).
 *
 * Licensed under the Apache License, Version 2.0
 */

require_once("config.inc");
require_once("util.inc");
require_once("interfaces.inc");
require_once("shaper.inc");
require_once("filter.inc");
require_once("/usr/local/pkg/shaperclassgen_i18n.inc");

define('SCG_DN_RULES', '/tmp/rules.limiter');
define('SCG_VERSION', '0.6.6');
define('SCG_BACKUP', '/conf/shaperclassgen.json');
define('SCG_QTAG', '[SCG]');      /* Prefix der Queue-Beschreibung */
define('SCG_RTAG', 'SCG:');       /* Prefix der Regelbeschreibung bis 0.1.2 */
define('SCG_RTAG2', 'SCG Pipe ');  /* Prefix ab 0.1.3: zeigt die Limiter-Pipes */
define('SCG_DESCR_MAX', 52);      /* pfSense-Grenze fuer Regelbeschreibungen */
define('SCG_CFG', 'installedpackages/shaperclassgen/settings');

/* ------------------------------------------------------------------ */
/* Config-Helfer (2.7+ config_*_path, sonst globales $config)          */
/* ------------------------------------------------------------------ */

function scg_cfg_get($path, $default = null) {
	if (function_exists('config_get_path')) {
		return config_get_path($path, $default);
	}
	global $config;
	$p = $config;
	foreach (explode('/', $path) as $k) {
		if (!is_array($p) || !array_key_exists($k, $p)) {
			return $default;
		}
		$p = $p[$k];
	}
	return $p;
}

function scg_cfg_set($path, $value) {
	if (function_exists('config_set_path')) {
		config_set_path($path, $value);
		return;
	}
	global $config;
	$p = &$config;
	foreach (explode('/', $path) as $k) {
		if (!isset($p[$k]) || !is_array($p[$k])) {
			$p[$k] = array();
		}
		$p = &$p[$k];
	}
	$p = $value;
}

/* ------------------------------------------------------------------ */
/* Klassenschemata                                                     */
/* ------------------------------------------------------------------ */

/*
 * simple:  4 Klassen nach Verhalten (Standard fuer neue Installationen)
 * classic: die 7 Klassen des pfSense-Wizards (qACK heisst jetzt qControl)
 */
function scg_schemes() {
	return array(
		'simple' => 'Four classes by behaviour: real-time, interactive, default, bulk',
		'classic' => 'Seven classes like the pfSense traffic shaper wizard',
	);
}

function scg_scheme($s) {
	$sch = is_array($s) ? ($s['scheme'] ?? 'classic') : (string)$s;
	return isset(scg_schemes()[$sch]) ? $sch : 'classic';
}

function scg_classes($scheme = 'classic') {
	if (scg_scheme($scheme) == 'simple') {
		return array(
			'qRealtime'    => array('label' => 'qRealtime',    'weight' => 100, 'hint' => 'Small, latency-critical flows: telephony, games, DNS, NTP, ICMP'),
			'qInteractive' => array('label' => 'qInteractive', 'weight' => 50,  'hint' => 'Interactive (SSH, RDP, VNC, TeamSpeak ...)'),
			'qDefault'     => array('label' => 'qDefault',     'weight' => 20,  'hint' => 'Everything not assigned to a class'),
			'qBulk'        => array('label' => 'qBulk',        'weight' => 2,   'hint' => 'Background: peer-to-peer, mail, bulk devices (backups, downloads), penalty box'),
		);
	}
	return array(
		'qControl'    => array('label' => 'qControl',    'weight' => 100, 'hint' => 'Network control traffic (DNS, ICMP, NTP); formerly qACK. True TCP ACK separation is not possible with limiters.'),
		'qVoIP'       => array('label' => 'qVoIP',       'weight' => 100, 'hint' => 'Telephony'),
		'qGames'      => array('label' => 'qGames',      'weight' => 60,  'hint' => 'Games'),
		'qOthersHigh' => array('label' => 'qOthersHigh', 'weight' => 40,  'hint' => 'Interactive (SSH, RDP, VNC ...)'),
		'qDefault'    => array('label' => 'qDefault',    'weight' => 20,  'hint' => 'Everything not assigned to a class'),
		'qOthersLow'  => array('label' => 'qOthersLow',  'weight' => 8,   'hint' => 'Mail, news, penalty box'),
		'qP2P'        => array('label' => 'qP2P',        'weight' => 3,   'hint' => 'Peer-to-Peer'),
	);
}

/* Rolle (Art des Verkehrs) -> Klasse im jeweiligen Schema */
function scg_role_class($scheme, $role) {
	$map = array(
		'classic' => array('control' => 'qControl', 'voip' => 'qVoIP', 'games' => 'qGames', 'interactive' => 'qOthersHigh',
			'default' => 'qDefault', 'low' => 'qOthersLow', 'p2p' => 'qP2P'),
		'simple' => array('control' => 'qRealtime', 'voip' => 'qRealtime', 'games' => 'qRealtime', 'interactive' => 'qInteractive',
			'default' => 'qDefault', 'low' => 'qBulk', 'p2p' => 'qBulk'),
	);
	return $map[scg_scheme($scheme)][$role] ?? '';
}

/* Reihenfolge der Match-Regeln: letzte passende Match-Regel gewinnt */
function scg_rule_order($scheme = 'classic') {
	if (scg_scheme($scheme) == 'simple') {
		return array('qBulk', 'qInteractive', 'qRealtime');
	}
	return array('qP2P', 'qOthersLow', 'qOthersHigh', 'qGames', 'qVoIP', 'qControl');
}

/* Klassen-Zuordnung beim Wechsel des Schemas */
function scg_scheme_map($from, $to) {
	if ($from == 'classic' && $to == 'simple') {
		return array('qControl' => 'qRealtime', 'qVoIP' => 'qRealtime', 'qGames' => 'qRealtime',
			'qOthersHigh' => 'qInteractive', 'qDefault' => 'qDefault', 'qOthersLow' => 'qBulk', 'qP2P' => 'qBulk');
	}
	if ($from == 'simple' && $to == 'classic') {
		return array('qRealtime' => 'qControl', 'qInteractive' => 'qOthersHigh', 'qDefault' => 'qDefault', 'qBulk' => 'qOthersLow');
	}
	return array();
}

/*
 * Wechselt das Klassenschema: Gewichte auf die Vorgaben des neuen Schemas,
 * AQM/Queue-Laenge von qDefault uebernommen, App-Zuordnungen umgeschluesselt.
 * Interfaces, Matchings und Adressen bleiben.
 */
function scg_switch_scheme($s, $to) {
	$from = scg_scheme($s);
	$to = scg_scheme($to);
	if ($from == $to) {
		return $s;
	}
	$map = scg_scheme_map($from, $to);
	$tmpl = $s['classes']['qDefault'] ?? array('aqm' => 'codel', 'qlimit' => '');
	$s['classes'] = array();
	foreach (scg_classes($to) as $c => $d) {
		$s['classes'][$c] = array('weight' => $d['weight'], 'aqm' => $tmpl['aqm'] ?? 'codel', 'qlimit' => $tmpl['qlimit'] ?? '');
	}
	foreach ($s['others'] as $k => $c) {
		$s['others'][$k] = ($c === '') ? '' : ($map[$c] ?? '');
	}
	$s['latclass'] = $map[$s['latclass'] ?? ''] ?? '';
	$s['scheme'] = $to;
	return $s;
}

function scg_prio_presets() {
	return array('high' => 1.0, 'mid' => 0.5, 'low' => 0.2);
}

/* ------------------------------------------------------------------ */
/* Matching-Listen aus dem Wizard                                      */
/* Format: array(Beschreibung, Protokoll, Startport, Endport)          */
/* ------------------------------------------------------------------ */

function scg_voip_list() {
	return array(
		'Generic' => array('label' => 'Generic (address only)', 'e' => array()),
		'Asterisk' => array('label' => 'Asterisk / Vonage / SIP', 'e' => array(
			array('SIP', 'udp', '5060', '5069'),
			array('RTP', 'udp', '10000', '20000'),
		)),
		'VoicePulse' => array('label' => 'VoicePulse', 'e' => array(
			array('VoicePulse-RTP', 'udp', '16384', '16482'),
			array('VoicePulse-IAX', 'udp', '4569', '4569'),
		)),
		'Panasonic' => array('label' => 'Panasonic TDA', 'e' => array(
			array('Panasonic1', 'udp', '8000', '8063'),
			array('Panasonic2', 'udp', '9300', '9301'),
			array('Panasonic3', 'udp', '2747', '2747'),
		)),
	);
}

function scg_games_list() {
	return array(
		'battlefield2' => array('label' => 'Battlefield 2', 'e' => array(
			array('BF2', 'udp', '1500', '4999'), array('BF2', 'tcp', '4711', '4711'),
			array('BF2', 'udp', '16567', '16567'), array('BF2', 'udp', '27900', '27900'),
			array('BF2', 'tcp', '28910', '28910'), array('BF2', 'udp', '29900', '29901'),
			array('BF2', 'tcp', '29900', '29901'), array('BF2', 'udp', '55123', '55125'))),
		'callofduty' => array('label' => 'Call of Duty', 'e' => array(
			array('CoD', 'tcp', '28960', '28960'), array('CoD', 'udp', '28960', '28960'))),
		'counterstrike' => array('label' => 'Counter-Strike', 'e' => array(
			array('CS-Titan', 'udp', '6003', '6003'), array('CS-Auth', 'udp', '7002', '7002'),
			array('CS-Master', 'udp', '27010', '27010'), array('CS-Mod', 'udp', '27011', '27011'),
			array('CS-Chat', 'udp', '27012', '27012'), array('CS-HL', 'udp', '27013', '27015'))),
		'deltaforce' => array('label' => 'Delta Force', 'e' => array(
			array('Delta', 'udp', '17478', '17488'))),
		'doom3' => array('label' => 'Doom 3', 'e' => array(
			array('DOOM3', 'udp', '27650', '27650'), array('DOOM3', 'udp', '27666', '27666'))),
		'empireearth' => array('label' => 'Empire Earth', 'e' => array(
			array('EE', 'tcp', '33335', '33336'), array('EE', 'udp', '33334', '33334'))),
		'everquest' => array('label' => 'EverQuest', 'e' => array(
			array('EQ', 'tcp', '1024', '6000'), array('EQ', 'tcp', '7000', '7000'),
			array('EQ', 'udp', '1024', '6000'), array('EQ', 'udp', '7000', '7000'))),
		'everquest2' => array('label' => 'EverQuest II', 'e' => array(
			array('EQ2', 'tcp', '7000', '7000'), array('EQ2', 'udp', '3016', '3021'),
			array('EQ2', 'udp', '9100', '9100'), array('EQ2', 'udp', '9700', '9703'),
			array('EQ2', 'udp', '32800', '33000'))),
		'farcry' => array('label' => 'Far Cry', 'e' => array(
			array('FarCry', 'tcp', '49001', '49002'), array('FarCry', 'udp', '49001', '49002'))),
		'gunzonline' => array('label' => 'GunZ Online', 'e' => array(
			array('GunZ', 'udp', '7700', '7700'))),
		'halflife' => array('label' => 'Half-Life', 'e' => array(
			array('HL', 'tcp', '27015', '27015'), array('HL', 'udp', '27650', '27650'),
			array('HL', 'udp', '27666', '27666'))),
		'halflife2' => array('label' => 'Half-Life 2', 'e' => array(
			array('HL2', 'tcp', '27020', '27050'), array('HL2', 'udp', '1200', '1200'),
			array('HL2', 'udp', '27000', '27015'))),
		'halo2' => array('label' => 'Halo 2 / Xbox Live', 'e' => array(
			array('Halo2', 'udp', '88', '88'), array('Halo2', 'udp', '3074', '3074'),
			array('Halo2', 'tcp', '3074', '3074'))),
		'lineage2' => array('label' => 'Lineage II', 'e' => array(
			array('L2', 'tcp', '2009', '2009'), array('L2', 'tcp', '2106', '2106'),
			array('L2', 'tcp', '7777', '7777'))),
		'planetside' => array('label' => 'PlanetSide', 'e' => array(
			array('PS', 'tcp', '7000', '7000'), array('PS', 'tcp', '7080', '7080'),
			array('PS', 'udp', '3016', '3021'), array('PS', 'udp', '45000', '45010'),
			array('PS', 'udp', '30000', '30500'))),
		'quakeiii' => array('label' => 'Quake III', 'e' => array(
			array('Q3', 'udp', '27910', '27919'))),
		'tigerwoods2004ps2' => array('label' => 'Tiger Woods 2004 (PS2)', 'e' => array(
			array('TW', 'udp', '3658', '3658'), array('TW', 'udp', '6000', '6000'),
			array('TW', 'tcp', '10300', '10301'))),
		'unrealtournament' => array('label' => 'Unreal Tournament', 'e' => array(
			array('UT', 'udp', '7777', '7787'), array('UT', 'tcp', '7777', '7787'))),
		'wolfet' => array('label' => 'Wolfenstein: ET', 'e' => array(
			array('WolfET', 'tcp', '27960', '27960'))),
		'battlenet' => array('label' => 'Battle.net (Blizzard)', 'e' => array(
			array('BattleNET', 'tcp', '6112', '6119'))),
		'worldofwarcraft' => array('label' => 'World of Warcraft', 'e' => array(
			array('WoW', 'tcp', '3724', '3724'), array('BattleNET', 'tcp', '6112', '6119'))),
		'archlord' => array('label' => 'ArchLord', 'e' => array(
			array('AL', 'tcp', '11000', '11000'), array('AL', 'tcp', '11002', '11002'),
			array('AL', 'tcp', '11008', '11008'))),
		'xbox360' => array('label' => 'Xbox 360', 'e' => array(
			array('xbox', 'udp', '88', '88'), array('xbox', 'udp', '3074', '3074'),
			array('xbox', 'tcp', '3074', '3074'))),
		/*
		 * Aktuelle Eintraege nach Herstellerangaben ('m' => 1). Nur Spielverkehr,
		 * keine Download-Ports. 5. Feld = Seite: dst (Server-Port), src (lokaler
		 * Port der Konsole), both.
		 * Quellen: Steam Support "Required Ports for Steam", PlayStation Support
		 * (WS-116520-5), Riot Support "Set up port forwarding", Activision
		 * Support "Ports used for Call of Duty games" (inkl. Xbox).
		 */
		'steam' => array('label' => 'Steam games (Valve servers, SDR, P2P, voice)', 'm' => 1, 'e' => array(
			array('Steam', 'udp', '27000', '27050'), array('Steam-P2P', 'udp', '3478', '3478'),
			array('Steam-P2P', 'udp', '4379', '4380'))),
		'psn' => array('label' => 'PlayStation Network (PS4/PS5)', 'm' => 1, 'e' => array(
			array('PSN', 'udp', '3478', '3479', 'both'), array('PSN', 'tcp', '3478', '3480'))),
		'xboxnet' => array('label' => 'Xbox network (Xbox One / Series)', 'm' => 1, 'e' => array(
			array('Xbox', 'udp', '3074', '3074', 'both'), array('Xbox', 'tcp', '3074', '3074', 'both'),
			array('Xbox', 'udp', '88', '88'))),
		'riot' => array('label' => 'Riot Games (LoL, VALORANT, TFT)', 'm' => 1, 'e' => array(
			array('Riot', 'udp', '5000', '5500'), array('Riot', 'udp', '7000', '8000'))),
		'cod' => array('label' => 'Call of Duty (BO6/BO7, Warzone, MW3)', 'm' => 1, 'e' => array(
			array('CoD', 'udp', '3074', '3074', 'both'), array('CoD', 'udp', '4379', '4380'),
			array('CoD', 'udp', '6112', '6119'), array('CoD', 'udp', '20500', '20500'),
			array('CoD', 'udp', '20510', '20510'), array('CoD', 'udp', '28960', '28960'))),
		/* Java Edition: TCP 25565 (Standard) plus haeufige Zusatzserver; Bedrock: UDP 19132/19133 (IPv4/IPv6) */
		'minecraft' => array('label' => 'Minecraft (Java + Bedrock)', 'm' => 1, 'e' => array(
			array('Minecraft', 'tcp', '25565', '25600'), array('Minecraft-BE', 'udp', '19132', '19133'))),
	);
}

/*
 * Netze von Spieleanbietern (ASN). Werden als URL-Table-Alias geladen und
 * nur fuer UDP ausser Port 443 verwendet: Downloads laufen ueber TCP bzw.
 * QUIC (UDP 443) und landen so nicht in der Echtzeit-Klasse.
 */
function scg_gamenet_presets() {
	return array(
		'valve' => array('label' => 'Steam / Valve (CS2, Dota 2, Deadlock, Steam Datagram Relay)', 'asn' => array(32590)),
		'riot' => array('label' => 'Riot Games', 'asn' => array(6507)),
		'blizzard' => array('label' => 'Blizzard / Battle.net', 'asn' => array(57976)),
		'sony' => array('label' => 'Sony Interactive Entertainment (PlayStation Network)', 'asn' => array(33353)),
	);
}

/* Gewaehlte ASNs: Nummer => Bezeichnung */
function scg_gamenet_asns($s) {
	$out = array();
	$pre = scg_gamenet_presets();
	foreach ((array)($s['gamenets'] ?? array()) as $k) {
		if (isset($pre[$k])) {
			foreach ($pre[$k]['asn'] as $n) {
				$out[(int)$n] = scg_short_label($pre[$k]['label']);
			}
		}
	}
	foreach (preg_split('/[\s,;]+/', trim((string)($s['gamenets_extra'] ?? ''))) as $t) {
		if (preg_match('/^(?:AS)?(\d{1,10})$/i', $t, $m) && !isset($out[(int)$m[1]])) {
			$out[(int)$m[1]] = 'AS' . $m[1];
		}
	}
	return $out;
}

function scg_gamenet_alias($asn) {
	return 'SCG_AS' . (int)$asn;
}

/*
 * Eigene Spielserver: IPs, Netze, Hostnamen oder Aliase. Ein einzelner
 * vorhandener Alias wird direkt verwendet, sonst legt das Paket den
 * Netz-Alias SCG_GAMESERVERS an (Hostnamen loest pfSense regelmaessig auf).
 * Rueckgabe: array(Zieladresse fuer die Regel, Alias-Eintraege oder null)
 */
function scg_gameservers($s) {
	$t = preg_split('/\s+/', trim((string)($s['gameservers'] ?? '')), -1, PREG_SPLIT_NO_EMPTY);
	if (!$t) {
		return array(null, null);
	}
	if (count($t) == 1 && function_exists('is_alias') && is_alias($t[0]) && $t[0] !== 'SCG_GAMESERVERS') {
		return array($t[0], null);
	}
	return array('SCG_GAMESERVERS', $t);
}

function scg_is_hostname($h) {
	return strlen($h) <= 253 && preg_match('/^(?=.*[a-z])([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z0-9-]{2,63}$/i', $h);
}

function scg_p2p_list() {
	return array(
		'aimster' => array('label' => 'Aimster', 'e' => array(array('Aimster', 'tcp', '7668', '7668'))),
		'bittorrent' => array('label' => 'BitTorrent', 'e' => array(
			array('BitTorrent', 'tcp', '6881', '6999'), array('BitTorrent', 'udp', '6881', '6999'))),
		'buddyshare' => array('label' => 'BuddyShare', 'e' => array(array('BuddyShare', 'tcp', '7788', '7788'))),
		'cutemx' => array('label' => 'CuteMX', 'e' => array(array('CuteMX', 'tcp', '2340', '2340'))),
		'dcplusplus' => array('label' => 'DC++', 'e' => array(array('DC++', 'tcp', '1412', '1412'))),
		'dcc' => array('label' => 'IRC DCC', 'e' => array(array('dcc', 'tcp', '6666', '6668'))),
		'directconnect' => array('label' => 'DirectConnect', 'e' => array(array('DirectConnect', 'tcp', '412', '412'))),
		'directfileexpress' => array('label' => 'DirectFileExpress', 'e' => array(array('DFE', 'tcp', '1044', '1045'))),
		'edonkey2000' => array('label' => 'eDonkey2000', 'e' => array(array('eDonkey', 'tcp', '4661', '4665'))),
		'fasttrack' => array('label' => 'FastTrack', 'e' => array(array('FastTrack', 'tcp', '1214', '1214'))),
		'gnutella' => array('label' => 'Gnutella', 'e' => array(
			array('Gnutella', 'tcp', '6346', '6346'), array('Gnutella', 'udp', '6346', '6346'))),
		'grouper' => array('label' => 'Grouper', 'e' => array(array('grouper', 'tcp', '8038', '8039'))),
		'hotcomm' => array('label' => 'hotComm', 'e' => array(array('hotComm', 'tcp', '28864', '28865'))),
		'hotlineconnect' => array('label' => 'HotlineConnect', 'e' => array(array('Hotline', 'tcp', '5500', '5503'))),
		'imesh' => array('label' => 'iMesh', 'e' => array(array('iMesh', 'tcp', '4329', '4329'))),
		'napster' => array('label' => 'Napster', 'e' => array(array('Napster', 'tcp', '6699', '6701'))),
		'opennap' => array('label' => 'OpenNap', 'e' => array(array('OpenNap', 'tcp', '8888', '8889'))),
		'scour' => array('label' => 'Scour', 'e' => array(array('Scour', 'tcp', '8311', '8311'))),
		'shareaza' => array('label' => 'Shareaza', 'e' => array(array('Shareaza', 'tcp', '6346', '6346'))),
		'songspy' => array('label' => 'SongSpy', 'e' => array(array('SongSpy', 'tcp', '5190', '5190'))),
		'winmx' => array('label' => 'WinMX', 'e' => array(array('WinMX', 'tcp', '6699', '6699'))),
	);
}

/* 'def' = Standardrolle (siehe scg_role_class; '' = nicht matchen, faellt in qDefault) */
function scg_others_list() {
	return array(
		'msrdp' => array('label' => 'MS RDP', 'def' => 'interactive', 'e' => array(array('RDP', 'tcp', '3389', '3389'))),
		'vnc' => array('label' => 'VNC', 'def' => 'interactive', 'e' => array(array('VNC', 'tcp', '5900', '5930'))),
		'appleremotedesktop' => array('label' => 'Apple Remote Desktop', 'def' => '', 'e' => array(
			array('ARD', 'tcp', '3283', '3283'), array('ARD', 'tcp', '5900', '5900'),
			array('ARD', 'udp', '3283', '3283'), array('ARD', 'udp', '5900', '5900'))),
		'pcanywhere' => array('label' => 'pcAnywhere', 'def' => '', 'e' => array(
			array('pcany', 'tcp', '5631', '5631'), array('pcany', 'udp', '5632', '5632'))),
		'ssh' => array('label' => 'SSH', 'x' => 1, 'def' => 'interactive', 'e' => array(array('SSH', 'tcp', '22', '22'))),
		'teamspeak' => array('label' => 'TeamSpeak', 'def' => 'interactive', 'e' => array(
			array('TS', 'tcp', '14534', '14534'), array('TS', 'tcp', '51234', '51234'),
			array('TS', 'udp', '8767', '8768'))),
		'dns' => array('label' => 'DNS', 'def' => 'control', 'e' => array(
			array('DNS', 'tcp', '53', '53'), array('DNS', 'udp', '53', '53'))),
		'dot' => array('label' => 'DNS over TLS', 'x' => 1, 'def' => 'control', 'e' => array(array('DoT', 'tcp', '853', '853'))),
		'ntp' => array('label' => 'NTP', 'x' => 1, 'def' => 'control', 'e' => array(array('NTP', 'udp', '123', '123'))),
		'icmp' => array('label' => 'ICMP', 'def' => 'control', 'e' => array(array('ICMP', 'icmp', '', ''))),
		'http' => array('label' => 'HTTP/HTTPS', 'def' => '', 'e' => array(
			array('HTTP', 'tcp', '80', '80'), array('HTTPS', 'tcp', '443', '443'))),
		'quic' => array('label' => 'QUIC / HTTP3', 'x' => 1, 'def' => '', 'e' => array(array('QUIC', 'udp', '443', '443'))),
		'smtp' => array('label' => 'SMTP', 'def' => 'low', 'e' => array(array('SMTP', 'tcp', '25', '25'))),
		'pop3' => array('label' => 'POP3', 'def' => 'low', 'e' => array(array('POP3', 'tcp', '110', '110'))),
		'imap' => array('label' => 'IMAP', 'def' => 'low', 'e' => array(array('IMAP', 'tcp', '143', '143'))),
		'nntp' => array('label' => 'NNTP', 'def' => 'low', 'e' => array(
			array('NNTP', 'tcp', '119', '119'), array('NNTP', 'udp', '119', '119'))),
		'pptp' => array('label' => 'PPTP', 'def' => '', 'e' => array(
			array('PPTP', 'tcp', '1723', '1723'), array('PPTP-GRE', 'gre', '', ''))),
		'ipsec' => array('label' => 'IPsec', 'def' => '', 'e' => array(
			array('IPSEC', 'udp', '500', '500'), array('IPSEC-AH', 'ah', '', ''), array('IPSEC-ESP', 'esp', '', ''))),
		'streamingmp3' => array('label' => 'Streaming MP3 (Shoutcast)', 'def' => '', 'e' => array(array('MP3', 'tcp', '8000', '8100'))),
		'rtsp' => array('label' => 'RTSP', 'def' => '', 'e' => array(array('RTSP', 'tcp', '554', '554'))),
		'irc' => array('label' => 'IRC', 'def' => '', 'e' => array(array('IRC', 'tcp', '6667', '6670'))),
		'jabber' => array('label' => 'Jabber/XMPP', 'def' => '', 'e' => array(
			array('XMPP', 'tcp', '5222', '5223'), array('XMPP', 'tcp', '5269', '5269'))),
		'icq' => array('label' => 'ICQ', 'def' => '', 'e' => array(
			array('ICQ', 'tcp', '5190', '5190'), array('ICQ', 'udp', '5190', '5190'))),
		'aolinstantmessenger' => array('label' => 'AIM', 'def' => '', 'e' => array(array('AIM', 'tcp', '5190', '5190'))),
		'msnmessenger' => array('label' => 'MSN Messenger', 'def' => '', 'e' => array(
			array('MSN', 'tcp', '1863', '1863'), array('MSN', 'tcp', '6891', '6901'),
			array('MSN', 'udp', '6901', '6901'))),
		'lotusnotes' => array('label' => 'Lotus Notes', 'def' => '', 'e' => array(
			array('Notes', 'tcp', '1352', '1352'), array('Notes', 'udp', '1352', '1352'))),
		'smb' => array('label' => 'SMB/CIFS', 'def' => '', 'e' => array(
			array('SMB', 'tcp', '445', '445'), array('NetBIOS', 'tcp', '137', '139'))),
		'snmp' => array('label' => 'SNMP', 'def' => '', 'e' => array(
			array('SNMP', 'tcp', '161', '161'), array('SNMP', 'udp', '161', '161'))),
		'mysqlserver' => array('label' => 'MySQL', 'def' => '', 'e' => array(array('MySQL', 'tcp', '3306', '3306'))),
		'cvsup' => array('label' => 'CVSup', 'def' => '', 'e' => array(array('cvsup', 'tcp', '5999', '5999'))),
		'hbci' => array('label' => 'HBCI', 'def' => '', 'e' => array(array('HBCI', 'tcp', '3000', '3000'))),
		'slingbox' => array('label' => 'Slingbox', 'def' => '', 'e' => array(
			array('Slingbox', 'tcp', '5001', '5001'), array('Slingbox', 'udp', '5001', '5001'))),
	);
}

/* ------------------------------------------------------------------ */
/* Einstellungen                                                       */
/* ------------------------------------------------------------------ */

function scg_default_settings($scheme = 'simple') {
	$scheme = scg_scheme($scheme);
	$s = array(
		'scheme' => $scheme,
		'vlans' => array(),
		'classes' => array(),
		'voip' => array('enable' => false, 'provider' => 'Asterisk', 'address' => ''),
		'penalty' => '',
		'gameservers' => '',
		'games' => array(),
		'p2p' => array('bittorrent'),
		'others' => array(),
		'srcports' => false,
		'ecn' => false,
		'latprobe' => false,
		'latclass' => '',
		'aliasname' => 'SCG_LOCALNETS',
		'devudp' => true,
		'gamenets' => array(),
		'gamenets_extra' => '',
		'asn_url' => 'https://asn.ipinfo.app/api/text/list/AS%s',
		'localnets' => '10.0.0.0/8 172.16.0.0/12 192.168.0.0/16 169.254.0.0/16 224.0.0.0/4 fc00::/7 fe80::/10 ff00::/8',
	);
	foreach (scg_classes($scheme) as $c => $d) {
		$s['classes'][$c] = array('weight' => $d['weight'], 'aqm' => 'codel', 'qlimit' => '');
	}
	foreach (scg_others_list() as $k => $d) {
		$s['others'][$k] = $d['def'] === '' ? '' : scg_role_class($scheme, $d['def']);
	}
	return $s;
}

function scg_load_settings() {
	$raw = scg_cfg_get(SCG_CFG, '');
	$saved = null;
	if (is_string($raw) && $raw !== '') {
		$saved = json_decode(base64_decode($raw), true);
	}
	/* Fallback: Sicherung in /conf (bleibt bei Paket-Neuinstallation erhalten) */
	if (!is_array($saved) && is_readable(SCG_BACKUP)) {
		$saved = json_decode(file_get_contents(SCG_BACKUP), true);
	}
	if (!is_array($saved)) {
		/* Neue Installation: 4-Klassen-Schema */
		return scg_default_settings('simple');
	}
	/* Gespeicherte Einstellungen ohne Schema stammen aus Versionen vor 0.5 = klassisch */
	$scheme = scg_scheme($saved['scheme'] ?? 'classic');
	/* Migration: qACK heisst seit 0.5 qControl */
	if (isset($saved['classes']['qACK'])) {
		if (!isset($saved['classes']['qControl'])) {
			$saved['classes']['qControl'] = $saved['classes']['qACK'];
		}
		unset($saved['classes']['qACK']);
	}
	if (isset($saved['others']) && is_array($saved['others'])) {
		foreach ($saved['others'] as $k => $c) {
			if ($c === 'qACK') {
				$saved['others'][$k] = 'qControl';
			}
		}
	}
	$s = scg_default_settings($scheme);
	/* Vor 0.5.1 galt die Geraete-Adresse fuer den gesamten Verkehr */
	if (!isset($saved['devudp'])) {
		$s['devudp'] = false;
	}
	$valid = scg_classes($scheme);
	foreach ($saved as $k => $v) {
		if ($k == 'classes') {
			foreach ((array)$v as $c => $d) {
				if (isset($valid[$c]) && is_array($d)) {
					$s['classes'][$c] = array_merge($s['classes'][$c], $d);
				}
			}
		} elseif ($k == 'others') {
			foreach ((array)$v as $app => $c) {
				if (isset($s['others'][$app]) && ($c === '' || isset($valid[$c]))) {
					$s['others'][$app] = $c;
				}
			}
		} elseif ($k == 'voip') {
			$s[$k] = array_merge($s[$k], is_array($v) ? $v : array());
		} else {
			$s[$k] = $v;
		}
	}
	$s['scheme'] = $scheme;
	return $s;
}

function scg_store_settings($s) {
	scg_cfg_set(SCG_CFG, base64_encode(json_encode($s)));
	@file_put_contents(SCG_BACKUP, json_encode($s));
}

/* Alias-Namen fuer die Autovervollstaendigung */
function scg_alias_names() {
	if (function_exists('get_alias_list')) {
		return array_values(get_alias_list(array("host", "network", "url", "urltable")));
	}
	$out = array();
	foreach ((array)scg_cfg_get('aliases/alias', array()) as $al) {
		if (is_array($al) && !empty($al['name']) && in_array($al['type'] ?? '', array('host', 'network', 'url', 'urltable'))) {
			$out[] = $al['name'];
		}
	}
	sort($out);
	return $out;
}

/* ------------------------------------------------------------------ */
/* Abfragen                                                            */
/* ------------------------------------------------------------------ */

/* LAN-artige Interfaces (ohne Gateway) */
function scg_local_interfaces() {
	$out = array();
	$list = get_configured_interface_with_descr();
	foreach ($list as $if => $descr) {
		if (function_exists('interface_has_gateway') && interface_has_gateway($if)) {
			continue;
		}
		$out[$if] = $descr;
	}
	return $out;
}

/* Vorhandene Limiter (Pipes) */
function scg_pipes() {
	$out = array();
	$pipes = scg_cfg_get('dnshaper/queue', array());
	if (!is_array($pipes)) {
		return $out;
	}
	foreach ($pipes as $i => $p) {
		if (!is_array($p) || empty($p['name'])) {
			continue;
		}
		$bw = '';
		if (isset($p['bandwidth']['item'][0]['bw'])) {
			$bw = $p['bandwidth']['item'][0]['bw'] . ' ' . ($p['bandwidth']['item'][0]['bwscale'] ?? '');
		}
		$sched = $p['sched'] ?? ($p['scheduler'] ?? '');
		$nq = (isset($p['queue']) && is_array($p['queue'])) ? count($p['queue']) : 0;
		$out[$p['name']] = array('idx' => $i, 'bw' => trim($bw), 'sched' => $sched, 'nq' => $nq);
	}
	return $out;
}

function scg_is_generated_queue($q) {
	return is_array($q) && isset($q['description']) && strpos($q['description'], SCG_QTAG) === 0;
}

function scg_is_generated_rule($r) {
	if (!is_array($r)) {
		return false;
	}
	if (isset($r['scg']) && $r['scg'] == 'yes') {
		return true;
	}
	return isset($r['descr']) && (strpos($r['descr'], SCG_RTAG) === 0 || strpos($r['descr'], SCG_RTAG2) === 0);
}

/* ------------------------------------------------------------------ */
/* Validierung                                                         */
/* ------------------------------------------------------------------ */

function scg_validate($s, &$errors) {
	$pipes = scg_pipes();
	$tags = array();
	$active = 0;
	foreach ($s['vlans'] as $v) {
		if (empty($v['enable'])) {
			continue;
		}
		$active++;
		$t = $v['tag'] ?? '';
		if (!preg_match('/^[A-Za-z0-9]{1,10}$/', $t)) {
			$errors[] = scg_t('Tag for %s must be 1-10 characters [A-Za-z0-9].', $v['if']);
		} elseif (isset($tags[strtolower($t)])) {
			$errors[] = scg_t('Tag "%s" is used twice.', $t);
		}
		$tags[strtolower($t)] = true;
		if (empty($v['down']) || !isset($pipes[$v['down']])) {
			$errors[] = scg_t('No valid download limiter selected for %s.', $v['if']);
		}
		if (empty($v['up']) || !isset($pipes[$v['up']])) {
			$errors[] = scg_t('No valid upload limiter selected for %s.', $v['if']);
		}
		if (!empty($v['down']) && $v['down'] === $v['up']) {
			$errors[] = scg_t('Download and upload limiter are identical for %s.', $v['if']);
		}
		$f = floatval($v['factor']);
		if ($f <= 0 || $f > 1) {
			$errors[] = scg_t('Priority factor for %s must be between 0.01 and 1.', $v['if']);
		}
	}
	foreach ($s['classes'] as $c => $d) {
		$w = $d['weight'];
		if (!is_numeric($w) || intval($w) < 1 || intval($w) > 100) {
			$errors[] = scg_t('Weight of %s must be between 1 and 100.', $c);
		}
		if ($d['qlimit'] !== '' && (!ctype_digit((string)$d['qlimit']) || intval($d['qlimit']) < 1)) {
			$errors[] = scg_t('Queue length of %s must be empty or a positive number.', $c);
		}
		if (!in_array($d['aqm'], array('droptail', 'codel'))) {
			$errors[] = scg_t('Unknown AQM value for %s.', $c);
		}
	}
	foreach (array('voip' => $s['voip']['address'], 'penalty' => $s['penalty']) as $what => $a) {
		if ($a !== '' && !is_ipaddr($a) && !is_subnet($a) && !is_alias($a)) {
			$errors[] = scg_t('Address "%s" (%s) is not an IP, network or alias.', $a, $what);
		}
	}
	if (!empty($s['voip']['enable']) && $s['voip']['provider'] == 'Generic' && $s['voip']['address'] === '') {
		$errors[] = scg_t('VoIP "Generic" requires an address or alias.');
	}
	if (!preg_match('/^[A-Za-z0-9_]{1,31}$/', $s['aliasname'])) {
		$errors[] = scg_t('Alias name is invalid.');
	} else {
		foreach ((array)scg_cfg_get('aliases/alias', array()) as $al) {
			if (is_array($al) && ($al['name'] ?? '') === $s['aliasname'] && strpos($al['descr'] ?? '', SCG_QTAG) !== 0) {
				$errors[] = scg_t('An alias "%s" already exists and was not created by this package.', $s['aliasname']);
			}
		}
	}
	foreach (preg_split('/\s+/', trim((string)($s['gameservers'] ?? '')), -1, PREG_SPLIT_NO_EMPTY) as $t) {
		if (!is_ipaddr($t) && !is_subnet($t) && !is_alias($t) && !scg_is_hostname($t)) {
			$errors[] = scg_t('Game server "%s" is not an IP, network, host name or alias.', $t);
		}
	}
	foreach (preg_split('/[\s,;]+/', trim((string)($s['gamenets_extra'] ?? ''))) as $t) {
		if ($t !== '' && !preg_match('/^(?:AS)?\d{1,10}$/i', $t)) {
			$errors[] = scg_t('"%s" is not a valid AS number.', $t);
		}
	}
	if (scg_gamenet_asns($s) && (!preg_match('#^https?://#', (string)$s['asn_url']) || substr_count((string)$s['asn_url'], '%s') != 1)) {
		$errors[] = scg_t('The ASN list URL must start with http(s):// and contain %%s exactly once.', '');
	}
	$nets = preg_split('/\s+/', trim($s['localnets']));
	if (empty($nets) || $nets[0] === '') {
		$errors[] = scg_t('The list of local networks must not be empty.');
	}
	foreach ($nets as $n) {
		if ($n !== '' && !is_subnet($n)) {
			$errors[] = scg_t('"%s" is not a valid network (CIDR).', $n);
		}
	}
	return $active;
}

/* ------------------------------------------------------------------ */
/* Plan erstellen                                                      */
/* ------------------------------------------------------------------ */

function scg_port($start, $end) {
	if ($start === '' || $start === null) {
		return '';
	}
	if ($end === '' || $end === null || $end == $start) {
		return (string)$start;
	}
	return "{$start}-{$end}";
}

/* Sammelt alle Match-Eintraege nach Klasse; tcp+udp mit gleichen Ports werden zusammengefasst */
function scg_collect_entries($s) {
	$sch = scg_scheme($s);
	$raw = array();
	foreach (array_keys(scg_classes($sch)) as $c) {
		$raw[$c] = array();
	}
	/* Klassisch (wie Wizard): Adresse ersetzt die Port-Liste. 4 Klassen: Geraete und Ports unabhaengig */
	if (!empty($s['voip']['enable']) && ($s['voip']['address'] === '' || $sch == 'simple')) {
		$vl = scg_voip_list();
		if (isset($vl[$s['voip']['provider']])) {
			foreach ($vl[$s['voip']['provider']]['e'] as $e) {
				$raw[scg_role_class($sch, 'voip')][] = $e;
			}
		}
	}
	$gl = scg_games_list();
	foreach ((array)$s['games'] as $k) {
		if (isset($gl[$k])) {
			foreach ($gl[$k]['e'] as $e) {
				$e[0] = scg_short_label($gl[$k]['label']);
				$raw[scg_role_class($sch, 'games')][] = $e;
			}
		}
	}
	$pl = scg_p2p_list();
	foreach ((array)$s['p2p'] as $k) {
		if (isset($pl[$k])) {
			foreach ($pl[$k]['e'] as $e) {
				$e[0] = scg_short_label($pl[$k]['label']);
				$raw[scg_role_class($sch, 'p2p')][] = $e;
			}
		}
	}
	$ol = scg_others_list();
	foreach ($ol as $k => $d) {
		$c = $s['others'][$k] ?? '';
		if ($c === '' || !isset($raw[$c])) {
			continue;
		}
		$lbl = scg_short_label($d['label']);
		foreach ($d['e'] as $e) {
			/* Unterscheidbare Teile (HTTP/HTTPS, ICMP6 ...) behalten */
			if (count($d['e']) == 1 || strpos($lbl, '/') === false) {
				$e[0] = $lbl;
			}
			$raw[$c][] = $e;
		}
	}

	$out = array();
	foreach ($raw as $c => $list) {
		$byport = array();
		foreach ($list as $e) {
			list($descr, $proto, $start, $end) = $e;
			$side = $e[4] ?? 'dst';
			$port = scg_port($start, $end);
			if ($proto == 'tcp' || $proto == 'udp') {
				$key = 'tu|' . $port . '|' . $side;
				if (isset($byport[$key])) {
					if ($byport[$key]['p'] != $proto && $byport[$key]['p'] != 'tcp/udp') {
						$byport[$key]['p'] = 'tcp/udp';
					}
					if (strpos($byport[$key]['d'], $descr) === false) {
						$byport[$key]['d'] .= '/' . $descr;
					}
					continue;
				}
				$byport[$key] = array('d' => $descr, 'p' => $proto, 'port' => $port, 'side' => $side);
			} else {
				$key = $proto . '|';
				if (!isset($byport[$key])) {
					$byport[$key] = array('d' => $descr, 'p' => $proto, 'port' => '', 'side' => 'dst');
				}
			}
		}
		$out[$c] = array_values($byport);
	}
	return $out;
}

/* Anzeigename ohne Zusaetze in Klammern, z. B. "Tiger Woods 2004 (PS2)" -> "Tiger Woods 2004" */
function scg_short_label($l) {
	return trim(preg_replace('/\s*\(.*\)\s*/', ' ', $l));
}

function scg_qname($dir, $tag, $cls) {
	return ($dir == 'down' ? 'D_' : 'U_') . $tag . '_' . $cls;
}

/*
 * Beschreibung, die die gesetzten Limiter zeigt, da pfSense Pipes in der
 * Regeluebersicht nicht anzeigt. Format:
 *   SCG Pipe U/D_<Kuerzel>_<Klasse> <Anwendung>
 * In-Pipe = U_<Kuerzel>_<Klasse> (Upload), Out-Pipe = D_<Kuerzel>_<Klasse> (Download).
 * Gekuerzt wird nur der Anwendungsname, die Pipe bleibt immer vollstaendig.
 */
function scg_rule_descr($tag, $cls, $what) {
	$head = SCG_RTAG2 . "U/D_{$tag}_{$cls}";
	$room = SCG_DESCR_MAX - strlen($head) - 1;
	if ($room <= 0 || $what === '') {
		return substr($head, 0, SCG_DESCR_MAX);
	}
	return $head . ' ' . substr($what, 0, $room);
}

function scg_make_rule($v, $cls, $s, $descr, $proto = null, $ipproto = 'inet46', $port = '', $side = 'dst', $srcaddr = null, $dstaddr = null) {
	$r = array(
		'type' => 'match',
		'interface' => $v['if'],
		'ipprotocol' => $ipproto,
		'direction' => 'in',
		'floating' => 'yes',
		'statetype' => 'keep state',
		'source' => array(),
		'destination' => array('address' => $s['aliasname'], 'not' => ''),
		'descr' => scg_rule_descr($v['tag'], $cls, $descr),
		'dnpipe' => scg_qname('up', $v['tag'], $cls),
		'pdnpipe' => scg_qname('down', $v['tag'], $cls),
		'scg' => 'yes',
	);
	if ($dstaddr !== null && $dstaddr !== '') {
		$r['destination'] = array('address' => $dstaddr);
	}
	if ($srcaddr !== null && $srcaddr !== '') {
		$r['source']['address'] = $srcaddr;
	} else {
		$r['source']['any'] = '';
	}
	if ($proto !== null && $proto !== '') {
		$r['protocol'] = $proto;
		if ($proto == 'icmp') {
			$r['icmptype'] = 'any';
		}
	}
	if ($port !== '' && ($proto == 'tcp' || $proto == 'udp' || $proto == 'tcp/udp')) {
		if ($side == 'src') {
			$r['source']['port'] = $port;
		} else {
			$r['destination']['port'] = $port;
		}
	}
	return $r;
}

/*
 * Latenzmessung je Interface: Der Autorate-Dienst pingt fuer jedes geshapte
 * Interface mit einem eigenen DSCP-Wert. Eine Floating-Match-Regel auf den
 * WAN-Interfaces (Richtung out, Quelle = pfSense selbst) legt diese Pings in
 * die gewaehlte Queue des Interfaces (scg_latprobe_class); die Antworten
 * folgen dem State.
 * DSCP-Werte aus Pool 2 (xxxx11, RFC 2474: experimentell/lokal), damit sie
 * nicht mit ueblichen Markierungen kollidieren. Als TOS-Byte, ECN-Bits 0.
 */
/*
 * Klasse, durch die die Latenzmessung laeuft: gewaehlt oder die interaktive
 * Klasse des Schemas. Zeigt, was bevorzugter Verkehr unter Last erlebt.
 */
function scg_latprobe_class($s) {
	$c = (string)($s['latclass'] ?? '');
	if ($c !== '' && isset(scg_classes(scg_scheme($s))[$c])) {
		return $c;
	}
	return scg_role_class(scg_scheme($s), 'interactive');
}

function scg_latprobe_tos_pool() {
	$out = array();
	for ($dscp = 3; $dscp < 64; $dscp += 4) {
		$out[] = sprintf('0x%02x', $dscp << 2);
	}
	return $out;
}

/* Interfaces mit Gateway (WAN, VPN-Clients): dort verlassen die Pings die pfSense */
function scg_wan_interfaces() {
	$out = array();
	foreach (array_keys(get_configured_interface_with_descr()) as $if) {
		if (scg_if_is_wan($if)) {
			$out[] = $if;
		}
	}
	return $out;
}

function scg_latprobe_rule($v, $wanifs, $tos, $cls) {
	return array(
		'type' => 'match',
		'interface' => implode(',', $wanifs),
		'ipprotocol' => 'inet',
		'protocol' => 'icmp',
		'icmptype' => 'echoreq',
		'direction' => 'out',
		'floating' => 'yes',
		'statetype' => 'keep state',
		'source' => array('network' => '(self)'),
		'destination' => array('any' => ''),
		'dscp' => $tos,
		'descr' => scg_rule_descr($v['tag'], $cls, 'Latency probe'),
		/* Richtung out auf dem WAN = Upload; Antworten = Download */
		'dnpipe' => scg_qname('up', $v['tag'], $cls),
		'pdnpipe' => scg_qname('down', $v['tag'], $cls),
		'scg' => 'yes',
	);
}

/*
 * Messkanaele aus den tatsaechlich erzeugten Regeln (nicht aus den
 * gespeicherten Einstellungen): nur was angewendet ist, wird gemessen.
 * Liefert array(array('tag', 'if', 'tos' (int), 'class'), ...) in Regelreihenfolge.
 */
function scg_latprobe_channels() {
	$ifs = array();
	$set = scg_load_settings();
	foreach ((array)($set['vlans'] ?? array()) as $v) {
		if (!empty($v['tag'])) {
			$ifs[$v['tag']] = $v['if'];
		}
	}
	$out = array();
	$seen = array();
	foreach ((array)scg_cfg_get('filter/rule', array()) as $r) {
		if (!scg_is_generated_rule($r) || empty($r['dscp']) || ($r['direction'] ?? '') !== 'out' || isset($r['disabled'])) {
			continue;
		}
		if (!preg_match('/^U_([A-Za-z0-9]+)_(q[A-Za-z0-9]+)$/', (string)($r['dnpipe'] ?? ''), $m) ||
		    !preg_match('/^0x([0-9a-f]{2})$/i', (string)$r['dscp'], $t)) {
			continue;
		}
		$tos = hexdec($t[1]);
		if ($tos == 0 || isset($seen[$tos])) {
			continue;
		}
		$seen[$tos] = true;
		$out[] = array('tag' => $m[1], 'if' => $ifs[$m[1]] ?? '', 'tos' => $tos, 'class' => $m[2]);
	}
	return $out;
}

function scg_build_plan($s) {
	$plan = array('queues' => array(), 'rules' => array(), 'perif' => array(), 'aliases' => array(), 'probes' => array(), 'notes' => array());
	$sch = scg_scheme($s);
	$asns = scg_gamenet_asns($s);
	foreach ($asns as $n => $lbl) {
		$lbl = ($lbl === "AS{$n}") ? '' : $lbl;
		$asns[$n] = $lbl;
		$plan['aliases'][scg_gamenet_alias($n)] = array('url' => sprintf($s['asn_url'], $n), 'descr' => trim(SCG_QTAG . " Game network AS{$n} {$lbl}"));
	}
	list($gsdst, $gsentries) = scg_gameservers($s);
	if ($gsentries) {
		$plan['aliases']['SCG_GAMESERVERS'] = array('address' => implode(' ', $gsentries), 'descr' => SCG_QTAG . ' Game servers');
	}
	/* UDP ohne Port 443 (QUIC-Downloads): zwei Bereiche */
	$udpnoquic = array('1-442', '444-65535');
	$entries = scg_collect_entries($s);
	$ecn = !empty($s['ecn']) ? 'on' : '';
	foreach ($s['vlans'] as $v) {
		if (empty($v['enable'])) {
			continue;
		}
		$f = floatval($v['factor']);
		foreach (scg_classes($sch) as $cls => $d) {
			$cs = $s['classes'][$cls];
			$w = (int)round(intval($cs['weight']) * $f);
			$w = max(1, min(100, $w));
			foreach (array('down', 'up') as $dir) {
				$plan['queues'][] = array(
					'pipe' => ($dir == 'down') ? $v['down'] : $v['up'],
					'name' => scg_qname($dir, $v['tag'], $cls),
					'weight' => $w,
					'aqm' => $cs['aqm'],
					'qlimit' => $cs['qlimit'],
					'ecn' => $ecn,
					'description' => SCG_QTAG . " {$v['tag']} {$cls} " . ($dir == 'down' ? 'Download' : 'Upload'),
				);
			}
		}

		$rules = array();
		/* 1. Catch-all: aller Internetverkehr -> qDefault */
		$rules[] = scg_make_rule($v, 'qDefault', $s, 'Default');
		/* 2. Klassen, niedrigste zuerst (letzte passende Match-Regel gewinnt) */
		foreach (scg_rule_order($sch) as $cls) {
			if ($cls == scg_role_class($sch, 'voip') && ($sch == 'simple' || !empty($s['voip']['enable'])) && $s['voip']['address'] !== '') {
				$dl = ($sch == 'simple') ? 'Devices' : 'VoIP device';
				if (!empty($s['devudp'])) {
					/* Nur UDP ohne QUIC: Spiel-/Sprachverkehr ja, Downloads und Updates nein */
					foreach ($udpnoquic as $pr) {
						$rules[] = scg_make_rule($v, $cls, $s, $dl . ' UDP', 'udp', 'inet46', $pr, 'dst', $s['voip']['address']);
					}
				} else {
					$rules[] = scg_make_rule($v, $cls, $s, $dl, null, 'inet46', '', 'dst', $s['voip']['address']);
				}
			}
			foreach ($entries[$cls] as $e) {
				if ($e['p'] == 'icmp') {
					$rules[] = scg_make_rule($v, $cls, $s, $e['d'], 'icmp', 'inet');
					$rules[] = scg_make_rule($v, $cls, $s, $e['d'] . '6', 'icmp', 'inet6');
					continue;
				}
				$side = $e['side'] ?? 'dst';
				if ($side != 'src') {
					$rules[] = scg_make_rule($v, $cls, $s, $e['d'], $e['p'], 'inet46', $e['port'], 'dst');
				}
				if ($e['port'] !== '' && ($side != 'dst' || !empty($s['srcports']))) {
					$rules[] = scg_make_rule($v, $cls, $s, $e['d'] . ' src', $e['p'], 'inet46', $e['port'], 'src');
				}
			}
			/* Netze der Spieleanbieter: UDP ohne QUIC */
			if ($cls == scg_role_class($sch, 'games')) {
				/* Eigene Spielserver: gesamter Verkehr (TCP und UDP), z. B. Minecraft Java */
				if ($gsdst !== null) {
					$rules[] = scg_make_rule($v, $cls, $s, 'Game servers', null, 'inet46', '', 'dst', null, $gsdst);
				}
				foreach ($asns as $n => $lbl) {
					foreach ($udpnoquic as $pr) {
						$rules[] = scg_make_rule($v, $cls, $s, trim("AS{$n} {$lbl}"), 'udp', 'inet46', $pr, 'dst', null, scg_gamenet_alias($n));
					}
				}
			}
		}
		/* 3. Strafbox zuletzt, damit sie immer gewinnt */
		if ($s['penalty'] !== '') {
			$rules[] = scg_make_rule($v, scg_role_class($sch, 'low'), $s, ($sch == 'simple') ? 'Devices' : 'Penalty box', null, 'inet46', '', 'dst', $s['penalty']);
		}
		$plan['perif'][$v['if']] = count($rules);
		$plan['rules'] = array_merge($plan['rules'], $rules);
	}
	/* 4. Latenzmessung je Interface: eine Regel pro Interface am Ende des Blocks */
	if (!empty($s['latprobe'])) {
		$wanifs = scg_wan_interfaces();
		$pool = scg_latprobe_tos_pool();
		$lcls = scg_latprobe_class($s);
		$k = 0;
		foreach ($s['vlans'] as $v) {
			if (empty($v['enable'])) {
				continue;
			}
			if (!$wanifs) {
				$plan['notes'][] = scg_t('Latency per interface: no interface with a gateway found, no probe rules created.');
				break;
			}
			if ($k >= count($pool)) {
				$plan['notes'][] = scg_t('Latency per interface: only the first %d interfaces are measured.', count($pool));
				break;
			}
			$plan['probes'][$v['tag']] = $pool[$k];
			$plan['rules'][] = scg_latprobe_rule($v, $wanifs, $pool[$k++], $lcls);
		}
	}
	return $plan;
}

/* ------------------------------------------------------------------ */
/* Anwenden / Entfernen                                                */
/* ------------------------------------------------------------------ */

/* Entfernt generierte Queues/Regeln aus den uebergebenen Arrays */
function scg_strip(&$pipes, &$rules, &$oldnums) {
	foreach ($pipes as $pi => $pipe) {
		if (!is_array($pipe) || !isset($pipe['queue']) || !is_array($pipe['queue'])) {
			continue;
		}
		$keep = array();
		foreach ($pipe['queue'] as $q) {
			if (scg_is_generated_queue($q)) {
				$oldnums[$q['name']] = intval($q['number'] ?? 0);
			} else {
				$keep[] = $q;
			}
		}
		if (empty($keep)) {
			unset($pipes[$pi]['queue']);
		} else {
			$pipes[$pi]['queue'] = $keep;
		}
	}
	$keep = array();
	foreach ($rules as $r) {
		if (!scg_is_generated_rule($r)) {
			$keep[] = $r;
		}
	}
	$rules = $keep;
}

/*
 * Identity of a generated rule: the fields that define what it matches and
 * where it sends traffic. Fields pfSense adds or changes on its own (tracker,
 * created, updated, id, disabled ...) are left out.
 */
function scg_rule_identity($r) {
	$keys = array('type', 'interface', 'ipprotocol', 'protocol', 'icmptype', 'direction',
		'floating', 'source', 'destination', 'dscp', 'dnpipe', 'pdnpipe', 'descr');
	$f = array();
	foreach ($keys as $k) {
		if (isset($r[$k])) {
			$f[$k] = scg_normalize_value($r[$k]);
		}
	}
	return md5(json_encode($f));
}

/* Arrays sorted by key and all scalars as strings, as read back from config.xml */
function scg_normalize_value($v) {
	if (is_array($v)) {
		ksort($v);
		foreach ($v as $k => $x) {
			$v[$k] = scg_normalize_value($x);
		}
		return $v;
	}
	return (string)$v;
}

/* Trackers of the currently generated rules, grouped by rule identity */
function scg_generated_rule_trackers($rules) {
	$out = array();
	foreach ($rules as $r) {
		if (!is_array($r) || !scg_is_generated_rule($r) || !isset($r['tracker']) || $r['tracker'] === '') {
			continue;
		}
		$out[scg_rule_identity($r)][] = array(
			'tracker' => (string)$r['tracker'],
			'created' => (isset($r['created']) && is_array($r['created'])) ? $r['created'] : null,
		);
	}
	return $out;
}

/* Anzahl generierter Floating-Regeln am Anfang der Floating-Liste */
function scg_leading_generated_floating($rules) {
	$n = 0;
	foreach ($rules as $r) {
		if (!is_array($r) || empty($r['floating'])) {
			continue;
		}
		if (!scg_is_generated_rule($r)) {
			break;
		}
		$n++;
	}
	return $n;
}

/*
 * pfSense speichert Trenner als Positionsnummer (frN) innerhalb der
 * Floating-Liste. Wenn der Generator-Block oben seine Groesse aendert,
 * werden die Trenner um die Differenz verschoben.
 */
function scg_adjust_separators($oldoff, $newoff) {
	if ($oldoff == $newoff) {
		return;
	}
	$seps = scg_cfg_get('filter/separator/floatingrules', null);
	if (!is_array($seps)) {
		return;
	}
	foreach ($seps as $k => $sep) {
		if (!is_array($sep) || !isset($sep['row'])) {
			continue;
		}
		$isarr = is_array($sep['row']);
		$row = $isarr ? ($sep['row'][0] ?? '') : $sep['row'];
		if (!preg_match('/^fr(\d+)$/', (string)$row, $m)) {
			continue;
		}
		$n = max(0, intval($m[1]) - $oldoff) + $newoff;
		$seps[$k]['row'] = $isarr ? array('fr' . $n) : 'fr' . $n;
	}
	scg_cfg_set('filter/separator/floatingrules', $seps);
}

/*
 * Bisheriger Versatz der Trenner. Erst ab 0.1.2 werden Trenner verschoben
 * (Merker 'sepshift'); aeltere Staende sind unverschoben -> 0.
 */
function scg_current_sep_offset($rules) {
	$prev = scg_load_settings();
	return !empty($prev['sepshift']) ? scg_leading_generated_floating($rules) : 0;
}

function scg_dummynet_names($pipes) {
	$names = array();
	$nums = array();
	foreach ($pipes as $p) {
		if (!is_array($p)) {
			continue;
		}
		$names[strtolower($p['name'] ?? '')] = true;
		if (isset($p['queue']) && is_array($p['queue'])) {
			foreach ($p['queue'] as $q) {
				$names[strtolower($q['name'] ?? '')] = true;
				if (isset($q['number'])) {
					$nums[intval($q['number'])] = true;
				}
			}
		}
	}
	return array($names, $nums);
}

function scg_dnctl_delete($numbers) {
	foreach ($numbers as $n) {
		$n = intval($n);
		if ($n > 0) {
			mwexec("/sbin/dnctl queue delete " . escapeshellarg($n), true);
		}
	}
}

/* Prueft, ob fremde Regeln Queues referenzieren, die verschwinden */
function scg_dangling_refs($rules, $removed) {
	$warn = array();
	foreach ($rules as $r) {
		foreach (array('dnpipe', 'pdnpipe') as $k) {
			if (!empty($r[$k]) && isset($removed[$r[$k]])) {
				$warn[] = scg_t('Rule "%s" refers to the removed queue %s.', ($r['descr'] ?? '?'), $r[$k]);
			}
		}
	}
	return array_unique($warn);
}

/*
 * URL-Tabellen (Spieleanbieter-Netze) vorab laden. pfSense ueberspringt Regeln
 * mit leerem oder nicht ladbarem URL-Table-Alias und meldet das bei jedem
 * Filter-Reload. Solche ASNs bekommen deshalb keine Regeln, mit Hinweis.
 */
function scg_prefetch_urltables(&$plan, &$msgs) {
	if (!function_exists('process_alias_urltable')) {
		return;
	}
	foreach ($plan['aliases'] as $an => $ad) {
		if (!isset($ad['url'])) {
			continue;
		}
		process_alias_urltable($an, 'urltable', $ad['url'], 1, true);
		$n = 0;
		$f = "/var/db/aliastables/{$an}.txt";
		if (is_readable($f)) {
			foreach (file($f, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES) as $l) {
				$l = trim($l);
				if ($l !== '' && $l[0] !== '#' && (is_subnet($l) || is_ipaddr($l))) {
					$n++;
				}
			}
		}
		if ($n == 0) {
			unset($plan['aliases'][$an]);
			$plan['rules'] = array_values(array_filter($plan['rules'], function($r) use ($an) {
				return ($r['destination']['address'] ?? '') !== $an;
			}));
			$msgs[] = scg_t('The address list for %s (%s) is empty or could not be loaded. No rules were created for it.', $an, $ad['url']);
		}
	}
}

function scg_apply($s, &$errors, &$msgs) {
	$plan = scg_build_plan($s);
	if (empty($plan['queues'])) {
		$errors[] = scg_t('No interface enabled - nothing to generate.');
		return false;
	}
	scg_prefetch_urltables($plan, $msgs);
	foreach ($plan['notes'] as $n) {
		$msgs[] = $n;
	}
	$pipes = scg_cfg_get('dnshaper/queue', array());
	$rules = scg_cfg_get('filter/rule', array());
	if (!is_array($pipes)) {
		$pipes = array();
	}
	if (!is_array($rules)) {
		$rules = array();
	}
	$oldoff = scg_current_sep_offset($rules);
	$oldtrackers = scg_generated_rule_trackers($rules);
	$oldnums = array();
	scg_strip($pipes, $rules, $oldnums);

	list($names, $usednums) = scg_dummynet_names($pipes);
	$pipeidx = array();
	$usedpipes = array();
	foreach ($plan['queues'] as $q) {
		$usedpipes[$q['pipe']] = true;
	}
	foreach ($pipes as $i => $p) {
		if (is_array($p) && isset($p['name'])) {
			$pipeidx[$p['name']] = $i;
		}
		/* Nur fuer Limiter warnen, die tatsaechlich verwendet werden */
		if (is_array($p) && isset($p['name'], $usedpipes[$p['name']]) && isset($p['sched']) &&
		    stripos($p['sched'], 'wf2q') === false && stripos($p['sched'], 'qfq') === false) {
			$msgs[] = scg_t('Note: limiter %s uses %s. Child queues need WF2Q+ (or QFQ), otherwise the weights have no effect.', $p['name'], $p['sched']);
		}
	}

	/* Nummern: bisherige Nummer je Name wiederverwenden, sonst naechste freie */
	foreach ($oldnums as $n => $num) {
		unset($usednums[$num]);
	}
	$reserved = array();
	foreach ($plan['queues'] as $q) {
		if (isset($oldnums[$q['name']]) && $oldnums[$q['name']] > 0 && !isset($usednums[$oldnums[$q['name']]])) {
			$reserved[$q['name']] = $oldnums[$q['name']];
			$usednums[$oldnums[$q['name']]] = true;
		}
	}
	$next = 1;
	foreach ($plan['queues'] as $q) {
		if (!isset($pipeidx[$q['pipe']])) {
			$errors[] = scg_t('Limiter %s does not exist.', $q['pipe']);
			return false;
		}
		if (isset($names[strtolower($q['name'])])) {
			$errors[] = scg_t('The name %s is already used by a limiter or queue.', $q['name']);
			return false;
		}
		$names[strtolower($q['name'])] = true;
		if (isset($reserved[$q['name']])) {
			$num = $reserved[$q['name']];
		} else {
			while (isset($usednums[$next])) {
				$next++;
			}
			$num = $next;
			$usednums[$num] = true;
		}
		$entry = array(
			'name' => $q['name'],
			'number' => (string)$num,
			'qlimit' => (string)$q['qlimit'],
			'weight' => (string)$q['weight'],
			'buckets' => '',
			'mask' => 'none',
			'maskbits' => '',
			'maskbitsv6' => '',
			'description' => $q['description'],
			'enabled' => 'on',
			'aqm' => $q['aqm'],
			'ecn' => $q['ecn'],
		);
		if ($q['aqm'] == 'codel') {
			$entry['param_codel_target'] = '5';
			$entry['param_codel_interval'] = '100';
		}
		$pi = $pipeidx[$q['pipe']];
		if (!isset($pipes[$pi]['queue']) || !is_array($pipes[$pi]['queue'])) {
			$pipes[$pi]['queue'] = array();
		}
		$pipes[$pi]['queue'][] = $entry;
	}

	/* Tracker-IDs eindeutig vergeben */
	$trackers = array();
	foreach ($rules as $r) {
		if (isset($r['tracker'])) {
			$trackers[$r['tracker']] = true;
		}
	}
	$t = (int)microtime(true);
	$now = time();
	$user = 'ShaperClassGen';
	if (isset($_SESSION['Username'])) {
		$user = $_SESSION['Username'] . '@ShaperClassGen';
	}
	/* trackers of the previous generated rules are never handed to a different rule */
	$retired = array();
	foreach ($oldtrackers as $list) {
		foreach ($list as $o) {
			$retired[$o['tracker']] = true;
		}
	}
	$new = array();
	foreach ($plan['rules'] as $r) {
		/*
		 * Unchanged rules keep their tracker and creation time, so tools that
		 * identify rules by tracker (rule statistics, Home Assistant switches)
		 * don't see a new rule on every apply.
		 */
		$id = scg_rule_identity($r);
		$old = null;
		while (!empty($oldtrackers[$id])) {
			$cand = array_shift($oldtrackers[$id]);
			if (!isset($trackers[$cand['tracker']])) {
				$old = $cand;
				break;
			}
		}
		if ($old !== null) {
			$trackers[$old['tracker']] = true;
			$r['tracker'] = $old['tracker'];
			$r['created'] = $old['created'] ?: array('time' => $now, 'username' => $user);
		} else {
			while (isset($trackers[$t]) || isset($retired[$t])) {
				$t++;
			}
			$trackers[$t] = true;
			$r['tracker'] = (string)$t;
			$r['created'] = array('time' => $now, 'username' => $user);
		}
		$new[] = $r;
	}
	/*
	 * Generierte Match-Regeln an den Anfang setzen, damit sie vor allen
	 * Quick-Regeln (pass/block) ausgewertet werden. Trenner werden mitgezogen.
	 */
	$rules = array_merge($new, $rules);

	/* Alias der lokalen Netze */
	$aliases = scg_cfg_get('aliases/alias', array());
	if (!is_array($aliases)) {
		$aliases = array();
	}
	$nets = preg_split('/\s+/', trim($s['localnets']));
	$alias = array(
		'name' => $s['aliasname'],
		'type' => 'network',
		'address' => implode(' ', $nets),
		'descr' => SCG_QTAG . ' Local networks (not shaped)',
		'detail' => implode('||', array_fill(0, count($nets), 'lokal')),
	);
	$found = false;
	foreach ($aliases as $i => $al) {
		if (is_array($al) && ($al['name'] ?? '') === $s['aliasname']) {
			$aliases[$i] = $alias;
			$found = true;
		}
	}
	if (!$found) {
		$aliases[] = $alias;
	}
	/* Spieleanbieter-Netze als URL Table (IPs), taeglich aktualisiert; nicht mehr benoetigte eigene entfernen */
	foreach ($aliases as $i => $al) {
		if (is_array($al) && (strpos($al['name'] ?? '', 'SCG_AS') === 0 || ($al['name'] ?? '') === 'SCG_GAMESERVERS') &&
		    strpos($al['descr'] ?? '', SCG_QTAG) === 0 && !isset($plan['aliases'][$al['name']])) {
			unset($aliases[$i]);
		}
	}
	foreach ($plan['aliases'] as $an => $ad) {
		if (isset($ad['address'])) {
			$n = count(explode(' ', $ad['address']));
			$entry = array('name' => $an, 'type' => 'network', 'address' => $ad['address'], 'descr' => $ad['descr'],
				'detail' => implode('||', array_fill(0, $n, 'game server')));
		} else {
			$entry = array('name' => $an, 'type' => 'urltable', 'url' => $ad['url'], 'updatefreq' => '1',
				'address' => '', 'descr' => $ad['descr'], 'detail' => '');
		}
		$done = false;
		foreach ($aliases as $i => $al) {
			if (is_array($al) && ($al['name'] ?? '') === $an) {
				if (strpos($al['descr'] ?? '', SCG_QTAG) !== 0) {
					$errors[] = scg_t('An alias "%s" already exists and was not created by this package.', $an);
					return false;
				}
				$aliases[$i] = $entry;
				$done = true;
			}
		}
		if (!$done) {
			$aliases[] = $entry;
		}
	}

	$removed = array();
	foreach ($oldnums as $n => $num) {
		$removed[$n] = $num;
	}
	foreach ($plan['queues'] as $q) {
		unset($removed[$q['name']]);
	}
	foreach (scg_dangling_refs($rules, $removed) as $w) {
		$msgs[] = $w;
	}

	scg_cfg_set('dnshaper/queue', array_values($pipes));
	scg_cfg_set('filter/rule', array_values($rules));
	scg_cfg_set('aliases/alias', array_values($aliases));
	scg_adjust_separators($oldoff, count($new));
	$s['sepshift'] = true;
	scg_store_settings($s);
	write_config("ShaperClassGen: " . count($plan['queues']) . " Queues und " . count($new) . " Regeln erzeugt");

	scg_dnctl_delete($removed);
	/* Synchron laden, damit die Queues danach geprueft werden koennen */
	if (function_exists('filter_configure_sync')) {
		filter_configure_sync();
	} else {
		filter_configure();
	}
	if (function_exists('clear_subsystem_dirty')) {
		clear_subsystem_dirty('shaper');
		clear_subsystem_dirty('filter');
		clear_subsystem_dirty('aliases');
	}
	$msgs[] = scg_t('%d queues and %d floating match rules created. Firewall is being reloaded.', count($plan['queues']), count($new));
	/* Geaenderte Queues verlieren im laufenden Betrieb ihr AQM (CoDel): Limiter neu anlegen */
	$rc = scg_dn_recreate();
	if ($rc !== null && $rc[0] > 0) {
		$msgs[] = $rc[1] == 0
			? scg_t('CoDel re-activated on %d queues (limiters re-created).', $rc[0])
			: scg_t('CoDel is still not active on %d queues. See Status > Limiter Live.', $rc[1]);
	}
	if ($removed) {
		/* Bestehende Verbindungen behalten ihre alte Queue-Nummer */
		$msgs[] = scg_t('%d queues were renamed or removed. Reset the firewall states (Diagnostics > States > Reset States) so that existing connections use the new queues.', count($removed));
	}
	return true;
}

function scg_remove_all(&$msgs) {
	$pipes = scg_cfg_get('dnshaper/queue', array());
	$rules = scg_cfg_get('filter/rule', array());
	if (!is_array($pipes)) {
		$pipes = array();
	}
	if (!is_array($rules)) {
		$rules = array();
	}
	$before = count($rules);
	$oldoff = scg_current_sep_offset($rules);
	$oldtrackers = scg_generated_rule_trackers($rules);
	$oldnums = array();
	scg_strip($pipes, $rules, $oldnums);
	$nrules = $before - count($rules);
	foreach (scg_dangling_refs($rules, $oldnums) as $w) {
		$msgs[] = $w;
	}
	$aliases = scg_cfg_get('aliases/alias', array());
	$keep = array();
	$refs = array();
	foreach ($rules as $r) {
		foreach (array('source', 'destination') as $k) {
			if (isset($r[$k]['address'])) {
				$refs[$r[$k]['address']] = true;
			}
		}
	}
	foreach ((array)$aliases as $al) {
		/* Eigene Aliase entfernen, solange keine fremde Regel sie noch nutzt */
		if (is_array($al) && strpos($al['descr'] ?? '', SCG_QTAG) === 0 && !isset($refs[$al['name'] ?? ''])) {
			continue;
		}
		$keep[] = $al;
	}
	scg_cfg_set('dnshaper/queue', array_values($pipes));
	scg_cfg_set('filter/rule', array_values($rules));
	scg_cfg_set('aliases/alias', $keep);
	scg_adjust_separators($oldoff, 0);
	$st = scg_load_settings();
	$st['sepshift'] = true;
	scg_store_settings($st);
	write_config("ShaperClassGen: generierte Objekte entfernt");
	scg_dnctl_delete($oldnums);
	filter_configure();
	$msgs[] = scg_t('%d queues and %d rules removed.', count($oldnums), $nrules);
	return true;
}

/* ------------------------------------------------------------------ */
/* Live-Status der Limiter (dnctl)                                     */
/* ------------------------------------------------------------------ */

/* Bandbreite aus "950.000 Mbit/s" in bit/s */
function scg_dn_bw($num, $unit) {
	$f = array('bit' => 1, 'Kbit' => 1000, 'Mbit' => 1000000, 'Gbit' => 1000000000);
	return (int)round(floatval($num) * ($f[$unit] ?? 1));
}

/*
 * Bucket-Zeile: "  0 ip  0.0.0.0/0  0.0.0.0/0  Tot_pkt Tot_bytes Pkt Byte Drp"
 * Die letzten fuenf Zahlen sind die Zaehler. Funktioniert auch fuer IPv6-Zeilen
 * und Masken mit mehreren Buckets.
 */
function scg_dn_bucket($line) {
	if (preg_match('/^\s*\d+\s+\S+\s.*\s(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*$/', $line, $m)) {
		return array('pkts' => (float)$m[1], 'bytes' => (float)$m[2], 'drops' => (float)$m[5]);
	}
	return null;
}

/*
 * AQM-Zustand einer Queue aus dem Ende ihrer Zeile in "dnctl queue show"
 * (sbin/ipfw/dummynet.c, print_flowset_parms): " AQM CoDel ..." = aktiv,
 * "droptail" = kein AQM, leer = AQM-Flag gesetzt, aber im Kernel nicht
 * angehaengt ("flowset busy" beim Konfigurieren) -> laeuft als Tail Drop.
 * Bei RED folgt "RED w_q ..." in der naechsten Zeile.
 */
function scg_dn_aqm_state($line) {
	if (preg_match('/\bAQM\s+(?:type\s+)?([A-Za-z_]+)/', $line, $m)) {
		return strtolower($m[1]);
	}
	if (preg_match('/\bdroptail\s*$/', $line)) {
		return 'droptail';
	}
	return 'missing';
}

/* "dnctl queue show" -> Child-Queues mit summierten Zaehlern */
function scg_dn_parse_queues($lines) {
	$q = array();
	$cur = null;
	foreach ($lines as $l) {
		if (preg_match('/^q(\d+)\s+\d+\s+sl\.\s+(\d+)\s+flows.*?\bsched\s+(\d+)\s+weight\s+(\d+)/', $l, $m)) {
			$cur = intval($m[1]);
			$q[$cur] = array('flows' => intval($m[2]), 'sched' => intval($m[3]), 'weight' => intval($m[4]),
				'aqm' => scg_dn_aqm_state($l), 'pkts' => 0, 'bytes' => 0, 'drops' => 0);
			continue;
		}
		if ($cur !== null && $q[$cur]['aqm'] === 'missing' && preg_match('/^\s+G?\s?RED\s+w_q/', $l)) {
			$q[$cur]['aqm'] = 'red';
			continue;
		}
		if ($cur !== null && ($b = scg_dn_bucket($l)) !== null) {
			$q[$cur]['pkts'] += $b['pkts'];
			$q[$cur]['bytes'] += $b['bytes'];
			$q[$cur]['drops'] += $b['drops'];
		}
	}
	return $q;
}

/* "dnctl sched show" bzw. "dnctl pipe show" -> Limiter mit Bandbreite, Typ und Summen */
function scg_dn_parse_pipes($lines) {
	$p = array();
	$cur = null;
	foreach ($lines as $l) {
		if (preg_match('/^(\d{5}):\s+(?:([\d.]+)\s+(bit|Kbit|Mbit|Gbit)\/s|unlimited)/', $l, $m)) {
			$cur = intval($m[1]);
			$p[$cur] = array('bw' => isset($m[2]) && $m[2] !== '' ? scg_dn_bw($m[2], $m[3]) : 0,
				'type' => '', 'pkts' => 0, 'bytes' => 0, 'drops' => 0, 'lines' => 0);
			continue;
		}
		if ($cur === null) {
			continue;
		}
		if (preg_match('/\bsched\s+\d+\s+type\s+(\S+)/', $l, $m)) {
			$p[$cur]['type'] = $m[1];
			continue;
		}
		if (($b = scg_dn_bucket($l)) !== null) {
			$p[$cur]['pkts'] += $b['pkts'];
			$p[$cur]['bytes'] += $b['bytes'];
			$p[$cur]['drops'] += $b['drops'];
			$p[$cur]['lines']++;
		}
	}
	return $p;
}

/*
 * Zuordnung dummynet-Nummer -> Name.
 * pfSense vergibt die Nummern beim Laden neu (read_dummynet_config); die
 * Nummern in config.xml gelten nicht. Darum werden die Objekte von pfSense
 * selbst abgefragt. Fallback: gleiche Logik nachgebildet (Pipes in
 * Konfigurationsreihenfolge, Queues fortlaufend je Limiter).
 */
function scg_dn_names() {
	$pname = array();
	$qname = array();
	$porder = array();
	global $dummynet_pipe_list;
	if (function_exists('read_dummynet_config')) {
		try {
			read_dummynet_config();
			if (is_array($dummynet_pipe_list) && count($dummynet_pipe_list) > 0) {
				foreach ($dummynet_pipe_list as $pipe) {
					$pn = intval($pipe->GetNumber());
					$pname[$pn] = array('name' => $pipe->GetQname(), 'descr' => (string)$pipe->GetDescription());
					$porder[] = $pn;
					$i = 0;
					foreach ((array)$pipe->subqueues as $q) {
						$qname[intval($q->GetNumber())] = array('name' => $q->GetQname(),
							'descr' => (string)$q->GetDescription(), 'pipe' => $pn, 'order' => $i++);
					}
				}
				return array($pname, $qname, $porder);
			}
		} catch (Throwable $e) {
			$pname = $qname = $porder = array();
		}
	}
	$pnext = 1;
	$qnext = 1;
	foreach ((array)scg_cfg_get('dnshaper/queue', array()) as $pc) {
		if (!is_array($pc) || empty($pc['name'])) {
			continue;
		}
		$pn = isset($pc['number']) && $pc['number'] !== '' ? intval($pc['number']) : $pnext;
		$pnext = max($pnext, $pn) + 1;
		$pname[$pn] = array('name' => $pc['name'], 'descr' => $pc['description'] ?? '');
		$porder[] = $pn;
		$i = 0;
		foreach ((array)($pc['queue'] ?? array()) as $qc) {
			if (is_array($qc) && !empty($qc['name'])) {
				$qname[$qnext++] = array('name' => $qc['name'], 'descr' => $qc['description'] ?? '', 'pipe' => $pn, 'order' => $i++);
			}
		}
	}
	return array($pname, $qname, $porder);
}

/*
 * Limiter neu anlegen, deren Queues AQM (CoDel) verloren haben.
 *
 * Der Kernel lehnt AQM ab, sobald der Scheduler eines Limiters eine Instanz
 * hat, also sobald Verkehr durchlief ("config_aqm: flowset busy"). Bei jeder
 * Aenderung einer Queue im laufenden Betrieb faellt sie dadurch auf Tail Drop
 * zurueck. Abhilfe: betroffene Limiter samt Queues loeschen und die von
 * pfSense erzeugten Regeln (/tmp/rules.limiter) sofort neu laden; die Queues
 * haengen dann an einem frischen Scheduler. Laufende Bandbreiten (Autorate,
 * Speedtest-Drosselung) werden danach wiederhergestellt. Die Nummern bleiben
 * gleich, Firewall-States behalten ihre Queue.
 * Liefert array(betroffen vorher, betroffen nachher) oder null ohne Regeldatei.
 */
function scg_dn_recreate($rulesfile = SCG_DN_RULES) {
	$rules = @file_get_contents($rulesfile);
	if ($rules === false || trim($rules) === '') {
		return null;
	}
	$o = array();
	exec('/sbin/dnctl queue show 2>/dev/null', $o);
	$queues = scg_dn_parse_queues($o);
	$pipes = array();
	$before = 0;
	foreach ($queues as $q) {
		if ($q['aqm'] === 'missing') {
			$pipes[$q['sched']] = true;
			$before++;
		}
	}
	if (!$pipes) {
		return array(0, 0);
	}
	$o = array();
	exec('/sbin/dnctl pipe show 2>/dev/null', $o);
	$bw = array();
	foreach (scg_dn_parse_pipes($o) as $num => $p) {
		$bw[$num] = $p['bw'];
	}
	/* Queues zuerst laden: ohne Scheduler bekommen sie ihr AQM ohne "flowset busy"
	   und werden beim Anlegen des Schedulers angehaengt. Danach pipe/sched mit der
	   laufenden Bandbreite. Andere Limiter bleiben unberuehrt. */
	$qlines = array();
	$plines = array();
	foreach (preg_split('/\r?\n/', $rules) as $line) {
		$line = trim($line);
		if (preg_match('/^queue\s+\d+\s+config\s+pipe\s+(\d+)\b/', $line, $m) && isset($pipes[(int)$m[1]])) {
			$qlines[] = $line;
		} elseif (preg_match('/^(pipe|sched)\s+(\d+)\s+config\b/', $line, $m) && isset($pipes[(int)$m[2]])) {
			$pn = (int)$m[2];
			if ($m[1] === 'pipe' && !empty($bw[$pn])) {
				$kbit = max(1, (int)round($bw[$pn] / 1000));
				$line = preg_match('/\sbw\s+\S+/', $line) ? preg_replace('/\sbw\s+\S+/', " bw {$kbit}Kb", $line, 1) : $line . " bw {$kbit}Kb";
			}
			$plines[] = $line;
		}
	}
	if (!$plines) {
		return array($before, $before);
	}
	foreach ($queues as $qn => $q) {
		if (isset($pipes[$q['sched']])) {
			mwexec('/sbin/dnctl queue delete ' . intval($qn), true);
		}
	}
	foreach (array_keys($pipes) as $pn) {
		mwexec('/sbin/dnctl pipe delete ' . intval($pn), true);
	}
	$qfile = $rulesfile . '.scg_q';
	$pfile = $rulesfile . '.scg_p';
	file_put_contents($qfile, implode("\n", $qlines) . "\n");
	file_put_contents($pfile, implode("\n", $plines) . "\n");
	if ($qlines) {
		mwexec('/sbin/dnctl ' . escapeshellarg($qfile), true);
	}
	mwexec('/sbin/dnctl ' . escapeshellarg($pfile), true);
	@unlink($qfile);
	@unlink($pfile);
	$o = array();
	exec('/sbin/dnctl queue show 2>/dev/null', $o);
	$after = 0;
	foreach (scg_dn_parse_queues($o) as $q) {
		if ($q['aqm'] === 'missing') {
			$after++;
		}
	}
	return array($before, $after);
}

/* dummynet-Zaehler bleiben nur stabil, wenn leere Queues nicht geloescht werden */
function scg_dn_expire() {
	$v = trim((string)shell_exec('/sbin/sysctl -n net.inet.ip.dummynet.expire 2>/dev/null'));
	return $v === '' ? null : intval($v);
}

/* Setzt net.inet.ip.dummynet.expire=0 sofort und dauerhaft als System Tunable */
function scg_dn_fix_expire() {
	$items = scg_cfg_get('sysctl/item', array());
	if (!is_array($items)) {
		$items = array();
	}
	$found = false;
	foreach ($items as $i => $it) {
		if (is_array($it) && ($it['tunable'] ?? '') == 'net.inet.ip.dummynet.expire') {
			$items[$i]['value'] = '0';
			$found = true;
		}
	}
	if (!$found) {
		$items[] = array('tunable' => 'net.inet.ip.dummynet.expire', 'value' => '0',
			'descr' => 'Keep empty limiter queues (stable counters for Limiter Live)');
	}
	scg_cfg_set('sysctl/item', array_values($items));
	write_config("ShaperClassGen: net.inet.ip.dummynet.expire=0");
	mwexec("/sbin/sysctl net.inet.ip.dummynet.expire=0");
}

/*
 * Momentaufnahme aller Limiter und Queues mit Namen aus der Konfiguration.
 * $raw kann Testdaten enthalten (array('queue'=>..., 'sched'=>..., 'pipe'=>...)).
 */
function scg_dn_snapshot($raw = null) {
	if ($raw === null) {
		$raw = array();
		foreach (array('queue', 'sched', 'pipe') as $k) {
			$out = array();
			exec("/sbin/dnctl {$k} show 2>/dev/null", $out);
			$raw[$k] = $out;
		}
	}
	$queues = scg_dn_parse_queues($raw['queue']);
	$sched = scg_dn_parse_pipes($raw['sched']);
	$pipe = scg_dn_parse_pipes($raw['pipe']);

	/* Nummer -> Name, so wie pfSense die Limiter tatsaechlich geladen hat */
	list($pname, $qname, $porder) = scg_dn_names();

	$res = array('t' => microtime(true), 'pipes' => array());
	$nums = array_unique(array_merge($porder, array_keys($sched), array_keys($pipe)));
	foreach ($nums as $n) {
		$s = $sched[$n] ?? null;
		$pp = $pipe[$n] ?? null;
		/* Summen bevorzugt aus dem Scheduler, sonst aus der Pipe (FIFO/FQ ohne Child-Queues) */
		$src = ($s && $s['lines'] > 0) ? $s : (($pp && $pp['lines'] > 0) ? $pp : ($s ?: $pp));
		$res['pipes'][$n] = array(
			'num' => $n,
			'name' => $pname[$n]['name'] ?? "Pipe {$n}",
			'descr' => $pname[$n]['descr'] ?? '',
			'loaded' => ($s !== null || $pp !== null),
			'bw' => $s['bw'] ?? ($pp['bw'] ?? 0),
			'type' => $s['type'] ?? '',
			'pkts' => $src['pkts'] ?? 0,
			'bytes' => $src['bytes'] ?? 0,
			'drops' => $src['drops'] ?? 0,
			'queues' => array(),
		);
	}
	/* Queues den Limitern zuordnen, Reihenfolge wie in der Konfiguration */
	$qs = array();
	foreach ($queues as $qn => $q) {
		$meta = $qname[$qn] ?? array('name' => "Queue {$qn}", 'descr' => '', 'pipe' => $q['sched'], 'order' => 100000 + $qn);
		$pn = $q['sched'];
		$qs[$pn][] = array('num' => $qn, 'name' => $meta['name'], 'descr' => $meta['descr'], 'weight' => $q['weight'],
			'flows' => $q['flows'], 'aqm' => $q['aqm'], 'pkts' => $q['pkts'], 'bytes' => $q['bytes'], 'drops' => $q['drops'], 'order' => $meta['order']);
	}
	foreach ($qs as $pn => $list) {
		usort($list, function ($a, $b) { return $a['order'] <=> $b['order']; });
		if (!isset($res['pipes'][$pn])) {
			$res['pipes'][$pn] = array('num' => $pn, 'name' => "Pipe {$pn}", 'descr' => '', 'loaded' => true, 'bw' => 0,
				'type' => '', 'pkts' => 0, 'bytes' => 0, 'drops' => 0, 'queues' => array());
		}
		$res['pipes'][$pn]['queues'] = $list;
	}
	$res['pipes'] = array_values($res['pipes']);
	return $res;
}

/*
 * Hinweis zur richtigen Limiter-Bandbreite, je Anschlussart.
 * $open = true: aufgeklappt anzeigen.
 */
function scg_limiter_hint_html($open = false) {
	$row = function ($a, $b, $c, $d, $e) {
		return '<tr><td>' . $a . '</td><td>' . $b . '</td><td>' . $c . '</td><td>' . $d . '</td><td>' . $e . '</td></tr>';
	};
	return '<details' . ($open ? ' open' : '') . ' style="margin:8px 0">' .
	'<summary><b>' . scg_t('Which bandwidth belongs in the limiter?') . '</b></summary>' .
	'<p style="margin-top:6px">' . scg_t('The limiter must be <b>below</b> the actually usable rate, otherwise traffic queues at the provider and the weights have no effect. The nominal rate of the plan includes protocol overhead and is often not delivered constantly. Therefore enter the <b>reduced value</b> in the limiter; Autorate uses it as maximum and base.') . '</p>' .
	'<table class="table table-condensed" style="width:auto">' .
	'<thead><tr><th>' . scg_t('Connection') . '</th><th>' . scg_t('Reference') . '</th><th>' . scg_t('Download limiter') . '</th><th>' . scg_t('Upload limiter') . '</th><th>' . scg_t('Example') . '</th></tr></thead><tbody>' .
	$row(scg_t('Cable (DOCSIS / DVB-C)'), scg_t('Plan'), '80–85 %', '85–90 %', '1000/50 &rarr; 800/45') .
	$row('VDSL / Vectoring / Supervectoring', scg_t('Sync rate in the modem'), '90–93 %', '90 %', '250/40 &rarr; 230/36') .
	$row('ADSL / ADSL2+', scg_t('Sync rate in the modem'), '85–88 %', '85 %', '16/1 &rarr; 14/0.85') .
	$row(scg_t('Fiber (FTTH)'), scg_t('Plan'), '92–95 %', '92–95 %', '1000/500 &rarr; 930/470') .
	'</tbody></table>' .
	'<ul style="padding-left:18px">' .
	'<li>' . scg_t('<b>Cable:</b> The segment is shared with the neighbors and is often full in the evening; therefore keep more margin and enable Autorate.') . '</li>' .
	'<li>' . scg_t('<b>DSL:</b> The sync rate from the modem status is what counts, not the plan. ADSL has particularly high overhead because of ATM.') . '</li>' .
	'<li>' . scg_t('<b>Fine tuning:</b> Measure without shaping at a quiet time of day and set the limiter to about 90% of the measured value. Then check with downloads in two interfaces in Limiter Live: if the split matches the weights, the value is right; if both share about equally despite different weights, the limiter is still too high.') . '</li>' .
	'</ul><p class="help-block">' . scg_t('Guide values; they can differ depending on provider and line.') . '</p></details>';
}

/* ------------------------------------------------------------------ */
/* Interface-Zaehler                                                   */
/* ------------------------------------------------------------------ */

/* Bytes rein/raus eines realen Interfaces (z. B. igb0, igb1.20) */
function scg_if_bytes($real) {
	if ($real === '' || $real === null) {
		return null;
	}
	if (function_exists('pfSense_get_interface_stats')) {
		$x = @pfSense_get_interface_stats($real);
		if (is_array($x) && isset($x['inbytes'], $x['outbytes'])) {
			return array('in' => (float)$x['inbytes'], 'out' => (float)$x['outbytes']);
		}
	}
	/* Fallback: netstat; die letzten 8 Spalten sind
	 * Ipkts Ierrs Idrop Ibytes Opkts Oerrs Obytes Coll */
	$o = array();
	exec('/usr/bin/netstat -I ' . escapeshellarg($real) . ' -b -n 2>/dev/null', $o);
	foreach ($o as $l) {
		if (strpos($l, '<Link#') === false) {
			continue;
		}
		$t = preg_split('/\s+/', trim($l));
		$n = count($t);
		if ($n >= 9 && is_numeric($t[$n - 5]) && is_numeric($t[$n - 2])) {
			return array('in' => (float)$t[$n - 5], 'out' => (float)$t[$n - 2]);
		}
	}
	return null;
}

function scg_if_is_wan($if) {
	return (function_exists('interface_has_gateway') && interface_has_gateway($if)) ||
		(function_exists('interface_has_gatewayv6') && interface_has_gatewayv6($if));
}

/* Tunnel-Interfaces (VPN): ihr Verkehr laeuft zusaetzlich verschluesselt ueber das echte WAN */
function scg_if_is_tunnel($real) {
	return (bool)preg_match('/^(ovpn[cs]\d|wg\d|tun_wg|tun\d|tap\d|gif\d|gre\d|ipsec\d|enc\d|l2tp|vti)/', (string)$real);
}

/* 'wan' = physisches WAN, 'vpn' = Tunnel, 'lan' = internes Interface */
function scg_if_kind($if, $real) {
	if (scg_if_is_tunnel($real)) {
		return 'vpn';
	}
	return scg_if_is_wan($if) ? 'wan' : 'lan';
}

function scg_if_real($if) {
	return function_exists('get_real_interface') ? get_real_interface($if) : $if;
}

/* Zaehler aller zugewiesenen Interfaces */
function scg_if_counters() {
	$out = array();
	foreach (get_configured_interface_with_descr() as $if => $descr) {
		$real = scg_if_real($if);
		$st = scg_if_bytes($real);
		if ($st === null) {
			continue;
		}
		$kind = scg_if_kind($if, $real);
		$out[] = array('if' => $if, 'descr' => $descr, 'real' => $real, 'in' => $st['in'], 'out' => $st['out'],
			'kind' => $kind, 'wan' => ($kind === 'wan'));
	}
	return $out;
}

/* Nach Installation: Autorate-Dienst starten, falls aktiviert */
function shaperclassgen_install() {
	@chmod('/usr/local/etc/rc.d/scg_autorate.sh', 0755);
	mwexec_bg('/bin/sh /usr/local/etc/rc.d/scg_autorate.sh start >/dev/null 2>&1 </dev/null');
	/* Speedtest-Zeitplan wiederherstellen */
	if (file_exists('/usr/local/pkg/shaperclassgen_speedtest.inc')) {
		require_once('/usr/local/pkg/shaperclassgen_speedtest.inc');
		scg_st_install_cron(scg_st_settings());
	}
	return true;
}

/*
 * Wird bei Deinstallation aufgerufen. Autorate wird gestoppt und die
 * Limiter-Bandbreiten wiederhergestellt; erzeugte Queues/Regeln bleiben.
 */
function shaperclassgen_deinstall() {
	if (file_exists('/usr/local/etc/rc.d/scg_autorate.sh')) {
		mwexec('/bin/sh /usr/local/etc/rc.d/scg_autorate.sh stop >/dev/null 2>&1 </dev/null');
	}
	/* Speedtest-Zeitplan entfernen; Messwerte in /var/db bleiben erhalten */
	if (!function_exists('install_cron_job')) {
		require_once('services.inc');
	}
	if (function_exists('install_cron_job')) {
		install_cron_job('/usr/local/bin/php -f /usr/local/pkg/shaperclassgen_speedtest.php run', false);
	}
	@unlink('/var/tmp/scg_speedtest.bin');
	return true;
}
