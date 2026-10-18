# Mainnet launch inputs

Decisions recorded 2026-10-14 (owner). Mainnet stays disabled (`--network=main` exits with code 3) until every
item under "Open" is closed and the owner gives the go-ahead.

## Decided
| Item | Decision | Why |
|---|---|---|
| Launch difficulty (spec Q1) | Keep the minimum `0x1d00ffff` (about 2^32 hashes per block) | The cautious floor: a lower limit is cheap for a rented-hashpower attacker. Too high can stall the first 2016-block period, so nothing above this is chosen without a known launch hashrate. |
| Automatic reorg limit (spec Q2) | 100 blocks (about 17 hours); deeper forks need the operator (`POST /api/node/approve-reorg`) | Blocks silent rewrites while hashrate is small. |
| Launch timing | Only after the testnet soak, the audit and the word-list review are finished | Owner decision. |
| Address prefix, merge mining, gold | Defaults: `B...` addresses (0x1A), no merge mining, gold module not in the first launch | Spec Q3, Q4 and the gold decision of 2026-10-13. |
| Genesis can be reset until launch | Yes | Nothing on mainnet exists yet; the testnet genesis was already re-mined once (2026-10-14). After launch a different genesis means a different chain. |

## Owner decisions of 2026-10-18

Recorded from the owner's answers in the launch-preparation session; they replace the rows above where they differ.

| Item | Decision |
|---|---|
| Launch timing | **Launch now, without waiting for open items 4-7** (word-list review, external audit, public testnet soak, legal advice). The owner overrode the row "Launch timing" above. Accepted risks: no independent review of the consensus, wallet and API code before its rules are frozen by the genesis (a consensus bug found later needs a hard fork, a wallet bug can lose users' coins); no multi-week soak with real peers; no legal review before coins circulate. |
| Difficulty algorithm | **ASERT** (aserti3-2d, 2-day half-life, anchored at genesis) on main and test, chosen by the assistant as the owner delegated the choice (audit-consensus NEW-2). Merged in this branch; it needs the external audit's attention like the rest of the consensus code. The old testnet's blocks do not validate under it (new network magic). |
| Word lists | **Freeze the current drafts as version 1** at the launch commit: each list gets a final id, status `release` and its SHA-256 pin, and is never edited afterwards (a change ships as a new list next to it). The owner chose to proceed without the scholar and translator reviews; the 137 words flagged in `docs/SCHOLAR_REVIEW_PACKET.md` stay in the Arabic list permanently. |
| Minimum difficulty (`powLimitBits`) | **Set from a benchmark of the launch server** (`docs/LAUNCH_READINESS.md`, "Benchmark"), replacing the fixed `0x1d00ffff` above. |

## Open (needed before launch)
1. **Mainnet genesis inscription** (ASCII, at most 96 bytes) and the **launch time**, chosen shortly before launch (a dated line or a news headline of the day). Then mine it with `bucks-mine-genesis --network=main --time=<unix time> --inscription="..."`, verify the hash independently with `tools/verify_genesis.py` (ctest runs it on every network that has a genesis), and put the values in `src/chainparams.cpp`. Use the time the tool prints (it moves on by one second when a nonce space runs out).
2. **Seed nodes**: at least one reachable host per network, added to `ChainParams::seedNodes` (or announced for `--seed`).
3. **Release checkpoints and `minimumChainWork`** for the first release (both are empty today).
4. **Final word lists** (overridden 2026-10-18: frozen as version 1 without the reviews, see above): scholar review of `ar-quran-draft` (questions and the 137 words kept on purpose, with reasons: `docs/SCHOLAR_REVIEW_PACKET.md`), translator review of `en-draft`, then freeze (list id, version, SHA-256) and make the Arabic list the default.
5. **External audit** [overridden 2026-10-18: launch without it; the risk is accepted above] of consensus, wallet and API (internal findings: `reports/audit-2026-10-14/`; scope for an outside firm: `docs/AUDIT_SCOPE.md`).
6. **Public testnet soak** [overridden 2026-10-18: launch without it; the risk is accepted above] of several weeks with real peers on more than one machine.
7. **Legal and regulatory advice** [overridden 2026-10-18: launch without it; the risk is accepted above] (and a Shariah review if marketed as Islamic), trademark and domain checks.
8. **HD coin type** (assessment of 2026-10-17, gate WALLET-COINTYPE): the derivation path is `m/44'/1'/0'/0/0` on every network, and `1` is SLIP-44's shared testnet value, marked as a placeholder in `include/hd_8192.h`. Keep it on purpose or choose (and possibly register) another value. Every phrase made after launch depends on it, so it is decided before the first mainnet wallet, including the miner's payout wallet; the wallet known-answer test (`tests/unit/test_wallet.cpp`) is recomputed if it changes.
9. ~~**Difficulty algorithm**~~ (audit-consensus NEW-2) **decided 2026-10-18: ASERT**, implemented and merged (see above). Closed once the external audit has covered it.
10. **Minimum difficulty against measured hashrate** (audit-consensus NEW-1, mining-launch ML-1): with ASERT the limit is also the easiest the chain can ever be, and it is frozen by the genesis. Decided 2026-10-18 to set it from a benchmark of the launch server (`docs/LAUNCH_READINESS.md`, "Benchmark"); still open until that number is chosen.
11. **GitHub default branch and release tag** (gate HYG-1): a plain clone gets the unrelated branch `main` (a licence and a one-line README, no code), and there are no tags or releases. Choose the branch users get, tag the commit with the mainnet genesis, and publish it. GitHub runs the nightly CI schedule only on the default branch.
12. **Licence** (gate HYG-2): there is no licence file on the code branch, GitHub shows GPL-3.0 from `main`, and `README.md` says "All rights reserved". Choose one, add it at the root of the release branch, and check it against `THIRD_PARTY.md` and the terms of the word-list sources.

The launch itself, step by step with these decisions as a checklist: `docs/LAUNCH.md`.
