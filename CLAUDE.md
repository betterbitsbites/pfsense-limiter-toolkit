# pfSense Limiter Toolkit – notes for development

pfSense package `pfSense-pkg-ShaperClassGen` (pfSense CE 2.7/2.8, FreeBSD,
PHP 8). Traffic shaping with **limiters** (dummynet, WF2Q+), not ALTQ.
User-facing documentation: `README.md` (English) and `README.de.md` (German).
History of changes: `CHANGELOG.md`.

## Rules

- **General, not tailored:** the package must work for any setup. No
  interface names, addresses, providers or measured values from a
  particular network in code, docs or tests.
- **Texts:** source texts are English. Every user-visible text goes through
  `scg_t('…')` in PHP or `t('…')` in page JavaScript; the German translation
  goes into `scg_dict_de()` in `shaperclassgen_i18n.inc`. The GUI follows
  the pfSense language setting. Literal `%` in a text with `scg_t()` is
  written `%%` and gets a dummy `''` argument. Run `tools/i18n_check.py`.
  Texts passed as variables (`scg_t($d['label'])`, class hints, scheme
  labels) are not found by the checker – add their translations by hand.
- **Generated floating match rules go to the top of the floating list**
  (users keep their pass/block quick rules below). Existing separators are
  shifted by the number of generated rules.
- Generated objects are marked: queue descriptions start with `[SCG]`,
  rule descriptions with `SCG Pipe U/D_<tag>_<class> …` (max 52 chars),
  aliases with `[SCG]` in the description. Only marked objects are ever
  replaced or removed.
- Limiters themselves: **Tail Drop**. CoDel on child queues is fine, but the
  kernel only attaches AQM while the limiter's scheduler has no instance
  (no traffic yet); a queue changed later keeps the AQM flag without AQM
  (`dnctl queue show` line ends empty). `scg_dn_recreate()` deletes the
  affected limiters and reloads `/tmp/rules.limiter`; called after apply and
  from Limiter Live.
  Autorate only changes limiters (pipes), never queues.
- Limiter Live: queues sorted by weight; interfaces grouped
  WAN → VPN → shaped → not shaped, by name; matrix by priority; "not shaped"
  sums always shown; red stays red (`#ff5c5c`) but readable.
- Commits: no `Claude-Session:` lines. Keep `Co-Authored-By` if used.
- Version number lives in three places: `SCG_VERSION` in
  `files/usr/local/pkg/shaperclassgen.inc`, `VERSION=` in `install.sh`, and
  the headings/install text in both READMEs. Add an entry to `CHANGELOG.md`.

## Files

| File | Purpose |
|---|---|
| `files/usr/local/pkg/shaperclassgen.inc` | Core: settings (base64 JSON in `installedpackages/shaperclassgen/settings`, backup `/conf/shaperclassgen.json`), class schemes, app/game/P2P lists, plan builder, apply/remove, dnctl parsing, interface helpers |
| `files/usr/local/pkg/shaperclassgen_autorate.inc` | Autorate service: pinger, control loop `SCGAutorate`, status `/var/run/scg_autorate.json`, minute history `/var/db/scg_autorate_history.jsonl` (48 h), learned base `/var/db/scg_autorate_baseline.json` |
| `files/usr/local/pkg/shaperclassgen_autorate.php` | CLI for the service: `run`, `restore`, `enabled` |
| `files/usr/local/etc/rc.d/scg_autorate.sh` | rc script, `daemon -f -r -R 5` |
| `files/usr/local/pkg/shaperclassgen_speedtest.inc/.php` | Speed test (curl to speed.cloudflare.com, cron), results `/var/db/scg_speedtest.jsonl` |
| `files/usr/local/pkg/shaperclassgen_i18n.inc` | `scg_lang()`, `scg_t()`, tabs, German dictionary |
| `files/usr/local/www/firewall_shaper_classgen.php` | Class Generator page |
| `files/usr/local/www/firewall_shaper_autorate.php` | Autorate settings page |
| `files/usr/local/www/status_limiters_live.php` | Limiter Live (polls `?getstats`, `?longhist`) |
| `files/usr/local/www/status_speedtest.php` | Speedtest page |
| `files/usr/local/pkg/shaperclassgen.xml`, `info.xml`, `priv.inc` | Package metadata, menus, privileges |
| `install.sh` / `uninstall.sh` | Builds a real pkg on pfSense (`pkg create`) and installs it |
| `tests/` | Stubs, fakes and smoke tests (see below) |
| `docs/images/` | README screenshots (polished GUI screenshots) |

## Design decisions

- **Limiters classify per state, not per packet.** No real TCP-ACK class is
  possible (only ALTQ can); the control class holds DNS, DoT, NTP, ICMP.
- **Direction:** match rules on the LAN-side interface, direction *in*;
  in pipe = upload queue, out pipe = download queue. The last matching match
  rule wins; later pass rules without pipes keep the assignment.
- **Structural changes need a state reset** (pfSense renumbers dummynet
  queues; states store numbers). Bandwidth changes apply live.
- **Class schemes:** `simple` (qRealtime 100, qInteractive 50, qDefault 20,
  qBulk 2; default for new installs) and `classic` (7 wizard classes,
  qACK renamed qControl). Settings without `scheme` are classic. Roles
  (`control`, `voip`, `games`, `interactive`, `default`, `low`, `p2p`) map to
  classes per scheme (`scg_role_class`).
- **Games:** pf cannot detect applications (Layer7 was removed in pfSense
  2.3). Options: real-time devices with "UDP only" (UDP except 443, so
  downloads/QUIC stay out), publisher networks by ASN as URL table aliases
  (UDP except 443, empty lists are skipped), vendor-documented ports,
  own game servers (alias `SCG_GAMESERVERS`, all protocols). Only ASNs that
  really belong to a publisher are presets; hosting companies are not.
- **Autorate:** ICMP to 2–3 reflectors; congestion = several of the last
  samples above baseline + threshold. Decrease to achieved × factor when
  load ≥ 50 %; if the line delivers far below the limit, cut the most used
  direction after 2 s once it reaches 2 % of the limit and 0.5 Mbit/s.
  Increase +2 %/tick under load; idle decays to the (learned) base.
  Throughput measured at the WAN interface (VPN tunnels excluded).
  Paused during speed tests (`/var/run/scg_autorate.pause` with expiry).
- **Latency per interface** (option `latprobe`): one match rule per shaped
  interface on the gateway interfaces, direction out, ICMP echoreq from
  `(self)`, own DSCP (pool 2, max 16) → `U/D_<tag>_<class>`; class selectable
  (`latclass`, default = interactive role via `scg_latprobe_class`). The service
  reads its channels from the *applied* rules (`scg_latprobe_channels`), not
  the settings. Each channel needs its own ICMP ID (pf keys ICMP states by
  ID; a shared ID would reuse the unmarked state without queue); IDs rotate
  every 5 min and on config changes.
- **Graphs:** download and upload differ by ~20× on asymmetric lines, so
  combined graphs show throughput in % of the Autorate maximum (right axis)
  and latency in ms (left axis); real values in the tooltip.
- **Speed test** measures the raw line from pfSense (not shaped). Throughput
  from the WAN counter after the TCP ramp-up; curl rate stored too. Runs
  only if the default route uses the selected WAN. Cloudflare needs a
  browser user agent and referer for large downloads (else HTTP 403).
- During `install.sh` the old package is removed first; pages answer 503 and
  CLI scripts exit quietly while library files are missing.

## Testing

```
tests/run.sh              # lint + smoke tests (class schemes, games, Autorate, speed test)
tests/serve.sh            # GUI pages with stubs on http://127.0.0.1:8765/
SCG_LANG=de_DE tests/serve.sh
tools/i18n_check.py       # missing German translations
```

`tests/setup.sh` copies the package into `tests/.work` and rewrites system
paths (`dnctl`, `ping`, `curl`, `route`, `/var/run`, `/var/db`) to the fakes
in `tests/fakes`. `tests/stubs/guiconfig.inc` provides a small pfSense
config (interfaces WAN, LAN, IOT, GUEST, DMZ, VPN) and helper functions.
Playwright with Chromium is useful for screenshots of the pages.

On a real pfSense: `sh install.sh` as root (tcsh: run `sh` first for
`$(…)`), then check Status > Limiter Live and the service with
`/usr/local/etc/rc.d/scg_autorate.sh status`.

## Open items and workflow

- Open items are GitHub issues in this repo. Work on one issue per
  session; reference it in commits (`Refs #4`) and close it with the last
  commit (`Fixes #4`) or via `gh issue close`. New ideas that come up while
  working become new issues instead of staying in the conversation.
- Before starting, update the local clone (`git pull`); others (other
  sessions, the GitHub web UI, pull requests) may have pushed.
- After a change: tests (`tests/run.sh`, `tools/i18n_check.py`), version
  bump, CHANGELOG entry, push to `main`. For a release, build
  `pfSense-pkg-ShaperClassGen-<version>.tar.gz` (files, install.sh,
  uninstall.sh, READMEs, LICENSE in a folder of the same name).
- Before changing files: restate the request in one or two sentences, name
  the issue it belongs to, and ask if anything is unclear. Never guess issue
  numbers – issues and pull requests share numbers; check `gh issue list` and
  `gh pr list` first.
- A maintainer may keep private notes about their own setup in a sibling
  directory, imported through a git-ignored `CLAUDE.local.md`. If such notes
  are loaded, they describe the setup the package is primarily tested on:
  take them into account, keep them up to date as described in their own
  `CLAUDE.md`, and never copy anything from them into this repository.
