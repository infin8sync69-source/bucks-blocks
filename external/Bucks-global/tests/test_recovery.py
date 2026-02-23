import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'backend'))

# Generate a mock secret (32 bytes hex)
mock_secret = "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff"

def test_recovery():
    print("Testing Recovery SSSS...")
    try:
        from utils.recovery import split_secret, combine_shards
        
        # 1. Split
        shards = split_secret(mock_secret, threshold=3, share_count=5)
        print(f"Generated {len(shards)} shards:")
        for s in shards:
            print(f"  {s}")
            
        if not shards:
            print("Failed to generate shards (ImportError?)")
            return
            
        # 2. Combine (All)
        recovered = combine_shards(shards)
        print(f"Recovered (All): {recovered}")
        assert recovered == mock_secret
        
        # 3. Combine (Threshold)
        subset = shards[:3]
        recovered_subset = combine_shards(subset)
        print(f"Recovered (Subset): {recovered_subset}")
        assert recovered_subset == mock_secret
        
        # 4. Fail (Below Threshold)
        # Note: Depending on implementation, it might produce garbage or fail.
        # But for GF(p), it will produce garbage.
        subset_fail = shards[:2]
        recovered_fail = combine_shards(subset_fail)
        print(f"Recovered (Fail): {recovered_fail}")
        assert recovered_fail != mock_secret
        
        print("Recovery Test Passed!")
        
    except ImportError as e:
        print(f"Import Error: {e}")
    except Exception as e:
        print(f"Test Error: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    test_recovery()
