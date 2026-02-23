import json
import asyncio
from datetime import datetime
import random
from typing import Dict, Optional, List
from .ipfs_rpc import IPFSRPCClient
from .social_dag import SocialDAG

class DiscoveryHub:
    def __init__(self, rpc_client: IPFSRPCClient, social_dag: SocialDAG, db_connection_factory):
        self.rpc = rpc_client
        self.dag = social_dag
        self.get_db = db_connection_factory

    async def handle_discovery_message(self, data: Dict):
        """Handle heartbeats from the global discovery topic."""
        peer_id = data.get("peer_id")
        if not peer_id:
            return

        conn = self.get_db()
        # Update or Insert discovered peer
        conn.execute("""
            INSERT INTO discovered_peers (peer_id, username, avatar, dag_root, last_seen, discovery_type)
            VALUES (?, ?, ?, ?, ?, 'pubsub')
            ON CONFLICT(peer_id) DO UPDATE SET
                username = COALESCE(excluded.username, username),
                avatar = COALESCE(excluded.avatar, avatar),
                dag_root = COALESCE(excluded.dag_root, dag_root),
                last_seen = excluded.last_seen
        """, (
            peer_id,
            data.get("username"),
            data.get("avatar"),
            data.get("dag_root"),
            datetime.now().isoformat()
        ))
        conn.commit()
        conn.close()

    async def handle_feed_update(self, data: Dict):
        """Handle real-time feed update notifications."""
        peer_id = data.get("peer_id")
        new_root = data.get("new_root")
        if not peer_id or not new_root:
            return

        conn = self.get_db()
        # 1. Update Profile Root in DB
        conn.execute("""
            INSERT INTO discovered_peers (peer_id, dag_root, last_seen, discovery_type)
            VALUES (?, ?, ?, 'pubsub')
            ON CONFLICT(peer_id) DO UPDATE SET
                dag_root = excluded.dag_root,
                last_seen = excluded.last_seen
        """, (peer_id, new_root, datetime.now().isoformat()))
        
        # 2. Stochastic Pinning (Social Sharding)
        # Check if we follow them or if it's a random discovery
        follow_row = conn.execute("SELECT 1 FROM following WHERE following_peer_id = ?", (peer_id,)).fetchone()
        is_followed = bool(follow_row)
        conn.close()

        # Sharding Probability:
        # 100% if followed, 10% if just discovered (Global Scale Availability)
        pin_chance = 1.0 if is_followed else 0.1
        
        if random.random() < pin_chance:
            print(f"Social Sharding: Pinning update for {peer_id[:8]} (chance: {pin_chance})")
            try:
                # Traverse newly announced DAG and pin the latest post
                posts = await self.dag.traverse_feed(new_root, limit=1)
                for post in posts:
                    cid = post.get("cid")
                    if cid:
                        # Async pin via RPC
                        await self.rpc.client.post(f"{self.rpc.base_url}/pin/add?arg={cid}")
                        # Also pin the DAG node itself
                        await self.rpc.client.post(f"{self.rpc.base_url}/pin/add?arg={new_root}")
            except Exception as e:
                print(f"Sharding Pin Error: {e}")

    async def send_heartbeat(self, my_profile: Dict, dag_root: str):
        """Publish my state to the global topic."""
        heartbeat = {
            "type": "heartbeat",
            "peer_id": my_profile.get("peer_id"),
            "username": my_profile.get("username"),
            "avatar": my_profile.get("avatar"),
            "dag_root": dag_root,
            "timestamp": datetime.now().isoformat()
        }
        await self.rpc.pubsub_pub("/app/discovery", json.dumps(heartbeat))
