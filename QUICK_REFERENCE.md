# Quick Reference Guide

## Core Concepts

### Block Structure
- **Version**: Block version number
- **Previous Hash**: Hash of previous block (creates chain)
- **Merkle Root**: Root of transaction Merkle tree
- **Timestamp**: Unix timestamp
- **Difficulty**: Mining difficulty target
- **Nonce**: Number used for proof-of-work
- **Transactions**: List of transactions in block

### Transaction Structure
- **Inputs**: References to previous transaction outputs (UTXOs)
- **Outputs**: New outputs with amounts and addresses
- **Signature**: ECDSA signature proving ownership

### Key Terms
- **UTXO**: Unspent Transaction Output - available coins
- **Merkle Tree**: Binary tree of transaction hashes
- **Proof of Work**: Mining algorithm requiring computational work
- **Difficulty**: Target hash value for mining
- **Genesis Block**: First block in blockchain

## Example Scenarios

### Scenario 1: Create Genesis Block
```cpp
Blockchain chain;
Block genesis = chain.createGenesisBlock();
// Genesis block has:
// - prevHash = "0" (no previous block)
// - Coinbase transaction (block reward)
// - Difficulty = initial difficulty
```

### Scenario 2: Create Wallet and Address
```cpp
Wallet wallet;
wallet.generateKeys();
std::string address = wallet.getAddress();
// Address format: Base58 encoded public key hash
```

### Scenario 3: Create Transaction
```cpp
// Alice wants to send 50 coins to Bob
Wallet alice, bob;
std::string bobAddress = bob.getAddress();

// Get Alice's UTXOs
std::vector<UTXO> aliceUTXOs = chain.getUTXOs(alice.getAddress());

// Create transaction
Transaction tx = alice.createTransaction(
    bobAddress,      // recipient
    5000000000,      // amount (50 BUCKS in awraq)
    chain            // blockchain reference
);

// Transaction includes:
// - Inputs: references to Alice's UTXOs
// - Outputs: 50 to Bob, change back to Alice
// - Signatures: Alice's signature on inputs
```

### Scenario 4: Mine Block
```cpp
Block block;
block.prevHash = chain.getLastBlock().hash;
block.merkleRoot = merkleTree.buildTree(transactions);
block.timestamp = getCurrentTime();
block.difficulty = chain.getDifficulty();

// Mine block (find nonce)
Miner::mineBlock(block, block.difficulty);

// Add to chain
chain.addBlock(block);
```

### Scenario 5: Validate Chain
```cpp
bool isValid = chain.isValidChain();
// Checks:
// - Each block's hash is valid
// - Each block's proof-of-work is valid
// - Previous hash links are correct
// - All transactions are valid
// - No double spending
```

## Hash Formats

### Block Hash
- **Input**: Block header (version, prevHash, merkleRoot, timestamp, difficulty, nonce)
- **Algorithm**: Double SHA-256
- **Output**: 32 bytes (64 hex characters)
- **Example**: `000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f`

### Transaction Hash
- **Input**: Transaction data (version, inputs, outputs, lockTime)
- **Algorithm**: Double SHA-256
- **Output**: 32 bytes (64 hex characters)

### Merkle Root
- **Input**: Array of transaction hashes
- **Algorithm**: Binary tree with SHA-256
- **Output**: 32 bytes (64 hex characters)

## Difficulty and Mining

### Target Calculation
```
target = 2^(256 - difficulty_bits)
```

### Mining Process
```
1. Set nonce = 0
2. Calculate block hash
3. If hash < target: SUCCESS
4. Else: increment nonce, goto step 2
```

### Difficulty Adjustment
- Adjust every N blocks (e.g., every 2016 blocks)
- Target time: 10 minutes per block
- Formula: new_difficulty = old_difficulty * (actual_time / target_time)

## Address Format

### Generation Steps
1. Generate private key (32 random bytes)
2. Derive public key using ECDSA (secp256k1)
3. Hash public key: SHA-256 → RIPEMD-160
4. Add version byte (0x00 for mainnet)
5. Calculate checksum: first 4 bytes of SHA-256(SHA-256(version + hash))
6. Encode: Base58(version + hash + checksum)

### Example Address
```
1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa
```

## Transaction Validation Rules

1. **Signature Verification**
   - Input signature must match input's public key
   - Signature must be valid for transaction hash

2. **UTXO Validation**
   - Referenced UTXO must exist
   - UTXO must not be already spent
   - Input amount must match UTXO amount

3. **Output Validation**
   - All outputs must have valid addresses
   - Output amounts must be positive
   - Total output ≤ total input (fees allowed)

4. **Double Spending Check**
   - No input can reference same UTXO twice
   - UTXO must not be spent in same block

## Block Validation Rules

1. **Hash Validation**
   - Block hash must match calculated hash
   - Hash must be less than difficulty target

2. **Chain Validation**
   - Previous hash must match previous block's hash
   - Block height must be previous + 1

3. **Merkle Root Validation**
   - Merkle root must match calculated root from transactions

4. **Transaction Validation**
   - All transactions must be valid
   - First transaction can be coinbase (mining reward)
   - No double spending

## Common Operations

### Get Balance
```cpp
uint64_t balance = chain.getBalance(address);
// Sum of all UTXOs for this address
```

### Get UTXOs
```cpp
std::vector<UTXO> utxos = chain.getUTXOs(address);
// Returns all unspent outputs for address
```

### Verify Transaction
```cpp
bool isValid = transaction.verify();
// Checks signatures and UTXO references
```

### Serialize Block
```cpp
std::string serialized = block.serialize();
// Convert block to string/bytes for storage/transmission
```

## Performance Considerations

- **Mining**: CPU-intensive, difficulty determines time
- **Validation**: Fast, O(n) where n = number of transactions
- **UTXO Lookup**: O(1) with hash map
- **Merkle Tree**: O(n log n) construction, O(log n) verification

## Security Notes

- **Private Keys**: Must be kept secret, never shared
- **Double Spending**: Prevented by UTXO model and validation
- **51% Attack**: Theoretical vulnerability if attacker controls >50% hash power
- **Replay Attacks**: Prevented by transaction signatures and UTXO tracking
