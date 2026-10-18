# Mainnet launch runbook

The exact steps to start the Bucks main network from its genesis block, in order. It follows the launch
rehearsal of 2026-10-17 (a throwaway genesis mined, verified, built in and run on two nodes; nothing of it
was committed). Steps 1-5 run on a build machine, 6-7 on the servers, 8 on a second machine.

Everything marked **Owner decision** is a checkbox: the launch does not start until each one is ticked.
`docs/MAINNET_INPUTS.md` explains each open input; this runbook only says when it is needed.

## 0. Before launch day

Owner decisions, all needed before the genesis is mined (each is frozen by the genesis or by the first
mainnet wallets):

- [ ] **Owner decision: launch difficulty.** Keep `powLimitBits = 0x1d00ffff` (about 2^32 hashes per block)
      or choose another limit, using a hashrate measured on the launch hardware (audit-consensus NEW-1,
      mining-launch ML-1). 10-minute blocks at `0x1d00ffff` need about 7.2 MH/s in total; below that,
      blocks are slower and stay that way, because the target never gets easier than the limit.
- [x] **Difficulty algorithm: decided 2026-10-18, ASERT** (`docs/MAINNET_INPUTS.md`). Nothing to do here.
- [ ] **Owner decision: HD coin type** in the derivation path (gate WALLET-COINTYPE): keep the placeholder
      `1` deliberately or choose another value. Every phrase made after launch depends on it, including the
      mining payout wallet of step 7.
- [ ] **Owner decision: final word lists.** Decided 2026-10-18: freeze the current drafts as version 1
      (final ids, status `release`, SHA-256 pins; `docs/LAUNCH_READINESS.md`, "Launch commit"). Tick when done.
- [ ] **Owner decision: genesis inscription**: printable ASCII, at most 96 bytes (for example a dated news
      headline of launch day).
- [ ] **Owner decision: seed nodes**: at least one host name or address with port 8733 that will run a
      seed node from step 6 onwards.
- [ ] **Owner decision: release tag name and GitHub default branch** (gate HYG-1): a plain `git clone`
      currently gets an unrelated branch with no code.
- [ ] **Owner decision: licence** of the released code (gate HYG-2).
- [ ] **Owner decision: release checkpoints and `minimumChainWork`** (`docs/MAINNET_INPUTS.md` item 3): they
      stay empty in the first release, because no chain exists to take them from, and the next release adds
      them from the running chain (see "After launch"); or the owner chooses otherwise now.
- [ ] **Owner decision: launch hashrate**: how many miner hosts and threads run at launch. Far more than
      needed hands the first periods' coins to whoever mines first; far less makes blocks slow (see the
      first decision).
- [ ] The other open items of `docs/MAINNET_INPUTS.md` are closed: external audit, public testnet soak of
      several weeks, legal and regulatory advice.
- [ ] Each server's clock is NTP-synchronised (`timedatectl` shows `System clock synchronized: yes`):
      blocks more than 2 hours ahead of a node's clock are rejected.

## 1. Build machine: build and test the release candidate

```bash
sudo apt-get install -y build-essential cmake git libssl-dev ca-certificates python3
git clone https://github.com/infin8sync69-source/bucks-blocks.git && cd bucks-blocks
git checkout <launch branch>          # the commit that will become the release, minus the genesis
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure      # everything passes
build/bucks --network=main --server             # must still refuse: exit 3, "main genesis is not configured"
```

## 2. Mine the genesis

Mine it on launch day: the genesis time must not be more than 2 hours ahead of the miners' clocks (they
refuse to build block 1), and a genesis more than 24 hours old when the nodes start makes them report
"syncing" and refuse peer transactions until block 1 is found.

```bash
T=$(date +%s)
build/bucks-mine-genesis --network=main --time=$T --inscription="<the inscription>"
```

- It uses every core (`--threads=<n>` to limit it) and prints its rate and progress on stderr every ten
  seconds. One time value covers 2^32 nonces; at the launch limit that takes about 40 minutes on one core
  of a 2.8 GHz Xeon without SHA-NI and about 10 minutes on four, and succeeds with a probability of about
  63%. Otherwise the tool moves the time on by one second and carries on.
- **Use the `time` it prints**, not the `--time` you passed. Write down all six output lines:

```
network:     main
time:        <unix time>
nonce:       <nonce>
inscription: <the inscription>
merkle root: <64 hex>
hash:        <64 hex, starting with at least 8 zeros>
```

## 3. Verify it independently

`tools/verify_genesis.py` rebuilds the genesis with Python's `hashlib` only, sharing no code with the node:

```bash
python3 -I tools/verify_genesis.py --time <time> --nonce <nonce> --bits 1d00ffff \
    --inscription "<the inscription>" --merkle <merkle root> --hash <hash>
```

It must print the same merkle root and hash followed by `ok`, and exit 0 (it checks the proof of work as
well). Do not continue otherwise. (Use the launch limit decided in step 0 for `--bits` if it is not
`1d00ffff`.)

## 4. Put the genesis and the seeds into the code

In `src/chainparams.cpp`, `makeMain()`:

```cpp
    // Mined with: bucks-mine-genesis --network=main --time=<time> --inscription="<the inscription>"
    p.genesisTime = <time>;
    p.genesisNonce = <nonce>u;
    p.genesisInscription = "<the inscription>";
    p.genesisMerkleRoot =
        hexHash("<merkle root>");
    p.genesisHash =
        hexHash("<hash>");
    p.seedNodes = {"<seed host>:8733"};
```

`makeTest()` overrides every genesis field, so testnet does not change. The genesis checkpoint is added
automatically. Release checkpoints and `minimumChainWork` stay empty for the first release unless the
owner decision in step 0 says otherwise. Set `BUCKS_VERSION` in `include/version.h` to the release number
if it changes (a node reports it with its network, for example `0.2.0-main`).

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Every test must pass. With the genesis set, `Genesis.MainnetIsMinedAndConfigured` stops being skipped and
checks the values, and the `genesis_independent` ctest recomputes the mainnet genesis from
`src/chainparams.cpp` with the Python verifier. Then:

```bash
build/bucks --network=main --server --datadir /tmp/bucks-launch-check   # starts; prints height 0
curl -s http://127.0.0.1:8080/api/blocks/height/0                       # "hash" is the genesis hash
```

Stop it (Ctrl-C) and delete `/tmp/bucks-launch-check`.

## 5. Commit, tag, publish

```bash
git commit -am "Set the mainnet genesis and seed nodes"
git tag -s <release tag> -m "Bucks <release tag>: mainnet genesis <hash>"
git push origin <launch branch> <release tag>
```

- [ ] **Owner decision applied**: on GitHub, set the default branch to the launch branch and publish a
      release for the tag.
- Publish the genesis hash, time, nonce and inscription, the tag and its commit id, so anyone can check
  them with step 3.

## 6. Start the seed node(s)

On each seed server (Ubuntu 22.04 or 24.04), from a checkout of the release tag:

```bash
sudo BUCKS_ROLE=seed BUCKS_REF=<release tag> BUCKS_COMMIT=<its commit> \
     BUCKS_SEEDS="<other seeds, if any>" BUCKS_START=1 deploy/bootstrap-ubuntu.sh
journalctl -u bucks -n 30        # "Network: main", "Current height: 0", P2P started on 8733
curl -s http://127.0.0.1:8080/api/blockchain/info     # "network":"main","height":0, tipHash = genesis hash
```

The bootstrap builds and tests the tag, installs it to `/opt/bucks`, opens SSH and 8733 in the firewall
and nothing else. Details: [deploy/README.md](../deploy/README.md).

## 7. Start the miner(s)

1. On **your own computer** (not the miner server), build the release, start
   `build/bucks --network=main --server`, open the console (<http://127.0.0.1:8080>), create a wallet,
   write the phrase down offline and confirm it, and copy its `B...` address. This is the payout address;
   its key never goes to the server.
2. On each miner server:

```bash
sudo BUCKS_ROLE=miner BUCKS_REF=<release tag> BUCKS_COMMIT=<its commit> \
     BUCKS_MINE_ADDRESS=<your B... address> BUCKS_SEEDS="<seed host>:8733" BUCKS_START=1 \
     deploy/bootstrap-ubuntu.sh
curl -s http://127.0.0.1:8080/api/mining/status      # "mining":true, a hashRate, the threads
```

Start the seed first and the miner second, so the miner's first block reaches the seed at once. The
miner uses all cores but one (`--mine-threads` in `/etc/bucks/bucks.env`).

## 8. Verify from a second machine

On a machine that is neither the seed nor the miner, build the release tag and run:

```bash
build/bucks --network=main --server --datadir ~/bucks-verify      # the seed is compiled in
curl -s http://127.0.0.1:8080/api/blocks/height/0                 # same genesis hash as published
curl -s http://127.0.0.1:8080/api/status                          # "peerCount" at least 1
curl -s http://127.0.0.1:8080/api/blockchain/info                 # after block 1: same height and
                                                                  # tipHash as the seed and the miner
```

The launch is done when this node, the seed and the miner report the same tip after the first blocks.

## After launch

- Mined coins can be spent after 100 more blocks. The difficulty (ASERT) adjusts on every block; at the minimum it cannot get easier.
- `bucks-healthcheck` (every 5 minutes) fails on a stopped miner, a stale tip or a fork alert; a fork
  deeper than 100 blocks waits for an operator decision (`POST /api/node/approve-reorg`).
- Configure the encrypted backups (`deploy/README.md`) on any node that holds wallets.
- Never reuse a testnet phrase on mainnet.
- The next release adds checkpoints and `minimumChainWork` from the running chain.

## If the genesis has to be redone

Before anyone else has mined, a genesis can still be replaced: repeat steps 2-8 with a new tag. A node
refuses a data directory made with another genesis (exit 3), so on every node stop the service and
delete only `<datadir>/main/chain.sqlite*`. Never delete the whole data directory: `wallets.json` is in
it. After other people have mined on it, a new genesis is a new chain.
