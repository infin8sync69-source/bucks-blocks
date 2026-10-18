#!/usr/bin/env python3
"""Independent check of Bucks genesis blocks: rebuilds the genesis from its parameters with nothing but
hashlib (no code shared with src/) and compares the merkle root, the hash and the proof of work.

Byte layout (docs/design/consensus-validation.md section 3.5, src/genesis.cpp):
  header, 84 bytes, integers little endian:
    version u32 (2) | height u32 (0) | prev 32 zero bytes | merkle root 32 | time u32 | bits u32 | nonce u32
  coinbase: version u32 (2), 1 input {32 zero bytes, index 0xffffffff, scriptSig = LE32(0) + inscription,
    sequence 0xffffffff}, 1 output {amount u64 0, scriptPubKey 0x6a}, lockTime u32 0.
  The merkle root of the single transaction is its txid (SHA256d, no reversal). Hashes are shown in raw byte
  order, like hexStr() in the node. Proof of work: the hash read as a big-endian number is at most the target
  that `bits` encodes.

Check every network that has a genesis in src/chainparams.cpp (ctest runs this; main is reported as not set
until launch, or refused with --require-main):
    python3 tools/verify_genesis.py --chainparams src/chainparams.cpp [--require-main]

Check one candidate, e.g. the output of bucks-mine-genesis before it goes into chainparams.cpp:
    python3 tools/verify_genesis.py --time 1791958761 --nonce 35444257 --bits 1e00ffff \\
        --inscription "Bucks testnet genesis" [--merkle <hex>] [--hash <hex>]

Exit code 0 when every check passes, 1 otherwise, 2 for bad arguments or an unreadable chainparams.cpp.
"""
import argparse
import hashlib
import re
import struct
import sys

MAX_INSCRIPTION = 96


def sha256d(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def compact_size(n):
    if n < 0xFD:
        return bytes([n])
    if n <= 0xFFFF:
        return b"\xfd" + struct.pack("<H", n)
    if n <= 0xFFFFFFFF:
        return b"\xfe" + struct.pack("<I", n)
    return b"\xff" + struct.pack("<Q", n)


def coinbase(inscription):
    script_sig = struct.pack("<I", 0) + inscription
    tx = struct.pack("<I", 2)                                    # version
    tx += compact_size(1)                                        # one input
    tx += b"\x00" * 32 + struct.pack("<I", 0xFFFFFFFF)
    tx += compact_size(len(script_sig)) + script_sig
    tx += struct.pack("<I", 0xFFFFFFFF)                          # sequence
    tx += compact_size(1)                                        # one output
    tx += struct.pack("<Q", 0) + compact_size(1) + b"\x6a"
    tx += struct.pack("<I", 0)                                   # lockTime
    return tx


def target_from_bits(bits):
    size, word = bits >> 24, bits & 0x007FFFFF
    if bits & 0x00800000 or word == 0:
        return 0                                                 # negative or zero: nothing satisfies it
    return word >> (8 * (3 - size)) if size <= 3 else word << (8 * (size - 3))


def verify(name, time, nonce, bits, inscription, merkle=None, block_hash=None, out=sys.stdout):
    """Returns a list of failures (empty when the genesis is right) and prints what it computed."""
    failures = []
    raw = inscription.encode("ascii", "replace")
    if len(raw) > MAX_INSCRIPTION:
        failures.append("inscription longer than %d bytes" % MAX_INSCRIPTION)
    if any(c < 0x20 or c > 0x7E for c in inscription.encode("utf-8")):
        failures.append("inscription is not printable ASCII")
    if time == 0:
        failures.append("time is 0")
    tx = coinbase(raw)
    root = sha256d(tx)
    header = struct.pack("<II", 2, 0) + b"\x00" * 32 + root + struct.pack("<III", time, bits, nonce)
    assert len(header) == 84
    h = sha256d(header)
    target = target_from_bits(bits)
    print("%s: time %d, nonce %d, bits %08x, inscription %r" % (name, time, nonce, bits, inscription), file=out)
    print("  merkle root %s" % root.hex(), file=out)
    print("  hash        %s" % h.hex(), file=out)
    if int.from_bytes(h, "big") > target or target == 0:
        failures.append("hash does not meet the target %064x" % target)
    if merkle is not None and merkle.lower() != root.hex():
        failures.append("merkle root differs: expected %s" % merkle.lower())
    if block_hash is not None and block_hash.lower() != h.hex():
        failures.append("hash differs: expected %s" % block_hash.lower())
    for f in failures:
        print("  FAIL: " + f, file=out)
    if not failures:
        print("  ok", file=out)
    return failures


# ---------------------------------------------------------------- src/chainparams.cpp

FIELDS = ("genesisTime", "genesisNonce", "genesisInscription", "genesisMerkleRoot", "genesisHash", "powLimitBits")


ESCAPES = {'"': '"', "\\": "\\", "'": "'", "?": "?"}


def cpp_string(text):
    """The value of one or more adjacent C++ string literals; only the escapes an inscription can need
    (\\" and \\\\), because checkChainParams allows nothing but printable ASCII."""
    text = text.strip()
    out, i = [], 0
    while i < len(text):
        if text[i] != '"':
            raise ValueError("not a string literal: %s" % text)
        i += 1
        while True:
            if i >= len(text):
                raise ValueError("unterminated string literal: %s" % text)
            c = text[i]
            if c == '"':
                i += 1
                break
            if c == "\\":
                if i + 1 >= len(text) or text[i + 1] not in ESCAPES:
                    raise ValueError("unsupported escape in: %s" % text)
                out.append(ESCAPES[text[i + 1]])
                i += 2
            else:
                out.append(c)
                i += 1
        while i < len(text) and text[i].isspace():
            i += 1
    return "".join(out)


def scan(body):
    """Splits C++ source into statements at ';' outside string literals, dropping // and /* */ comments
    that are outside them too. Whitespace outside literals is kept as is."""
    statements, cur, i, n = [], [], 0, len(body)
    while i < n:
        c = body[i]
        if c == '"':
            j = i + 1
            while j < n and body[j] != '"':
                j += 2 if body[j] == "\\" else 1
            cur.append(body[i:j + 1])
            i = j + 1
        elif body.startswith("//", i):
            j = body.find("\n", i)
            i = n if j < 0 else j
        elif body.startswith("/*", i):
            j = body.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == ";":
            statements.append("".join(cur))
            cur = []
            i += 1
        else:
            cur.append(c)
            i += 1
    return statements


def parse_value(field, text):
    if field == "genesisInscription":
        return cpp_string(text)
    text = " ".join(text.split())
    if field in ("genesisMerkleRoot", "genesisHash"):
        if text == "Hash256{}":
            return "00" * 32
        m = re.fullmatch(r'hexHash\(\s*"([0-9a-f]{64})"\s*\)', text)
        if not m:
            raise ValueError("cannot read %s = %s" % (field, text))
        return m.group(1)
    m = re.fullmatch(r"(0x[0-9a-fA-F]+|[0-9]+)[uU]?", text)
    if not m:
        raise ValueError("cannot read %s = %s" % (field, text))
    return int(m.group(1), 0)


def read_chainparams(path):
    """The genesis fields of makeMain, makeTest and makeRegtest; a function that starts from makeMain()
    inherits what it does not set, as in the C++."""
    src = open(path, encoding="utf-8").read()
    networks = {}
    for fn, name in (("makeMain", "main"), ("makeTest", "test"), ("makeRegtest", "regtest")):
        m = re.search(r"ChainParams %s\(\)\s*\{(.*?)\n\}" % fn, src, re.S)
        if not m:
            raise ValueError("%s() not found in %s" % (fn, path))
        statements = scan(m.group(1))
        values = dict(networks["main"]) if any(re.search(r"ChainParams p = makeMain\(\)", t) for t in statements) else {}
        for t in statements:
            a = re.match(r"\s*p\.(\w+)\s*=(.*)", t, re.S)
            if a and a.group(1) in FIELDS:
                values[a.group(1)] = parse_value(a.group(1), a.group(2))
        missing = [f for f in FIELDS if f not in values]
        if missing:
            raise ValueError("%s() does not set %s" % (fn, ", ".join(missing)))
        networks[name] = values
    return networks


def self_test():
    """The parser reads what checkChainParams accepts: an inscription may contain ; // /* " and \\."""
    inscription = 'Fed; cut // "rates" \\ /* 100% */ done'
    literal = '"' + inscription.replace("\\", "\\\\").replace('"', '\\"') + '"'
    src = (
        "ChainParams makeMain() {\n"
        "    ChainParams p;\n"
        "    p.genesisTime = 0;  // a comment with ; and \"quotes\"\n"
        "    p.genesisInscription = " + literal + ";\n"
        "    p.genesisNonce = 7u; /* block; comment */\n"
        "    p.genesisMerkleRoot = Hash256{};\n"
        "    p.genesisHash = Hash256{};\n"
        "    p.powLimitBits = 0x1d00ffff;\n"
        "    return p;\n"
        "}\n")
    m = re.search(r"ChainParams makeMain\(\)\s*\{(.*?)\n\}", src, re.S)
    values = {}
    for t in scan(m.group(1)):
        a = re.match(r"\s*p\.(\w+)\s*=(.*)", t, re.S)
        if a and a.group(1) in FIELDS:
            values[a.group(1)] = parse_value(a.group(1), a.group(2))
    expect = {"genesisTime": 0, "genesisInscription": inscription, "genesisNonce": 7,
              "genesisMerkleRoot": "00" * 32, "genesisHash": "00" * 32, "powLimitBits": 0x1d00ffff}
    if values != expect:
        print("FAIL: parser self-test: %r != %r" % (values, expect))
        return 1
    print("ok: parser self-test")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chainparams", help="check every configured network in this chainparams.cpp")
    ap.add_argument("--require-main", action="store_true", help="fail if the mainnet genesis is not set")
    ap.add_argument("--self-test", action="store_true", help="check the chainparams.cpp parser on awkward inscriptions")
    ap.add_argument("--time", type=int)
    ap.add_argument("--nonce", type=int)
    ap.add_argument("--bits", type=lambda s: int(s, 16), help="hex, e.g. 1d00ffff")
    ap.add_argument("--inscription")
    ap.add_argument("--merkle", help="expected merkle root (hex, raw byte order)")
    ap.add_argument("--hash", dest="block_hash", help="expected block hash (hex, raw byte order)")
    a = ap.parse_args(argv)

    if a.self_test:
        return self_test()
    if a.chainparams:
        try:
            networks = read_chainparams(a.chainparams)
        except (OSError, ValueError) as e:
            print("error: %s" % e, file=sys.stderr)
            return 2
        failed = False
        for name in ("regtest", "test", "main"):
            p = networks[name]
            if p["genesisHash"] == "00" * 32:
                print("%s: genesis not set" % name)
                if name != "main" or a.require_main:
                    print("  FAIL: no genesis")
                    failed = True
                continue
            failed |= bool(verify(name, p["genesisTime"], p["genesisNonce"], p["powLimitBits"],
                                  p["genesisInscription"], p["genesisMerkleRoot"], p["genesisHash"]))
        return 1 if failed else 0

    if None in (a.time, a.nonce, a.bits, a.inscription):
        ap.error("give --chainparams, or --time, --nonce, --bits and --inscription")
    return 1 if verify("candidate", a.time, a.nonce, a.bits, a.inscription, a.merkle, a.block_hash) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
