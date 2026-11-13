# Changelog

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
