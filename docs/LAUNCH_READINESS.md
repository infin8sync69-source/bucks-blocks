# Launch readiness

State of the mainnet launch preparation on 2026-10-18. This page answers three questions: what is ready and
how it was checked, what only you can supply, and what the day looks like. The exact commands for the launch
itself are in [LAUNCH.md](LAUNCH.md); the server kit is in [../deploy/README.md](../deploy/README.md); the
reasons behind each open input are in [MAINNET_INPUTS.md](MAINNET_INPUTS.md).

Confidence tags: **[Certain]** was run or read in this repository, **[Likely]** is a strong inference,
**[Guessing]** fills a gap and should be checked.

## 1. Where things stand

| Area | State |
|---|---|
| Node: consensus, chain store, mempool, miner, P2P, wallet, API, web console | Built and tested on Ubuntu 24.04 / GCC 13 (section 2). |
| Mainnet genesis | **Not set.** `bucks --network=main` refuses to start (exit 3) until the launch commit (section 6). |
| Seed nodes | **None compiled in.** Needs your server's address (section 4). |
| Minimum difficulty | Open: choose from the benchmark (section 5). |
| Word lists | Current drafts are frozen as `ar-quran-v1` and `en-v1` on the branch `claude/freeze-wordlists-v1`, to be merged at the launch commit (section 6). |
| Server deployment kit | Done: `deploy/` (systemd unit, firewall, nginx for the operator console, backups, health check). |
| External audit, multi-week testnet soak, legal review | **Skipped by the owner's decision of 2026-10-18** (section 3). |

## 2. What was checked

Everything in this table was run against the integrated branch `claude/mainnet-launch-readiness`.

| Check | Result |
|---|---|
| Full `ctest` (unit, integration, system, performance/security, word lists, genesis verifier, deploy kit, `shellcheck`), Ubuntu 24.04, GCC 13, RelWithDebInfo | 14 of 14 pass [Certain] |
| AddressSanitizer + UndefinedBehaviorSanitizer, same suite | 14 of 14 pass, no reports [Certain] |
| ThreadSanitizer, same suite | 14 of 14 pass, no reports [Certain] |
| libFuzzer: seed corpus replay, then 60 s each on `transaction`, `block`, `header`, `undo`, `netmessage`, `compactsize` (with ASan/UBSan) | No crash or finding [Certain] |
| Three-node soak, 300 s: miner, `kill -9` of one node, restart | 45,031 blocks (regtest); all three nodes ended on the identical tip [Certain] |
| ASERT difficulty rule | All 14,000 official aserti3-2d test vectors pass; an independent Python implementation agrees [Certain] |
| **Launch rehearsal on the integrated tree** (scratch clone, nothing committed): mine a mainnet genesis at `0x1d00ffff`, verify it with `tools/verify_genesis.py`, paste it into `makeMain()`, build, run the full suite, start two mainnet nodes | Genesis verified; **all 14 tests still pass with the genesis set**; both nodes report network `main`, the same genesis, 1 peer, version `0.2.0-main`; one node mined block 1 (318 s, 4 threads) and the other reached height 1 [Certain] |
| Web console in real Chromium (desktop and phone width, keyboard only): sign-in, create and back up a wallet (Arabic and English lists), receive and QR code, mining, send (address, amount and fee checks), restore, no-peers status, console errors | Every flow works, no blocker. Two medium defects found and fixed (mining rewards shown as "1 confirmation"; mining rewards could be sent to a wallet whose phrase was never confirmed). Six low-severity items remain (see the pull request) [Certain] |
| Web unit tests (`node --test web/test/*.mjs`) | 8 of 8 pass [Certain] |
| Independent adversarial review of each of the seven fix branches, then a fix-up round | Done; every must-fix item closed. Four findings that the review summaries had dropped were re-checked in the merged code and fixed with regression tests (deep-reorg message after a partial move, genesis verifier and punctuation in the inscription, word-list pins on Windows line endings, per-address header budget) [Certain] |
| Hashrate of the development VM (`tools/hashrate_bench.sh`) | 5.3 MH/s per thread, 21.1 MH/s on 4 threads [Certain] |

**Not checked**, so do not read them as covered: GitHub Actions on this branch (it runs when the pull request
opens); macOS and Windows builds of this branch; a run across different machines and networks; ASERT under
real hashpower; anything on mainnet itself.

## 3. Risks you accepted by launching now

You decided on 2026-10-18 to launch without finishing the items in `MAINNET_INPUTS.md` that need weeks or
outside parties. In plain terms:

1. **No independent review of money-handling code.** The internal audits were done by AI-assisted read-only
   review, and every serious finding was fixed and re-reviewed, but the same kind of reviewer wrote and
   checked the fixes. Consensus rules freeze at the genesis: a consensus bug found later is a **hard fork**
   that every node has to install. A wallet or API bug can lose users' coins. [Certain]
2. **ASERT is new consensus code.** It passes all 14,000 official aserti3-2d test vectors from the Bitcoin Cash
   Node repository and an independent Python implementation agrees with it, but nothing has run it for weeks
   against real hashpower. [Certain]
3. **No multi-week soak with real peers.** The 4-node localhost tests and the 5-minute soak show the
   mechanics work; they do not show how the network behaves under real-world latency, NAT and hostile peers.
   [Certain]
4. **Word lists frozen without review.** Phrases made at launch restore for ever with these exact lists.
   The 137 words listed in `SCHOLAR_REVIEW_PACKET.md` stay in the Arabic list for good; if the scholar
   later objects, the fix is a new list (`ar-quran-v2`) that new wallets use, while old phrases keep working
   with v1. [Certain]
5. **No legal review before coins circulate.** Nothing in the code or the documents is legal advice. [Certain]
6. **A single operator mines the first blocks**, so the first coins sit with whoever runs the first miner.
   Say how you will handle that in the announcement. [Certain]

## 4. What only you can supply

None of these can be guessed. Each blocks the genesis.

| # | Input | Format | Why it blocks |
|---|---|---|---|
| 1 | **Server details**: `lsb_release -a; nproc; lscpu \| grep 'Model name'` | text | Ubuntu 22.04 vs 24.04 decides the build, and the core count decides the benchmark. |
| 2 | **Seed address**: public IP or DNS name with TCP port 8733 open | `host:8733` | Compiled into the release; nodes find the network through it. A second seed on another machine and network is strongly advised: with one seed, the network depends on it entirely. |
| 3 | **Genesis inscription** | printable ASCII, at most 96 bytes, for example a dated headline | Frozen in the genesis. |
| 4 | **Launch time** | the day you will mine and start | The genesis time must not be more than 2 hours ahead of the miners' clocks. |
| 5 | **Payout address** | a `B...` address from a wallet made **on your own computer** | The miner server must never hold the key (`--mine-address`). |
| 6 | **Minimum difficulty** | from the benchmark (section 5) | Frozen in the genesis and the easiest the chain can ever be. |
| 7 | **HD coin type** | keep `1` or choose another | Every phrase made after launch depends on it; decide before the first wallet, including the payout wallet. Recommendation [Likely]: keep `1`. These phrases are not usable in other wallet software anyway (the word lists are Bucks' own), so a different number buys no interoperability, and changing it after launch would need the restore path to try both. What it does not give you is separation between networks: a phrase used on testnet derives the same key on mainnet, so tell users never to reuse a testnet phrase. |
| 8 | **GitHub default branch and release tag** | e.g. default branch `master`, tag `v1.0.0` | A plain `git clone` currently gets branch `main`, which holds only a licence and a one-line README. |
| 9 | **Licence** | one licence file at the root | `main` shows GPL-3.0, `README.md` says "All rights reserved". |

## 5. Benchmark: choosing the minimum difficulty

Run this **on the server that will mine**, with nothing else busy (about 40 seconds, nothing is saved):

```bash
git clone -b master https://github.com/infin8sync69-source/bucks-blocks.git && cd bucks-blocks
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUCKS_BUILD_TESTS=OFF && cmake --build build -j"$(nproc)"
tools/hashrate_bench.sh build/bucks-mine-genesis
```

It prints your total MH/s and the average seconds per block at three candidate limits. Reading it:

- With ASERT the difficulty rises by itself when blocks come faster than 10 minutes, and falls again when
  they come slower, but **never below the limit** you freeze. So the limit only matters if it is *too hard*
  for your launch hashrate: then blocks stay slow for as long as the hashrate stays that low.
- `0x1d00ffff` (about 2^32 hashes per block) needs about 7.2 MH/s for 10-minute blocks. On the development
  VM one thread measured 5.3 to 5.5 MH/s [Certain], so a 2-thread server already gives blocks faster than
  10 minutes (ASERT then raises the difficulty), and a 1-thread server gives about 13 minutes.
- **Recommendation [Likely]: keep `0x1d00ffff` if the benchmark shows 7 MH/s or more.** If the server
  is much slower, choose the harder-to-attack limit that still gives you blocks you can live with (the
  table shows the options); do not pick `0x1e00ffff` (2^24, seconds per block) for a server that
  can reach 2^32 in minutes, because the first hours would mint a large number of coins at almost no cost
  before ASERT catches up.
- If you change the limit, change `p.powLimitBits` in `makeMain()` and the matching assertion in
  `tests/unit/test_chainparams.cpp`, and pass the same value to `tools/verify_genesis.py --bits`.

## 6. The launch commit

Do these in order on the day, after section 4 is complete. The exact commands are in `LAUNCH.md` steps 1-5.

- [ ] Merge this pull request into `master` (the code, the tests and the deploy kit).
- [ ] Merge `claude/freeze-wordlists-v1` (renames the lists to `ar-quran-v1` and `en-v1`, marks them
      `release`, refreshes their SHA-256 pins; the 2048 words of each list are byte-identical to the
      drafts, so phrases made on test networks still decode). **This is the irreversible step for users:
      from the first mainnet wallet on, the lists never change.**
- [ ] Decide the HD coin type (section 4, row 7).
- [ ] Mine the genesis on launch day (`bucks-mine-genesis`), verify it with `tools/verify_genesis.py`,
      write the values, the seed address(es) and the minimum difficulty into `makeMain()`.
- [ ] `ctest` passes in full. `Genesis.MainnetIsMinedAndConfigured` stops being skipped and
      `genesis_independent` recomputes the mainnet genesis with Python.
- [ ] Set `BUCKS_VERSION` in `include/version.h` to the release number (`1.0.0`) and commit.
- [ ] Add the licence file, set the default branch, tag the commit (`git tag -s`), publish the release
      with the genesis hash, time, nonce, inscription, tag and commit id.

## 7. Server checklist

Seed and miner can share one machine for the first day; separate machines are better.

- [ ] Ubuntu 22.04 or 24.04, a normal user with sudo, SSH key login. A binary built on 24.04 does not run on
      22.04: build on the machine that runs it (`bootstrap-ubuntu.sh` does).
- [ ] Size [Guessing]: 2 vCPU, 4 GB RAM and 40 GB disk are enough for a seed at launch; a miner wants as
      many cores as you can give it (the node leaves one core free). The build needs about 1.2 GB of RAM per
      compile job.
- [ ] Clock: `timedatectl` shows `System clock synchronized: yes`. Blocks more than 2 hours ahead of a
      node's clock are rejected.
- [ ] A public IP or DNS name, and **TCP 8733 open inbound**. Nothing else is opened except SSH; the API
      port (8080) is never public.
- [ ] GitHub reachable from the server (clone, and the googletest download unless `BUCKS_RUN_TESTS=0`).
- [ ] The payout wallet exists **on your own computer** and its recovery phrase is written down offline
      and tested by restoring it once.
- [ ] A GnuPG public key for backups (private key kept off the server) and somewhere off-site to copy
      `/var/backups/bucks` to.
- [ ] Optional operator console: a domain name pointing at the server, your own IP for the allowlist.
      It is a single-operator tool, **not** a wallet service for users (section 9).

## 8. Launch day and the first 48 hours

1. T-1 day: sections 4 and 7 complete; both servers bootstrapped on **testnet** once as a dry run
   (`BUCKS_NETWORK=test`) to prove the kit works on your machines.
2. T-0: section 6, then `LAUNCH.md` steps 6-8: seed first, miner second, a third machine verifies.
3. Go / no-go before announcing, all true:
   - [ ] the verifying machine, the seed and the miner report the **same tip** after the first blocks;
   - [ ] the genesis hash on all three equals the published one;
   - [ ] the health check is OK on the seed and the miner (`systemctl status bucks-healthcheck.service`);
   - [ ] a block mined by the miner appears on the verifying machine within a minute;
   - [ ] a fresh install built from the published tag, using only the README, syncs from the seed.
4. Watch: `journalctl -u bucks -f` on the seed and the miner; the health check fails on a stopped miner, a
   stale tip, a wrong network or a fork alert.
5. **A fork alert is not routine.** The node parks a branch deeper than 100 blocks and waits for an operator.
   Do not approve it (`POST /api/node/approve-reorg`) until you have confirmed with other operators which
   chain is right.
6. After the first 100 blocks the first coins can be spent. Send a test payment between two of your own
   wallets on different machines before telling anyone else to.

## 9. Known limitations at launch (not blockers)

- **Users must run their own node.** The web console is a single-operator tool: one API token controls every
  wallet on that node, and keys are stored unencrypted in `wallets.json`. The README says so. There is no
  light wallet, no hosted wallet and no prebuilt binary yet; the Swift and Kotlin apps need two small
  endpoints (coins for an address, broadcast a signed transaction) that are not written.
- **No external miners or pools**: the node has its own miner only (no `getblocktemplate`).
- **Peer transport is encrypted but not authenticated**: an attacker on the path can sit between two
  nodes. Blocks and transactions are self-validating, so this is an eclipse and privacy risk rather than a
  forgery risk [Likely].
- **IPv4 only**; the node always listens on all addresses.
- **Checkpoints and `minimumChainWork` are empty** in the first release; the next release adds them from
  the running chain.
- **Every future consensus change is a hard fork.** Plan upgrades like one.
- The storage-spec items (crash-injection harness, read pool, lock ranking) are not done.

## 10. If something goes wrong

- Before anyone else mines: the genesis can be redone (steps 2-8 of `LAUNCH.md` with a new tag; delete only
  `<datadir>/main/chain.sqlite*` on each node, never `wallets.json`). After others have mined, a new genesis
  is a new chain.
- A bad release after launch: roll back with `ln -sfn /opt/bucks/releases/<old> /opt/bucks/current &&
  systemctl restart bucks`. A consensus bug cannot be rolled back; it needs a coordinated release.
- Corrupt database (exit code 6): restore from the daily backup or run `--reindex`
  (`deploy/README.md`).
