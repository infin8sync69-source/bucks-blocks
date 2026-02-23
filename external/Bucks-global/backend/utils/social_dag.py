from .ipfs_rpc import IPFSRPCClient
from datetime import datetime
from typing import Optional, List, Dict

class SocialDAG:
    def __init__(self, rpc_client: IPFSRPCClient):
        self.rpc = rpc_client

    async def create_post(self, content: Dict, prev_post_cid: Optional[str] = None) -> str:
        """
        Create a post object in the Social DAG.
        Structure: { "data": content, "prev": CID }
        """
        post_obj = {
            "version": "2.0",
            "type": "social_post",
            "timestamp": datetime.now().isoformat(),
            "data": content,
            "prev": prev_post_cid
        }
        return await self.rpc.dag_put(post_obj)

    async def update_profile(self, profile: Dict, latest_post_cid: Optional[str] = None) -> str:
        """
        Update profile root DAG.
        Structure: { "profile": profile, "feed_head": CID }
        """
        root_obj = {
            "version": "2.0",
            "type": "profile_root",
            "profile": profile,
            "feed_head": latest_post_cid
        }
        root_cid = await self.rpc.dag_put(root_obj)
        # Publish to IPNS so others can find this root
        await self.rpc.name_publish(root_cid)
        return root_cid

    async def traverse_feed(self, head_cid: str, limit: int = 20) -> List[Dict]:
        """Traverse the Linked List of posts (DAG)."""
        posts = []
        current_cid = head_cid
        
        while current_cid and len(posts) < limit:
            try:
                node = await self.rpc.dag_get(current_cid)
                if node.get("type") == "social_post":
                    posts.append(node["data"])
                    current_cid = node.get("prev")
                elif node.get("type") == "profile_root":
                    current_cid = node.get("feed_head")
                else:
                    break
            except:
                break
        return posts
