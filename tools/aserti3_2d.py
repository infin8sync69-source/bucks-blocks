#!/usr/bin/env python3
"""Independent reference of the aserti3-2d difficulty rule (Bitcoin Cash upgrade specification
2020-11-15-asert.md, version 0.6), written from the specification's pseudo-code with Python's
unbounded integers, so it checks src/pow.cpp without sharing its 256-bit arithmetic.

    python3 -I tools/aserti3_2d.py check tests/unit/data/aserti3-2d/run*
        validates the official test vectors (prints one line per file, exit 1 on a mismatch)
    python3 -I tools/aserti3_2d.py edge
        prints the extra vectors embedded in tests/unit/test_pow.cpp (Pow.AsertEdgeVectors)
    python3 -I tools/aserti3_2d.py random <count> <seed>
        prints random cases, one per line: refBits timeDiff heightDiff powLimitBits halfLife result
"""
import random
import sys

SPACING = 600
HALF_LIFE = 172800      # 2 days
RADIX = 1 << 16


def bits_to_target(bits):
    size, word = bits >> 24, bits & 0x007FFFFF
    assert not (bits & 0x00800000), 'negative bits'
    return word >> (8 * (3 - size)) if size <= 3 else word << (8 * (size - 3))


def target_to_bits(target):
    size = (target.bit_length() + 7) // 8
    c = target << (8 * (3 - size)) if size <= 3 else target >> (8 * (size - 3))
    if c & 0x00800000:
        c >>= 8
        size += 1
    return c | (size << 24)


def trunc_div(a, b):
    # Exact truncating division (the specification's note 3); int(a / b) loses precision above 2^53.
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def aserti3_2d(ref_target, time_diff, height_diff, pow_limit, half_life=HALF_LIFE, spacing=SPACING):
    """Next target for the child of the evaluation block. time_diff = evaluation time - anchor
    parent time, height_diff = evaluation height - anchor height."""
    assert 0 < ref_target <= pow_limit and height_diff >= 0
    exponent = trunc_div((time_diff - spacing * (height_diff + 1)) * RADIX, half_life)
    shifts = exponent >> 16                  # floor, as an arithmetic shift
    frac = exponent - shifts * RADIX
    poly = 195766423245049 * frac + 971821376 * frac ** 2 + 5127 * frac ** 3 + 2 ** 47
    assert poly < 2 ** 64                    # the specification's note 7: fits in uint64
    factor = (poly >> 48) + RADIX
    target = ref_target * factor
    target = target >> -shifts if shifts < 0 else target << shifts
    target >>= 16
    if target == 0:
        return 1
    return min(target, pow_limit)


def check(paths):
    ok = True
    for path in paths:
        head, rows = {}, 0
        with open(path, encoding='ascii') as f:
            for line in f:
                line = line.strip()
                if line.startswith('##') and ':' in line:
                    k, v = line[2:].split(':', 1)
                    head[k.strip()] = v.strip()
                elif line and not line.startswith('#'):
                    _, h, t, want = line.split()
                    got = target_to_bits(aserti3_2d(bits_to_target(int(head['anchor nBits'], 16)),
                                                    int(t) - int(head['anchor parent time']),
                                                    int(h) - int(head['anchor height']),
                                                    bits_to_target(0x1d00ffff)))
                    rows += 1
                    if got != int(want, 16):
                        print(f'{path}: height {h}: got 0x{got:08x}, want {want}')
                        ok = False
        if rows != int(head['iterations']):
            print(f'{path}: {rows} rows, header says {head["iterations"]}')
            ok = False
        print(f'{path}: {rows} vectors checked')
    return ok


def edge():
    # Cases the official vectors (all at powLimit 0x1d00ffff) do not reach: the testnet limit
    # 0x1e00ffff, the largest limit the startup assertion admits (0x1e7fffff), shifts of 256 bits
    # or more, the 1-target floor, a negative exponent that truncates to 0 and the last
    # fractional step before a whole shift (largest polynomial).
    cases = []
    for limit, refs in ((0x1d00ffff, (0x01010000,)), (0x1e00ffff, (0x1e00ffff, 0x1c0ffff0)),
                        (0x1e7fffff, (0x1e7fffff, 0x01010000))):
        for ref in refs:
            for td, hd in ((600, 0), (-7200, 0), (7800, 0), (1_200_000, 10),
                           (SPACING * 1001 + 172799, 1000), (SPACING * 1001 - 1, 1000),
                           (2**32, 0), (-2**32, 2**32 - 1), (172800 * 300, 0), (-172800 * 300, 0),
                           (SPACING * 2 + 2, 1)):
                cases.append((ref, td, hd, limit))
    for ref, td, hd, limit in cases:
        want = target_to_bits(aserti3_2d(bits_to_target(ref), td, hd, bits_to_target(limit)))
        print(f'        {{0x{ref:08x}, {td}, {hd}, 0x{limit:08x}, 0x{want:08x}}},')


def random_cases(count, seed):
    # Inputs stay inside what a chain can produce (src/pow.cpp): heightDiff below 2^32 and
    # |timeDiff| at most 2^33, because both come from 32-bit header fields.
    rnd = random.Random(seed)
    for _ in range(count):
        limit = rnd.choice((0x1d00ffff, 0x1e00ffff, 0x1e7fffff))
        lt = bits_to_target(limit)
        ref = target_to_bits(max(1, rnd.randint(1, lt) >> rnd.randint(0, 220)))
        hd = rnd.choice((0, 1, rnd.randint(0, 5000), rnd.randint(0, 2**32 - 1)))
        td = SPACING * (hd + 1) + rnd.choice((rnd.randint(-2000, 2000), rnd.randint(-5_000_000, 5_000_000)))
        if abs(td) > 2**33 or rnd.random() < 0.2:
            td = rnd.randint(-2**33, 2**33)
        hl = rnd.choice((HALF_LIFE, 3600, rnd.randint(1, 10_000_000)))
        want = target_to_bits(aserti3_2d(bits_to_target(ref), td, hd, lt, hl))
        print(f'{ref:08x} {td} {hd} {limit:08x} {hl} {want:08x}')


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'check':
        sys.exit(0 if check(sys.argv[2:]) else 1)
    elif len(sys.argv) == 2 and sys.argv[1] == 'edge':
        edge()
    elif len(sys.argv) == 4 and sys.argv[1] == 'random':
        random_cases(int(sys.argv[2]), int(sys.argv[3]))
    else:
        sys.exit(__doc__)
