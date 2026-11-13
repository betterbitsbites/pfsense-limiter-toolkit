# Issues for the public repo (copy and paste)

Open https://github.com/betterbitsbites/pfsense-limiter-toolkit/issues/new for each.

---

**Title:** Limiter Live: decide on the combined graphs, then remove the old ones

**Text:**

Since 0.5.4/0.5.5 Limiter Live has two new graphs marked *test*:

- **Latency and throughput – last 5 minutes** (latency in ms on the left, throughput/limit of both directions in % of the Autorate maximum on the right)
- **Latency and throughput – history** (1–48 h, from per-minute values of the Autorate service)

The old latency graph and the two Autorate sparklines are still shown below them.

To do after testing on a real pfSense:
- [ ] Check readability during upload/download load, speed test and idle
- [ ] Decide what stays (combined graphs only, or keep the old ones)
- [ ] Remove the *test* labels and the replaced graphs
- [ ] Update README/README.de and the screenshots in `docs/images/`

---

**Title:** Verify 0.5.7 on pfSense: live priority column and "not applied" hint

**Text:**

0.5.7 refreshes priorities, shaped interfaces and limiter assignment with every live query, and marks interfaces in the matrix whose running queues do not match the saved settings.

To check on a real pfSense:
- [ ] Change an interface priority in the Class Generator and save without generating → matrix shows "not applied"
- [ ] Generate → the hint disappears without reloading Limiter Live
- [ ] Disable an interface → its queues show "not in settings" until generated
- [ ] Switch the pfSense language → reload hint appears
