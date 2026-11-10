<?php
global $priv_list;

$priv_list['page-firewall-shaperclassgen'] = array();
$priv_list['page-firewall-shaperclassgen']['name'] = "WebCfg - Firewall: Traffic Shaper: Class Generator";
$priv_list['page-firewall-shaperclassgen']['descr'] = "Allow access to the Shaper Class Generator page.";
$priv_list['page-firewall-shaperclassgen']['match'] = array();
$priv_list['page-firewall-shaperclassgen']['match'][] = "firewall_shaper_classgen.php*";

$priv_list['page-status-limiterslive'] = array();
$priv_list['page-status-limiterslive']['name'] = "WebCfg - Status: Limiters Live";
$priv_list['page-status-limiterslive']['descr'] = "Allow access to the Limiters Live status page.";
$priv_list['page-status-limiterslive']['match'] = array();
$priv_list['page-status-limiterslive']['match'][] = "status_limiters_live.php*";

$priv_list['page-firewall-shaperautorate'] = array();
$priv_list['page-firewall-shaperautorate']['name'] = "WebCfg - Firewall: Traffic Shaper: Autorate";
$priv_list['page-firewall-shaperautorate']['descr'] = "Allow access to the Autorate page.";
$priv_list['page-firewall-shaperautorate']['match'] = array();
$priv_list['page-firewall-shaperautorate']['match'][] = "firewall_shaper_autorate.php*";

$priv_list['page-status-shaperspeedtest'] = array();
$priv_list['page-status-shaperspeedtest']['name'] = "WebCfg - Status: Speedtest (ShaperClassGen)";
$priv_list['page-status-shaperspeedtest']['descr'] = "Allow access to the periodic speed test page.";
$priv_list['page-status-shaperspeedtest']['match'] = array();
$priv_list['page-status-shaperspeedtest']['match'][] = "status_speedtest.php*";
