#!/bin/bash
# Serves the GUI pages with the stubs on http://127.0.0.1:8765/ (e.g. /status_limiters_live.php)
# Environment: SCG_LANG=de_DE for German, SCG_CONTROL=0 for Autorate off
T=$(cd "$(dirname "$0")" && pwd)
"$T/setup.sh" >/dev/null
cd "$T/.work" && exec php -S 127.0.0.1:8765
