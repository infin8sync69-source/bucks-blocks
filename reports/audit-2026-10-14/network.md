# Network / P2P / Concurrency Audit — 2026-10-14

Scope: `src/network.cpp`, `include/network.h`, `src/protocol_enc.cpp`,
`include/protocol_enc.h`, `src/node.cpp`/`node.h`, `src/miner.cpp`,
`src/server.cpp` threading, `src/main.cpp`, `src/mempool_file.cpp`,
`src/fsutil.cpp`, plus `docs/design/storage-concurrency.md` and the P2P
section of `docs/design/consensus-validation.md`.

Method: full read of every in-scope file, plus a private build
(`cmake -S . -B <scratchpad>/build`, built outside the repo's own `build/`)
exercised against raw sockets on isolated regtest instances in the
scratchpad, on ports/datadirs that do not touch the repo's own
`blockchain.db` / `utxodb.sqlite` / `wallets.json` or `~/.bucks`. Every
process I started has been killed; the one pre-existing `bucks` process the
user already had running (pid 32784, `~/bucks-regtest2`) was left untouched.

## Summary

| Severity | Count |
|---|---|
| Critical | 1 |
| High | 2 |
| Medium | 2 |
| Low | 1 |
| Info | 1 |

---

## NET-1 — Critical — Single-frame, unauthenticated, permanent CPU-spin hang of the whole node

**File/line:** `src/network.cpp`, `Peer::disconnect()` (L276-286) not being
followed by a `peers.erase()` on two of its three non-loop call sites:
`Peer::sendMsg` L328 (write-timeout path) and `Peer::processBuffer` L359-365
(oversized-frame path); the fallout is in `Network::run()` L867-905,
specifically the `select()` call at L883 and the `if (activity < 0) { ...;
continue; }` branch at L885-892.

**What is wrong:**
`Network::peers` is a `std::unordered_map<int, std::unique_ptr<Peer>>` keyed
by the raw socket fd, and `Network::run()`'s `select()` loop puts every
entry's fd into the read-set every iteration (L876-879). A `Peer` is removed
from this map in exactly five places (`network.cpp` L1026, 1036, 1083-1084,
1092, 1097/1103) — zombie-handshake timeout, block-sync stall, receive
buffer overflow, orderly peer close (`recv()==0`), and a hard recv error.

It is **not** removed when:
* `Peer::processBuffer()` bans+disconnects a peer for sending an oversized
  frame header (`network.cpp` L359-365: `network.banIP(ip); disconnect();
  return;`), or
* `Peer::sendMsg()` gives up after its 5-second write deadline and calls
  `disconnect()` (L328) because the peer never drains its receive buffer.

In both cases `disconnect()` calls `close(socket_fd)` (L283) but the `Peer`
object — and its now-closed fd — stays in `peers`, and that stale fd is
still pushed into `readFds` on every subsequent loop iteration. `select()`
on a set containing a closed fd returns `-1`/`EBADF`. The `activity < 0`
branch in `Network::run()` only checks for `EINTR`; for every other error
(including `EBADF`) it just does `continue` — straight back to the top of
the loop, rebuilds the identical poisoned fd set, and calls `select()`
again. No cleanup function (`pruneZombiePeers()`, `handleSocketIO()`) ever
runs again, because they're only reached *after* a successful `select()`.
The network thread is now in an unconditional, unthrottled `select()` retry
loop: 100% of one CPU core, forever, until the process is killed.

While this is happening, `acceptNewConnection()` is never called again (new
inbound peers/API-adjacent diagnostics never see "Accepted connection"), no
further bytes are read from or written to **any** peer, no blocks/txs are
relayed, `Network::peerCount()`/`broadcast()` (which also take
`peersMutex`, uncontended here since the hang is in `select()` itself, not
inside the mutex — but the node is still functionally dead on the network
side), and `Network::stop()` would itself hang forever in
`networkThread.join()` since the thread never returns from `run()` — meaning
even `SIGTERM`/`SIGINT` cannot cleanly shut the node down; `main.cpp`'s
`network.stop()` on shutdown would block indefinitely.

**Concrete reproduction (proven on a private build):**
One TCP connection to the node's P2P port, one 24-byte message header
(no body needed), nothing else:

```python
import socket, struct
MAGIC = bytes([0xB7,0xC5,0xE3,0x5D])  # regtest magic
s = socket.socket(); s.connect(('127.0.0.1', P2P_PORT))
s.sendall(MAGIC + b'ping'.ljust(12, b'\0') + struct.pack('<I', 5_000_000) + b'\0\0\0\0')
```

Observed on the target node:
```
DEBUG: Peer 127.0.0.1 sent an oversized ping (5000000 bytes). Banning.
```
...and then nothing else, ever. `ps` showed the process pinned at
97-100% CPU indefinitely (watched for 4+ minutes; a second, well-behaved
client's `connect()` succeeded at the TCP level but never received even the
node's own unsolicited `ENCINIT` within a 10-second wait — i.e. the node is
fully unresponsive to *every* peer, not just the attacker). Confirmed via
`lldb -p <pid> -o "bt all"` that the sole network thread is parked inside
`Bucks::Network::run()` at the `select()` call.

I also reproduced the same underlying bug via the slow-loris path explicitly
named in the task brief (`Peer::sendMsg`'s 5 s write deadline): connecting
with a shrunk `SO_RCVBUF`, sending a large `ping` and never reading the
reply, which forces the 5-second write-timeout `disconnect()` at L328 and
the identical permanent hang.

**Impact:** any single TCP connection to the P2P port — no handshake,
no valid magic beyond the header, no encryption negotiation, from any
address including ones that would otherwise be ban-exempt (localhost/LAN) —
permanently wedges the node's entire P2P subsystem at 100% CPU with no
automatic recovery, and leaves it unable to even shut down cleanly on
`SIGTERM`. This is a trivial, remotely-triggerable denial of service against
every reachable Bucks node, and under "Harden the node for a public
testnet" this is the highest-priority item in the whole audit.

**Suggested fix:** erase the entry from `peers` (or mark it and sweep
immediately) at both missed call sites — simplest is to have
`Peer::disconnect()` not be the only thing that matters; have every
`disconnect()` call site in `network.cpp` go through one helper that also
removes the fd from `peers` (or, if `disconnect()` is called from inside an
iterator-based loop already, erase via the iterator as the other five sites
do). At minimum, harden `Network::run()` itself so a negative `select()`
return is never fatal to progress: on `EBADF` specifically, drop any fd in
the set whose `fcntl(fd, F_GETFD)` fails before retrying, so a stale map
entry can never wedge the loop again — this is a good defense-in-depth
backstop even after the root cause is fixed.

**Verified:** yes (reproduced live against a private build; CPU pinned,
`select()`/`EBADF` loop confirmed with `lldb`, no recovery after 4+ minutes;
test node killed).

---

## NET-2 — High — Uncaught `std::stoi` exception in `Network::handleAddrMessage` crashes the node from a single `ADDR` message

**File/line:** `src/network.cpp` L1194-1231, specifically L1215:
`uint16_t port = static_cast<uint16_t>(std::stoi(addr.substr(colonPos + 1)));`

**What is wrong:** `Peer::handleMessage`'s `ADDR` case (L621-648) parses
attacker-controlled address strings with essentially no validation (just a
length bound) and hands them to `Network::queueAddresses()`, which
`Network::flushQueues()` later passes to `Network::handleAddrMessage()` on
the network thread, **outside** any try/catch (`network.cpp` L941: `for
(const auto& list : addrs) handleAddrMessage(list);`). Inside, `std::stoi`
on the substring after the colon throws `std::invalid_argument` (non-numeric
port) or `std::out_of_range` (oversized numeral) for arbitrary attacker
input such as `"1.2.3.4:notaport"`. Nothing catches it on this path, so it
propagates out of the network thread and `std::terminate()`s the whole
process.

Contrast this with the identical parse in `Network::discoverPeers()`
(L1154: `try { portNum = std::stoi(...); } catch (...) { continue; }`),
which is correctly guarded, and with `Peer::handleMessage`'s own call sites,
which run inside `processBuffer`'s try/catch (`network.cpp` L373-381) and so
would only cost misbehavior points, not a crash — the fix pattern already
exists elsewhere in the same file, it's just missing here.

**Reproduction (proven on a private build):** connect, do **not** reply to
the node's `ENCINIT` (so the session stays unencrypted), send one plaintext
`addr` frame with a single malformed entry:

```python
payload = struct.pack('<I', 1) + struct.pack('<H', len(b"1.2.3.4:notaport")) + b"1.2.3.4:notaport"
s.sendall(frame('addr', payload))
```

Node's own stderr, verbatim, followed by process exit:
```
DEBUG: Peer 127.0.0.1 msg: 'addr'
DEBUG: Received 1 peer addresses
libc++abi: terminating due to uncaught exception of type std::invalid_argument: stoi: no conversion
```
The process was gone immediately afterward (confirmed via `ps`).

**Impact:** a single unauthenticated frame from any peer that ever connects
(inbound or outbound) kills the entire node process. No handshake or
encryption negotiation is needed, since `ADDR` is accepted and queued before
the session is required to be encrypted.

**Suggested fix:** wrap the `std::stoi` call in `handleAddrMessage` in the
same `try { ... } catch (...) { continue; }` pattern used in
`discoverPeers()`, and reject/skip malformed entries instead of crashing.
Same for the equally unguarded `std::stoi` on the peer-supplied port in
`Peer::handleMessage`'s `ADDR` case at L621-648 if it is ever moved outside
`processBuffer`'s try/catch in a refactor.

**Verified:** yes (reproduced live against a private build; process
terminated with the exact libc++abi message above).

---

## NET-3 — High — P2P session encryption is opportunistic with no enforcement, no peer authentication, and no replay protection

**File/line:** `src/protocol_enc.cpp` (whole file), `src/network.cpp`
`Peer::handleMessage` `ENCINIT`/default dispatch (L418-653).

**What is wrong (three related gaps, all in the design rather than a single
line):**

1. **Silent plaintext fallback, not a negotiated requirement.** Encryption
   is applied transparently *if and only if* `encryptionNegotiated` is
   already `true` at send/receive time (`sendMsg` L293, `handleMessage`
   L422). A peer that simply never sends its own `ENCINIT` (or whose
   `ENCINIT` is dropped/stripped by something in the path) causes both
   sides to silently exchange every subsequent message — `VERSION`,
   `INV`, `BLOCK`, `TX`, everything — in cleartext, with **no** log message,
   penalty, or refusal distinguishing "peer doesn't support encryption"
   from "an active intermediary is stripping it." I exercised this directly
   while building NET-1/NET-2's PoCs: simply not answering the node's
   unsolicited `ENCINIT` was sufficient to talk to it in plaintext for the
   rest of the session; nothing in the protocol ever required otherwise.
2. **Unauthenticated ECDH, so no protection against an active MITM.**
   `P2PSessionEncryption` generates a fresh ephemeral keypair per connection
   and derives the shared secret purely from the two ephemeral public keys
   (`establishSharedSecret` → `computeSharedSecret`, `protocol_enc.cpp`
   L20-25). There is no long-term identity key, no signature over the
   ephemeral key, and no out-of-band pinning. A network-level
   man-in-the-middle that terminates both TCP legs can run two completely
   independent, successfully-authenticated (by this protocol's own
   definition — AES-GCM only proves "the two ends of *this* leg agree on a
   key") encrypted sessions, one with each side, and transparently
   decrypt/re-encrypt/relay or modify everything in between. This is the
   textbook failure mode of unauthenticated Diffie-Hellman and is not
   mitigated anywhere in this code path.
3. **No replay protection.** AES-256-GCM is correctly used for
   confidentiality + integrity (a real improvement captured in the code's
   own comments: this replaces an older unauthenticated CBC scheme), and
   the random 12-byte IV per message means no nonce reuse within a session.
   But there is no sequence number, message counter, or session id bound
   into the AAD or ciphertext. A on-path or passive-then-replaying attacker
   who captures one valid encrypted frame (e.g. a `PONG`, an `INV`) can
   resend that exact byte sequence later in the same still-open connection
   and it will decrypt and be processed again as if newly received —
   `decrypt()` only verifies the GCM tag, never freshness.

**Impact:** operators who assume "the node encrypts its P2P traffic" get
confidentiality/integrity against passive eavesdropping only when *both*
peers happen to complete the handshake, and get no protection at all
against an active attacker positioned to intercept the TCP connection (the
exact adversary encryption is usually added to defend against). Given this
is explicitly a "harden the node for a public testnet" change, this is a
meaningful gap, though it does not itself cause a crash/hang like NET-1/2.

**Suggested fix:** (a) make `ENCINIT` mandatory — refuse to proceed past
the handshake (disconnect, don't silently continue in plaintext) if the
peer doesn't send one within a short timeout; (b) bind the ephemeral keys
to something verifiable — at minimum log/expose a fingerprint so operators
can pin known-good peers, ideally a lightweight signature/identity scheme;
(c) add a monotonically increasing per-direction counter into the AAD so a
replayed ciphertext fails authentication.

**Verified:** point 1 (plaintext fallback) is directly verified — it is how
the NET-1 and NET-2 PoCs were delivered, in plaintext, with no handshake
completed. Points 2 and 3 (MITM, replay) are unverified, reasoning only — I
did not stand up an actual two-legged MITM proxy or a replay harness; they
follow directly from reading `computeSharedSecret`/`establishSharedSecret`
and `decrypt()` and are standard, well-understood properties of
unauthenticated-ECDH + counter-less-AEAD designs, not a guess about unseen
code.

---

## NET-4 — Medium — `connectToPeer`'s `connect()` and `resolveIPv4`'s `getaddrinfo()` are both blocking calls made directly on the single network thread

**File/line:** `src/network.cpp` L753-800 (`resolveIPv4`, `connectToPeer`),
called from `Network::maintainConnections()` (L816-865),
`Network::discoverPeers()` (L1133-1192), and `Network::handleAddrMessage()`
(L1194-1231) — all three run synchronously inside `Network::run()`'s single
thread.

**What is wrong:** `resolveIPv4()` calls `getaddrinfo()` with no timeout;
`connectToPeer()`'s socket is created without `O_NONBLOCK` before `connect()`
is called (non-blocking mode is only set in `Peer`'s constructor,
*after* `connectToPeer` already returns), so `connect()` itself is a
blocking syscall subject to the OS's default TCP connect timeout (commonly
tens of seconds to minutes for a filtered or black-holed destination).
Both calls happen directly on the same thread that runs the `select()` loop,
`handleSocketIO()`, and `flushQueues()` for every other peer. A
misconfigured or malicious `--connect`/`--seed` target (a hostname whose DNS
answer hangs, or an IP that is firewalled rather than immediately refused)
stalls the entire P2P subsystem — no accepts, no reads/writes to any other
peer, no relay — for as long as the hang lasts, and `maintainConnections()`
retries this on every due redial, so the stall can recur indefinitely with
a misconfigured persistent peer.

**Reproduction attempted, inconclusive:** I pointed `--connect` at a
non-routable RFC 5737/private test address from this sandbox; in this
particular environment the kernel returned "no route to host" immediately
rather than hanging (the sandbox's network namespace has no default route
for that prefix, so there was nothing to black-hole against). This does not
contradict the finding — `connect()` not being set non-blocking before the
call, and `getaddrinfo()` having no timeout, are plain facts from reading
the code — but I was not able to produce an actual multi-second stall in
this constrained network environment, so I'm not claiming a timed
reproduction here.

**Suggested fix:** set `O_NONBLOCK` on the socket in `connectToPeer()`
*before* calling `connect()` (the existing `EINPROGRESS` handling already
assumes this is the case! `connectToPeer` already has `if (errno !=
EINPROGRESS) { close(s); return false; }` — logic written for a
non-blocking connect but applied to a blocking socket, so it currently just
masks the blocking wait instead of avoiding it), and run `getaddrinfo()`
off the network thread (a small resolver thread pool, or cache results from
the main loop without blocking it).

**Verified:** unverified, reasoning only (code inspection of the missing
`O_NONBLOCK` and the unbounded `getaddrinfo()` call; a timed hang was not
reproduced in this sandbox's network environment).

---

## NET-5 — Medium — `Peer::sendMsg`'s 5-second write deadline is held under the single global `peersMutex`, so one slow peer can stall every other peer and the API for up to 5 s per message

**File/line:** `src/network.cpp` `Network::handleSocketIO()` (L1063-1110,
takes `peersMutex` for the whole function body) → `Peer::processBuffer()` →
`Peer::handleMessage()` → `Peer::sendMsg()` (L288-330, up to a 5-second
`poll()`-based retry loop).

**What is wrong:** `handleSocketIO()` holds `peersMutex` for its entire
loop over every peer, and for any peer whose incoming message triggers a
synchronous reply (`PING`→`PONG`, `GETDATA`→`BLOCK`/`TX`,
`ENCINIT`→`VERSION`, `GETADDR`→`ADDR`, etc.), that reply is sent via
`sendMsg()` *while still holding `peersMutex`*. If that peer is slow to
drain its receive window (classic slow-loris: shrink `SO_RCVBUF`, never
`recv()`), `sendMsg()` legitimately blocks for up to its full 5-second
deadline before giving up — and for that whole window, `peersMutex` is
held, so no other peer can be read from or written to, no new connection
can be accepted (`acceptNewConnection()` runs earlier in the same
`Network::run()` iteration, so it isn't blocked *by* this, but the next
iteration's accept is delayed by however long this one took), and
`Network::peerCount()`/`Network::broadcast()`/`Network::flushQueues()`
(used by block/tx relay and the API's status endpoint) also block on the
same mutex for up to 5 s.

**Reproduction (proven on a private build):** this is the same slow-loris
setup used in NET-1's secondary reproduction (shrink `SO_RCVBUF`, send a
large `ping`, never read the `pong`). I confirmed a second, well-behaved
peer's connection was accepted at the TCP level immediately but received
nothing at the application level for the full observation window — in this
build the stall in fact never ends (it hits NET-1's permanent-hang bug
instead of recovering after 5 s), which also means I could not cleanly
isolate "exactly a 5-second stall, then full recovery" as a standalone,
bounded phenomenon separate from NET-1. The 5-second mutex-holding
mechanism itself, however, is a direct, unambiguous reading of the code
(`handleSocketIO`'s lock scope visibly wraps `processBuffer`/`handleMessage`
/`sendMsg`), independent of whether NET-1's cleanup bug is also present.

**Suggested fix:** don't do synchronous, potentially-blocking sends to
arbitrary peers while holding the lock that guards every other peer's I/O —
queue replies (the codebase already has exactly this pattern for broadcasts,
via `queueBroadcast`/`relayQueue`/`flushQueues`) and send them outside the
per-peer-iteration lock, or take a per-peer lock instead of the global one
for the send path.

**Verified:** partially — the lock-holds-across-a-blocking-call structure is
verified by direct code reading; the resulting "other peers starve for up
to 5 s" consequence is verified in combination with NET-1 (where it in fact
never recovers), but a clean, isolated "stalls for exactly ~5 s and then
resumes" scenario (i.e. with NET-1 fixed) was not separately reproduced.

---

## NET-6 — Low — `Network::acceptNewConnection`'s per-IP connection-rate throttle and `MAX_PEERS`/`MAX_PEERS_PER_IP` Sybil guards are sound but only cover inbound connections

**File/line:** `src/network.cpp` L944-1014.

**What is wrong:** The 5-second per-IP reconnect throttle
(`lastConnectionTimes`, L965-978), the per-IP cap (`MAX_PEERS_PER_IP = 8`,
L986-1001) and the global `MAX_PEERS = 125` cap (L1003-1011) are all applied
only in `acceptNewConnection()`, i.e. to inbound connections. Outbound
connections made by this node (via `connectToPeer`, from
`maintainConnections`, `discoverPeers`, or `handleAddrMessage`) are not
checked against `MAX_PEERS_PER_IP` or `MAX_PEERS` at all before insertion
into `peers` (`network.cpp` L796-799). `handleAddrMessage` does cap how many
*new* connections it will dial per call (`peers.size() + toConnect.size() >=
MAX_PEERS / 2`, L1209) but that is a one-shot check at collection time, not
an enforced ceiling — if multiple address-list messages arrive close
together from different peers before any of the dialled connections land in
`peers`, the nominal `MAX_PEERS/2` soft cap can be exceeded, and repeated
`ADDR` floods advertising many distinct IPs could still walk the node's
outbound slots toward peers chosen entirely by whichever remote peer sent
the addresses — a mild eclipse-amplification vector (an attacker who can
get its preferred IPs into enough `ADDR` messages has a better-than-nothing
chance of occupying a meaningful fraction of this node's *outbound* peers,
which are normally the harder-to-eclipse slot type in Bitcoin-style
networks precisely because outbound peer selection is usually more
curated/diversified than what's implemented here — there's no per-/16 or
similar network-diversity bucketing anywhere in this file, just "not an IP
we're already connected to" and "not banned").

**Impact:** low on its own (bounded by the existing size caps most of the
time, and by `MAX_PEERS` globally), but it means the per-IP and total caps
that are clearly intended as the node's Sybil/eclipse defense are not
uniformly enforced across both connection directions, and there is no
network-diversity selection at all for outbound peers, which is the
property that actually matters most for eclipse resistance.

**Suggested fix:** apply the same `MAX_PEERS_PER_IP` check in
`connectToPeer()` itself (covers all three outbound call sites in one
place), and consider bucketing known/candidate peers by network prefix
(e.g. /16) when choosing who to dial, rather than purely "first not already
connected, not banned."

**Verified:** unverified, reasoning only (code inspection; not exploited
live — building a convincing multi-peer eclipse demonstration was out of
proportion to the finding's severity).

---

## Info — `mempool.dat` loader, startup/shutdown, header-first sync, 100-block reorg path: checked and found sound

* **`src/mempool_file.cpp`** (`decodeMempoolFile`): minimum-length check,
  trailing 32-byte double-SHA256 checksum verified before anything else is
  trusted, fixed 4-byte magic, version byte, 4-byte chain tag, transaction
  count capped at `MEMPOOL_FILE_MAX_TXS`, each transaction read via
  `Reader::varBytes` capped at `Policy::MAX_STANDARD_TX_SIZE`, all wrapped
  in a single try/catch over `ParseError`. A hand-crafted hostile
  `mempool.dat` (truncated, wrong magic, wrong version, wrong chain,
  oversized count, oversized/corrupt entries, bad checksum) is rejected
  cleanly at each stage with a log line and no transactions loaded; nothing
  here is reachable without first tampering with the file on disk, i.e. not
  remote. `Node::loadMempoolFile()`'s retry loop (parents possibly ordered
  after children) terminates correctly since it only continues while
  `reloadValidated` makes forward progress (`n == 0` breaks).
* **`src/fsutil.cpp`**: `writePrivateFileAtomic` (temp file + `fsync` +
  `rename` + directory `fsync`) is a correct crash-safe atomic-write
  pattern; mode 0600 via `O_CREAT|O_EXCL` with a saved umask is right for
  secrets.
* **`src/chaindb.cpp`**'s `chain.lock` `flock(LOCK_EX | LOCK_NB)`: correctly
  scoped per-process, dies with the process (no stale-lock-after-crash
  problem), returns a clear error rather than corrupting a second
  instance's view of the database.
* **Header-first sync / 100-block reorg / fork-alert path**
  (`src/chainstate.cpp` `activateBestChain`, `raiseForkAlert`,
  `approveReorg`): the depth check against `params_.maxAutoReorgDepth`
  (100 blocks on main/regtest) correctly gates automatic reorgs, parks the
  deeper candidate in `skipped_` rather than looping forever retrying it
  every call, and the operator's `POST /api/node/approve-reorg` path
  (`Node::approveReorg` → `ChainState::approveReorg`) re-runs
  `activateBestChain` with the approval recorded, clearing `skipped_` for
  that hash. I did not find a wrong-fork lockup: `pickCandidate()` always
  re-scans for the best *non-skipped* chain, so a node that's stuck behind
  a fork alert still correctly tracks and can still switch to a *third*,
  even-better chain if one later has enough work, without needing the
  operator's approval for the one that's still parked.
* **`Peer::requestedBlocks` / `headersInFlight` stall handling**
  (`pruneZombiePeers`, `requestHeaders`/`requestMissingBlocks`): the 60 s
  in-flight-block timeout and 10 s headers-in-flight cooldown look
  reasonable and are not obviously gameable into a permanent stall from the
  logic alone (independent of NET-1, which of course defeats all of this
  once triggered, since `pruneZombiePeers` never runs again either).
* **`src/server.cpp` auto-mine thread / shutdown ordering** (`startAutoMine`
  /`stopAutoMine`/`autoMineLoop`, `Server::stop()`): `stop()` correctly
  stops the miner (`autoMineStop`/`autoMining` + `join()`) *before* tearing
  down the HTTP server and before `main.cpp` calls `network.stop()` then
  `node.shutdown()` — the miner thread's only shared state is the chain
  lock (`Miner::lock_` = `Node::mu_`) and `wallets`/`walletMutex`, both
  properly guarded; I did not find a lock-ordering inversion between the
  chain lock and `walletMutex` (the miner never holds both at once — wallet
  access in `autoMineLoop`/`mineBlock` is copied out under `walletMutex`
  before mining starts, and mining itself only ever takes the chain lock).
* **`SIGPIPE`/signal handling** (`main.cpp`): `SIGPIPE` is ignored
  globally and `send()` additionally uses `MSG_NOSIGNAL` where available
  (`network.cpp` L303-307) — belt-and-suspenders, correct. `SIGINT`/
  `SIGTERM` only flip a lock-free `std::atomic<bool>`, safe from a signal
  handler.

## What I did not verify

* NET-3 points 2 and 3 (MITM, replay) and NET-4 (blocking DNS/connect) are
  explicitly marked unverified above — standing up a real MITM proxy or a
  hostile DNS resolver was judged disproportionate effort for this pass;
  the code-level claims themselves are unambiguous on inspection.
* I did not run the ThreadSanitizer build (`tools/ci.sh tsan`) — given how
  quickly NET-1 and NET-2 turned up with plain functional testing, and the
  time budget for this pass, I prioritized proving those over a TSan sweep.
  I'd recommend running it next, specifically around `Network::peers`
  mutation during the window NET-1 describes (erasing while a stale fd is
  still selected on is exactly the kind of thing TSan is good at catching
  structurally, even though the bug here is a logic bug, not a data race).
* I did not fuzz `NetMessage::deserialize`/the per-command parsers
  (`parseInvs`, `parseGetHeaders`, `parseHeaders`) beyond reading them
  closely; they all route through `Reader`/`ParseError` and are called only
  from inside `processBuffer`'s try/catch, which looks correctly
  exception-safe. The project's existing fuzz targets
  (`tests/fuzz`, CI'd per `829c5b3`) are better suited to that than a
  manual pass.

All test node processes I started (ports 19101/19102, 19201, 19301, 19401,
various scratchpad datadirs) have been killed. The user's own pre-existing
`bucks` process (pid 32784, `~/bucks-regtest2`, port 18745) was left
running untouched, as it predates and is unrelated to this audit.
