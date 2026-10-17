# Bucks Blockchain

A production-ready blockchain implementation in C++ with advanced security features.

## Features

### Core Blockchain
- ✅ Block structure with headers and transactions
- ✅ SHA-256 cryptographic hashing
- ✅ Proof of Work mining algorithm
- ✅ Merkle tree for transaction verification
- ✅ Transaction signing and verification (ECDSA secp256k1)
- ✅ UTXO (Unspent Transaction Output) model
- ✅ Comprehensive blockchain validation
- ✅ Persistent block and UTXO storage (SQLite)

### Security & Privacy
- ✅ **BIP-8192 HD Wallets** - Hierarchical Deterministic wallet support
- ✅ **P2P Encryption** - End-to-end encrypted peer communication (ECDH + AES-256-CBC)
- ✅ DoS protection with misbehavior scoring
- ✅ IP banning and connection throttling
- ✅ Mempool safety limits (50K tx, 300MB max)
- ✅ Consensus validation (coinbase maturity, block size limits)

### Network & API
- ✅ **Web-based UI** for interactive blockchain management
- ✅ HTTP REST API for programmatic access
- ✅ Multi-threaded server architecture
- ✅ P2P networking with encrypted handshake
- ✅ Rate limiting and CORS support

## Building

### Prerequisites
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.10 or higher
- OpenSSL 3.x development libraries
- SQLite3

### Windows Build Instructions

1. Ensure dependencies are available:
   - MinGW-w64 (included in `tools/mingw64/`)
   - OpenSSL (install to `C:\Program Files\OpenSSL-Win64`)
   - CMake

2. Build the project:
   ```powershell
   cmake -S . -B build -G "MinGW Makefiles"
   cmake --build build
   ```

3. Run the node:
   ```powershell
   .\build\bucks.exe --server
   ```

### Linux/macOS Build Instructions

1. Install dependencies:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install cmake libssl-dev build-essential sqlite3

   # macOS
   brew install cmake openssl sqlite
   ```

2. Build:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

3. Run:
   ```bash
   ./bucks --server
   ```

## Usage

### Starting the Server

Start the web server to access the full blockchain platform:

```bash
./bucks --server
```

Or specify a custom port:

```bash
./bucks --server --port 8080
```

Then open your browser and navigate to:
```
http://localhost:8080
```

### Web UI Features

The web interface provides:

- **Dashboard**: View blockchain statistics, chain height, difficulty, and recent blocks
- **Wallets**: Create HD wallets with BIP-8192 mnemonic phrases, view balances
- **Transactions**: Create transactions between wallets, view transaction history
- **Block Explorer**: Search and explore blocks by height or hash
- **Mining**: Mine new blocks and earn block rewards
- **Network**: View connected peers and P2P encryption status

### Command Line Options

```bash
./bucks [options]

Options:
  --server, -s     Start web server mode
  --port, -p PORT  Set server port (default: 8080)
  --help, -h       Show help message
```

## Project Structure

```
Blockchain/
├── src/              # Source code
│   ├── crypto.cpp    # Cryptographic primitives
│   ├── block.cpp     # Block structure
│   ├── blockchain.cpp # Blockchain management
│   ├── wallet.cpp    # Wallet functionality
│   ├── hd_8192.cpp   # HD wallet implementation
│   ├── network.cpp   # P2P networking
│   ├── protocol_enc.cpp # P2P encryption
│   └── server.cpp    # HTTP server
├── include/          # Header files
├── web/              # Web UI files
├── tests/            # Test suites
├── wordlists/        # BIP-8192 wordlist
└── CMakeLists.txt    # Build configuration
```

## Key Components

### Block
- Contains transactions with Merkle root
- Links to previous block via hash
- Includes proof-of-work (nonce)
- Validated for consensus rules

### Transaction
- Inputs reference previous UTXOs
- Outputs specify amounts and recipients
- Cryptographically signed with ECDSA
- Validated before inclusion in blocks

### Mining
- Proof of Work consensus algorithm
- Difficulty adjusted every block (aserti3-2d, 2-day half-life; spec section 6.5)
- Block reward system (50 BUCKS, halving every 210K blocks)
- Transaction fee support

### HD Wallets (BIP-8192)
- 64-byte seed generation from mnemonic phrases
- Master extended key derivation
- Child key derivation (CKDpriv) using HMAC-SHA512
- Path-based derivation (e.g., `m/0/1/2`)
- Hardened and non-hardened derivation support

### P2P Network
- Encrypted peer-to-peer communication
- ECDH key exchange for session keys
- AES-256-CBC payload encryption
- `ENCINIT` handshake protocol
- Block and transaction propagation
- DoS protection with misbehavior scoring

## Security Features

### Cryptographic Security
- ECDSA cryptographic signatures (secp256k1)
- SHA-256 hashing for blocks and transactions
- RIPEMD-160 for address generation
- Base58Check encoding

### Network Security
- P2P message encryption (ECDH + AES-256-CBC)
- Misbehavior scoring and IP banning
- Connection limits (125 max peers, 8 per IP)
- Rate limiting on API endpoints

### Consensus Security
- UTXO double-spend prevention
- Coinbase maturity (100 blocks)
- Maximum block size (1MB)
- SigOp limit tracking
- Difficulty retargeting

### Mempool Safety
- Transaction count cap (50,000)
- Aggregate byte-size cap (300MB)
- Conflict detection (first-seen rule)
- Transaction standardness checks

## API Documentation

The blockchain exposes a REST API:

- `GET /api/blockchain/info` - Get blockchain information
- `GET /api/blocks/recent` - Get recent blocks
- `GET /api/blocks/height/:height` - Get block by height
- `POST /api/wallets/create` - Create new HD wallet
- `POST /api/transactions/create` - Create transaction
- `POST /api/mining/mine` - Mine new block
- `GET /api/network/peers` - Get connected peers

## Testing

The project includes a comprehensive 4-phase test suite:

1.  **Unit Tests**: Verify individual components (Crypto, Block, Transaction, HD-8192, etc.).
2.  **Integration Tests**: Verify interactions (Wallet persistence, Server network startup).
3.  **System Tests**: verification of full node startup and mining simulation.
4.  **Performance & Security**: Benchmarks (SHA-256) and Fuzzing (Protocol decoding).

### Running Tests

Ensure you have CMake installed and are using the MinGW environment.

**Run All Tests:**
```powershell
powershell -ExecutionPolicy Bypass -File ./run_tests.ps1
```

**Run Performance Benchmarks:**
```powershell
powershell -ExecutionPolicy Bypass -File ./run_perf.ps1
```

**Run System Tests:**
```powershell
powershell -ExecutionPolicy Bypass -File ./run_system.ps1
```

## Performance

- Multi-threaded server architecture
- Efficient UTXO database (SQLite with indexing)
- Optimized cryptographic operations (OpenSSL)
- Configurable worker threads
- Block and UTXO caching

## Production Hardening

Bucks has undergone extensive security testing:

- ✅ DoS attack resistance (message limits, connection throttling)
- ✅ Mempool bloat protection
- ✅ Sybil attack mitigation
- ✅ Consensus theft prevention
- ✅ Reorg handling (UTXO rollback)
- ✅ P2P encryption verification

## License

Copyright © 2026 Bucks Blockchain. All rights reserved.

## Support

For issues, questions, or contributions, please contact the development team.
