from shamirs import shares
# We don't need interpolate from shamirs as we implement it manually to handle strings/tuples
import binascii

# 13th Mersenne Prime (2**521 - 1)
# Sufficiently large for 256-bit secrets (Ed25519 private keys are 256-bit)
PRIME_MODULUS = 2**521 - 1

def split_secret(secret_hex: str, threshold: int = 3, share_count: int = 5) -> list[str]:
    """
    Split a hex secret into N shares, requiring K to reconstruct.
    Returns a list of strings formatted as "x-y_hex".
    """
    # Convert hex secret to integer
    secret_int = int(secret_hex, 16)
    
    try:
        # shares(secret, quantity=n, threshold=k, modulus=p)
        share_list = shares(secret_int, quantity=share_count, threshold=threshold, modulus=PRIME_MODULUS)
        
        formatted_shares = []
        for share in share_list:
            # Handle share objects (which act like tuples)
            if hasattr(share, 'index') and hasattr(share, 'value'):
                x = share.index
                y = share.value
            elif isinstance(share, (list, tuple)) and len(share) >= 2:
                x = share[0]
                y = share[1]
            else:
                 # Fallback access
                 x = share[0]
                 y = share[1]
            
            # Format as "index-value_hex"
            formatted_shares.append(f"{x}-{hex(y)[2:]}")
        return formatted_shares
    except Exception as e:
        print(f"Error splitting secret: {e}")
        return []

def _extended_gcd(a: int, b: int) -> tuple[int, int, int]:
    """Extended GCD algorithm to find inverse."""
    x0, x1, y0, y1 = 1, 0, 0, 1
    while b != 0:
        q, a, b = a // b, b, a % b
        x0, x1 = x1, x0 - q * x1
        y0, y1 = y1, y0 - q * y1
    return a, x0, y0

def _modular_inverse(k: int, p: int) -> int:
    """Compute modular inverse of k mod p."""
    g, x, y = _extended_gcd(k, p)
    if g != 1:
        raise ValueError("Modular inverse does not exist")
    return x % p

def _lagrange_interpolate(points: list[tuple[int, int]], p: int) -> int:
    """
    Compute f(0) using Lagrange interpolation over GF(p).
    points: list of (x, y)
    """
    k = len(points)
    secret = 0
    
    for j in range(k):
        xj, yj = points[j]
        
        # Compute Lagrange basis polynomial L_j(0)
        # lj(0) = product( (0 - xm) / (xj - xm) ) for m != j
        numerator = 1
        denominator = 1
        
        for m in range(k):
            if j == m:
                continue
            xm, _ = points[m]
            
            numerator = (numerator * (0 - xm)) % p
            denominator = (denominator * (xj - xm)) % p
            
        # lj(0) = num * den^-1
        den_inv = _modular_inverse(denominator, p)
        lj_0 = (numerator * den_inv) % p
        
        # Add term to sum
        term = (yj * lj_0) % p
        secret = (secret + term) % p
        
    return secret

def combine_shards(shards: list[str]) -> str:
    """
    Combine formatted shards ("x-y_hex") to reconstruct the hex secret.
    """
    try:
        points = []
        for s in shards:
            try:
                x_str, y_hex = s.split('-')
                x = int(x_str)
                y = int(y_hex, 16)
                points.append((x, y))
            except ValueError:
                continue
                
        if not points:
            return ""

        # Use robust manual Lagrange interpolation
        secret_int = _lagrange_interpolate(points, PRIME_MODULUS)
        
        # Convert back to hex
        h = hex(secret_int)[2:]
        if len(h) % 2 != 0:
            h = '0' + h
        return h.zfill(64) 
    except Exception as e:
        print(f"Error combining shards: {e}")
        return ""
