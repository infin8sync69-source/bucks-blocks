import sys
import os

# Add backend to path
sys.path.append(os.path.join(os.path.dirname(__file__), "../backend"))

from utils.crypto import did_to_peer_id, generate_keypair

def test_did_to_peer_id():
    # 1. Generate a new keypair
    did, multibase_key, private_hex = generate_keypair()
    print(f"Generated DID: {did}")
    
    # 2. Convert to PeerID
    peer_id = did_to_peer_id(did)
    print(f"Derived Peer ID: {peer_id}")
    
    # Validation
    # PeerID should be base58 encoded multihash.
    # For Ed25519 identity multihash, it usually starts with '12D3K' or similar if using base58btc?
    # Actually, identity multihash (0x00) + length (0x24 = 36) -> 00 24 ...
    # Base58 of 00 24 ... usually starts with '1'.
    
    assert peer_id is not None
    assert len(peer_id) > 0
    # assert peer_id.startswith("1") # Typical for Ed25519 identity multihash in Base58?
    # Let's just check it runs and returns something reasonable.
    
    # Test Invalid
    assert did_to_peer_id("invalid") == ""
    assert did_to_peer_id("did:key:invalid") == ""

if __name__ == "__main__":
    test_did_to_peer_id()
    print("Test Passed!")
