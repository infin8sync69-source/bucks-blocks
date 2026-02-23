# Data Flow and Component Interactions

## Block Creation Flow

```
1. User creates transaction
   └─> Wallet.signTransaction()
       └─> Transaction.sign()
           └─> ECDSA signature

2. Transaction added to mempool
   └─> Transaction.calculateHash()
       └─> SHA-256(tx data)

3. Miner selects transactions
   └─> Build Merkle Tree
       └─> MerkleTree.buildTree()
           └─> Merkle root hash

4. Create block header
   ├─> Previous block hash
   ├─> Merkle root
   ├─> Timestamp
   ├─> Difficulty
   └─> Nonce (starts at 0)

5. Mining process
   └─> Miner.mineBlock()
       ├─> Calculate target from difficulty
       ├─> Block.calculateHash()
       │   └─> SHA-256(SHA-256(header))
       ├─> Check if hash < target
       ├─> If not: increment nonce, repeat
       └─> If yes: block mined!

6. Add block to chain
   └─> Blockchain.addBlock()
       ├─> Validate block
       ├─> Update UTXO set
       └─> Append to chain
```

## Transaction Flow

```
Transaction Creation:
┌─────────────┐
│   Wallet    │
│  (Sender)   │
└──────┬──────┘
       │ 1. Create transaction
       │    - Select UTXOs
       │    - Calculate change
       │    - Create outputs
       ▼
┌─────────────┐
│ Transaction │
│  (Unsigned) │
└──────┬──────┘
       │ 2. Sign transaction
       │    - Hash transaction
       │    - Sign with private key
       ▼
┌─────────────┐
│ Transaction │
│   (Signed)  │
└──────┬──────┘
       │ 3. Broadcast to network
       │    (In our case: add to mempool)
       ▼
┌─────────────┐
│   Miner     │
└──────┬──────┘
       │ 4. Include in block
       ▼
┌─────────────┐
│    Block    │
└──────┬──────┘
       │ 5. Mine block
       ▼
┌─────────────┐
│ Blockchain  │
└─────────────┘
```

## UTXO Lifecycle

```
UTXO Creation:
Transaction Output
    │
    ├─> Amount
    ├─> Address (public key hash)
    └─> Locking script
        │
        └─> Added to UTXO Set
            └─> Key: txHash:outputIndex
                Value: UTXO object

UTXO Spending:
Transaction Input
    │
    ├─> References: prevTxHash:outputIndex
    ├─> Unlocking script (signature + pubkey)
    └─> Verification:
        │
        ├─> Check UTXO exists
        ├─> Verify signature
        ├─> Check amount sufficient
        └─> If valid:
            │
            ├─> Remove from UTXO set
            └─> Create new UTXOs (outputs)
```

## Block Validation Flow

```
Block Validation:
┌─────────────┐
│    Block    │
└──────┬──────┘
       │
       ├─> 1. Check block hash
       │   └─> SHA-256(SHA-256(header)) == blockHash
       │
       ├─> 2. Check proof of work
       │   └─> blockHash < target
       │
       ├─> 3. Check previous hash
       │   └─> block.prevHash == prevBlock.hash
       │
       ├─> 4. Check Merkle root
       │   └─> MerkleTree.root == block.merkleRoot
       │
       ├─> 5. Validate transactions
       │   ├─> Verify signatures
       │   ├─> Check UTXO references
       │   ├─> Check amounts
       │   └─> No double spending
       │
       └─> 6. Update UTXO set
           ├─> Remove spent UTXOs
           └─> Add new UTXOs
```

## Hash Relationships

```
Block Hash Calculation:
blockHash = SHA256(SHA256(
    version +
    prevHash +
    merkleRoot +
    timestamp +
    difficulty +
    nonce
))

Transaction Hash Calculation:
txHash = SHA256(SHA256(
    version +
    inputs[] +
    outputs[] +
    lockTime
))

Merkle Root Calculation:
merkleRoot = buildTree([
    SHA256(SHA256(tx1)),
    SHA256(SHA256(tx2)),
    SHA256(SHA256(tx3)),
    ...
])
```

## Address Generation

```
Private Key (32 bytes, random)
    │
    └─> ECDSA Key Generation
        │
        ├─> Public Key (65 bytes, uncompressed)
        │   │
        │   └─> SHA-256(publicKey)
        │       │
        │       └─> RIPEMD-160(hash)
        │           │
        │           └─> Add version byte (0x00)
        │               │
        │               └─> SHA-256(SHA-256(version + hash))
        │                   │
        │                   └─> Take first 4 bytes (checksum)
        │                       │
        │                       └─> version + hash + checksum
        │                           │
        │                           └─> Base58Encode()
        │                               │
        │                               └─> Address (e.g., "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa")
```

## Component Dependencies

```
Blockchain
    ├─> depends on: Block, Transaction, UTXO
    └─> uses: MerkleTree, Miner

Block
    ├─> contains: Transaction[]
    └─> uses: SHA-256, MerkleTree

Transaction
    ├─> contains: TransactionInput[], TransactionOutput[]
    └─> uses: ECDSA, SHA-256

Wallet
    ├─> generates: KeyPair
    └─> creates: Transaction

Miner
    └─> uses: Block, SHA-256, ProofOfWork

MerkleTree
    └─> uses: SHA-256

UTXO
    └─> referenced by: TransactionInput
```
