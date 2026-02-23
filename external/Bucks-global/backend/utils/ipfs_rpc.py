import httpx
import json
import io
from typing import Optional, List, Dict, Union, Any

class IPFSRPCClient:
    def __init__(self, host: str = "http://127.0.0.1", port: int = 5001):
        self.base_url = f"{host}:{port}/api/v0"
        self.client = httpx.AsyncClient(timeout=30.0)
        self._dag_cache: Dict[str, Dict] = {} # Simple CID cache

    async def add(self, data: Union[str, bytes, Dict]) -> str:
        """Add data to IPFS and return CID."""
        if isinstance(data, dict):
            data = json.dumps(data)
        
        files = {'path': data}
        response = await self.client.post(f"{self.base_url}/add?cid-version=1", files=files)
        response.raise_for_status()
        return response.json()["Hash"]

    async def cat(self, cid: str) -> str:
        """Fetch content for a CID."""
        response = await self.client.post(f"{self.base_url}/cat?arg={cid}")
        response.raise_for_status()
        return response.text

    async def dag_put(self, data: Dict) -> str:
        """Put object as IPFS-DAG (JSON/CBOR)."""
        files = {'file': json.dumps(data)}
        response = await self.client.post(f"{self.base_url}/dag/put?store-codec=dag-json&input-codec=dag-json", files=files)
        response.raise_for_status()
        return response.json()["Cid"]["/"]

    async def dag_get(self, cid: str) -> Dict:
        """Get object from IPFS-DAG with local caching."""
        if cid in self._dag_cache:
            return self._dag_cache[cid]
            
        response = await self.client.post(f"{self.base_url}/dag/get?arg={cid}")
        response.raise_for_status()
        data = response.json()
        
        # Cache the result (bounded to prevent memory leaks in real apps)
        if len(self._dag_cache) > 1000:
            self._dag_cache.clear()
        self._dag_cache[cid] = data
        return data

    async def name_publish(self, cid: str) -> str:
        """Publish CID to IPNS."""
        response = await self.client.post(f"{self.base_url}/name/publish?arg={cid}")
        response.raise_for_status()
        return response.json()["Name"]

    async def name_resolve(self, peer_id: str) -> Optional[str]:
        """Resolve IPNS name using faster resolve flags if possible."""
        try:
            # Using dht-timeout and stream flags for faster resolution in global networks
            response = await self.client.post(
                f"{self.base_url}/name/resolve?arg={peer_id}&dht-timeout=10s&stream=false"
            )
            response.raise_for_status()
            path = response.json()["Path"]
            return path.replace("/ipfs/", "").replace("/ipns/", "")
        except Exception as e:
            # print(f"IPNS Resolve failed for {peer_id}: {e}")
            return None

    async def pubsub_pub(self, topic: str, message: str):
        """Publish message to PubSub."""
        await self.client.post(f"{self.base_url}/pubsub/pub?arg={topic}&arg={message}")

    async def close(self):
        await self.client.aclose()
