<?php
$config = [
 'interfaces'=>[],
 'dnshaper'=>['queue'=>[
   ['name'=>'LimDown','number'=>'1','sched'=>'wf2q+','bandwidth'=>['item'=>[['bw'=>'950','bwscale'=>'Mb']]]],
   ['name'=>'LimUp','number'=>'2','sched'=>'wf2q+','bandwidth'=>['item'=>[['bw'=>'47','bwscale'=>'Mb']]],'queue'=>[['name'=>'manual','number'=>'1','weight'=>'10','description'=>'mine']]],
 ]],
 'filter'=>['rule'=>[['type'=>'pass','interface'=>'lan','tracker'=>'100','descr'=>'Default allow','source'=>['network'=>'lan'],'destination'=>['any'=>'']]]],
 'aliases'=>['alias'=>[]],
];
if (getenv("CFG")) $config = json_decode(file_get_contents(getenv("CFG")), true);
function config_get_path($p,$d=null){global $config;$x=$config;foreach(explode('/',$p) as $k){if(!is_array($x)||!array_key_exists($k,$x))return $d;$x=$x[$k];}return $x;}
function config_set_path($p,$v){global $config;$x=&$config;foreach(explode('/',$p) as $k){if(!isset($x[$k])||!is_array($x[$k]))$x[$k]=[];$x=&$x[$k];}$x=$v;}
function get_configured_interface_with_descr(){return ['wan'=>'WAN','lan'=>'LAN','opt1'=>'IOT','opt2'=>'GUEST','opt3'=>'DMZ','opt5'=>'VPN_PROVIDER'];}
function interface_has_gateway($i){return $i=='wan'||$i=='opt5';}
function is_ipaddr($a){return filter_var($a,FILTER_VALIDATE_IP)!==false;}
function is_subnet($a){return (bool)preg_match('#^[0-9a-f:.]+/\d+$#i',$a);}
function is_alias($a){return false;}
function write_config($m){echo "WRITE_CONFIG: $m\n";}
function filter_configure(){echo "FILTER_CONFIGURE\n";}
function print_input_errors($e){echo "ERRORS: ".implode(' | ',$e)."\n";}
function print_info_box($m,$t='info',$x=true){echo "INFO[$t]: $m\n";}
function display_top_tabs($t){ echo "<nav id=tabs>"; foreach ($t as $x) echo "<span>".($x[1]?"[":"").$x[0].($x[1]?"]":"")."</span> "; echo "</nav>"; }
$config['dnshaper']['queue'] = [
 ['name'=>'Backup_Down','number'=>'1','enabled'=>'on'],['name'=>'Backup_Up','number'=>'2','enabled'=>'on'],
 ['name'=>'LimDown','number'=>'3','enabled'=>'on','queue'=>[]],
 ['name'=>'LimUp','number'=>'4','enabled'=>'on','queue'=>[]],
];
$tags=['LAN','GUEST','IOT','DMZ']; $cls=['qACK','qVoIP','qGames','qOthersHigh','qDefault','qOthersLow','qP2P']; $n=1;
foreach ([2=>'D',3=>'U'] as $pi=>$d) foreach ($tags as $t) foreach ($cls as $c) $config['dnshaper']['queue'][$pi]['queue'][]=['name'=>"{$d}_{$t}_{$c}",'number'=>(string)$n++,'description'=>"[SCG] $t $c ".($d=='D'?'Download':'Upload')];
$config['installedpackages']['shaperclassgen']['settings'] = base64_encode(json_encode(['vlans'=>[
 ['if'=>'lan','enable'=>true,'tag'=>'LAN','factor'=>1.0],['if'=>'opt2','enable'=>true,'tag'=>'GUEST','factor'=>0.2],
 ['if'=>'opt1','enable'=>true,'tag'=>'IOT','factor'=>0.5],['if'=>'opt3','enable'=>true,'tag'=>'DMZ','factor'=>0.2]]]));
// Autorate settings for the test
$__s = json_decode(base64_decode($config['installedpackages']['shaperclassgen']['settings']), true);
$__s['autorate'] = ['enable'=>getenv('SCG_CONTROL')!=='0','monitor'=>true,'down'=>['pipe'=>'LimDown','min'=>250,'base'=>650,'max'=>800],'up'=>['pipe'=>'LimUp','min'=>15,'base'=>40,'max'=>47],
 'reflectors'=>'127.0.0.1 127.0.0.2','interval'=>200,'threshold'=>30,'decfactor'=>0.9,'incstep'=>2,'tick'=>500];
$config['installedpackages']['shaperclassgen']['settings'] = base64_encode(json_encode($__s));
$config['dnshaper']['queue'][2]['bandwidth']['item'][0] = ['bw'=>'800','bwscale'=>'Mb'];
$config['dnshaper']['queue'][3]['bandwidth']['item'][0] = ['bw'=>'47','bwscale'=>'Mb'];
function mwexec($c,$q=false){ $GLOBALS['mwexec_log'][] = $c; return 0; } function mwexec_bg($c){}
$config["dnshaper"]["queue"][0]["bandwidth"]["item"][0] = ["bw"=>"500","bwscale"=>"Kb"];
function get_real_interface($if) { return ['wan'=>'igb0','lan'=>'igb1','opt1'=>'igb1.10','opt2'=>'igb1.20','opt3'=>'igb1.30','opt5'=>'ovpnc1'][$if] ?? $if; }
function pfSense_get_interface_stats($r) { $t = microtime(true) - 1794200000;
  $rates = ['igb0'=>[700e6, 12e6], 'igb1'=>[4e6, 500e6], 'igb1.10'=>[1e6, 150e6], 'igb1.20'=>[6e6, 40e6], 'igb1.30'=>[2e6, 20e6], 'ovpnc1'=>[225e6, 0.8e6]];  // bit/s rein, raus
  return ['inbytes' => $rates[$r][0]/8*$t, 'outbytes' => $rates[$r][1]/8*$t]; }
$config['system']['language'] = getenv('SCG_LANG') ?: 'en_US';
