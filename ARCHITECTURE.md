# Bucks Blockchain Architecture

## Overview
Bucks is a production-grade blockchain implementation in C++ featuring core blockchain functionality: blocks, transactions, proof-of-work mining, cryptographic security, and a multi-threaded web server for network access.

## Core Components

### 1. Block Structure
- **Block Header:**
  - Previous block hash (32 bytes)
  - Merkle root (32 bytes)
  - Timestamp
  - Difficulty target (nBits)
  - Nonce (for mining)
  - Block version
  - Block height

- **Block Body:**
  - Transaction count
  - List of transactions
  - Block hash (calculated from header)

### 2. Transaction Structure
- **Transaction Input (UTXO):**
  - Previous transaction hash
  - Output index
  - Signature script (signature + public key)
  - Sequence number

- **Transaction Output:**
  - Amount (in awraq)
  - Public key hash (address)
  - Locking script

- **Transaction:**
  - Version
  - Input count and inputs
  - Output count and outputs
  - Lock time
  - Transaction ID (hash)

### 3. Cryptographic Components
- **SHA-256 Hashing:** Double SHA-256 for block hashes
- **ECDSA Signatures:** For transaction signing (using secp256k1 curve)
- **RIPEMD-160:** For address generation
- **Base58 Encoding:** For human-readable addresses

### 4. Proof of Work
- Dynamic difficulty adjustment algorithm
- Target calculation from difficulty bits
- Nonce incrementing and hash checking
- Mining loop with configurable difficulty
- Block reward halving mechanism

### 5. Merkle Tree
- Binary tree of transaction hashes
- Root calculation for block validation
- Efficient transaction verification
- Optimized for large transaction sets

### 6. Blockchain Management
- Genesis block creation
- Block validation (hash, proof-of-work, transactions)
- Chain validation (previous hash links)
- UTXO set management with SQLite persistence
- Transaction validation and double-spend prevention
- Consensus rule enforcement

### 7. Wallet
- Secure key pair generation (private/public)
- Address generation with checksums
- Transaction creation and signing
- Balance calculation from UTXO set
- Multi-wallet support

### 8. Web Server & API
- HTTP REST API for blockchain interaction
- Multi-threaded request handling
- Rate limiting for DoS protection
- CORS support for cross-origin requests
- WebSocket support for real-time updates
- Static file serving for Web UI

### 9. Database Layer
- SQLite-based UTXO database
- Transaction support for atomic operations
- Indexed lookups for performance
- Persistent storage of blockchain state

## System Architecture

```
┌─────────────────────────────────────────────────┐
│              Web UI (Frontend)                  │
│         HTML/CSS/JavaScript                     │
└───────────────────┬─────────────────────────────┘
                    │ HTTP/REST API
┌───────────────────▼─────────────────────────────┐
│            HTTP Server (C++)                    │
│  - Request routing                              │
│  - Authentication & rate limiting               │
│  - JSON serialization                           │
└───────────────────┬─────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────┐
│           Blockchain Core                       │
│  ┌─────────────────────────────────────┐       │
│  │  Block Manager                      │       │
│  │  - Block creation                   │       │
│  │  - Validation                       │       │
│  │  - Chain management                 │       │
│  └─────────────────────────────────────┘       │
│  ┌─────────────────────────────────────┐       │
│  │  Transaction Manager                │       │
│  │  - TX validation                    │       │
│  │  - Signature verification           │       │
│  │  - Mempool management               │       │
│  └─────────────────────────────────────┘       │
│  ┌─────────────────────────────────────┐       │
│  │  Wallet Manager                     │       │
│  │  - Key management                   │       │
│  │  - TX creation                      │       │
│  │  - Balance tracking                 │       │
│  └─────────────────────────────────────┘       │
└───────────────────┬─────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────┐
│          UTXO Database (SQLite)                 │
│  - Persistent storage                           │
│  - Transaction support                          │
│  - Indexed queries                              │
└─────────────────────────────────────────────────┘
```

## File Structure

```
Blockchain/
├── src/
│   ├── block.cpp/h          # Block structure and operations
│   ├── blockheader.h        # Block header definition
│   ├── transaction.cpp/h    # Transaction structure and operations
│   ├── crypto.cpp/h         # Cryptographic functions
│   ├── merkle.cpp/h         # Merkle tree implementation
│   ├── blockchain.cpp/h     # Blockchain management
│   ├── wallet.cpp/h         # Wallet functionality
│   ├── utxo.cpp/h           # UTXO data structure
│   ├── utxodb.cpp/h         # UTXO database layer
│   ├── validation.cpp/h     # Block/transaction validation
│   ├── server.cpp/h         # HTTP server and API
│   ├── consensus.h          # Consensus parameters
│   └── main.cpp             # Main executable
├── include/                  # Header files
├── web/                      # Web UI assets
├── tests/                    # Unit tests
├── tools/                    # Build tools (MinGW, etc.)
├── CMakeLists.txt           # Build configuration
└── README.md                # Documentation
```

## Key Algorithms

### Block Hashing
```
block_hash = SHA256(SHA256(block_header))
```

### Proof of Work
```
while (block_hash >= target) {
    nonce++
    block_hash = SHA256(SHA256(block_header))
}
```

### Merkle Root
```
merkle_root = build_merkle_tree(transaction_hashes)
```

### Address Generation
```
public_key_hash = RIPEMD160(SHA256(public_key))
address = Base58Encode(version_byte + public_key_hash + checksum)
```

## Security Considerations

- **Cryptographic Security:**
  - Double SHA-256 for block hashes (prevents length extension attacks)
  - ECDSA signatures for transaction authorization
  - Secure random number generation for keys

- **Consensus Security:**
  - UTXO model prevents double-spending
  - Chain validation ensures integrity
  - Proof of Work provides security against tampering
  
- **Network Security:**
  - Rate limiting prevents DoS attacks
  - Input validation on all API endpoints
  - CORS configured for trusted origins

## Performance Optimizations

- Multi-threaded server architecture
- SQLite database with indexing
- Efficient UTXO lookup
- Merkle tree caching
- Connection pooling
- Optimized hash computations

## Scalability Features

- Configurable worker thread pool
- Database connection pooling
- Transaction mempool for pending TXs
- Block height indexing
- Efficient UTXO set management

## API Endpoints

- `GET /api/status` - Server health check
- `GET /api/blockchain/info` - Blockchain statistics
- `GET /api/blocks/recent` - Recent blocks
- `GET /api/blocks/height/:height` - Block by height
- `GET /api/blocks/hash/:hash` - Block by hash
- `GET /api/wallets` - List all wallets
- `POST /api/wallets/create` - Create new wallet
- `GET /api/transactions` - Transaction history
- `POST /api/transactions/create` - Create transaction
- `POST /api/transactions/send` - Send transaction
- `POST /api/mining/mine` - Mine new block
