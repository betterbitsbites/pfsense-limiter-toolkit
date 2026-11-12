# Tests

Runs the package code on a normal Linux machine with PHP 8, without pfSense.
`stubs/` replaces pfSense functions and config, `fakes/` replaces `dnctl`,
`ping`, `curl`, `route` and `sysctl`.

    tests/run.sh       # lint + smoke tests
    tests/serve.sh     # GUI pages on http://127.0.0.1:8765/ (SCG_LANG=de_DE for German)

`setup.sh` builds `tests/.work/` (ignored by git); run it again after code
changes (`run.sh` and `serve.sh` do this automatically).
