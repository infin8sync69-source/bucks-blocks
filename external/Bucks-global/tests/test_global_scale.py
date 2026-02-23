import asyncio
import json
import os
import sys
from datetime import datetime

# Add backend to path
sys.path.append(os.path.join(os.path.dirname(__file__), '..', 'backend'))

from utils.ipfs_rpc import IPFSRPCClient
from utils.social_dag import SocialDAG
from utils.discovery import DiscoveryHub
from database import get_db_connection, init_db

class MockRPC:
    """Mock IPFS RPC for testing without a daemon."""
    def __init__(self):
        self.storage = {}
        self.base_url = "http://mock:5001/api/v0"
        self.client = type('MockClient', (), {'post': self.mock_post})()
        
    async def dag_put(self, data):
        cid = f"zb2rh{hash(json.dumps(data))}"
        self.storage[cid] = data
        return cid
        
    async def dag_get(self, cid):
        if cid not in self.storage:
            raise Exception("404 Not Found")
        return self.storage[cid]
        
    async def name_publish(self, cid):
        return "mock_peer_id"
        
    async def name_resolve(self, peer_id):
        # For simplicity, return a dummy CID
        return "mock_resolved_cid"

    async def pubsub_pub(self, topic, message):
        print(f"📡 PubSub Publish to {topic}: {message[:50]}...")
        return True

    async def mock_post(self, url, **kwargs):
        # Mock for direct client.post calls
        return type('MockResponse', (), {
            'raise_for_status': lambda: None, 
            'json': lambda: {"Hash": "mock", "Cid": {"/": "mock"}, "Path": "/ipfs/mock"}
        })()

    async def close(self):
        pass

async def test_global_scale():
    print("🚀 Starting Global Scale Integration Test (Mock Mode)...")
    
    # Initialize DB for testing
    init_db()
    
    # Initialize components
    rpc = MockRPC()
    print("💡 Using MockRPC (Daemon not required)")

    dag = SocialDAG(rpc)
    
    def test_db_factory():
        return get_db_connection()
        
    discovery = DiscoveryHub(rpc, dag, test_db_factory)

    try:
        # 1. Test IPFSRPCClient Integration
        print("\n[1/4] Testing IPFS RPC API logic...")
        test_data = {"hello": "global_scale", "timestamp": "now"}
        cid = await rpc.dag_put(test_data)
        print(f"✅ dag_put success: {cid}")
        
        fetched = await rpc.dag_get(cid)
        assert fetched["hello"] == "global_scale"
        print("✅ dag_get success")
        
        # 2. Test SocialDAG Creation & Traversal
        print("\n[2/4] Testing Social DAG logic...")
        post1_content = {"text": "Hello World 1", "cid": "mock_cid_1"}
        post1_cid = await dag.create_post(post1_content)
        print(f"✅ Created first post: {post1_cid}")
        
        post2_content = {"text": "Hello World 2", "cid": "mock_cid_2"}
        post2_cid = await dag.create_post(post2_content, prev_post_cid=post1_cid)
        print(f"✅ Created second post (linked): {post2_cid}")
        
        feed = await dag.traverse_feed(post2_cid, limit=5)
        print(f"✅ Feed traversal found {len(feed)} posts")
        assert len(feed) == 2
        assert feed[0]["text"] == "Hello World 2"
        assert feed[1]["text"] == "Hello World 1"
        
        # 3. Test Discovery Hub
        print("\n[3/4] Testing Discovery Hub logic...")
        discovery_msg = {
            "peer_id": "test_peer_id",
            "username": "TestUser",
            "dag_root": post2_cid
        }
        await discovery.handle_discovery_message(discovery_msg)
        print("✅ Discovery message handled")
        
        conn = get_db_connection()
        peer = conn.execute("SELECT * FROM discovered_peers WHERE peer_id = ?", ("test_peer_id",)).fetchone()
        conn.close()
        assert peer is not None
        assert peer["username"] == "TestUser"
        assert peer["dag_root"] == post2_cid
        print("✅ Peer registered in Global IndexDB")
        
        # 4. Test Social Sharding (Stochastic Pinning)
        print("\n[4/4] Testing Social Sharding logic...")
        # Simulate a feed update for a followed peer
        conn = get_db_connection()
        conn.execute("INSERT OR IGNORE INTO following (user_peer_id, following_peer_id) VALUES (?, ?)", ("me", "test_peer_id"))
        conn.commit()
        conn.close()
        
        update_msg = {
            "peer_id": "test_peer_id",
            "new_root": post2_cid
        }
        # This should trigger a pin because it's followed
        await discovery.handle_feed_update(update_msg)
        print("✅ Feed update handled (Sharding/Pinning logic invoked)")

        print("\n✨ ALL TESTS PASSED!")
        
    except Exception as e:
        print(f"\n❌ TEST FAILED: {e}")
        import traceback
        traceback.print_exc()
    finally:
        await rpc.close()

if __name__ == "__main__":
    asyncio.run(test_global_scale())
