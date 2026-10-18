# aserti3-2d test vectors

The official test vectors of the aserti3-2d difficulty rule (Bitcoin Cash upgrade specification
`2020-11-15-asert.md`, section "Test vectors"), copied unchanged from
https://gitlab.com/bitcoin-cash-node/bchn-sw/qa-assets, directory `test_vectors/aserti3-2d`,
commit `e1a6a9d4b0cc701b9fdd57204a4d611af419b3d0`, fetched 2026-10-17. MIT licence: see `LICENSE`.

Each file has a header (anchor height, anchor parent time, anchor nBits) and rows
`iteration height time target`: the target is the one the child of a block at that height and
time must carry. Parameters: powLimit `0x1d00ffff`, spacing 600 s, half-life 172,800 s.
`Pow.AsertVectors` (tests/unit/test_pow.cpp) checks every row; `python3 -I tools/aserti3_2d.py
check tests/unit/data/aserti3-2d/run*` checks them with the independent Python reference.

| File | SHA-256 |
|---|---|
| run01 | `fd49ffbfda52ca07acac56b19ea4458248f31c075e12b63dd4bf71d61c690afd` |
| run02 | `e8b772c407a7f6613daf6e49498e73c8b3b18b1c20aba5b3c08dd6b37aa2aef0` |
| run03 | `512080a24491df4f15bb874c20d77ae244d854de6a50a5a9a6ec501996808a8f` |
| run04 | `19c577bf7e4256d8db559cb485b976347d4a581db24caee1bd95857b0a68df10` |
| run05 | `4cfd246bdcfa42770d65e7b92edce36b75e882240b448a0207c0a1569df9107e` |
| run06 | `792e14651c25f83afe290817497fd7252ddde1d4574dd2f1f1b5d558e401addc` |
| run07 | `4488cbb6703526d7d53f18b69a4cc97ed124942d35a2f2f3c15c82da68f5c9b0` |
| run08 | `ff5287a5d2f8f877f251e9f0e485cf4c45e277b0bf13056165e69804ffead209` |
| run09 | `c5268d37b11114b705361c631493905b8ddf5751b1fbe87daab87037bba019b5` |
| run10 | `f75f9885ae32d2261d77afadfe281b7440074a7e59158e0fbf61cb03057c9659` |
| run11 | `f912d4fb2edcf65037fa7e8c279c49a291e8e74ce507b15b3478146fe653ad88` |
| run12 | `897418f85dcb17814ea7b7556547565271c524723ece8af8d47fab4bdc0ab179` |
