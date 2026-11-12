# Changelog

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
