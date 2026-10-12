# Bucks Gold Standard — Vault Governance

## Overview

Every gram of gold backing a Bucks coin is tracked on-chain via a verified
OP_RETURN marker. No gold can be minted or redeemed without cryptographic proof
from at least **3 of 5** authorised vault keyholders. This document records who
those keyholders are, how the signing ceremony works, and how keys are rotated
or revoked.

---

## 1. The Five Vault Keyholders

| # | Role | Responsibility |
|---|------|----------------|
| 1 | **Founding Team** | Custodian of the master operational key; initiates mint/redeem requests |
| 2 | **Independent Auditor** | Third-party gold auditing firm; signs only after physical verification |
| 3 | **Community Representative** | Elected by coin-holders; provides democratic oversight |
| 4 | **Standards Board** | Classical gold standard body; confirms coin weight (4.25 g mithqal) |
| 5 | **Geographic Reserve** | Physical gold storage facility in the relevant jurisdiction |

All five keyholders must keep their private keys on air-gapped hardware signing
devices (e.g. Ledger, Trezor, or custom HSM). No keyholder's private key ever
touches an internet-connected computer.

---

## 2. On-Chain Signature Format

Gold mint and redemption events are published as OP_RETURN outputs in regular
transactions. The script layout is:

```
OP_RETURN  [0x6a]
PUSH_4     [0x04]
MARKER     "GOLD" (0x474F4C44) or "REDM" (0x5245444D)
PUSH_8     [0x08]
AMOUNT_LE8 <8 bytes, grams × 1000, little-endian>
-- repeated for each signer (minimum 3) --
KEY_IDX    <1 byte: 0–4, index into VAULT_AUTHORITY_PUBKEYS>
SIG_LEN    <1 byte: length of following DER signature>
SIG_BYTES  <SIG_LEN bytes: ECDSA-secp256k1 DER signature>
```

The message each signer signs is:

```
sha256( MARKER_LE4 || AMOUNT_LE8 )
```

A node accepts the event only if it can verify **at least 3 distinct valid
signatures** from the published pubkey list in `consensus.h`.

---

## 3. Signing Ceremony — Minting

1. The vault receives a physical gold deposit (minimum 4.25 g per classical
   gold standard weight, mithqal).
2. The Independent Auditor (keyholder 2) performs an in-person assay and weight
   verification.
3. The Founding Team (keyholder 1) prepares an unsigned mint transaction
   containing the OP_RETURN payload with the verified gram amount.
4. The unsigned transaction hash is shared out-of-band (QR code, courier) with
   all five keyholders.
5. Each keyholder independently verifies the physical gold documentation before
   signing.
6. At least **3 keyholders** return their DER signatures.
7. A designated broadcaster assembles the final transaction and broadcasts it.
8. Once included in a block and confirmed by 6 blocks, the gold reserve is
   considered updated.

---

## 4. Signing Ceremony — Redemption

The process is identical to minting, except:

- The MARKER is `REDM` (0x5245444D).
- The amount represents grams being physically withdrawn from the vault.
- The Standards Board (keyholder 4) must be one of the three signers to confirm
  the physical gold is being correctly disbursed.
- `goldReserve` is decremented on-chain by the verified amount.

---

## 5. Key Rotation

Keys are reviewed **annually** (or immediately after any suspected compromise).
Rotation procedure:

1. A proposal transaction is broadcast listing the new compressed pubkey for the
   keyholder being rotated.
2. The proposal must carry valid signatures from **4 of the 5** current
   keyholders (heightened threshold for key management).
3. Once confirmed in 100 blocks (coinbase maturity), the `consensus.h` constant
   `VAULT_AUTHORITY_PUBKEYS` is updated in a scheduled hard fork.
4. The outgoing key is considered retired; any future gold events signed by it
   are rejected.

---

## 6. Emergency Key Revocation

If a keyholder is compromised or unresponsive:

1. The remaining 4 keyholders sign a revocation + replacement announcement.
2. The announcement includes the new replacement pubkey.
3. Nodes running the current software version will reject the compromised key
   from the moment the revocation transaction is confirmed.
4. A hard fork activating the new keyset is released within 72 hours.

---

## 7. Replacing Placeholder Keys Before Mainnet

`consensus.h` currently contains placeholder pubkeys:

```cpp
"02aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa01"
```

**Before mainnet launch**, each keyholder must:

1. Generate a fresh secp256k1 keypair on an air-gapped device.
2. Export the **compressed** 33-byte public key in hex (66 characters).
3. Deliver it to the development team through a verified out-of-band channel.

The development team then replaces the placeholder entries in `consensus.h`
and releases a signed software build. **Do not launch mainnet with placeholder
pubkeys.**
