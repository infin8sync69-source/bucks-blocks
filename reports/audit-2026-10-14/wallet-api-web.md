# Audit: wallet, mnemonic, REST API, storage and web UI

Date: 2026-10-14. Branch `chain-v2-phase1-scaffold` (HEAD 926c3ed). Scope: src/wallet.cpp, hd_8192.cpp, bip_8192.cpp, crypto.cpp, server.cpp, chaindb.cpp, fsutil.cpp, web/, wordlists/.
Method: source review plus a private build (scratchpad `build/`, binary copied to `bin/auditnode`), regtest nodes on ports 191xx with scratch datadirs and token files, probing with curl/Python, a small harness linked against `libbucks_core.a` (arabicFold), Python re-implementations for wordlist analysis, and the browser pane for the UI. No repo file was modified. All nodes I started are stopped.

## Summary

| Severity | Count | IDs |
|---|---|---|
| Critical | 0 | |
| High | 1 | WAL-1 |
| Medium | 4 | WAL-2, WAL-3, WAL-4, WAL-5 |
| Low | 8 | WAL-6 to WAL-13 |
| Info | 3 | WAL-14, WAL-15, WAL-16 |

Everything below is "verified: yes" unless stated. Nothing found lets a remote, unauthenticated party read keys or spend funds. The main risks are wallet-file handling (which keys are loaded, and which are lost), and the API defaults that create wallets on placeholder word lists.

Out-of-scope observation (P2P, not my area, mentioned because it kills the node): `Network::handleAddrMessage` (src/network.cpp:1194-1211) calls `std::stoi` on the port part of every peer-supplied "ip:port" string with no try/catch inside the network thread. A peer sending `addr` with a non-numeric or empty port aborts the process. A host crash report (`~/Library/Logs/DiagnosticReports/bucks-2026-10-14-115705.ips`, SIGABRT, `stoi` in `handleAddrMessage`) shows exactly this stack; I cannot say which node on this machine it was. (Code reading plus that crash report; I did not reproduce it with my own peer.)

---

## Findings

### WAL-1 (High) Legacy `./wallets.json` in the working directory is auto-loaded as the primary wallet and its keys are copied into the datadir
File: include/server.h:68 (`legacyWalletFile = "wallets.json"`, relative path), src/server.cpp:1844-1859 (`loadWallets`), src/server.cpp:1714-1777 (`saveWalletsLocked`).

What is wrong: when `<datadir>/wallets.json` does not exist, the node loads `wallets.json` from the process's current directory with no prompt, no flag, and no check of ownership or permissions. The only signal is one stderr line. Those keys become `wallets[0]`, which is the "primary" wallet: it is the mining payout address (`mineBlock`, `startAutoMine`, `autoMineLoop` use `wallets[0]`), the address `/api/wallets/primary` and the UI show first, and the default sender. The log text says "read-only ... new wallets are saved to the data directory only", but the first `create`/`restore` writes every loaded wallet, legacy keys included, into the new `<datadir>/wallets.json` (`saveWalletsLocked` serialises all of `wallets`). The legacy keys therefore spread to a second file and survive deleting the original.

Scenario: this repository root contains `wallets.json`, documented as a compromised legacy wallet. Anyone who runs `./build/bucks --server ...` from the repo root with a fresh or empty `--datadir` (the case the legacy fallback exists for) gets the compromised key as primary wallet. Mined rewards (`--auto-mine`, `/api/mining/start`, `/api/mining/mine`) are paid to an address whose key an attacker holds. The UI "Receive" card shows that address, so the user may fund it.

Reproduction (done with a fake legacy file in a scratch cwd): started a regtest node from a directory containing `wallets.json` (mode 0644, array format). `GET /api/wallets` listed the legacy address; `POST /api/mining/mine` returned `minerAddress` equal to it; after `POST /api/wallets/create` the new `dd_b/wallets.json` held both the legacy and the new address.

Fix: remove the cwd fallback, or make it an explicit `--import-legacy-wallets <file>` flag. If kept: require the file to be owned by the user and mode 0600, never make an imported wallet the mining/primary wallet, require a confirmation or an explicit `--import` (not a log line), and never copy legacy keys into the datadir implicitly. Separately, quarantine the compromised repo-root file outside the repo.

Verified: yes.

### WAL-2 (Medium) `wallets.json` is rewritten from process memory with no inter-process lock: concurrent nodes silently erase each other's keys
File: src/server.cpp:1714-1777 (`saveWalletsLocked`), src/chaindb.cpp:344-354 (the flock covers only `<datadir>/<network>/chain.lock`).

What is wrong: `wallets.json` lives in the datadir root and is shared by every network (`main`, `test`, `regtest`), but the lock is per network directory. Each process loads the file once and then writes its whole in-memory list on each change. A second process, or a manual edit while the node runs, is overwritten. The API still answers `"persisted": true`.

Scenario: a user runs a testnet and a regtest (or main) node with the default `~/.bucks`. Creating a wallet on one, then on the other, removes the first wallet's private key from disk. The only copy is process memory until it exits. `wallets.json.bak` is rewritten once per process start, so it holds the older file only until the next restart. The recovery phrase is the only safe net, and legacy-imported keys and anything created via `--auto-mine` rewards have none.

Reproduction: regtest node X (port 19161) and test node Y (port 19171) on the same `--datadir`. Created a wallet on X (persisted true), then on Y (persisted true). `wallets.json` contained one wallet (Y's); X's public key was absent while X still listed its wallet in memory.

Fix: take an exclusive `flock` on `<datadir>/wallets.json.lock` for the process lifetime (fail startup if held), or reload-merge under the lock before each write, or store wallets per network directory.

Verified: yes.

### WAL-3 (Medium) The API creates real wallets on the placeholder "ar-dev" list by default
File: src/bip_8192.cpp:501-506 (`defaultLang` = first list with status dev or release), src/server.cpp:1055 and 1074 (empty `language` means default), wordlists/manifest.json (ar-dev listed first).

What is wrong: `POST /api/wallets/create` with no `language` uses `ar-dev`. The code itself says dev lists "exist to be replaced" (`checkWordlistSet` exempts them from all checks), and the wallet UI explicitly hides the list. Any API client, script, or older UI creates a wallet whose phrase will stop restoring when the placeholder is replaced. Even the UI's English/Arabic choice uses draft lists (status `draft`) that the UI itself says may change. Nothing pins list content (see WAL-14): the word-to-index mapping is the wallet's recovery format.

Reproduction: `POST /api/wallets/create {}` returned `"language":"ar-dev"` and a 13-word phrase. `GET /api/wallets/languages` reports `"default":"ar-dev"`.

Fix: refuse `create` when the chosen list is `dev` (or `test`) unless an explicit `allowDev` flag or a regtest-only switch is set; make the default the first non-dev list; return `status` in the create response so clients can warn.

Verified: yes.

### WAL-4 (Medium) `create` persists the wallet, then can fail before returning the phrase; the unrecoverable wallet can become the mining payee
File: src/server.cpp:1067-1107.

What is wrong: the wallet is generated and written to disk by `addWallet(w)` (line 1072) before `body.value("includeCanonical", false)` (line 1092) is evaluated. If `includeCanonical` is not a JSON boolean, nlohmann throws `type_error.302`, the catch at 1104 answers 500, and the phrase is never sent. The wallet already exists on disk and in memory. If it is the first wallet, it is `wallets[0]` and `/api/mining/start` pays block rewards to an address nobody has the phrase for. (The restore route evaluates `value()` before persisting, so it is not affected.)

Reproduction: fresh node, `POST /api/wallets/create {"includeCanonical":"yes"}` gave HTTP 500; `GET /api/wallets` then showed the new address; `POST /api/mining/start` returned `minerAddress` equal to it.

Fix: parse and validate every request field before `addWallet`; persist only after the response body is fully built; use `body.contains(...) && is_boolean()` instead of `value()`.

Verified: yes.

### WAL-5 (Medium on a non-loopback bind, Low on the default) Pre-auth slow-connection denial of service on the HTTP API and UI
File: src/server.cpp:462-471 (read timeout 15 s, default thread pool because `maxWorkers == 0`), pre-routing handler at line 484 runs only after the request head is parsed.

What is wrong: the worker pool is small (about 8) and each connection that sends an incomplete request head holds a worker for up to 15 s. The rate limiter, Host check and auth all run after parsing, so they cannot help. There is no per-IP connection limit.

Reproduction: opened 40 sockets, each sending `GET /api/status HTTP/1.1\r\nHost: ...\r\nX-A: ` without finishing. `/api/status` timed out for about 80 s (until the queue drained). With 200 sockets it stayed unanswered for more than 25 s while they were held. A client that keeps about 10 sockets alive keeps the API and UI down indefinitely; P2P is unaffected.

Fix: larger pool and a bounded accept queue that sheds load, a per-IP connection cap, a short header-read deadline, or document a mandatory reverse proxy for any non-loopback `--api-bind`.

Verified: yes.

### WAL-6 (Low, only with `--trust-proxy`) The rate limiter keys on the first `X-Forwarded-For` entry, which the client controls
File: src/server.cpp:499-503.

What is wrong: with `--trust-proxy` the key is `xff.substr(0, xff.find(','))`, the leftmost entry. A proxy that appends (nginx `$proxy_add_x_forwarded_for`, most defaults) leaves attacker-supplied entries on the left, so the limit is bypassed by rotating the header. Each distinct value also creates a map key, so memory grows by one entry per request (eviction only drops keys idle longer than the window).

Reproduction: `--trust-proxy` node: 320 requests with a fixed client gave 299 x 200 and 21 x 429; 320 requests with a different spoofed leftmost XFF each gave 320 x 200. Without `--trust-proxy` the header is ignored (191 x 429 of 320 on the other node).

Fix: take the rightmost entry the trusted proxy added (or a configurable hop count), and cap the number of tracked keys.

Verified: yes.

### WAL-7 (Low) Unauthenticated disclosure of filesystem paths and the miner address
File: src/server.cpp:875-888 (`/api/auth/status`), 1443-1450 (`/api/mining/status`).

`GET /api/auth/status` is public and returns `cookieFile` (absolute path of the token file, e.g. `/Users/<name>/.bucks/.cookie`) and `walletError` (which can contain paths). `GET /api/mining/status` is public and returns `minerAddress`, tying the node to a payout address. Verified live (cookieFile returned unauthenticated). Fix: return `cookieFile`/`walletError` only to authenticated callers (show a generic hint on the login screen) and consider authenticating `minerAddress`.

Verified: yes.

### WAL-8 (Low) `--datadir` pointing at an existing directory is silently `chmod 0700`
File: src/server.cpp:146-162 (`ensurePrivateDir`).

An existing directory is chmodded to 0700 unconditionally, with no ownership check or message. `--datadir ~` or a shared project folder loses its group/other access. (It also creates missing parents with the default umask first.) Verified: a 0755 directory became 0700 after start. Fix: refuse (or warn) on an existing directory that is not already private, as `chaindb.cpp:324-334` does for the chain directory.

Verified: yes.

### WAL-9 (Low) No minimum strength for an operator-supplied API token; token file permissions are not checked
File: src/server.cpp:388-423; src/main.cpp:41-42 (help says "0600").

`--api-token a`, `$BUCKS_API_TOKEN=a`, or a one-character token file are accepted; a world-readable (0644) token file is read without a warning. Login has no throttling beyond the global 300 requests/min per peer (and see WAL-6). Verified: node started with a 0644 token file containing `a`, and `Authorization: Bearer a` returned 200. The generated default (256-bit random, `.cookie` 0600) is fine. Fix: require at least 32 characters or 128 bits, and stat the file: refuse if group/other readable.

Verified: yes.

### WAL-10 (Low) `POST /api/node/approve-reorg` accepts any 64-hex string and reports success
File: src/server.cpp:1425-1441, src/chainstate.cpp:245-249, include/chainstate.h:136.

It requires auth and, for cookie sessions, the CSRF header (both verified), so it is not reachable by a third party. But `approved_.insert(tipHash)` happens without checking that the hash is a known header with more work, and the answer is `{"success":true}` even when nothing can ever activate (verified with `ab`x32). An operator who typo-pastes the tip believes the reorg was approved; the set is unbounded in memory (one entry per request, bounded only by the rate limit) and lost on restart. The route also takes the whole body without the shared `parseBody` helper (a non-object body is just a 400). Fix: check the hash against the block index, return 404 for unknown tips, report whether the active tip changed, bound the set, and consider requiring the fork depth in the request.

Verified: yes.

### WAL-11 (Low) Secrets are not wiped from memory; no core-dump or swap hardening
Files: src/server.cpp:1731-1776 (`walletDoc` keeps every `privKeyHex` as heap strings for the process lifetime, and `json out = walletDoc` copies them on each save), src/server.cpp:1221-1231 and 1280-1291 (`Wallet` copies with `privKey`/`masterKey`, never cleansed), include/wallet.h:94 (`setMnemonic("")` only truncates; the old buffer contents remain), src/server.cpp:1078-1103 (`out` JSON and `req.body` carrying the phrase persist until freed, only `mnemonic`/`canonical` are cleansed). `Wallet` has no destructor. `masterKey` (the HD master key) stays in `wallets` for wallets made by create/restore in this run. No `RLIMIT_CORE=0`, `PR_SET_DUMPABLE`, or `mlock` anywhere in src/.

Impact: a core dump, swap file, or memory read exposes keys and recent phrases. The code's own comments claim phrases "live only in the creation response". Fix: cleansing destructor on `Wallet`/`ExtendedKey`, `OPENSSL_cleanse` the request bodies and response string, drop `masterKey` after deriving, disable core dumps at startup. This finding is reasoning from code (leftover bytes in freed heap memory are not directly inspected): unverified, reasoning only.

### WAL-12 (Low) Session cookie lacks `Secure`; origin check accepts port-less origins and port-less allow-list entries
File: src/server.cpp:906-908 (cookie attributes), 727-745 (`hostAllowed`), 756-762 (`originAllowed`).

(a) The cookie is `HttpOnly; SameSite=Strict` but not `Secure` and not `__Host-` prefixed, which matters as soon as the node sits behind HTTPS via `--api-allowed-host`. (b) `hostAllowed` accepts a Host/Origin with no port for loopback names, and an `--api-allowed-host` entry without a port matches any port. Verified: a cookie session with `Origin: http://localhost` (no port) passed the CSRF check on a node bound to :19101 while `Origin: http://127.0.0.1:1` was refused. This is not exploitable today: the check is a second layer, the custom `X-Bucks-CSRF` header forces a CORS preflight, and preflights from unlisted origins get 403 (verified). It would matter if the header requirement were ever relaxed. Fix: set `Secure` when the request came over TLS/proxy, require an exact scheme+host+port match for origins.

Verified: yes (for the port-less acceptance).

### WAL-13 (Low) Restoring with a passphrase silently creates and persists a second, empty wallet
File: src/server.cpp:1111-1165, web/js/pages/wallet.js (restore form).

The scheme has no passphrase at creation, so a user who fills the "Advanced: passphrase" box (or whose password manager autofills it) gets a different wallet. The node persists it, reports "Wallet restored" and `existing:false`, and the UI toasts success. The user sees a 0 balance and may believe funds are gone. The passphrase is used as raw bytes with no normalisation (documented). Verified at the API: same phrase plus passphrase `x` returned a different address, persisted. Fix: UI: show the derived address before saving ("this phrase plus passphrase gives address ..."), and flag a wallet with zero balance and no history as probably a wrong passphrase; consider NFKC-normalising the passphrase.

Verified: yes.

### WAL-14 (Info) Word lists: no integrity pin; mechanical properties of the three lists
Files: src/bip_8192.cpp:451-505 (registry load), wordlists/.

- Lists are not hashed or signed; `manifest.json` has no digest, and the directory is resolved from `BUCKS_WORDLIST_DIR`, then paths next to the executable, then `./wordlists` in the cwd (bip_8192.cpp:420-446). Because the word-to-index map is the recovery format, a silent edit or a different directory changes which phrases restore. Failure is closed (the 15-bit checksum rejects almost all mismatches) but a tampered or swapped directory makes every existing phrase unrecoverable. Recommend a SHA-256 per list in the manifest, checked on load, and refusal of the cwd candidate when `--datadir` is set.
- Measured, all three lists: 2048 unique entries, no duplicates under the normaliser.
- ar-dev and ar-quran-draft share 249 words (after folding), only 1 at the same index. A phrase that is valid in both is rejected as AMBIGUOUS (fail-closed); the chance a random phrase is is about 1e-12 x 2^-15, negligible. `checkWordlistSet` skips dev lists, so this overlap is not reported.
- ar-quran-draft: 16 entries start with a standalone hamza plus alef (U+0621 U+0627, e.g. "ءاباء"). The normaliser does not fold that to alef, so the natural spelling (آباء/أباء) is rejected as an unknown word. Fail-closed, but it is the typing problem the v2 rewrite was meant to avoid.
- en-draft: 502 ordered pairs at substitution distance 1 (ear/far/tar/war, test/best, safe/save), 998 words share their first four letters (the tool warns), and the list has two-letter words. A single mistyped word that lands on another valid word is still caught by the 15-bit checksum (about 1 in 32768 miss rate, better than BIP-39 12-word at 1 in 16).
- Homoglyph/invisible-character check: the Arabic folding table matches Python `unicodedata` NFKD for every code point in the five Arabic blocks (0 differences over the whole range, including presentation forms and ligatures). Mathematical Arabic letters (U+1EE00 block, 133 code points) and all non-Arabic compatibility forms are not folded, so they are rejected rather than accepted. Zero-width characters U+200B-200F, U+061C, U+FEFF and harakat are stripped, so a pasted phrase with them still restores the same wallet (verified); bidi isolates U+2066-2069 and U+202A-202E are not stripped and cause a rejection.
- The UI collapses all whitespace (`\s+`) before sending, but the API splits only on ASCII whitespace: a phrase pasted by an API client with NBSP or ideographic spaces fails with "phrase must have 13 or 25 words" (verified, fail-closed).

### WAL-15 (Info) Small API parsing laxness
- `GET /api/blocks/height/4294967297` returns the block at height 1 (`std::stoul` result truncated to `uint32_t`, src/server.cpp:991). Verified.
- `GET /api/blocks/hash/<64 hex>EXTRA` returns the block (`hexToHash256` reads only the first 64 characters, lines 82-89, 971-974). Verified.
- httplib 0.32.0 accepts two `Content-Length` headers and uses the first (a request with `Content-Length: 2` then `50` was processed as 2 bytes). Only relevant behind a proxy that picks the last; `Transfer-Encoding` plus `Content-Length` produced a single response, no smuggling.
- `parseHex` (src/crypto.cpp) is built on `std::stoi` and accepts forms like `" 1"` or `"-1"`; wallet loading is protected by the pub/priv consistency check.
- Concurrent sends from one wallet race on coin selection and mostly fail with `txn-mempool-conflict` (verified: 12 parallel sends gave 2 accepted, 10 rejected). No double spend; usability only.

### WAL-16 (Info) Private keys are stored in plaintext in `wallets.json`
`privKeyHex` is cleartext, mode 0600, in a 0700 directory (verified). No passphrase encryption at rest. The web UI keeps the pending phrase in a module variable across route changes until confirmed (wallet.js lines 8-14), and the words are in the DOM, only CSS-blurred (`.words.hidden`, `user-select:none`), so page text extractors and extensions can read them while shown.

---

## Checked and found sound

Authentication and web security (all with live probes unless noted):
- Default deny: every `/api/*` path outside a 6-entry GET allowlist plus two block prefixes returns 401 without a token; unknown API paths also 401. Query-string tokens are ignored.
- Token comparison hashes both sides then `CRYPTO_memcmp`; the generated token is 32 random bytes from `RAND_bytes`, written 0600 to `<datadir>/.cookie` via temp file, fsync, rename.
- Session cookie is HttpOnly, SameSite=Strict, 12 h, 64-session cap, 256-bit random id; logout destroys it.
- CSRF: cookie sessions need `X-Bucks-CSRF: 1` plus a matching Origin or Referer; `Origin: null`, a foreign origin, a wrong port, and `text/plain` bodies were all refused (403/415). Preflight from an unlisted origin returns 403 and there is no `Access-Control-Allow-Credentials`.
- DNS rebinding: Host header allow-list covers every route and the static files; `Host: evil.com`, duplicate or userinfo-style hosts, empty Host and HTTP/1.0 without Host were refused.
- Static file serving: dot-segments, `%2e%2e`, `..%2f` and double slashes could not escape the web root or reach another API route (all 404/400/401). Request body limit 1 MiB (413 on a 2 MB body), URI limit (414), header limit, and deeply nested JSON (1,000,000 `[`) returned 400 without harming the node.
- Response headers on every response including errors: CSP (`default-src 'self'`, no inline script or style, `frame-ancestors 'none'`), `X-Frame-Options: DENY`, `nosniff`, `Referrer-Policy: no-referrer`, `Cache-Control: no-store` on `/api/`. Clickjacking not possible.
- Errors do not echo bodies or phrases; the exception handler hides `what()`. Node logs after create/restore contain no phrases or keys (grepped).
- No request logging of bodies; process arguments carry no token when `--api-token-file` is used.

Wallet and mnemonic:
- Entropy comes from `RAND_bytes` only (checked and fails closed); no `rand()`/`mt19937` in key or wallet code. Creation self-checks the phrase round trip before returning.
- 13/25-word phrases, 15/19-bit checksum compared without early exit, exact word-count to entropy mapping, no accepted length other than 13 or 25. Swapped words, a wrong word, 12 words, and mixed-language phrases were all rejected. Multi-list auto-detection fails closed (AMBIGUOUS_LANGUAGE) rather than picking one.
- Same wallet from English, Arabic canonical, uppercase, tatweel + fatha decorated, and RLM/ZWSP-wrapped spellings (verified). Seed derivation is language independent (PBKDF2-HMAC-SHA512 over the entropy bytes), so index-aligned conversion gives the same wallet.
- HD derivation (BIP-32 style): master and child keys validated for 0 and >= n, hardened path, fail-closed on malformed paths; ECDSA signing emits low-S DER and re-verifies its own signature; verification is strict DER, canonical pubkey and low-S; no keyless forgery path.
- Wallet loading rejects a public key that does not match the private key. Unparseable entries are kept in the file rather than dropped.
- Transaction building: recipient address is checked first (wrong network and bad checksum rejected), amount must be an unsigned JSON integer (floats, negatives, strings rejected), insufficient funds and dust are rejected, change below dust goes to fees (max 545 units), immature coinbase and coins already spent in the mempool are excluded. Fee is `max(100000, size*100)` with a 107-byte-per-input signature estimate; a 4-input transaction was accepted. The UI cannot send a different destination than the one reviewed (the same `dest` string is shown and posted).
- Signing endpoint is domain-separated (`Bucks Signed Message:` prefix) and cannot be confused with a transaction signature.
- Wallet file writes are atomic (temp file `O_EXCL` 0600, fsync, rename, directory fsync); `.bak` also 0600.

Storage:
- SQLite: all statements prepared with bound parameters; the only concatenated SQL carries compile-time integers. Schema has STRICT tables, CHECK constraints, immutability triggers on headers, block data and undo, `SQLITE_DBCONFIG_DEFENSIVE` and `TRUSTED_SCHEMA=0`, foreign keys on, `cell_size_check`, `mmap_size=0`, WAL with full fsync. Directory must already be 0700 and owned by the user; chain DB, `-wal`, `-shm` and `chain.lock` are all 0600 (verified by `ls`); symlinked database or lock refused (`lstat`, `O_NOFOLLOW`); application id, schema version, network and genesis are checked at open.
- A second process on the same network directory is refused by `flock` (verified: "another Bucks process is already using ..."). (The wallet file is not covered, see WAL-2.)

Web UI:
- No `innerHTML`, `eval`, `document.write` or `insertAdjacentHTML` anywhere; every node is built with `createElement`/text nodes, and the only `DOMParser` use is on locally generated QR SVG. API data (addresses, hashes, error strings) only reaches text nodes or fixed `#/...` hrefs. CSP loads cleanly in the browser (no console violations).
- Recovery phrase handling: only in a module variable and the DOM while shown; nothing in `localStorage`, `sessionStorage` (only the selected address) or the URL (verified in the browser after creating a wallet); cleared after the check step; `beforeunload` guard while pending; the token is never stored and the input is cleared.
- Address validation mirrors the node (Base58Check, version byte per network: 0x1a main, 0x41 test, 0x7f regtest, identical to src/chainparams.cpp), and wrong-network addresses are refused with a clear message. Amount parsing is strict (BigInt, 8 decimals, safe-integer cap).
- QR encoder: independently decoded with Apple CoreImage (CIDetector) for three 34-character addresses, including a mainnet-style one; all decoded to the exact text. `web/test/*.mjs` pass under Node.
- `sw.js` only unregisters itself and clears caches.
