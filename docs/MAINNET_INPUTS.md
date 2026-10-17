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

## Open (needed before launch)
1. **Mainnet genesis inscription** (ASCII, at most 96 bytes) and the **launch time**, chosen shortly before launch (a dated line or a news headline of the day). Then mine it with `bucks-mine-genesis --network=main --time=<unix time> --inscription="..."`, verify the hash independently (see the Python check in the commit that re-mined testnet), and put the values in `src/chainparams.cpp`.
2. **Seed nodes**: at least one reachable host per network, added to `ChainParams::seedNodes` (or announced for `--seed`).
3. **Release checkpoints and `minimumChainWork`** for the first release (both are empty today).
4. **Final word lists**: scholar review of `ar-quran-draft` (questions and the 137 words kept on purpose, with reasons: `docs/SCHOLAR_REVIEW_PACKET.md`), translator review of `en-draft`, then freeze (list id, version, SHA-256) and make the Arabic list the default.
5. **External audit** of consensus, wallet and API (internal findings: `reports/audit-2026-10-14/`; scope for an outside firm: `docs/AUDIT_SCOPE.md`).
6. **Public testnet soak** of several weeks with real peers on more than one machine.
7. **Legal and regulatory advice** (and a Shariah review if marketed as Islamic), trademark and domain checks.
8. **HD coin type** (assessment of 2026-10-17, gate WALLET-COINTYPE): the derivation path is `m/44'/1'/0'/0/0` on every network, and `1` is SLIP-44's shared testnet value, marked as a placeholder in `include/hd_8192.h`. Keep it on purpose or choose (and possibly register) another value. Every phrase made after launch depends on it, so it is decided before the first mainnet wallet, including the miner's payout wallet; the wallet known-answer test (`tests/unit/test_wallet.cpp`) is recomputed if it changes.
9. **Difficulty algorithm** (audit-consensus NEW-2): the chain keeps Bitcoin's retarget every 2016 blocks, limited to 4x per period. A large miner that raises the difficulty and then leaves can leave blocks days apart and the next retarget years away (the audit replayed a 1 GH/s miner leaving: about 28,000 days for the next period). Either keep it with the risk written down (and a prepared hard-fork plan), or adopt a per-block rule such as ASERT before the genesis, which also means a testnet reset and a new soak.
10. **Launch difficulty against measured hashrate** (audit-consensus NEW-1, mining-launch ML-1): the decided `0x1d00ffff` needs about 7.2 MH/s for 10-minute blocks, and the target never gets easier than that limit. The decision above assumed one CPU core does about 10^7 hashes per second; the node's miner measured 0.5 MH/s per thread before its midstate rewrite (about 2 MH/s per thread after it, without SHA-NI). Re-confirm the limit, and how much hashrate runs at launch, with numbers measured on the launch hardware.
11. **GitHub default branch and release tag** (gate HYG-1): a plain clone gets the unrelated branch `main` (a licence and a one-line README, no code), and there are no tags or releases. Choose the branch users get, tag the commit with the mainnet genesis, and publish it. GitHub runs the nightly CI schedule only on the default branch.
12. **Licence** (gate HYG-2): there is no licence file on the code branch, GitHub shows GPL-3.0 from `main`, and `README.md` says "All rights reserved". Choose one, add it at the root of the release branch, and check it against `THIRD_PARTY.md` and the terms of the word-list sources.

The launch itself, step by step with these decisions as a checklist: `docs/LAUNCH.md`.
