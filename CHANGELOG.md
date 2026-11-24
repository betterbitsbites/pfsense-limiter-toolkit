# Changelog

## 0.6.21
- Pinger diagnostics: a reply counts as "stray" only when it comes from one
  of the service's own Autorate targets and carries an ICMP ID the service
  gave that target (current or previous generation). Before, replies to
  other programs' pings (gateway monitoring, the reflector selection) were
  counted too and filled the per-target list with foreign addresses.

## 0.6.20
- Autorate pings get their own ICMP ID per target, renewed every 5 minutes
  (new pf states). Before, all Autorate pings ran under one ID for hours;
  the same reflectors lost about a quarter of their replies inside the
  service but almost none in a separate process with a fresh ID (issue #15).
- Replaced reflectors are stored in the selection file and skipped for
  7 days, also after a restart and in new selections; before, a restart
  brought them back from the old selection.
- Reflectors whose base latency is more than 30 ms above the median of the
  others are replaced (an overloaded host had drifted to a 210 ms base).

## 0.6.19
- Reflector selection, second stage: the best 12 candidates are pinged for
  20 s at the real rate (5/s each); only those with at most 5 % loss are
  kept (`longtest` in `/var/db/scg_ts_reflectors.json`). Three short rounds
  accepted hosts that lose up to a third of their replies (issue #15).
- Pinger diagnostics: failed sends are counted with their error code
  (`senderr` in the status) instead of later counting as loss; replies with
  the service's ICMP ID that match no request are counted as "stray"
  (per target in `pcount`).
- New CLI `shaperclassgen_autorate.php probe <ip> [ts|echo] [count]
  [interval ms]`: the service's pinger against one host in its own process,
  to tell network loss from loss inside the running service.
- Autorate page: redirect after Save, Restart and "Select reflectors now";
  reloading the page no longer submits the form again (which saved the
  settings and restarted the service).

## 0.6.18
- READMEs: credit for cake-autorate (OpenWrt, lynxthecat and contributors),
  which inspired Autorate (independent implementation for dummynet, no code
  taken), and for the timestamp reflector list by tievolu.

## 0.6.17
- Autorate, ICMP timestamp: a reflector is only replaced for loss when it
  loses clearly more than the other targets in the same check (> 10 % and
  more than twice the median loss of all Autorate targets, reflectors and
  echo targets, plus 5 points; at least 100 pings). With a fixed 10 % limit,
  general packet loss during downstream congestion replaced good reflectors
  and used up the spares (issue #15).
- Cut log: decreases decided by the window majority (no median available)
  are labelled `window` instead of `owd`.

## 0.6.16
- Autorate, ICMP timestamp: when the timestamps say that no direction is
  clearly congested (both median rises below half the threshold) although a
  few round-trip samples spiked, nothing is lowered any more (state "short
  spike, no direction clearly congested"). Before, the last direction was
  reused or the load rule applied, which lowered upload and download on
  short spikes. The last direction is now only held when timestamp data is
  missing (issue #15).
- Reflectors that lose more than 10 % of their replies (at least 100 pings
  since the last check) are replaced by a spare. Some hosts rate-limit
  timestamp replies; two such reflectors showed about 27 % loss and made up
  the whole "loss" of the latency panel.
- Latency per interface: in timestamp mode the WAN reference row shows the
  echo targets (which the probes ping) instead of the reflectors.
- Limiter Live shows the package version in the top right corner.

## 0.6.15
- Speed test: Autorate stays paused 6 s after the test. The modem buffer is
  still full of the test upload at that moment; Autorate took that for
  congestion, lowered the upload and dropped its learned base after every
  test (issue #15).
- Autorate, ICMP timestamp: in heavy upload congestion replies come late or
  not at all, so direction data was missing for a few seconds and Autorate
  fell back to "lower every loaded direction", cutting the download too.
  The last direction found from timestamps is now held for up to 10 s
  (label "held" in Limiter Live).
- Pings count as lost after 4 s instead of 2 s (upload congestion on cable
  lines can exceed 2 s).
- Diagnostics: sent/answered/late counts per target, separately for
  Autorate pings and latency probes (`pcount`; Limiter Live shows the loss
  since service start per target), and the last 20 decreases with their
  reason (`cuts`: owd, held, load, lowload) in `/var/run/scg_autorate.json`.

## 0.6.14
- Autorate, ICMP timestamp: steadier direction decision (issue #15). The
  direction is now taken from the median over the reflectors of each
  reflector's average in the last second, instead of a vote over the last
  6 single samples; one outlier reflector no longer turns "upload" into
  "both directions". A direction only counts as congested when its own rise
  is at least half the threshold (and at least a quarter of the total).
- Pinger: 1 MB receive buffer on the raw socket, so replies are not dropped
  while the service is briefly busy. Replies that arrive after the 2 s
  timeout (already counted as lost) are counted separately for Autorate
  pings and latency probes (`late` in the status, shown in Limiter Live) to
  tell local delays from real loss.

## 0.6.13
- Autorate service: config changes were never picked up on pfSense 2.9
  (issue #14). The service re-read the configuration with `parse_config()`,
  which no longer exists there; it skipped silently and kept the config it
  had at start. Latency probes then kept the DSCP values and limiter numbers
  of the old rules (wrong queues, implausible queue delays) and the table
  could stay empty after "Generate / Update" until the service was
  restarted. Now `config_read_file()` (pfSense 2.7+) is used, with
  `parse_config()` only as fallback on older versions; a failed re-read is
  logged and retried.
- Apply, "Remove generated objects" and the limiter re-create signal the
  service (`/var/run/scg_autorate.reload`): within one control tick it
  re-reads the configuration, maps the limiters again and gives the probes
  new ICMP IDs.
- Status: the last "reapplied after filter reload" event is kept with time,
  direction, kernel and expected bandwidth (`lastreset` in
  `/var/run/scg_autorate.json`) to see why the counter rises.

## 0.6.12
- Autorate: new ping type **ICMP timestamp** (issue #15). Timestamp replies
  (ICMP type 14) carry the reflector's receive and transmit time, so the
  delay is split into upload and download. The clock offset cancels out
  against a per-reflector upload baseline that follows clock drift while the
  line is clean. Congestion is still detected from the round-trip time; the
  direction that carries the rise is the one that is lowered, the other
  direction holds ("congestion in the other direction"). Without timestamp
  data the previous load rules apply.
- Reflectors from github.com/tievolu/timestamp-reflectors (downloaded by the
  pfSense weekly, filtered to the WAN country from the Cloudflare trace or a
  set country; sub-region or worldwide if the country has too few). The
  selection (`shaperclassgen_autorate.php select`, run in the background by
  the service, daily and when reflectors fail) tests 100 random entries three
  times and keeps the 4 with the lowest, stable RTT and a steady clock,
  preferably from different /16 networks, plus spares. Fixed reflectors can
  be entered instead.
- The echo targets keep running in timestamp mode as fallback (used for
  control when no reflector answers) and as the reference for the latency
  per interface, whose probes stay echo pings.
- Limiter Live shows upload and download delay rise per reflector and in the
  10-second figures, and which direction is congested.

## 0.6.11
- Releases also contain `pfSense-2.9.0-pkg-ShaperClassGen.pkg` (same
  FreeBSD 16 base as 2.9.1).

## 0.6.10
- Tested on pfSense CE 2.9.1 (FreeBSD 16): class generator, CoDel on the
  queues, Autorate, Limiter Live and speed test work. Releases now also
  contain `pfSense-2.9.1-pkg-ShaperClassGen.pkg`.

## 0.6.9
- Releases contain a ready-made package per pfSense version
  (`pfSense-<version>-pkg-ShaperClassGen.pkg`, first: CE 2.8.1), built by a
  GitHub Actions workflow on FreeBSD when a release is published. Install
  with `pkg-static add <URL>`; no download and build on the box needed.
- `install.sh --build <ABI> <file>` only builds the package (used by the
  workflow); `tools/release_targets.txt` lists the pfSense versions and
  their pkg ABI, `tools/build_release.sh` builds all release assets.

## 0.6.8
- Limiter Live (test): the fair-share tick is computed like WF2Q+ shares the
  limiter. Queues that use less than their offered share and do not drop
  are satisfied and leave the rest to the others by weight; this repeats
  until no queue is left over. Before, queues with a high weight and only a
  trickle of traffic (e.g. latency probe pings) counted fully and pushed
  every other tick far too low. Idle queues show what they would get if
  they started (issue #13).

## 0.6.7
- Limiter Live (test): each queue bar has a tick at the queue's fair share,
  i.e. limiter bandwidth × weight ÷ sum of the weights of the active queues
  (above 5 Kbit/s). Bar colour: red = drops now, orange = at or above the
  share, green = below, grey = idle; the tooltip shows rate, fair share and
  drops/s. A queue past its tick is normal when other queues leave
  bandwidth unused. Helps to tell a queue held back by the shaper from one
  that does not ask for more (issue #12).

## 0.6.6
- Limiter Live: every panel (latency graphs, latency per interface, Autorate,
  Interfaces, Classes per interface and each limiter) has a *Show* checkbox
  in its heading. Hidden panels shrink to the heading; the choice is
  remembered per browser (issue #11).

## 0.6.5
- README: corrected the note added in 0.6.4. A filter reload or rule/alias
  change under load keeps CoDel; only changed queues or limiters lose it,
  and the Class Generator repairs those since 0.6.4 (tested under load:
  *Filter Reload* and *Generate / Update* with a changed weight keep CoDel
  on all queues).

## 0.6.4
- Re-creating limiters (0.6.3) now loads the queues *before* the limiter.
  In pfSense's order (limiter, scheduler, queues) the first packet that
  reaches the new scheduler blocks CoDel for the remaining queues, so under
  load the repair did not work. Queues loaded without their scheduler get
  CoDel and are attached when the limiter is created (tested under load:
  all queues of the limiter with CoDel). Only the affected limiters are
  reloaded; the running Autorate bandwidth is written into the limiter line
  directly (issue #9).
- README: a filter reload under load can lose CoDel too.

## 0.6.3
- CoDel on the child queues was lost whenever queues were changed while the
  limiter carried traffic: the kernel refuses AQM then
  (`config_aqm: flowset busy`) and the queue ran as tail drop with its full
  queue length, i.e. with much higher latency under load (issue #9).
  - The Class Generator re-creates the affected limiters after
    *Generate / Update* (delete and reload within milliseconds; running
    Autorate bandwidths are kept, queue numbers and firewall states stay).
  - Limiter Live detects queues without AQM (`dnctl queue show`), marks them
    with *no AQM* and offers *Re-create limiters*.
  - README: the console message is not harmless after all; explained.

## 0.6.2
- Latency per interface: each interface is compared with the WAN reference
  over the same time span. After a speed test the reference still contained
  the latency of the test while the probes had been paused, so the queue
  delay was negative for up to 10 s (issue #7).
- Limiter Live: idle threshold for queues raised to 5 Kbit/s. The probe
  pings of *Latency per interface* alone put ~2 Kbit/s into each probed
  queue, so these queues were never hidden (issue #8).

## 0.6.1
- Limiter Live: a queue counts as idle below 1 Kbit/s (before: only at
  exactly 0 bit/s and no flows), so *Hide idle queues* also hides queues
  with a few bit/s of background traffic. Selectable next to the checkbox:
  *below 1 Kbit/s and no flows* keeps queues with an assigned connection
  visible, to check the classification (issue #6). Matrix cells use the
  same threshold.

## 0.6.0
- Limiter Live: latency per interface, measured through a selectable queue
  of each interface (default: the interactive class, qInteractive or
  qOthersHigh, to show what prioritized traffic gets; issue #4). Optional in
  the Class Generator (*Options*):
  one floating match rule per interface (WAN side, direction out, ICMP echo
  request from pfSense itself, own DSCP value from DSCP pool 2). The Autorate
  service pings the Autorate targets ~5x/s per interface with that DSCP value
  and a separate ICMP ID per interface. New panel with a WAN reference row,
  average/P95 bar, queue delay (red above 25 ms) and a 2-minute sparkline;
  status JSON contains `iflat`.
- Class Generator preview shows direction, DSCP and destination *any*
  correctly.
- Limiter Live: the Interfaces panel can be collapsed with a *Show*
  checkbox in its header, like *Classes per interface* (remembered in the
  browser).

## 0.5.9
- Generated rules keep their tracker ID and creation time when the classes
  are applied again, as long as the rule itself is unchanged. Before, every
  apply gave all generated rules new tracker IDs, so tools that identify
  rules by tracker (e.g. the Home Assistant pfSense integration, which
  creates a switch per rule) saw new rules each time. Changed rules get a
  new tracker that was never used by a previous rule.

## 0.5.8
- Speedtest: manual test can run *exclusive*. All limiters are throttled to
  8 kbit/s for the duration of the test and restored afterwards (also on
  abort, and by the Autorate service once the pause has expired), so the test
  measures the full line. Scheduled tests are never exclusive.

## 0.5.7
- Limiter Live: priorities, shaped interfaces and limiter assignment are
  refreshed with every live query. The matrix marks interfaces whose running
  queues do not match the saved priority/weights yet ("not applied") or that
  are no longer enabled ("not in settings"). Reload hint after a language
  change.

## 0.5.6
- No PHP crash report while the package is being updated: pages answer 503,
  CLI scripts exit quietly while library files are missing.

## 0.5.5
- History graph in Limiter Live (test): latency and throughput over 1–48 h
  from per-minute values stored by the Autorate service.
- Congestion threshold line in combined graphs is gray.

## 0.5.4
- Combined graph in Limiter Live (test): latency (ms) and throughput/limit
  of both directions (% of the Autorate maximum) over the last 5 minutes.
- Autorate keeps 5 minutes of latency history.

## 0.5.3
- Autorate graphs in Limiter Live are complete when the page is opened (the
  service keeps 5 minutes of limit and throughput).

## 0.5.2
- First public release on GitHub, English and German README, screenshots.
- Game servers field (all traffic to given servers, e.g. Minecraft Java).
- Minecraft ports (Java TCP 25565–25600, Bedrock UDP 19132–19133).
- Publisher network lists that are empty are skipped with a message;
  Ubisoft preset removed.
- English menu entries and package description.

## 0.5.1
- Real-time devices "UDP only" (UDP except 443).
- Game networks by publisher ASN as URL table aliases (UDP except 443).
- Current vendor port lists (Steam, PSN, Xbox, Riot, Call of Duty); wizard
  list kept as legacy.

## 0.5.0
- Class schemes: 4 classes (qRealtime, qInteractive, qDefault, qBulk) or the
  7 wizard classes; qACK renamed qControl; automatic conversion when
  switching.
- Autorate: "From limiter" minimum 5 % (at least 1 Mbit/s); lowering when the
  line is far below the limit already from 2 % load and 0.5 Mbit/s.
- Speed test runs with HTTP errors count as failed.

## 0.4.x
- Periodic speed test (Cloudflare via curl) with history graphs; Autorate
  pauses during the test. Browser user agent and referer for Cloudflare.

## 0.3.x and earlier
- Class Generator with wizard matchings as floating match rules at the top
  of the floating list; Limiter Live; Autorate with learned base and
  measuring mode; English/German GUI.
