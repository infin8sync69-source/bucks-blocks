import httpx
import json
import logging
from typing import Optional, List, Dict, Any

logger = logging.getLogger(__name__)

class BlockchainRPCClient:
    """Client for communicating with the Bucks C++ Blockchain server."""
    
    def __init__(self, host: str = "http://127.0.0.1", port: int = 8080):
        self.base_url = f"{host}:{port}/api"
        self.client = httpx.AsyncClient(timeout=10.0)

    async def get_status(self) -> Dict[str, Any]:
        """Get general status of the blockchain."""
        try:
            response = await self.client.get(f"{self.base_url}/status")
            response.raise_for_status()
            return response.json()
        except Exception as e:
            logger.error(f"Blockchain RPC Error (get_status): {e}")
            return {"status": "error", "message": str(e)}

    async def get_info(self) -> Dict[str, Any]:
        """Get detailed blockchain info."""
        try:
            response = await self.client.get(f"{self.base_url}/blockchain/info")
            response.raise_for_status()
            return response.json()
        except Exception as e:
            logger.error(f"Blockchain RPC Error (get_info): {e}")
            return {"error": str(e)}

    async def anchor_cid(self, cid: str, address: Optional[str] = None) -> Dict[str, Any]:
        """
        Anchor an IPFS CID on the blockchain.
        In the future, this will call a specific RPC method to create an anchoring transaction.
        For now, we mock the success or call a generic 'send' if available.
        """
        try:
            # Placeholder for actual anchoring RPC call
            # e.g., POST /api/anchor { "cid": cid, "address": address }
            payload = {"cid": cid}
            if address:
                payload["address"] = address
                
            # Simulate a call to a yet-to-be-implemented C++ endpoint
            response = await self.client.post(f"{self.base_url}/anchor", json=payload)
            if response.status_code == 200:
                return response.json()
            else:
                # Fallback or mock for Phase 1
                return {"success": True, "txid": "mock_txid_" + cid[:8], "anchored": True}
        except Exception as e:
            logger.error(f"Blockchain RPC Error (anchor_cid): {e}")
            # Mock success for Phase 1 integration testing
            return {"success": True, "txid": "mock_txid_" + cid[:8], "anchored": True, "error": str(e)}

    async def close(self):
        await self.client.aclose()
