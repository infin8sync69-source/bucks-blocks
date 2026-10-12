#!/usr/bin/env python3
"""Generate include/arabic_nfkd_table.h for the "arabic" mnemonic normalizer.

The table lists every code point in the Arabic Unicode blocks whose NFKD form
differs from itself (presentation forms, ligatures, and the precomposed
hamza/madda letters), together with its full NFKD decomposition.  The C++
fold in src/bip_8192.cpp applies it before stripping marks, so it never needs
ICU.  Re-run this script (and commit the header) when the Unicode version of
the Python used here changes.

Usage: python3 tools/make_arabic_nfkd_table.py > include/arabic_nfkd_table.h
"""
import sys
import unicodedata

BLOCKS = [
    (0x0600, 0x06FF),  # Arabic
    (0x0750, 0x077F),  # Arabic Supplement
    (0x08A0, 0x08FF),  # Arabic Extended-A
    (0xFB50, 0xFDFF),  # Arabic Presentation Forms-A
    (0xFE70, 0xFEFF),  # Arabic Presentation Forms-B
]


def main():
    entries = []
    for lo, hi in BLOCKS:
        for cp in range(lo, hi + 1):
            ch = chr(cp)
            dec = unicodedata.normalize("NFKD", ch)
            if dec != ch:
                entries.append((cp, [ord(c) for c in dec]))

    pool = []
    rows = []
    for cp, seq in entries:
        rows.append((cp, len(pool), len(seq)))
        pool.extend(seq)

    out = sys.stdout
    out.write("// GENERATED FILE - do not edit by hand.\n")
    out.write("// Produced by tools/make_arabic_nfkd_table.py with Python %s, "
              "Unicode %s.\n" % (sys.version.split()[0], unicodedata.unidata_version))
    out.write("//\n// NFKD decompositions of every code point in the Arabic blocks "
              "(U+0600-06FF,\n// U+0750-077F, U+08A0-08FF, U+FB50-FDFF, U+FE70-FEFF) "
              "that decomposes.\n// Rows are sorted by code point; `offset`/`length` "
              "index ARABIC_NFKD_POOL.\n")
    out.write("#ifndef ARABIC_NFKD_TABLE_H\n#define ARABIC_NFKD_TABLE_H\n\n")
    out.write("#include <cstdint>\n#include <cstddef>\n\nnamespace Bucks {\n\n")
    out.write("struct ArabicNfkdRow {\n    uint32_t cp;\n    uint16_t offset;\n"
              "    uint16_t length;\n};\n\n")
    out.write("static constexpr size_t ARABIC_NFKD_ROWS = %d;\n" % len(rows))
    out.write("static constexpr size_t ARABIC_NFKD_POOL_SIZE = %d;\n\n" % len(pool))
    out.write("static constexpr ArabicNfkdRow ARABIC_NFKD_TABLE[ARABIC_NFKD_ROWS] = {\n")
    for cp, off, ln in rows:
        out.write("    {0x%04X, %d, %d},\n" % (cp, off, ln))
    out.write("};\n\n")
    out.write("static constexpr uint32_t ARABIC_NFKD_POOL[ARABIC_NFKD_POOL_SIZE] = {\n")
    for i in range(0, len(pool), 8):
        out.write("    " + ", ".join("0x%04X" % v for v in pool[i:i + 8]) + ",\n")
    out.write("};\n\n} // namespace Bucks\n\n#endif // ARABIC_NFKD_TABLE_H\n")


if __name__ == "__main__":
    main()
