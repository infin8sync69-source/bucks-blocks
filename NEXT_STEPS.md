# Bucks Blockchain - Roadmap

## Where things stand

Mainnet has not launched, and nothing below has had an external audit. What must happen before
launch, and what the owner still has to decide, is in [docs/MAINNET_INPUTS.md](docs/MAINNET_INPUTS.md);
the launch itself is [docs/LAUNCH.md](docs/LAUNCH.md).

### Implemented in the code (tested, not audited)
- Cryptographic functions (SHA-256, RIPEMD-160, Base58Check, ECDSA secp256k1)
- Block and transaction data structures, Merkle root
- Proof of work with Bitcoin's 2016-block difficulty retarget
- Chain, coins (UTXO) and undo data in SQLite
- HD wallets from 13- or 25-word phrases on 2048-word Arabic/English lists (the lists are drafts until the review is done)
- REST API with token login, and the web console served by the node
- P2P networking with encrypted sessions (ECDH + AES-256-GCM)
- DoS limits: misbehaviour scoring and bans, connection limits (125 peers, 8 per IP), mempool caps (50K transactions, 300 MB)
- Consensus checks: coinbase maturity, block size and sigop limits, reorganisations with undo data
- CMake build (Linux, macOS, Windows/MinGW), test suites, sanitizer and fuzz CI jobs
- A deployment kit for Ubuntu servers ([deploy/](deploy/README.md))

## Enhancement Roadmap

### Phase 6: Advanced P2P Features

#### 6.1 Network Optimization
- [ ] WebSocket support for real-time block/tx notifications
- [ ] Peer discovery mechanism (DNS seeds, addr messages)
- [ ] Headers-first sync for faster IBD
- [ ] Compact block relay (BIP 152)
- [ ] Transaction relay optimization

#### 6.2 Network Security Enhancements
- [ ] Peer authentication with certificates
- [ ] Eclipse attack prevention
- [ ] Enhanced DDoS protection
- [ ] Network topology analysis

### Phase 7: Smart Contracts & Scripting

#### 7.1 Script VM Implementation
- [ ] Full Bitcoin Script VM (100+ opcodes)
- [ ] P2SH (Pay-to-Script-Hash) support
- [ ] Time-locked transactions (CHECKLOCKTIMEVERIFY)
- [ ] Relative time locks (CHECKSEQUENCEVERIFY)
- [ ] Multi-signature scripts (OP_CHECKMULTISIG)

#### 7.2 Advanced Transaction Types
- [ ] Segregated Witness (SegWit)
- [ ] Transaction malleability fixes
- [ ] Schnorr signatures
- [ ] Taproot support (BIP 340/341/342)

### Phase 8: Privacy Features

#### 8.1 Transaction Privacy
- [ ] CoinJoin implementation
- [ ] Stealth addresses
- [ ] Confidential transactions (research)
- [ ] Ring signatures (optional)

#### 8.2 Network Privacy
- [ ] Tor/I2P integration
- [ ] Dandelion++ transaction relay
- [ ] Address reuse prevention

### Phase 9: Performance & Scalability

#### 9.1 Database Optimizations
- [ ] LevelDB migration for better performance
- [ ] UTXO cache layer
- [ ] Block index for O(1) lookups
- [ ] Database sharding for large chains

#### 9.2 Mining Optimizations
- [ ] Multi-threaded mining
- [ ] GPU mining support (CUDA/OpenCL)
- [ ] Mining pool protocol (Stratum)
- [ ] ASIC-resistant PoW (research)

#### 9.3 Network Performance
- [ ] Compression for P2P messages
- [ ] Connection keep-alive optimization
- [ ] Request batching
- [ ] Bloom filters for SPV clients

### Phase 10: Enterprise Features

#### 10.1 Monitoring & Analytics
- [ ] Prometheus metrics export
- [ ] Grafana dashboards
- [ ] Real-time alerting
- [ ] Performance profiling tools

#### 10.2 Administration
- [ ] Admin CLI tools
- [ ] Configuration management
- [ ] Log aggregation (ELK stack)
- [ ] Automated backups

#### 10.3 API Enhancements
- [ ] GraphQL API
- [ ] Batch request support
- [ ] API versioning
- [ ] SDK libraries (Python, JavaScript, Go, Rust)

### Phase 11: Testing & Quality

#### 11.1 Testing Infrastructure
- [x] Unit test suite (Google Test)
- [ ] Integration tests
- [ ] Stress testing framework
- [ ] Fuzzing for security
- [ ] Regression test suite

#### 11.2 Documentation
- [ ] API documentation (OpenAPI/Swagger)
- [ ] Developer guides
- [ ] Deployment guides
- [ ] Security best practices

#### 11.3 CI/CD
- [ ] Automated build pipeline
- [ ] Cross-platform testing
- [ ] Docker containers
- [ ] Kubernetes deployment

## Deployment Considerations

### Production Environment
- **Hardware Requirements:**
  - 4+ CPU cores
  - 8GB+ RAM
  - 100GB+ SSD storage
  - Stable network connection

- **Software Requirements:**
  - Linux server (Ubuntu 22.04 or 24.04)
  - Systemd for service management, nginx with TLS for the operator console: see [deploy/](deploy/README.md)

### High Availability
- Load balancer for multiple nodes
- Database replication
- Automated failover
- Health monitoring

### Security Hardening
- Firewall configuration
- Rate limiting at network level
- Regular security audits
- Encrypted communications (already implemented)

## Performance Targets

- **Transaction Throughput:** 100+ TPS (current), 1000+ TPS (target)
- **Block Time:** 10 minutes (configurable)
- **API Response Time:** < 100ms (p50), < 500ms (p99)
- **Node Sync Time:** < 1 hour for 100k blocks

## Maintenance Schedule

- Security patches: As needed
- Feature releases: Quarterly
- Performance optimization: Ongoing
- Infrastructure upgrades: Bi-annually

## Support & Operations

- 24/7 monitoring capability
- Incident response procedures
- Regular backups (automated)
- Disaster recovery plan
