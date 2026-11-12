<?php
/* Shared bootstrap for the CLI tests: loads stubs and test copies from tests/.work */
$W = getenv('SCG_W') ?: __DIR__ . '/../.work';
require_once("$W/guiconfig.inc");
require_once("$W/scg.inc");
require_once("$W/scg_ar.inc");
function check($cond, $msg) { echo ($cond ? "ok   " : "FAIL ") . $msg . "\n"; }
