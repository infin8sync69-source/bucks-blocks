import base64
import json
from typing import Tuple, Optional
import nacl.signing
import nacl.encoding
from nacl.public import PrivateKey, PublicKey
import multibase
import ecdsa
import hashlib

def generate_keypair() -> Tuple[str, str, str]:
    """
    Generate a new Ed25519 keypair and return formatted DID and keys.
    Returns:
        did (str): did:key:z...
        public_key_multibase (str): z...
        private_key_hex (str): Hex-encoded private key (secret)
    """
    signing_key = nacl.signing.SigningKey.generate()
    verify_key = signing_key.verify_key
    
    # Serialize public key to bytes
    public_bytes = verify_key.encode(encoder=nacl.encoding.RawEncoder)
    
    # Create valid did:key (ed25519-pub header + bytes)
    # Multicodec for ed25519-pub is 0xed01 (varint extended) -> 0xed 0x01
    # But for did:key, we typically use the multicodec prefix directly.
    # W3C did:key method for Ed25519:
    # 1. 0xed01 (2 bytes) prefix for Ed25519 public key
    # 2. 32 bytes public key
    # 3. Multibase encode with base58btc (z)
    
    # 0xed = 237, 0x01 = 1
    header = bytes([0xed, 0x01])
    pub_key_bytes_with_header = header + public_bytes
    
    # Encode with multibase base58btc
    multibase_key = multibase.encode('base58btc', pub_key_bytes_with_header).decode('utf-8')
    did = f"did:key:{multibase_key}"
    
    # Private key to hex for storage
    private_hex = signing_key.encode(encoder=nacl.encoding.HexEncoder).decode('utf-8')
    
    return did, multibase_key, private_hex

def sign_message(message: str, private_key_hex: str) -> str:
    """Sign a message with the private key"""
    signing_key = nacl.signing.SigningKey(private_key_hex, encoder=nacl.encoding.HexEncoder)
    signed = signing_key.sign(message.encode('utf-8'))
    return base64.b64encode(signed.signature).decode('utf-8')

def verify_message(message: str, signature_b64: str, did: str) -> bool:
    """
    Verify a signature against a DID.
    Resolves the public key from the did:key string.
    Supports both Ed25519 and Secp256k1.
    """
    try:
        # Extract multibase key from did:key:z...
        if not did.startswith("did:key:"):
            return False
            
        mb_key = did.split(":")[-1]
        
        # Decode multibase
        decoded = multibase.decode(mb_key)
        
        # 1. Check Ed25519 (0xed01 header)
        if len(decoded) == 34 and decoded[0] == 0xed and decoded[1] == 0x01:
            public_bytes = decoded[2:]
            verify_key = nacl.signing.VerifyKey(public_bytes)
            signature_bytes = base64.b64decode(signature_b64)
            verify_key.verify(message.encode('utf-8'), signature_bytes)
            return True
        
        # 2. Check Secp256k1 (0xe701 header for did:key)
        # Note: Some implementations use 0xe7 directly, others use 0xe7 0x01
        if (decoded[0] == 0xe7 and decoded[1] == 0x01) or (decoded[0] == 0xe7):
            header_len = 2 if decoded[1] == 0x01 else 1
            public_bytes = decoded[header_len:]
            
            # Signature from Bucks Blockchain is likely DER-encoded
            signature_bytes = base64.b64decode(signature_b64)
            
            # Use ecdsa library to verify
            vk = ecdsa.VerifyingKey.from_string(public_bytes, curve=ecdsa.SECP256k1)
            # The message hash in Bucks Blockchain is custom (doubleSha256)
            # But here we verify the raw message using the library's default hasher (sha256)
            # OR we match the blockchain's hashing:
            msg_hash = hashlib.sha256(hashlib.sha256(message.encode('utf-8')).digest()).digest()
            # verify_digest assumes the signature is DER if not specified? 
            # Actually ecdsa library default is raw (r|s), but Bucks uses DER from OpenSSL.
            try:
                vk.verify_digest(signature_bytes, msg_hash, sigdecode=ecdsa.util.sigdecode_der)
                return True
            except ecdsa.BadSignatureError:
                # Try raw decode just in case
                vk.verify_digest(signature_bytes, msg_hash, sigdecode=ecdsa.util.sigdecode_string)
                return True

        return False
    except Exception as e:
        print(f"Verification failed: {e}")
        return False

def did_to_peer_id(did: str) -> str:
    """
    Convert a did:key (Ed25519) to a legacy IPFS Peer ID (12D3K...).
    Logic: Base58( Multihash( Protobuf( PublicKey ) ) )
    """
    try:
        if not did.startswith("did:key:"):
            return ""
        
        # did:key:z... -> split by : -> [did, key, z...]
        parts = did.split(":")
        if len(parts) < 3:
            return ""
            
        mb_key = parts[-1]
        decoded = multibase.decode(mb_key)
        
        # Expect header 0xed 0x01 (Ed25519) or 0xe7 0x01 (Secp256k1)
        if len(decoded) > 2 and decoded[0] == 0xed and decoded[1] == 0x01:
            raw_pubkey = decoded[2:]
            protobuf_prefix = bytes([0x08, 0x01, 0x12, 0x20]) # Type 1 (Ed25519), 32 bytes
        elif (decoded[0] == 0xe7 and decoded[1] == 0x01) or (decoded[0] == 0xe7):
            header_len = 2 if decoded[1] == 0x01 else 1
            raw_pubkey = decoded[header_len:]
            # Protobuf for Secp256k1 (Type 2)
            # 0x08 0x02 (Type 2 - Secp256k1)
            # 0x12 0x21 (Field 2, Length 33)
            protobuf_prefix = bytes([0x08, 0x02, 0x12, 0x21])
        else:
            return ""
        protobuf_serialized = protobuf_prefix + raw_pubkey
        
        # Identity Multihash
        # Code: 0x00
        # Len: varint(len(protobuf_serialized))
        # Digest: protobuf_serialized
        
        length = len(protobuf_serialized) # Should be 4 + 32 = 36
        # Varint of 36 is just 36 (0x24)
        
        multihash_bytes = bytes([0x00, length]) + protobuf_serialized
        
        # Base58 Encode (using bitcoin alphabet usually for IPFS)
        # we need base58 encoding. 
        # multibase 'base58btc' uses 'z' prefix.
        encoded_mb = multibase.encode('base58btc', multihash_bytes).decode('utf-8')
        if encoded_mb.startswith('z'):
            return encoded_mb[1:]
        return encoded_mb
        
    except Exception as e:
        print(f"DID conversion error: {e}")
        return ""
