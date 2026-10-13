#!/usr/bin/env python3
"""Writes the seed corpus of tests/fuzz/corpus/<target>/ from the byte layouts of
docs/design/consensus-validation.md (sections 3.5, 4, 5.4), computed here without the C++ code,
so the seeds are an independent check of the encoders as well as fuzzer food.

    python3 tools/make_fuzz_corpus.py
"""
import hashlib, os, struct

root = os.path.join(os.path.dirname(__file__), '..', 'tests', 'fuzz', 'corpus')

def sha256d(b): return hashlib.sha256(hashlib.sha256(b).digest()).digest()
def compact(n):
    if n < 0xFD: return bytes([n])
    if n <= 0xFFFF: return b'\xfd' + struct.pack('<H', n)
    if n <= 0xFFFFFFFF: return b'\xfe' + struct.pack('<I', n)
    return b'\xff' + struct.pack('<Q', n)

def write(target, name, data):
    d = os.path.join(root, target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), 'wb') as f: f.write(data)

# V-TX-1 (section 5.4)
vtx1 = bytes.fromhex(
    "0200000002b5bc37193893ea8bd44b15fa740ef4a1b55178c9bd4311346a14767a8d95231e010000006a47304402202501be07e71d53496ba8eaf91eb75647247b9b1a6fab4c6e40902e239c79d05b02207ae641960c78565d17ca9257e5337303b3ac56789eea6fe51718a117b386620e0121022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cfffffffffb8081d4d2e3b6439ca8ecf94856e0a6226ba328e9df210e08d4a41a8ac1bede6000000006a47304402206bbdb2e3d4e972aad2e7b6f2a436d656f03bef746ac8e18afd39d0c6c98f861d02203f106868c0f5482226cb3d0a09c004d791908722d2ca37e62e024312a0cf45f20121022234063b5187e027c46ef631d200ac1ee5d43d75b8829a5048ff8983016579cffeffffff02008c8647000000001976a914e3a42ed3976ddeecae5a126beee85549f1b1125788ac0000000000000000076a056275636b7300000000")
assert len(vtx1) == 354

# Regtest genesis coinbase and block (section 3.5)
inscription = b"Bucks regtest genesis"
script_sig = b"\x00\x00\x00\x00" + inscription
coinbase = (struct.pack('<I', 2) + compact(1) + b'\x00' * 32 + b'\xff' * 4 + compact(len(script_sig)) + script_sig +
            b'\xff' * 4 + compact(1) + struct.pack('<Q', 0) + compact(1) + b'\x6a' + struct.pack('<I', 0))
assert len(coinbase) == 86
txid = sha256d(coinbase)
header = (struct.pack('<I', 2) + struct.pack('<I', 0) + b'\x00' * 32 + txid + struct.pack('<I', 1767225600) +
          struct.pack('<I', 0x207fffff) + struct.pack('<I', 0))
assert len(header) == 84
assert sha256d(header).hex() == "3491115920c29000d16f7d776bbab4a3399e5065abeeaf73fa23a273406bf520"
genesis_block = header + compact(1) + coinbase

write('transaction', 'v-tx-1', vtx1)
write('transaction', 'regtest-coinbase', coinbase)
write('header', 'regtest-genesis-header', header)
write('block', 'regtest-genesis-block', genesis_block)
write('block', 'block-with-spend', header + compact(2) + coinbase + vtx1)

# Block undo record v1 (section 4.7): one spent coin, then three, with gold bytes
def coin(height, cb, amount, key):
    script = bytes([0x76, 0xa9, 0x14]) + bytes([key]) * 20 + bytes([0x88, 0xac])
    return struct.pack('<I', height) + bytes([1 if cb else 0]) + struct.pack('<Q', amount) + compact(len(script)) + script
def undo(block_hash, coins, gold=b''):
    body = b'\x01' + block_hash + compact(len(coins)) + b''.join(coins) + compact(len(gold)) + gold
    return body + sha256d(body)
u1 = undo(b'\xab' * 32, [coin(5, False, 1000, 0x11)])
assert len(u1) == 106
# The undo target's first byte is a mode switch (low bit set: recompute the checksum), then the record.
write('undo', 'one-coin', b'\x00' + u1)
write('undo', 'three-coins-gold', b'\x00' + undo(b'\x5a' * 32, [coin(1, True, 5000000000, 1), coin(2, False, 12345, 2), coin(3, False, 1, 3)], b'\x01\x02\x03\x04'))
write('undo', 'empty', b'\x00' + undo(b'\x00' * 32, []))
write('undo', 'fix-checksum-one-coin', b'\x01' + u1[:-32] + b'\x00' * 32)

# P2P frames (regtest magic B7 C5 E3 5D)
def frame(cmd, payload, magic=bytes.fromhex('b7c5e35d')):
    return magic + cmd.encode().ljust(12, b'\0') + struct.pack('<I', len(payload)) + sha256d(payload)[:4] + payload
write('netmessage', 'tx', frame('tx', vtx1))
write('netmessage', 'block', frame('block', genesis_block))
write('netmessage', 'ping', frame('ping', b'\x01\x02\x03\x04\x05\x06\x07\x08'))
write('netmessage', 'getheaders', frame('getheaders', struct.pack('<I', 1) + compact(1) + b'\x34' * 32 + b'\x00' * 32))
write('netmessage', 'empty-verack', frame('verack', b''))

# CompactSize vectors (section 4.1): valid, then the three non-minimal forms
for name, hexs in [('v0', '00'), ('v252', 'fc'), ('v253', 'fdfd00'), ('v65535', 'fdffff'), ('v65536', 'fe00000100'),
                   ('v100000', 'fea0860100'), ('v4294967295', 'feffffffff'), ('bad-fdfc00', 'fdfc00'),
                   ('bad-fe00010000', 'fe00010000'), ('bad-feffff0000', 'feffff0000'),
                   ('big-ff', 'ff0000000100000000'), ('varbytes', '03aabbcc'), ('varbytes-short', '05aabb')]:
    write('compactsize', name, bytes.fromhex(hexs))
print('corpus written to', os.path.normpath(root))
