from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
import json
import os
import subprocess
import threading
import time
import uuid
import tempfile
import shutil
from datetime import datetime
from typing import Optional, List, Dict, Union, Any
from pydantic import BaseModel

# ==================== Helper Functions ====================

def generate_pdf_thumbnail(pdf_path: str) -> Optional[str]:
    """Generate a thumbnail for a PDF file using qlmanage (macOS)"""
    try:
        temp_dir = tempfile.gettempdir()
        # qlmanage -t -s 512 -o output_dir input_file
        subprocess.run(
            ["qlmanage", "-t", "-s", "512", "-o", temp_dir, pdf_path],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        base_name = os.path.basename(pdf_path)
        thumb_name = f"{base_name}.png"
        thumb_path = os.path.join(temp_dir, thumb_name)
        return thumb_path if os.path.exists(thumb_path) else None
    except Exception as e:
        print(f"Error generating PDF thumbnail: {e}")
        return None

# Initialize FastAPI app
app = FastAPI(
    title="IPFS Social Feed API",
    description="Backend API for decentralized social feed",
    version="1.0.0"
)

# CORS configuration - allow frontend to connect
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# Configuration
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IPFS_BIN = os.path.join(BASE_DIR, "bin/ipfs")
CLUSTER_CTL = os.path.join(BASE_DIR, "bin/ipfs-cluster-ctl")
LIBRARY_FILE = os.path.join(BASE_DIR, "library.json")
INTERACTIONS_FILE = os.path.join(BASE_DIR, "local_interactions.json")
USER_PROFILE_FILE = os.path.join(BASE_DIR, "user_profile.json")
FOLLOWING_FILE = os.path.join(BASE_DIR, "following.json")
MESSAGES_FILE = os.path.join(BASE_DIR, "messages.json")
RECOVERY_FILE = os.path.join(BASE_DIR, "recovery_requests.json")
VOUCHED_FILE = os.path.join(BASE_DIR, "vouched.json")
MANIFEST_FILE = os.path.join(BASE_DIR, "manifest.json")

# Pydantic Models
class LibraryItem(BaseModel):
    name: str
    description: str
    filename: str
    cid: str
    type: str
    author: str
    avatar: str
    timestamp: str

class Comment(BaseModel):
    text: str

class UserProfile(BaseModel):
    username: str
    handle: str
    avatar: str
    banner: str
    bio: str
    location: str
    tags: List[str]
    stats: Dict[str, int]
    peer_id: Optional[str] = None
    guardians: List[str] = [] # List of Peer IDs

class DirectMessage(BaseModel):
    sender: str
    text: str
    timestamp: str
    cid: Optional[str] = None  # CID of the message object in IPFS

# Helper Functions
def run_command(command: Union[str, List[str]]) -> Optional[str]:
    """Execute shell command and return output"""
    try:
        # If command is a string, we might still need shell=True for complex pipes,
        # but for simple IPFS/Cluster calls, we prefer a list.
        is_shell = isinstance(command, str)
        result = subprocess.run(
            command, shell=is_shell, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=BASE_DIR
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {e.stderr}")
        return None

def update_social_manifest():
    """Create and publish manifest.json containing library and recommendations"""
    try:
        # Load library and vouched lists
        library = load_json(LIBRARY_FILE, [])
        vouched = load_json(VOUCHED_FILE, [])
        
        # Add library and vouched files to IPFS to get their CIDs
        lib_cid = run_command(f"{IPFS_BIN} add -Q '{os.path.abspath(LIBRARY_FILE)}'").strip()
        vouched_cid = run_command(f"{IPFS_BIN} add -Q '{os.path.abspath(VOUCHED_FILE)}'").strip()
        
        if not lib_cid:
            return None
            
        manifest = {
            "version": "1.0",
            "library_cid": lib_cid,
            "vouched_cid": vouched_cid,
            "timestamp": datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
        }
        
        save_json(MANIFEST_FILE, manifest)
        manifest_cid = run_command(f"{IPFS_BIN} add -Q '{os.path.abspath(MANIFEST_FILE)}'").strip()
        
        if manifest_cid:
            run_command(f"{CLUSTER_CTL} pin add {manifest_cid}")
            publish_to_ipns(manifest_cid)
            # Update local manifest.json with its own CID for reference
            manifest["manifest_cid"] = manifest_cid
            save_json(MANIFEST_FILE, manifest)
            return manifest_cid
    except Exception as e:
        print(f"Error updating manifest: {e}")
    return None

def load_json(filepath: str, default_value=None):
    """Load JSON file safely"""
    if not os.path.exists(filepath):
        return default_value if default_value is not None else []
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)
            # Ensure we return the correct type if default_value is provided
            if default_value is not None and not isinstance(data, type(default_value)):
                return default_value
            return data
    except (json.JSONDecodeError, IOError):
        return default_value if default_value is not None else []

def save_json(filepath: str, data):
    """Save data to JSON file"""
    with open(filepath, 'w') as f:
        json.dump(data, f, indent=2)

# API Endpoints

@app.get("/")
async def root():
    """API root endpoint"""
    return {"message": "IPFS Social Feed API", "version": "1.0.0"}

@app.post("/api/auth/generate-identity")
async def generate_identity():
    """Generate a new P2P DID (Simple implementation for demo)"""
    # In a real system, we'd use ed25519 key pairs
    new_id = f"did:ipfs:{uuid.uuid4().hex}"
    secret = f"key_{uuid.uuid4().hex}"
    return {"did": new_id, "secret": secret}

@app.get("/api/library")
async def get_library():
    """Get all posts from library"""
    library = load_json(LIBRARY_FILE, [])
    return {"library": library, "count": len(library)}

@app.post("/api/search")
async def search_library(request: Request):
    """Search library by metadata (title, description, author, filename)"""
    body = await request.json()
    query = body.get("query", "").lower().strip()
    
    if not query:
        return {"results": [], "count": 0}
    
    library = load_json(LIBRARY_FILE, [])
    
    # Search across multiple fields
    results = [
        item for item in library
        if query in item.get("name", "").lower()
        or query in item.get("description", "").lower()
        or query in item.get("author", "").lower()
        or query in item.get("filename", "").lower()
        or query in item.get("cid", "").lower()
    ]
    
    return {"results": results, "count": len(results), "query": query}

@app.get("/api/library/{cid}")
async def get_post(cid: str):
    """Get specific post by CID"""
    library = load_json(LIBRARY_FILE, [])
    post = next((item for item in library if item["cid"] == cid), None)
    if not post:
        raise HTTPException(status_code=404, detail="Post not found")
    return post

@app.delete("/api/library/{cid}")
async def delete_post(cid: str):
    """Delete a post from library by CID"""
    library = load_json(LIBRARY_FILE, [])
    original_length = len(library)
    library = [item for item in library if item["cid"] != cid]
    if len(library) == original_length:
        raise HTTPException(status_code=404, detail="Post not found")
    save_json(LIBRARY_FILE, library)

    # Also remove interactions for this post
    interactions = load_json(INTERACTIONS_FILE, {})
    if cid in interactions:
        del interactions[cid]
        save_json(INTERACTIONS_FILE, interactions)

    return {"success": True, "message": "Post deleted"}

@app.put("/api/library/{cid}")
async def update_post(cid: str, title: str = Form(...), description: str = Form(...)):
    """Update post title and description"""
    library = load_json(LIBRARY_FILE, [])
    post = next((item for item in library if item["cid"] == cid), None)
    if not post:
        raise HTTPException(status_code=404, detail="Post not found")
    post["name"] = title
    post["description"] = description
    save_json(LIBRARY_FILE, library)
    return {"success": True, "post": post}

@app.post("/api/upload")
async def upload_file(
    file: UploadFile = File(...),
    title: str = Form(""),
    description: str = Form(""),
    upload_type: str = Form("post") # post, avatar, banner
):
    """Upload file to IPFS and add to library (if post)"""
    try:
        # Save uploaded file temporarily
        temp_dir = tempfile.gettempdir()
        temp_path = os.path.join(temp_dir, f"temp_{file.filename}")
        with open(temp_path, "wb") as f:
            content = await file.read()
            f.write(content)
        
        # Add to IPFS
        cid = run_command(f"{IPFS_BIN} add -Q '{temp_path}'")
        if not cid:
            raise HTTPException(status_code=500, detail="Failed to add file to IPFS")
        
        # Pin to cluster
        run_command(f"{CLUSTER_CTL} pin add {cid}")

        thumbnail_cid = None
        if upload_type == "post" and file.filename.lower().endswith(".pdf"):
            # Generate thumbnail for PDF
            try:
                thumb_path = generate_pdf_thumbnail(temp_path)
                if thumb_path and os.path.exists(thumb_path):
                    thumbnail_cid = run_command(f"{IPFS_BIN} add -Q '{thumb_path}'")
                    if thumbnail_cid:
                        run_command(f"{CLUSTER_CTL} pin add {thumbnail_cid}")
                    os.remove(thumb_path)
            except Exception as te:
                print(f"Thumbnail generation failed: {te}")

        if upload_type == "post":
            # Load user profile
            user_profile = load_json(USER_PROFILE_FILE, {
                "username": "IPFS Explorer",
                "avatar": "🌐"
            })
            
            # Create library entry
            entry = {
                "name": title or file.filename,
                "description": description,
                "filename": file.filename,
                "cid": cid,
                "thumbnail_cid": thumbnail_cid,
                "type": "file",
                "author": user_profile.get("username", "IPFS Explorer"),
                "avatar": user_profile.get("avatar", "🌐"),
                "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            }
            
            # Add to library
            library = load_json(LIBRARY_FILE, [])
            if isinstance(library, list) and not any(item.get("cid") == cid for item in library if isinstance(item, dict)):
                library.append(entry)
                save_json(LIBRARY_FILE, library)
            
            # Publish library update to IPNS
            abs_lib_path = os.path.abspath(LIBRARY_FILE)
            lib_cid = run_command(f"{IPFS_BIN} add -Q '{abs_lib_path}'")
            if lib_cid:
                run_command(f"{CLUSTER_CTL} pin add {lib_cid}")
                publish_to_ipns(lib_cid)
        
        # Cleanup
        if os.path.exists(temp_path):
            os.remove(temp_path)
        
        return {
            "success": True,
            "cid": cid,
            "thumbnail_cid": thumbnail_cid,
            "filename": file.filename,
            "upload_type": upload_type
        }
    
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/interactions")
async def get_interactions():
    """Get all user interactions"""
    interactions = load_json(INTERACTIONS_FILE, {})
    return interactions if isinstance(interactions, dict) else {}

@app.get("/api/interactions/{cid}")
async def get_post_interactions(cid: str):
    """Get interactions for specific post"""
    interactions = load_json(INTERACTIONS_FILE, {})
    return interactions.get(cid, {
        "recommended": False,
        "not_recommended": False,
        "comments": [],
        "views": 0
    })

@app.post("/api/interactions/{cid}/like")
async def toggle_like(cid: str):
    """Toggle like for a post"""
    interactions = load_json(INTERACTIONS_FILE, {})
    
    if cid not in interactions:
        interactions[cid] = {
            "recommended": False,
            "not_recommended": False,
            "comments": [],
            "views": 0
        }
    
    # Toggle like
    if isinstance(interactions, dict) and cid in interactions:
        interactions[cid]["recommended"] = not interactions[cid]["recommended"]
        # Remove dislike if liking
        if interactions[cid]["recommended"]:
            interactions[cid]["not_recommended"] = False
            # Pin content when liked
            run_command([CLUSTER_CTL, "pin", "add", cid])
            
        # Update vouched list
        vouched = load_json(VOUCHED_FILE, [])
        if interactions[cid]["recommended"]:
            if cid not in vouched:
                vouched.append(cid)
        else:
            if cid in vouched:
                vouched.remove(cid)
        save_json(VOUCHED_FILE, vouched)
        
        # Update manifest
        update_social_manifest()
    
    save_json(INTERACTIONS_FILE, interactions)
    return interactions[cid]

@app.post("/api/interactions/{cid}/dislike")
async def toggle_dislike(cid: str):
    """Toggle dislike for a post"""
    interactions = load_json(INTERACTIONS_FILE, {})
    
    if cid not in interactions:
        interactions[cid] = {
            "recommended": False,
            "not_recommended": False,
            "comments": [],
            "views": 0
        }
    
    # Toggle dislike
    interactions[cid]["not_recommended"] = not interactions[cid]["not_recommended"]
    # Remove like if disliking
    if interactions[cid]["not_recommended"]:
        interactions[cid]["recommended"] = False
        
        # Update vouched list
        vouched = load_json(VOUCHED_FILE, [])
        if cid in vouched:
            vouched.remove(cid)
            save_json(VOUCHED_FILE, vouched)
            # Update manifest
            update_social_manifest()
    
    save_json(INTERACTIONS_FILE, interactions)
    return interactions[cid]

@app.post("/api/interactions/{cid}/comment")
async def add_comment(cid: str, comment: Comment):
    """Add comment to a post"""
    interactions = load_json(INTERACTIONS_FILE, {})
    
    if cid not in interactions:
        interactions[cid] = {
            "recommended": False,
            "not_recommended": False,
            "comments": [],
            "views": 0
        }
    
    if "comments" not in interactions[cid]:
        interactions[cid]["comments"] = []
    
    interactions[cid]["comments"].append(comment.text)
    save_json(INTERACTIONS_FILE, interactions)
    
    return {"success": True, "comment": comment.text}

@app.delete("/api/interactions/{cid}/comment/{index}")
async def delete_comment(cid: str, index: int):
    """Delete comment from a post"""
    interactions = load_json(INTERACTIONS_FILE, {})
    
    if cid not in interactions or "comments" not in interactions[cid]:
        raise HTTPException(status_code=404, detail="Post or comments not found")
    
    if index < 0 or index >= len(interactions[cid]["comments"]):
        raise HTTPException(status_code=404, detail="Comment index out of range")
    
    interactions[cid]["comments"].pop(index)
    save_json(INTERACTIONS_FILE, interactions)
    
    return {"success": True}

# ==================== Follow/Sync System ====================

def publish_to_ipns(library_cid: str) -> bool:
    """Publish library CID to IPNS"""
    try:
        result = run_command(f"{IPFS_BIN} name publish {library_cid}")
        return bool(result)
    except Exception as e:
        print(f"IPNS publish error: {e}")
        return False

def resolve_ipns(peer_id: str) -> Optional[str]:
    """Resolve peer's IPNS name to get their library CID"""
    try:
        cid = run_command(f"{IPFS_BIN} name resolve /ipns/{peer_id}")
        return cid.strip() if cid else None
    except Exception:
        return None

def fetch_ipfs_json(cid: str) -> Union[List, Dict]:
    """Fetch JSON from IPFS"""
    try:
        content = run_command(f"{IPFS_BIN} cat {cid}")
        if content:
            return json.loads(content)
        return [] if cid == "[]" else {}
    except Exception as e:
        print(f"Fetch IPFS JSON error for {cid}: {e}")
        return []

def fetch_peer_library(library_cid: str) -> List[Dict]:
    """Fetch library content from IPFS"""
    res = fetch_ipfs_json(library_cid)
    return res if isinstance(res, list) else []

@app.post("/api/follow/{peer_id}")
async def follow_peer(peer_id: str, relationship_type: str = "sync"):
    """Follow an IPFS peer and sync their manifest (library + recommendations)"""
    try:
        following = load_json(FOLLOWING_FILE, [])
        if any(f["peer_id"] == peer_id for f in following):
            return {"success": False, "message": "Already following this peer"}
        
        # Resolve IPNS to get manifest/library CID
        resolved_cid = resolve_ipns(peer_id)
        if not resolved_cid:
            raise HTTPException(status_code=404, detail="Could not resolve peer's IPNS name")
        
        # Determine if it's a manifest or a library (version 1.0 support)
        resolved_data = fetch_ipfs_json(resolved_cid)
        
        library_cid = resolved_cid
        vouched_cid = None
        
        if isinstance(resolved_data, dict) and resolved_data.get("version") == "1.0":
            library_cid = resolved_data.get("library_cid")
            vouched_cid = resolved_data.get("vouched_cid")
        
        # Fetch library
        peer_library = fetch_peer_library(library_cid)
        
        # Pin latest content
        for item in peer_library[:10]:
            run_command(f"{IPFS_BIN} pin add {item['cid']}")
        
        # Resolve username from profile if possible, else library author
        username = "Unknown"
        if peer_library:
            username = peer_library[0].get("author", "Unknown")
            
        follow_entry = {
            "peer_id": peer_id,
            "username": username,
            "ipns_name": f"/ipns/{peer_id}",
            "followed_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "last_synced": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "library_cid": library_cid,
            "vouched_cid": vouched_cid,
            "manifest_cid": resolved_cid if vouched_cid else None,
            "relationship_type": relationship_type
        }
        
        following.append(follow_entry)
        save_json(FOLLOWING_FILE, following)
        
        return {
            "success": True, 
            "message": f"Now following {username}",
            "synced_items": len(peer_library[:10]),
            "vouched_items_found": bool(vouched_cid)
        }
    except Exception as e:
        print(f"Follow error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/api/unfollow/{peer_id}")
async def unfollow_peer(peer_id: str):
    """Unfollow a peer"""
    following = load_json(FOLLOWING_FILE, [])
    
    original_length = len(following)
    following = [f for f in following if f["peer_id"] != peer_id]
    
    if len(following) == original_length:
        raise HTTPException(status_code=404, detail="Not following this peer")
    
    save_json(FOLLOWING_FILE, following)
    return {"success": True, "message": "Unfollowed successfully"}

@app.get("/api/following")
async def get_following():
    """Get list of peers being followed"""
    following = load_json(FOLLOWING_FILE, [])
    return {"following": following, "count": len(following)}

@app.post("/api/sync-peers")
async def sync_peers():
    """Manually sync content from all followed peers"""
    following = load_json(FOLLOWING_FILE, [])
    synced_count = 0
    
    for peer in following:
        try:
            # Resolve latest library CID
            library_cid = resolve_ipns(peer["peer_id"])
            if library_cid and library_cid != peer.get("library_cid"):
                # Fetch and pin new content
                peer_library = fetch_peer_library(library_cid)
                for item in peer_library[:10]:
                    run_command(f"{IPFS_BIN} pin add {item['cid']}")
                
                # Update sync time
                peer["library_cid"] = library_cid
                peer["last_synced"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                synced_count += 1
        except Exception as e:
            print(f"Sync error for {peer['peer_id']}: {e}")
    
    if synced_count > 0:
        save_json(FOLLOWING_FILE, following)
    
    return {"success": True, "synced_peers": synced_count}

@app.get("/api/feed/aggregated")
async def get_aggregated_feed():
    """Get feed aggregated from own library + followed peers"""
    # Get own library
    own_library = load_json(LIBRARY_FILE, [])
    
    # Get followed peers
    following = load_json(FOLLOWING_FILE, [])
    
    all_posts = list(own_library)
    
    # Fetch content from each followed peer
    for peer in following:
        try:
            library_cid = peer.get("library_cid")
            if library_cid:
                peer_library = fetch_peer_library(library_cid)
                # Mark posts as from followed user
                for item in peer_library:
                    item["_from_peer"] = peer["username"]
                    item["_peer_id"] = peer["peer_id"]
                all_posts.extend(peer_library)
        except Exception as e:
            print(f"Error fetching peer {peer['peer_id']}: {e}")
    
    # Sort by timestamp (newest first)
    all_posts.sort(key=lambda x: x.get("timestamp", ""), reverse=True)
    
    return {"library": all_posts, "count": len(all_posts)}

@app.get("/api/feed/recommended")
async def get_recommended_feed():
    """Aggregate posts recommended (socially vouched) by followed peers"""
    try:
        following = load_json(FOLLOWING_FILE, [])
        recommended_posts = []
        seen_cids = set()

        for peer in following:
            vouched_cid = peer.get("vouched_cid")
            if not vouched_cid:
                continue
                
            # Fetch the list of CIDs this peer recommended
            vouched_list = fetch_ipfs_json(vouched_cid)
            if not isinstance(vouched_list, list):
                continue
                
            # For each CID, fetch the actual post metadata
            # We look for it in the peer's own library first, then global
            peer_library = []
            if peer.get("library_cid"):
                peer_library = fetch_peer_library(peer["library_cid"])
                
            for cid in vouched_list:
                if cid in seen_cids:
                    # Update recommendation count/list if already seen
                    for post in recommended_posts:
                        if post["cid"] == cid:
                            if "recommended_by" not in post:
                                post["recommended_by"] = []
                            if peer["username"] not in post["recommended_by"]:
                                post["recommended_by"].append(peer["username"])
                    continue
                
                # Try to find post metadata in peer's library
                post_meta = next((item for item in peer_library if item["cid"] == cid), None)
                
                # If not in library, it's a transitive recommendation (vouching for someone else)
                # In a real app, we'd fetch the CID metadata from IPFS directly
                # For now, we'll try to resolve it if it's missing
                if not post_meta:
                    # Logic to fetch CID metadata would go here
                    # For MVP, we only surface if we can find the metadata
                    continue
                
                # Attach social provenance
                post_meta["recommended_by"] = [peer["username"]]
                post_meta["_is_social_discovery"] = True
                
                recommended_posts.append(post_meta)
                seen_cids.add(cid)
                
        # Sort by engagement/relevance (simple: count of recommendations)
        recommended_posts.sort(key=lambda x: len(x.get("recommended_by", [])), reverse=True)
        
        return {"library": recommended_posts, "count": len(recommended_posts)}
    except Exception as e:
        print(f"Recommended feed error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# ==================== Direct Messages (DM) System ====================

def get_my_peer_id() -> str:
    """Get own IPFS Peer ID"""
    try:
        return run_command(f"{IPFS_BIN} id --format='<id>'").strip()
    except Exception:
        return "unknown"

def get_chat_topic(peer_id: str) -> str:
    """Generate a stable chat topic for two peers"""
    my_id = get_my_peer_id()
    participants = sorted([my_id, peer_id])
    return f"ipfs-chat-v1-{participants[0][:10]}-{participants[1][:10]}"

@app.get("/api/messages")
async def list_conversations():
    """List all active conversations"""
    messages = load_json(MESSAGES_FILE, {})
    conversations = []
    for peer_id, chat in messages.items():
        if chat:
            last_msg = chat[-1]
            conversations.append({
                "peer_id": peer_id,
                "last_message": last_msg["text"],
                "timestamp": last_msg["timestamp"],
                "unread_count": 0  # To be implemented
            })
    return {"conversations": conversations}

@app.get("/api/messages/{peer_id}")
async def get_chat_history(peer_id: str):
    """Get chat history with a specific peer"""
    messages = load_json(MESSAGES_FILE, {})
    return {"history": messages.get(peer_id, [])}

@app.post("/api/messages/send")
async def send_direct_message(peer_id: str, text: str = Form(...)):
    """Send a DM: verify contact status, save locally, pin to IPFS, and broadcast via PubSub"""
    try:
        # Check relationship tier
        following = load_json(FOLLOWING_FILE, [])
        peer_rel = next((f for f in following if f["peer_id"] == peer_id), None)
        
        if not peer_rel or peer_rel.get("relationship_type") != "contact":
            # If only syncing, this is a "Message Request"
            # In a full implementation, we'd flag this as a request
            pass
            
        my_id = get_my_peer_id()
        timestamp = datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
        
        # 1. Create message object
        msg_data = {
            "sender": my_id,
            "text": text,
            "timestamp": timestamp
        }
        
        # 2. Add to IPFS (Store-and-Forward)
        msg_str = json.dumps(msg_data)
        cid = run_command(f"echo '{msg_str}' | {IPFS_BIN} add -Q").strip()
        
        # 3. Pin it locally
        if cid:
            run_command(f"{IPFS_BIN} pin add {cid}")
            msg_data["cid"] = cid
        
        # 4. Save to local history
        messages = load_json(MESSAGES_FILE, {})
        if peer_id not in messages:
            messages[peer_id] = []
        messages[peer_id].append(msg_data)
        save_json(MESSAGES_FILE, messages)
        
        # 5. Broadcast CID over PubSub
        topic = get_chat_topic(peer_id)
        # Ensure we are subscribed to this topic too
        subscribe_to_chat(topic)
        run_command(f"{IPFS_BIN} pubsub pub {topic} -- '{cid}'")
        
        return {"success": True, "message": msg_data}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

# --- PubSub Background Listener ---

ACTIVE_TOPICS = set()

def subscribe_to_chat(topic: str):
    """Subscribe to an IPFS PubSub topic if not already subscribed"""
    if topic not in ACTIVE_TOPICS:
        run_command(f"{IPFS_BIN} pubsub sub {topic} &") # Run in background
        ACTIVE_TOPICS.add(topic)

def pubsub_listener_worker():
    """Background worker to listen for PubSub messages and fetch content"""
    my_id = get_my_peer_id()
    # We poll 'ipfs pubsub ls' to see what topics we are in, or use a persistent sub
    # For simplicity, we assume we sub to topics as we open chats
    # A more robust way is to use a single 'announce' topic to discover new chats
    
    # Pre-subscribe to existing conversations
    messages = load_json(MESSAGES_FILE, {})
    for peer_id in messages:
        subscribe_to_chat(get_chat_topic(peer_id))

    # Keep a persistent process for incoming messages (this is tricky with run_command)
    # Instead, we'll try to periodically check or use a subprocess.Popen
    print(f"PubSub Listener started for Peer ID: {my_id}")
    
    # Start a persistent sub for ALL subscribed topics
    # Note: IPFS PubSub sub is a blocking stream.
    # We need to handle it per topic or use a dedicated tool.
    
    # For now, let's implement a simplified polling/CID fetch for the approved plan
    # Real-time is handled by the frontend polling /api/messages/{peer_id}
    # This worker will focus on "Store-and-Forward" fetching of CIDs from PubSub

@app.on_event("startup")
async def startup_event():
    """Initialize background tasks"""
    thread = threading.Thread(target=pubsub_listener_worker, daemon=True)
    thread.start()

@app.get("/api/profile")
async def get_profile():
    """Get user profile"""
    profile = load_json(USER_PROFILE_FILE, {
        "username": "Marcus Cole",
        "handle": "@marcus_cole",
        "avatar": "https://i.pravatar.cc/300?img=11",
        "banner": "https://images.unsplash.com/photo-1451187580459-43490279c0fa?w=1200&h=400&fit=crop",
        "bio": "Exploring the app as a guest user. | Coffee enthusiast ☕ | Designer 🎨",
        "location": "Bengaluru, KA",
        "tags": ["#AppDev", "#Prototype"],
        "stats": {"syncs": 450, "contacts": 1204}
    })
    return profile

@app.post("/api/profile")
async def update_profile(
    username: str = Form(None),
    handle: str = Form(None),
    bio: str = Form(None),
    location: str = Form(None),
    avatar: str = Form(None),
    banner: str = Form(None)
):
    """Update user profile"""
    profile = load_json(USER_PROFILE_FILE, {})
    if not isinstance(profile, dict):
        profile = {}
    
    if username: profile["username"] = username
    if handle: profile["handle"] = handle
    if bio: profile["bio"] = bio
    if location: profile["location"] = location
    if avatar: profile["avatar"] = avatar
    if banner: profile["banner"] = banner
    
    save_json(USER_PROFILE_FILE, profile)
    return {"success": True, "profile": profile}

# ==================== Social Recovery System ====================

@app.get("/api/guardians")
async def get_guardians():
    """Get list of user's guardians"""
    profile = load_json(USER_PROFILE_FILE, {})
    return {"guardians": profile.get("guardians", [])}

@app.post("/api/guardians/add")
async def add_guardian(peer_id: str = Form(...)):
    """Add a guardian (max 7)"""
    profile = load_json(USER_PROFILE_FILE, {})
    guardians = profile.get("guardians", [])
    
    if len(guardians) >= 7:
        raise HTTPException(status_code=400, detail="Maximum 7 guardians allowed")
    
    if peer_id in guardians:
        raise HTTPException(status_code=400, detail="Peer already a guardian")
        
    guardians.append(peer_id)
    profile["guardians"] = guardians
    save_json(USER_PROFILE_FILE, profile)
    return {"success": True, "guardians": guardians}

@app.post("/api/recovery/request")
async def start_recovery(old_peer_id: str = Form(...), new_peer_id: str = Form(...)):
    """Initiate an account recovery request"""
    requests = load_json(RECOVERY_FILE, {})
    
    # Generate request ID
    req_id = f"rec_{uuid.uuid4().hex[:8]}"
    requests[req_id] = {
        "old_peer_id": old_peer_id,
        "new_peer_id": new_peer_id,
        "approvals": [], # List of guardian Peer IDs
        "status": "pending",
        "created_at": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }
    
    save_json(RECOVERY_FILE, requests)
    return {"success": True, "request_id": req_id}

@app.post("/api/recovery/approve")
async def approve_recovery(request_id: str = Form(...), guardian_peer_id: str = Form(...)):
    """Guardian approves a recovery request"""
    requests = load_json(RECOVERY_FILE, {})
    if request_id not in requests:
        raise HTTPException(status_code=404, detail="Recovery request not found")
        
    req = requests[request_id]
    if guardian_peer_id not in req["approvals"]:
        req["approvals"].append(guardian_peer_id)
    
    # Check threshold (majority of 7 is 4)
    if len(req["approvals"]) >= 4:
        req["status"] = "completed"
        # In a real system, we'd trigger a DID migration here
        
    save_json(RECOVERY_FILE, requests)
    return {"success": True, "status": req["status"], "approvals_count": len(req["approvals"])}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
