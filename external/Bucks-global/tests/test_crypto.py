import sys
import os
print(f"Python Executable: {sys.executable}")
print(f"Sys Path: {sys.path}")
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'backend'))

from utils.crypto import generate_keypair, sign_message, verify_message

def test_crypto_flow():
    print("Testing Crypto Flow...")
    
    # 1. Generate Identity
    did, multibase_key, secret = generate_keypair()
    print(f"Generated DID: {did}")
    print(f"Secret Length: {len(secret)}")
    
    assert did.startswith("did:key:z")
    assert secret is not None
    
    # 2. Sign Message
    message = "Hello IPFS"
    signature = sign_message(message, secret)
    print(f"Signature: {signature}")
    
    # 3. Verify Message
    is_valid = verify_message(message, signature, did)
    print(f"Verification Result: {is_valid}")
    
    assert is_valid == True
    
    # 4. Verify Invalid Message
    is_valid_bad = verify_message("Tampered", signature, did)
    print(f"Tampered Verification Result: {is_valid_bad}")
    
    assert is_valid_bad == False
    
    print("Crypto Flow Test Passed!")

if __name__ == "__main__":
    test_crypto_flow()
