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
