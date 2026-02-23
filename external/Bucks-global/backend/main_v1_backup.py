from fastapi import FastAPI, UploadFile, File, Form, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response, StreamingResponse
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
import socket
import asyncio
import random
from pydantic import BaseModel

from database import get_db_connection, init_db
from werkzeug.utils import secure_filename
import re
import hmac
import hashlib
from utils.crypto import generate_keypair, sign_message, verify_message, did_to_peer_id
from utils.recovery import split_secret, combine_shards
from utils.p2p import P2PClient


# Agent Service Removed
# agent_service = AgentService()
p2p_client = None

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

@app.on_event("startup")
async def startup_event():
    """Initialize services on startup."""
    # Initialize database (creates tables on first run)
    init_db()

    # Initialize P2P Client
    global p2p_client
    p2p_client = P2PClient(IPFS_BIN)
    
    # Get Peer ID (run in thread to avoid blocking)
    try:
        my_peer_id = await asyncio.to_thread(get_my_peer_id)
        print(f"P2P Node Identity: {my_peer_id}")
        
        # Subscribe to my inbox
        inbox_topic = f"/app/inbox/{my_peer_id}"
        await p2p_client.subscribe(inbox_topic, handle_inbox_message)
        print(f"Subscribed to P2P Inbox: {inbox_topic}")
        
    except Exception as e:
        print(f"Failed to initialize P2P: {e}")

@app.on_event("shutdown")
async def shutdown_event():
    """Cleanup services on shutdown."""
    pass



def verify_signature(request: Request, secret: str = None) -> bool:
    """
    Verify Ed25519 signature if DID is present.
    If using legacy secret (HMAC), fallback to that (optional, for migration).
    For Phase 1, we focus on Real DID verification.
    """
    try:
        signature = request.headers.get("X-Signature")
        timestamp = request.headers.get("X-Timestamp")
        did = request.headers.get("X-DID")
        
        if not signature or not timestamp or not did:
            return False
            
        # Prevent replay attacks (5 minute window)
        try:
            req_ts = int(timestamp)
            now = int(time.time() * 1000)
            if abs(now - req_ts) > 300000: # 5 minutes
                return False
        except ValueError:
            return False
            
        # Reconstruct payload: method + path + timestamp
        # In a real system, we'd buffer the body or use a specific header payload.
        # For this prototype: method + path + timestamp
        
        # If it's a legacy DID (uuid based), we might fail or use the secret (HMAC) logic
        if not did.startswith("did:key:"):
             # Legacy Logic fallback
             payload = f"{request.method}{request.url.path}{timestamp}"
             if secret:
                 expected_sig = hmac.new(
                    secret.encode(), 
                    payload.encode(), 
                    hashlib.sha256
                 ).hexdigest()
                 return hmac.compare_digest(signature, expected_sig)
             return False

        # Ed25519 Verification
        # The signature should be over the payload
        payload = f"{request.method}{request.url.path}{timestamp}"
        
        # Verify using the public key embedded in the DID
        return verify_message(payload, signature, did)

    except Exception as e:
        print(f"Auth error: {e}")
        return False

@app.middleware("http")
async def auth_middleware(request: Request, call_next):
    """
    Security Headers Middleware.
    Individual endpoints handle their own auth checks.
    """
    response = await call_next(request)
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Content-Security-Policy"] = "default-src 'self'; img-src 'self' data: http: https:; script-src 'self' 'unsafe-inline' 'unsafe-eval'; style-src 'self' 'unsafe-inline';"
    return response




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



# Configuration
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IPFS_BIN = os.path.join(BASE_DIR, "bin/ipfs")
CLUSTER_CTL = os.path.join(BASE_DIR, "bin/ipfs-cluster-ctl")
LIBRARY_FILE = os.path.join(BASE_DIR, "library.json")
INTERACTIONS_FILE = os.path.join(BASE_DIR, "local_interactions.json")
USER_PROFILE_FILE = os.path.join(BASE_DIR, "user_profile.json")
FOLLOWING_FILE = os.path.join(BASE_DIR, "following.json")
MESSAGES_FILE = os.path.join(BASE_DIR, "messages.json")
CONNECTIONS_FILE = os.path.join(BASE_DIR, "connections.json")
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
    peer_id: Optional[str] = None

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
    """Execute shell command and return output.
    Now prefers list arguments to avoid shell injection.
    """
    try:
        # If command is a string, we might still need shell=True for complex pipes,
        # but for simple IPFS/Cluster calls, we prefer a list.
        # SECURITY: If list provided, shell=False is enforced.
        is_shell = isinstance(command, str)
        
        if is_shell:
             # Sanity check: warn or block obvious dangerous chars if needed
             # For legacy calls that are still strings
             pass

        result = subprocess.run(
            command, shell=is_shell, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=BASE_DIR
        )
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {e.stderr}")
        return None

async def run_command_async(command: Union[str, List[str]]) -> Optional[str]:
    """Execute shell command asynchronously to avoid blocking the event loop"""
    return await asyncio.to_thread(run_command, command)

def update_social_manifest():
    """Create and publish manifest.json containing library and recommendations"""
    try:
        conn = get_db_connection()
        
        # Load library from DB
        posts = conn.execute("SELECT * FROM posts").fetchall()
        library = [dict(p) for p in posts]
        
        # Write library to file for IPFS add
        with open(LIBRARY_FILE, 'w') as f:
            json.dump(library, f, indent=2)
            
        # Generate vouched list from DB (likes)
        my_peer_id = get_my_peer_id()
        vouched_rows = conn.execute("SELECT post_cid FROM interactions WHERE user_peer_id = ? AND type = 'like'", (my_peer_id,)).fetchall()
        vouched = [r["post_cid"] for r in vouched_rows]
        
        with open(VOUCHED_FILE, 'w') as f:
            json.dump(vouched, f, indent=2)
            
        # Add library and vouched files to IPFS
        lib_res = run_command([IPFS_BIN, "add", "-Q", os.path.abspath(LIBRARY_FILE)])
        lib_cid = lib_res.strip() if lib_res else None
        
        vouched_res = run_command([IPFS_BIN, "add", "-Q", os.path.abspath(VOUCHED_FILE)])
        vouched_cid = vouched_res.strip() if vouched_res else None
        
        if not lib_cid:
            conn.close()
            return None
            
        # Get following list from DB
        following_rows = conn.execute("SELECT following_peer_id FROM following WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
        following_ids = [r["following_peer_id"] for r in following_rows]
        
        conn.close()
            
        manifest = {
            "version": "1.1",
            "library_cid": lib_cid,
            "vouched_cid": vouched_cid or "",
            "following": following_ids,
            "timestamp": datetime.now().strftime("%Y-%m-%dT%H:%M:%SZ")
        }
        
        save_json(MANIFEST_FILE, manifest)
        manifest_res = run_command([IPFS_BIN, "add", "-Q", os.path.abspath(MANIFEST_FILE)])
        manifest_cid = manifest_res.strip() if manifest_res else None
        
        if manifest_cid:
            run_command([CLUSTER_CTL, "pin", "add", manifest_cid])
            publish_to_ipns(manifest_cid)
            # Update local manifest.json with its own CID for reference
            manifest["manifest_cid"] = manifest_cid
            save_json(MANIFEST_FILE, manifest)
            return manifest_cid
    except Exception as e:
        print(f"Error updating manifest: {e}")
    return None

def load_user_data(filepath: str, did: str, default_item=None):
    """Load user-specific data from a shared file"""
    full_data = load_json(filepath, {})
    if not isinstance(full_data, dict):
        full_data = {}
    return full_data.get(did, default_item if default_item is not None else [])

def save_user_data(filepath: str, did: str, user_data):
    """Save user-specific data into a shared file"""
    full_data = load_json(filepath, {})
    full_data[did] = user_data
    save_json(filepath, full_data)

def get_current_did(request: Request):
    """Get DID from X-DID header"""
    return request.headers.get("X-DID", "anonymous")

def load_json(filepath: str, default_value=None):
    """Load JSON file safely"""
    if not os.path.exists(filepath):
        return default_value if default_value is not None else []
    try:
        with open(filepath, 'r') as f:
            data = json.load(f)
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
    """Generate a new P2P DID (did:key) and secret key"""
    try:
        # Generate Ed25519 keypair
        # did: string (did:key:z...)
        # secret: string (hex encoded private key)
        did, _, secret = generate_keypair()
        
        # Generate default profile
        # Use last 8 chars of DID for username suffix
        short_id = did.split(":")[-1][-6:]
        username = f"User_{short_id}"
        avatar = "files/avatar_placeholder.png"
        
        # Store in DB
        conn = get_db_connection()
        # Ensure user exists
        # We store the private key locally. 
        # In a real app, this should be encrypted at rest!
        conn.execute("""
            INSERT OR IGNORE INTO users (peer_id, username, handle, avatar, did, secret_key) 
            VALUES (?, ?, ?, ?, ?, ?)
        """, (did, username, f"@{username.lower()}", avatar, did, secret))
        conn.commit()
        conn.close()
        
        return {
            "did": did, 
            "secret": secret,
            "username": username,
            "avatar": avatar
        }
    except Exception as e:
        print(f"Identity generation error: {e}")
        raise HTTPException(status_code=500, detail="Failed to generate identity")

@app.get("/api/library")
async def get_library(request: Request):
    """Get all posts from library (Global + Recommended)"""
    did = get_current_did(request)
    my_peer_id = did # Use DID as my_peer_id
    conn = get_db_connection()
    
    # Fetch all posts from DB
    posts = conn.execute("SELECT * FROM posts ORDER BY timestamp DESC").fetchall()
    
    library = []
    for r in posts:
        p = dict(r)
        # Ensure compatibility
        p["cid"] = p["id"]
        # If peer_id missing, assume me
        if not p.get("peer_id"):
            p["peer_id"] = my_peer_id
        library.append(p)
    
    conn.close()
    return {"library": library, "count": len(library)}

@app.get("/api/profile/{peer_id}")
async def get_user_profile(peer_id: str, request: Request):
    """Get profile for a specific peer ID"""
    my_id = get_current_did(request)
    conn = get_db_connection()
    
    # 1. Check if it's me or in 'users' table
    user = conn.execute("SELECT * FROM users WHERE peer_id = ?", (peer_id,)).fetchone()
    if user:
        profile = dict(user)
        conn.close()
        return profile
    
    # 2. Check if we follow them
    following = conn.execute("SELECT * FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_id, peer_id)).fetchone()
    
    conn.close()
    
    if following:
        # We have some info from following
        return {
            "username": following["username"] if following["username"] else "Unknown",
            "peer_id": peer_id,
            "avatar": "files/avatar_placeholder.png", 
            "bio": f"Followed peer. Last synced: {following['last_synced']}",
            "stats": {"syncs": 0, "contacts": 0} 
        }

    # 3. Fallback / Unknown
    return {
        "username": f"User {peer_id[:6]}...",
        "peer_id": peer_id,
        "avatar": "files/avatar_placeholder.png",
        "bio": "Unknown peer",
        "stats": {"syncs": 0, "contacts": 0}
    }

@app.get("/api/feed/user/{peer_id}")
async def get_user_feed(peer_id: str, request: Request):
    """Get aggregated feed for a specific user"""
    # 1. Check if it's me
    my_id = get_current_did(request)
    if peer_id == my_id:
        conn = get_db_connection()
        posts = conn.execute("SELECT * FROM posts ORDER BY timestamp DESC").fetchall()
        library = [dict(post) for post in posts]
        conn.close()
        
        # Enrich with my ID
        for item in library:
            item["_peer_id"] = my_id
            item["peer_id"] = my_id
        return {"library": library, "count": len(library)}

    # 2. Check if we follow them
    conn = get_db_connection()
    # Check following table for library_cid
    peer_follow = conn.execute("SELECT * FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_id, peer_id)).fetchone()
    conn.close()
    
    if peer_follow and peer_follow["library_cid"]:
        try:
            peer_library = await fetch_peer_library(peer_follow["library_cid"])
            # Enrich
            for item in peer_library:
                item["_peer_id"] = peer_id
                item["peer_id"] = peer_id
            return {"library": peer_library, "count": len(peer_library)}
        except Exception as e:
            print(f"Error fetching peer library: {e}")
            return {"library": [], "count": 0}
            
    return {"library": [], "count": 0}

@app.post("/api/search")
async def search_library(request: Request):
    """Search library by metadata (title, description, author, filename) and Users"""
    body = await request.json()
    query = body.get("query", "").lower().strip()
    
    if not query:
        return {"results": [], "count": 0}
    
    conn = get_db_connection()
    wildcard_query = f"%{query}%"
    
    # 1. Search Posts
    search_posts_sql = """
        SELECT * FROM posts 
        WHERE lower(name) LIKE ? 
        OR lower(description) LIKE ? 
        OR lower(author) LIKE ? 
        OR lower(filename) LIKE ?
        OR id LIKE ?
    """
    posts = conn.execute(search_posts_sql, (wildcard_query, wildcard_query, wildcard_query, wildcard_query, wildcard_query)).fetchall()
    post_results = []
    for post in posts:
        p = dict(post)
        if not p.get("type"):
            p["type"] = "post" # Default fall back
        post_results.append(p)
    
    # 2. Search Users
    search_users_sql = """
        SELECT * FROM users 
        WHERE lower(username) LIKE ? 
        OR lower(handle) LIKE ?
        OR peer_id LIKE ?
    """
    users = conn.execute(search_users_sql, (wildcard_query, wildcard_query, wildcard_query)).fetchall()
    user_results = []
    
    for u in users:
        user_dict = dict(u)
        # Format as a "card" similar to posts or distinct type
        user_card = {
            "type": "user",
            "cid": user_dict["peer_id"], # Use peer_id as ID
            "name": user_dict["username"],
            "description": user_dict.get("bio", "IPFS User"),
            "author": user_dict.get("handle", ""),
            "avatar": user_dict.get("avatar", ""),
            "peer_id": user_dict["peer_id"],
            "did": user_dict.get("did"),
            "filename": "",
            "timestamp": ""
        }
        user_results.append(user_card)
        
    conn.close()
    
    return {"results": post_results + user_results, "count": len(post_results) + len(user_results)}
    
    # Combine (Users first?)
    combined = user_results + post_results
    
    return {"results": combined, "count": len(combined), "query": query}

@app.get("/api/library/{cid}")
async def get_post(cid: str):
    """Get specific post by CID"""
    conn = get_db_connection()
    post = conn.execute("SELECT * FROM posts WHERE id = ?", (cid,)).fetchone()
    conn.close()
    
    if not post:
        raise HTTPException(status_code=404, detail="Post not found")
        
    return dict(post)


@app.delete("/api/library/{cid}")
async def delete_post(cid: str):
    """Delete a post from library by CID"""
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check if exists
    exists = c.execute("SELECT 1 FROM posts WHERE id = ?", (cid,)).fetchone()
    if not exists:
        conn.close()
        raise HTTPException(status_code=404, detail="Post not found")
        
    c.execute("DELETE FROM posts WHERE id = ?", (cid,))
    c.execute("DELETE FROM interactions WHERE post_cid = ?", (cid,))
    conn.commit()
    conn.close()

    # Re-publish IPNS so synced peers see the deletion
    update_social_manifest()

    return {"success": True, "message": "Post deleted"}

@app.put("/api/library/{cid}")
async def update_post(cid: str, title: str = Form(...), description: str = Form(...)):
    """Update post title and description"""
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check if exists
    exists = c.execute("SELECT 1 FROM posts WHERE id = ?", (cid,)).fetchone()
    if not exists:
        conn.close()
        raise HTTPException(status_code=404, detail="Post not found")
        
    c.execute("UPDATE posts SET name = ?, description = ? WHERE id = ?", (title, description, cid))
    conn.commit()
    
    # Fetch updated
    post = c.execute("SELECT * FROM posts WHERE id = ?", (cid,)).fetchone()
    conn.close()
    
    return {"success": True, "post": dict(post)}

@app.post("/api/upload")
async def upload_file(
    request: Request,
    file: UploadFile = File(...),
    title: str = Form(""),
    description: str = Form(""),
    upload_type: str = Form("post"), # post, avatar, banner
    visibility: str = Form("public"), # public, connections
):
    """Upload file to IPFS and add to library (if post)"""
    did = get_current_did(request)
    
    if len(title) > 200:
        raise HTTPException(status_code=400, detail="Title too long (max 200 chars)")
    if len(description) > 5000:
        raise HTTPException(status_code=400, detail="Description too long (max 5000 chars)")
        
    try:
        # Save uploaded file temporarily
        temp_dir = tempfile.gettempdir()
        safe_filename = secure_filename(file.filename)
        temp_path = os.path.join(temp_dir, f"temp_{safe_filename}")
        with open(temp_path, "wb") as f:
            content = await file.read()
            f.write(content)
        
        
        final_path = temp_path

        # Add to IPFS
        cid = run_command([IPFS_BIN, "add", "-Q", final_path])
        if not cid:
            raise HTTPException(status_code=500, detail="Failed to add file to IPFS")
        
        # Pin to cluster
        run_command([CLUSTER_CTL, "pin", "add", cid])

        thumbnail_cid = None
        if upload_type == "post" and file.filename.lower().endswith(".pdf"):
            # Generate thumbnail for PDF
            try:
                thumb_path = generate_pdf_thumbnail(temp_path)
                if thumb_path and os.path.exists(thumb_path):
                    thumbnail_cid = run_command([IPFS_BIN, "add", "-Q", thumb_path])
                    if thumbnail_cid:
                        run_command([CLUSTER_CTL, "pin", "add", thumbnail_cid])
                    os.remove(thumb_path)
            except Exception as te:
                print(f"Thumbnail generation failed: {te}")

        if upload_type == "post":
            # Load user profile from DB
            conn = get_db_connection()
            # Use DID to find user
            user_profile = conn.execute("SELECT * FROM users WHERE peer_id = ?", (did,)).fetchone()
            
            if not user_profile:
                # Fallback if user not found (e.g. anonymous or new)
                user_profile = {"username": "Anonymous", "avatar": "files/avatar_placeholder.png"}
            else:
                user_profile = dict(user_profile)
            
            # Create library entry
            entry_dict = {
                "name": title or safe_filename,
                "description": description,
                "filename": safe_filename,
                "cid": cid,
                "thumbnail_cid": thumbnail_cid,
                "type": "file",
                "author": user_profile.get("username", "Anonymous"),
                "avatar": user_profile.get("avatar", "files/avatar_placeholder.png"),
                "peer_id": did, # Use DID as peer_id
                "visibility": visibility, 
                "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            }

            # Insert into DB
            c = conn.cursor()
            c.execute("""
                INSERT INTO posts (id, name, description, filename, type, author, avatar, timestamp, peer_id, tag)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                entry_dict["cid"],
                entry_dict["name"],
                entry_dict["description"],
                entry_dict["filename"],
                entry_dict["type"],
                entry_dict["author"],
                entry_dict["avatar"],
                entry_dict["timestamp"],
                entry_dict["peer_id"],

                "" # tag
            ))
            conn.commit()
            
            # Regenerate library.json for IPNS publishing
            posts = conn.execute("SELECT * FROM posts").fetchall()
            library_list = [dict(p) for p in posts]
            with open(LIBRARY_FILE, 'w') as f:
                json.dump(library_list, f, indent=2)
            conn.close()
            
            # Publish library update to IPNS
            abs_lib_path = os.path.abspath(LIBRARY_FILE)
            lib_cid = run_command([IPFS_BIN, "add", "-Q", abs_lib_path])
            if lib_cid:
                run_command([CLUSTER_CTL, "pin", "add", lib_cid])
                publish_to_ipns(lib_cid)
        
        # Cleanup
        if os.path.exists(temp_path):
            os.remove(temp_path)
        
        return {
            "success": True,
            "cid": cid,
            "thumbnail_cid": thumbnail_cid,
            "filename": safe_filename,
            "upload_type": upload_type
        }
    
    except Exception as e:
        print(f"Upload error: {e}")
        raise HTTPException(status_code=500, detail=str(e))

@app.get("/api/interactions")
async def get_interactions():
    """Get all user interactions (Aggregated)"""
    conn = get_db_connection()
    rows = conn.execute("SELECT post_cid, type, COUNT(*) as count FROM interactions GROUP BY post_cid, type").fetchall()
    conn.close()
    
    result = {}
    for r in rows:
        cid = r["post_cid"]
        if cid not in result: 
            result[cid] = {"likes_count": 0, "dislikes_count": 0, "views": 0, "comments": []}
            
        if r["type"] == "like": result[cid]["likes_count"] = r["count"]
        elif r["type"] == "dislike": result[cid]["dislikes_count"] = r["count"]
        elif r["type"] == "view": result[cid]["views"] = r["count"]
        
    return result

@app.get("/api/interactions/{cid}")
async def get_post_interactions(cid: str, request: Request):
    """Get interactions for specific post from SQLite"""
    peer_id = get_current_did(request)
    conn = get_db_connection()
    
    # Counts
    likes = conn.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'like'", (cid,)).fetchone()[0]
    dislikes = conn.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'dislike'", (cid,)).fetchone()[0]
    views = conn.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'view'", (cid,)).fetchone()[0]
    
    # My status
    my_like = conn.execute("SELECT 1 FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'like'", (cid, peer_id)).fetchone()
    my_dislike = conn.execute("SELECT 1 FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'dislike'", (cid, peer_id)).fetchone()
    
    # Comments
    comments_rows = conn.execute("SELECT * FROM comments WHERE post_cid = ?", (cid,)).fetchall()
    comments = [c["text"] for c in comments_rows] # Frontend expects list of strings currently? Or objects? 
                                                  # Checking original: "comments": [text, text...]
                                                  # Original save_comment: interactions[cid]["comments"].append(comment.text) -> YES list of strings.
    
    conn.close()
    
    return {
        "recommended": bool(my_like),
        "not_recommended": bool(my_dislike),
        "comments": comments,
        "views": views,
        "likes_count": likes,
        "dislikes_count": dislikes
    }

@app.post("/api/interactions/{cid}/like")
async def toggle_like(cid: str, request: Request):
    """Toggle like for a post"""
    peer_id = get_current_did(request)
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check current status
    existing = c.execute("SELECT id FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'like'", (cid, peer_id)).fetchone()
    
    if existing:
        # Unlike
        c.execute("DELETE FROM interactions WHERE id = ?", (existing["id"],))
        recommended = False
    else:
        # Like
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        c.execute("INSERT INTO interactions (post_cid, user_peer_id, type, timestamp) VALUES (?, ?, 'like', ?)", (cid, peer_id, timestamp))
        recommended = True
        
        # Remove dislike if exists
        c.execute("DELETE FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'dislike'", (cid, peer_id))
        
        # Pin content to cluster
        run_command([CLUSTER_CTL, "pin", "add", cid])
            
    conn.commit()
    
    # Get Updated Counts
    likes = c.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'like'", (cid,)).fetchone()[0]
    dislikes = c.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'dislike'", (cid,)).fetchone()[0]
    conn.close()
    
    # Update manifest
    update_social_manifest()
    
    return {
        "recommended": recommended,
        "likes_count": likes,
        "dislikes_count": dislikes
    }

@app.post("/api/interactions/{cid}/dislike")
async def toggle_dislike(cid: str, request: Request):
    """Toggle dislike for a post"""
    peer_id = get_current_did(request)
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check current status
    existing = c.execute("SELECT id FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'dislike'", (cid, peer_id)).fetchone()
    
    if existing:
        # Un-dislike
        c.execute("DELETE FROM interactions WHERE id = ?", (existing["id"],))
        not_recommended = False
    else:
        # Dislike
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        c.execute("INSERT INTO interactions (post_cid, user_peer_id, type, timestamp) VALUES (?, ?, 'dislike', ?)", (cid, peer_id, timestamp))
        not_recommended = True
        
        # Remove like if exists
        c.execute("DELETE FROM interactions WHERE post_cid = ? AND user_peer_id = ? AND type = 'like'", (cid, peer_id))
            
    conn.commit()
    
    # Get Updated Counts
    likes = c.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'like'", (cid,)).fetchone()[0]
    dislikes = c.execute("SELECT COUNT(*) FROM interactions WHERE post_cid = ? AND type = 'dislike'", (cid,)).fetchone()[0]
    conn.close()
    
    # Update manifest
    update_social_manifest()
    
    return {
        "not_recommended": not_recommended,
        "likes_count": likes,
        "dislikes_count": dislikes
    }

@app.post("/api/interactions/{cid}/comment")
async def add_comment(cid: str, comment: Comment, request: Request):
    """Add comment to a post"""
    peer_id = get_current_did(request)
    conn = get_db_connection()
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    # Need username
    user = conn.execute("SELECT username FROM users WHERE peer_id = ?", (peer_id,)).fetchone()
    username = user["username"] if user else "Anonymous"
    
    conn.execute("""
        INSERT INTO comments (post_cid, user_peer_id, username, text, timestamp)
        VALUES (?, ?, ?, ?, ?)
    """, (cid, peer_id, username, comment.text, timestamp))
    
    conn.commit()
    conn.close()
    
    return {"success": True, "comment": comment.text}

@app.delete("/api/interactions/{cid}/comment/{index}")
async def delete_comment(cid: str, index: int):
    """Delete comment from a post by index (legacy support)"""
    conn = get_db_connection()
    # Fetch all comments for this post ordered by timestamp
    comments = conn.execute("SELECT id FROM comments WHERE post_cid = ? ORDER BY timestamp ASC", (cid,)).fetchall()
    
    if index < 0 or index >= len(comments):
        conn.close()
        raise HTTPException(status_code=404, detail="Comment index out of range")
        
    comment_id = comments[index]["id"]
    conn.execute("DELETE FROM comments WHERE id = ?", (comment_id,))
    conn.commit()
    conn.close()
    
    return {"success": True}

# ==================== Cluster Replication Status ====================

@app.get("/api/cluster/status")
async def get_cluster_status():
    """Get cluster replication status for all pinned content"""
    try:
        output = run_command([CLUSTER_CTL, "status", "--local"])
        if not output:
            return {"status": "offline", "pins": []}
        
        pins = []
        lines = output.strip().split("\n")
        current_cid = None
        for line in lines:
            line = line.strip()
            if line and not line.startswith(">"):
                # CID line
                current_cid = line.split(":")[0].strip()
            elif line.startswith(">") and current_cid:
                parts = line.lstrip("> ").split(":")
                if len(parts) >= 2:
                    node_name = parts[0].strip()
                    status = parts[1].strip().split("|")[0].strip()
                    pins.append({
                        "cid": current_cid,
                        "node": node_name,
                        "status": status
                    })
        return {"status": "online", "pins": pins, "count": len(pins)}
    except Exception as e:
        return {"status": "error", "error": str(e)}

@app.get("/api/cluster/peers")
async def get_cluster_peers():
    """Get cluster peers and their replication status"""
    try:
        output = run_command([CLUSTER_CTL, "peers", "ls"])
        if not output:
            return {"peers": [], "count": 0}
        
        peers = []
        lines = output.strip().split("\n")
        for line in lines:
            if "|" in line and not line.strip().startswith(">"):
                parts = [p.strip() for p in line.split("|")]
                if len(parts) >= 3:
                    peers.append({
                        "peer_id": parts[0],
                        "name": parts[1],
                        "status": parts[2]
                    })
        return {"peers": peers, "count": len(peers)}
    except Exception as e:
        return {"peers": [], "count": 0, "error": str(e)}

# ==================== Follow/Sync System ====================

def publish_to_ipns(library_cid: str) -> bool:
    """Publish library CID to IPNS"""
    try:
        result = run_command([IPFS_BIN, "name", "publish", library_cid])
        return bool(result)
    except Exception as e:
        print(f"IPNS publish error: {e}")
        return False

async def resolve_ipns(peer_id: str) -> Optional[str]:
    """Resolve peer's IPNS name to get their library CID asynchronously"""
    try:
        cid = await run_command_async([IPFS_BIN, "name", "resolve", f"/ipns/{peer_id}"])
        return cid.strip() if cid else None
    except Exception:
        return None

async def fetch_ipfs_json(cid: str) -> Union[List, Dict]:
    """Fetch JSON from IPFS asynchronously"""
    try:
        # Avoid unnecessary shell calls for empty CIDs
        if not cid or cid == "[]" or cid == "{}":
            return [] if cid == "[]" else {}
            
        content = await run_command_async([IPFS_BIN, "cat", cid])
        if content:
            return json.loads(content)
        return [] if cid == "[]" else {}
    except Exception as e:
        print(f"Fetch IPFS JSON error for {cid}: {e}")
        return []

async def fetch_peer_library(library_cid: str) -> List[Dict]:
    """Fetch library content from IPFS asynchronously"""
    res = await fetch_ipfs_json(library_cid)
    return res if isinstance(res, list) else []

@app.post("/api/follow/{peer_id}")
async def follow_peer(peer_id: str, request: Request, relationship_type: str = "sync"):
    """Follow an IPFS peer and sync their manifest"""
    try:
        my_peer_id = get_current_did(request)
        conn = get_db_connection()
        c = conn.cursor()
        
        # Check if already following
        exists = c.execute("SELECT 1 FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_peer_id, peer_id)).fetchone()
        if exists:
            conn.close()
            return {"success": False, "message": "Already following this peer"}
        
        # Resolve IPNS to get manifest/library CID
        resolved_cid = await resolve_ipns(peer_id)
        if not resolved_cid:
            conn.close()
            raise HTTPException(status_code=404, detail="Could not resolve peer's IPNS name")
        
        # Determine if it's a manifest or a library (version 1.0/1.1 support)
        resolved_data = await fetch_ipfs_json(resolved_cid)
        
        library_cid = resolved_cid
        vouched_cid = None
        
        if isinstance(resolved_data, dict) and resolved_data.get("version") in ("1.0", "1.1"):
            library_cid = resolved_data.get("library_cid")
            vouched_cid = resolved_data.get("vouched_cid")
        
        # Fetch library
        if not library_cid:
             conn.close()
             raise HTTPException(status_code=404, detail="Could not find library CID")
        peer_library = await fetch_peer_library(library_cid)
        
        # Pin latest content
        for item in peer_library[:10]:
            item_cid = item.get('cid', item.get('id', ''))
            if item_cid:
                run_command([IPFS_BIN, "pin", "add", item_cid])
        
        # Resolve username: check users table first, then library author
        username = "Unknown"
        user_row = c.execute("SELECT username FROM users WHERE peer_id = ?", (peer_id,)).fetchone()
        if user_row:
            username = user_row["username"]
        elif peer_library:
            username = peer_library[0].get("author", "Unknown")
            
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        
        c.execute("""
            INSERT INTO following (user_peer_id, following_peer_id, relationship_type, timestamp, library_cid, vouched_cid, username)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (my_peer_id, peer_id, relationship_type, timestamp, library_cid, vouched_cid, username))
        conn.commit()
        conn.close()
        
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
async def unfollow_peer(peer_id: str, request: Request):
    """Unfollow a peer"""
    my_peer_id = get_current_did(request)
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check if exists
    exists = c.execute("SELECT 1 FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_peer_id, peer_id)).fetchone()
    
    if not exists:
         conn.close()
         raise HTTPException(status_code=404, detail="Not following this peer")
         
    c.execute("DELETE FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_peer_id, peer_id))
    conn.commit()
    conn.close()
    
    return {"success": True, "message": "Unfollowed successfully"}

@app.get("/api/peers/discover")
async def discover_peers():
    """Discover peers on the local network via IPFS swarm"""
    try:
        # Get list of connected peers
        peers_raw_output = run_command([IPFS_BIN, "swarm", "peers"])
        peers_raw = peers_raw_output.splitlines() if peers_raw_output else []
        discovered = []
        
        # Limit to first 10 for performance
        for line in peers_raw[:10]:
            parts = line.split('/')
            if len(parts) > 2:
                peer_id = parts[-1]
                discovered.append({
                    "peer_id": peer_id,
                    "address": line,
                    "is_local": "192.168" in line or "127.0.0.1" in line
                })
        
        return {"peers": discovered, "count": len(discovered)}
    except Exception as e:
        return {"error": str(e), "peers": []}

@app.post("/api/connections/request")
async def send_connection_request(request: Request, peer_id: str = Form(...)):
    """Send a connection request to another peer"""
    did = get_current_did(request)
    if did == "anonymous":
        return {"success": False, "message": "Not authenticated"}
    
    # In a real decentralised app, this would be an IPFS message
    # For now we use local storage to simulate the "Sent" state
    connections = load_user_data(CONNECTIONS_FILE, did, {"sent": [], "received": [], "contacts": []})
    
    if peer_id not in connections["sent"]:
        connections["sent"].append(peer_id)
        save_user_data(CONNECTIONS_FILE, did, connections)
        
        # In a real app, the RECEIVER would generate this notification upon receiving the P2P message.
        # Since we are mocking the connection flow locally (or via shared FS for now),
        # we can't easily notify the *other* person unless they are on the same node.
        # But if we were sending a P2P message, the receiver's handle_inbox_message would trigger it.
        # For this prototype, if we simply add it to "sent", we don't notify ourselves.
        pass
    
    return {"success": True, "message": "Connection request sent"}

@app.post("/api/connections/accept")
async def accept_connection_request(request: Request, peer_id: str = Form(...)):
    """Accept an incoming connection request"""
    did = get_current_did(request)
    if did == "anonymous":
        return {"success": False, "message": "Not authenticated"}
    
    connections = load_user_data(CONNECTIONS_FILE, did, {"sent": [], "received": [], "contacts": []})
    
    if peer_id in connections["received"]:
        connections["received"].remove(peer_id)
        if peer_id not in connections["contacts"]:
            connections["contacts"].append(peer_id)
        save_user_data(CONNECTIONS_FILE, did, connections)
        
        # Also auto-follow
        following = load_user_data(FOLLOWING_FILE, did, [])
        if peer_id not in following:
            following.append(peer_id)
            save_user_data(FOLLOWING_FILE, did, following)
            
    return {"success": True, "message": "Connection accepted"}

@app.get("/api/connections")
async def get_connections(request: Request):
    """Get user's connections and requests"""
    did = get_current_did(request)
    return load_user_data(CONNECTIONS_FILE, did, {"sent": [], "received": [], "contacts": []})

@app.get("/api/following")
async def get_following(request: Request):
    """Get list of peers being followed"""
    my_peer_id = get_current_did(request)
    conn = get_db_connection()
    # Fetch full details?
    # Original 'following' list had metadata like 'username', 'last_synced'.
    # Our DB only has 'following_peer_id', 'timestamp'.
    # We might need to fetch profile info from 'users' table if we stored it?
    # Or 'users' table only stores OUR profile?
    # Schema for 'users' is for the local user profile usually, but maybe we should store other users there too?
    # The 'users' table has 'peer_id' unique. 
    # Let's assume we store discovered users there?
    # If not, we just return peer_ids and client fetches profile?
    # Or for MVP just return simple list. 
    # Front-end expects array of objects.
    
    rows = conn.execute("SELECT following_peer_id, timestamp, relationship_type FROM following WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
    
    following_list = []
    for r in rows:
        following_list.append({
            "peer_id": r["following_peer_id"],
            "username": "Unknown", # We'd need to fetch this or store it in 'following' table?
            "relationship_type": r["relationship_type"],
            "followed_at": r["timestamp"]
        })
        
    conn.close()
    return {"following": following_list, "count": len(following_list)}

@app.post("/api/sync-peers")
async def sync_peers(request: Request):
    """Manually sync content from all followed peers and their network (Hierarchy)"""
    my_peer_id = get_current_did(request)
    conn = get_db_connection()
    c = conn.cursor()
    
    following_rows = c.execute("SELECT * FROM following WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
    following = [dict(r) for r in following_rows]
    synced_count = 0
    
    # Track which CIDs we've pinned to avoid redundant work
    pinned_cids = set()
    
    async def process_peer(peer_id, depth=0):
        if depth > 1: # Limit depth to prevent infinite loops / excessive pinning
            return
        
        try:
            # Resolve latest manifest/library CID
            resolved_cid = await resolve_ipns(peer_id)
            if not resolved_cid:
                return
            
            manifest = await fetch_ipfs_json(resolved_cid)
            if not isinstance(manifest, dict):
                # Fallback for old 1.0 style where root was library_cid
                library_cid = resolved_cid
                nested_following = []
            else:
                library_cid = manifest.get("library_cid")
                nested_following = manifest.get("following", [])
            
            if not library_cid:
                return

            # Determine sample rate for this tier
            # Tier 0 (Direct Connections): 100%
            # Tier 1 (Extended Network/Friends of Friends): 20%
            sample_rate = 1.0 if depth == 0 else 0.2

            # Fetch and pin library content
            peer_library = await fetch_peer_library(library_cid)
            for item in peer_library[:15]: # Increased limit for direct peers
                if item['cid'] not in pinned_cids:
                    # Stochastic Pinning: only pin if sample check passes
                    if random.random() < sample_rate:
                        # print(f"{'  ' * depth}Pinning {'Direct' if depth == 0 else 'Network Sample'}: {item.get('name')}")
                        await run_command_async([IPFS_BIN, "pin", "add", item['cid']])
                        pinned_cids.add(item['cid'])
            
            # Recursive sync for hierarchy
            for nested_id in nested_following[:5]: # Speed optimization: cap nested peers
                await process_peer(nested_id, depth + 1)
                
            return resolved_cid
        except Exception as e:
            print(f"Recursive sync error for {peer_id}: {e}")
            return None

    for peer in following:
        new_cid = await process_peer(peer["following_peer_id"], depth=0)
        if new_cid and new_cid != peer.get("library_cid"):
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            c.execute("UPDATE following SET library_cid = ?, last_synced = ? WHERE user_peer_id = ? AND following_peer_id = ?", 
                      (new_cid, timestamp, my_peer_id, peer["following_peer_id"]))
            synced_count += 1
    
    if synced_count > 0:
        conn.commit()
    
    conn.close()
    return {"success": True, "synced_peers": synced_count}

@app.get("/api/feed/aggregated")
async def get_aggregated_feed():
    """Get feed aggregated from own library + followed peers"""
    my_peer_id = get_my_peer_id()
    conn = get_db_connection()
    c = conn.cursor()
    
    # 1. Fetch my posts — include ALL fields for PostCard rendering
    my_posts_rows = c.execute("""
        SELECT id as cid, name, description, filename, type, author, avatar,
               timestamp, peer_id, size, is_pinned, content, visibility, 
               original_cid, tag
        FROM posts
    """).fetchall()
    my_posts = [dict(r) for r in my_posts_rows]
        
    # 2. Fetch following
    following_rows = c.execute("SELECT * FROM following WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
    following = [dict(r) for r in following_rows]
    following_ids = [f["following_peer_id"] for f in following]
    
    # 3. Fetch interactions stats
    counts_rows = c.execute("SELECT post_cid, type, COUNT(*) as count FROM interactions GROUP BY post_cid, type").fetchall()
    interactions_map = {}
    for r in counts_rows:
        cid = r["post_cid"]
        if cid not in interactions_map: interactions_map[cid] = {"likes_count": 0, "dislikes_count": 0}
        if r["type"] == "like": interactions_map[cid]["likes_count"] = r["count"]
        elif r["type"] == "dislike": interactions_map[cid]["dislikes_count"] = r["count"]
        
    conn.close()
    
    all_posts = list(my_posts)
    
    # 4. Fetch followed content
    for peer in following:
        try:
            library_cid = peer.get("library_cid")
            if library_cid:
                peer_library = await fetch_peer_library(library_cid)
                for item in peer_library:
                    item["_from_peer"] = peer.get("username", "Unknown")
                    item["peer_id"] = peer.get("following_peer_id")
                all_posts.extend(peer_library)
        except Exception as e:
            # print(f"Error fetching peer {peer.get('following_peer_id')}: {e}")
            pass

    # 5. Discovery & Privacy Filtering
    filtered_posts = []
    
    for item in all_posts:
        cid = item.get("cid")
        item_peer_id = item.get("peer_id")
        
        # Privacy Check
        item_visibility = item.get("visibility", "public")
        is_mine = item_peer_id == my_peer_id
        is_from_connection = item_peer_id in following_ids
        
        if item_visibility == "connections" and not (is_mine or is_from_connection):
            continue
            
        # Sentiment Filtering
        item_stats = interactions_map.get(cid, {})
        likes = item_stats.get("likes_count", 0)
        dislikes = item_stats.get("dislikes_count", 0)
        
        if dislikes > likes and not (is_mine or is_from_connection):
            continue
            
        filtered_posts.append(item)

    filtered_posts.sort(key=lambda x: x.get("timestamp", ""), reverse=True)
    return {"library": filtered_posts, "count": len(filtered_posts)}

@app.get("/api/feed/recommended")
async def get_recommended_feed():
    """Aggregate posts recommended (socially vouched) by followed peers"""
    try:
        my_peer_id = get_my_peer_id()
        conn = get_db_connection()
        following_rows = conn.execute("SELECT * FROM following WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
        following = [dict(r) for r in following_rows]
        conn.close()
        
        recommended_posts = []
        seen_cids = set()

        for peer in following:
            vouched_cid = peer.get("vouched_cid")
            if not vouched_cid:
                continue
                
            # Fetch the list of CIDs this peer recommended
            vouched_list = await fetch_ipfs_json(vouched_cid)
            if not isinstance(vouched_list, list):
                continue
                
            # For each CID, fetch the actual post metadata
            # We look for it in the peer's own library first, then global
            peer_library = []
            if peer.get("library_cid"):
                peer_library = await fetch_peer_library(peer["library_cid"])
                
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

@app.get("/api/profile/{peer_id}/likes")
async def get_user_likes(peer_id: str):
    """Get list of post CIDs liked by a specific peer"""
    conn = get_db_connection()
    rows = conn.execute("SELECT post_cid FROM interactions WHERE user_peer_id = ? AND type = 'like'", (peer_id,)).fetchall()
    conn.close()
    return {"likes": [r["post_cid"] for r in rows]}

# ==================== Notifications ====================

def create_notification(user_did: str, notif_type: str, title: str, message: str, link: str = ""):
    """Create a notification for a user in the DB"""
    try:
        conn = get_db_connection()
        timestamp = datetime.now().isoformat()
        conn.execute("""
            INSERT INTO notifications (user_peer_id, type, title, message, link, timestamp, is_read)
            VALUES (?, ?, ?, ?, ?, ?, 0)
        """, (user_did, notif_type, title, message, link, timestamp))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"create_notification error: {e}")

@app.get("/api/notifications")
async def get_notifications(request: Request):
    """Get notifications for the current user"""
    did = get_current_did(request)
    conn = get_db_connection()
    rows = conn.execute(
        "SELECT * FROM notifications WHERE user_peer_id = ? ORDER BY timestamp DESC LIMIT 50",
        (did,)
    ).fetchall()
    conn.close()
    return {"notifications": [dict(r) for r in rows]}

@app.post("/api/notifications/{notif_id}/read")
async def mark_notification_read(notif_id: int):
    """Mark a notification as read"""
    conn = get_db_connection()
    conn.execute("UPDATE notifications SET is_read = 1 WHERE id = ?", (notif_id,))
    conn.commit()
    conn.close()
    return {"success": True}

@app.post("/api/notifications/read-all")
async def mark_all_notifications_read(request: Request):
    """Mark all notifications as read"""
    did = get_current_did(request)
    conn = get_db_connection()
    conn.execute("UPDATE notifications SET is_read = 1 WHERE user_peer_id = ?", (did,))
    conn.commit()
    conn.close()
    return {"success": True}

# ==================== Direct Messages (DM) System ====================

def get_my_peer_id() -> str:
    """Get own IPFS Peer ID"""
    try:
        return run_command([IPFS_BIN, "id", "--format=<id>"]).strip()
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
    my_peer_id = get_my_peer_id()
    conn = get_db_connection()
    c = conn.cursor()
    
    # Get distinct peers with latest message
    # SQL to get latest message per peer is complex, simpler: get all messages, group in python? 
    # Or simplified query:
    query = """
        SELECT 
            CASE WHEN sender_peer_id = ? THEN receiver_peer_id ELSE sender_peer_id END as peer_id,
            text, timestamp, is_read
        FROM messages 
        WHERE sender_peer_id = ? OR receiver_peer_id = ?
        ORDER BY timestamp DESC
    """
    rows = c.execute(query, (my_peer_id, my_peer_id, my_peer_id)).fetchall()
    
    conversations = {}
    for r in rows:
        pid = r["peer_id"]
        if pid not in conversations:
            conversations[pid] = {
                "peer_id": pid,
                "last_message": r["text"],
                "timestamp": r["timestamp"],
                "unread_count": 0 if r["is_read"] or r["peer_id"] == my_peer_id else 1 # logic approx
            }
        else:
             # Just counting unreads if it's incoming and unread
             pass # logic for unread count omitted for brevity/simplicity unless column exists
             
    conn.close()
    return {"conversations": list(conversations.values())}

@app.get("/api/network-info")
def get_network_info():
    """Get local network info for QR codes"""
    try:
        # Connect to a dummy external IP to get local interface IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
        return {"ip": local_ip}
    except Exception:
        return {"ip": "127.0.0.1"}

@app.get("/api/messages/{peer_id}")
async def get_chat_history(peer_id: str):
    """Get chat history with a specific peer"""
    my_peer_id = get_my_peer_id()
    conn = get_db_connection()
    
    query = """
        SELECT sender_peer_id as sender, text, timestamp, cid, filename, mime_type 
        FROM messages 
        WHERE (sender_peer_id = ? AND receiver_peer_id = ?) 
           OR (sender_peer_id = ? AND receiver_peer_id = ?)
        ORDER BY timestamp ASC
    """
    rows = conn.execute(query, (my_peer_id, peer_id, peer_id, my_peer_id)).fetchall()
    history = [dict(r) for r in rows]
    conn.close()
    
    return {"history": history}

# --- PubSub Background Listener ---

ACTIVE_TOPICS = set()



# ==================== P2P Messaging Logic ====================

async def handle_inbox_message(data: dict):
    """
    Handle incoming P2P messages from our inbox topic.
    Payload expected: { "payload": str(json), "signature": str, "did": str }
    """
    try:
        sender_did = data.get("did")
        signature = data.get("signature")
        payload_str = data.get("payload")
        
        if not sender_did or not signature or not payload_str:
            return
            
        # Verify Signature
        if not verify_message(payload_str, signature, sender_did):
            print(f"Invalid signature from {sender_did}")
            return
            
        # Parse content
        try:
            content = json.loads(payload_str)
        except json.JSONDecodeError:
            print("Invalid JSON payload")
            return
            
        text = content.get("text")
        timestamp = content.get("timestamp")
        cid = content.get("cid")
        filename = content.get("filename")
        mime_type = content.get("mime_type")
        
        if not text and not cid:
            return

        # Use the IPFS sender provided by P2PClient envelope
        sender_peer_id = data.get("_from_peer_id")
        
        if not sender_peer_id:
             print("No sender peer ID found")
             return
            
        # Store in DB
        my_peer_id = get_my_peer_id()
        conn = get_db_connection()
        conn.execute("""
            INSERT INTO messages (sender_peer_id, receiver_peer_id, text, timestamp, cid, filename, mime_type, is_read)
            VALUES (?, ?, ?, ?, ?, ?, ?, 0)
        """, (sender_peer_id, my_peer_id, text, timestamp or datetime.now().isoformat(), cid, filename, mime_type))
        conn.commit()
        conn.close()
        
        print(f"Received P2P message from {sender_peer_id}: {text[:20]}...")
        
        # Create Notification
        # We need the DID of the receiver (myself) to look up the peer_id in create_notification
        # But create_notification takes DID.
        # We can just get my DID from the DB since I am the receiver.
        conn = get_db_connection()
        my_user = conn.execute("SELECT did FROM users WHERE peer_id = ?", (my_peer_id,)).fetchone()
        conn.close()
        
        if my_user:
             create_notification(
                my_user["did"], 
                "message", 
                f"Message from {sender_peer_id[:8]}...", 
                text[:50], 
                f"/messages/{sender_peer_id}"
            )

    except Exception as e:
        print(f"Error handling P2P message: {e}")

@app.post("/api/messages/send")
async def send_direct_message(
    peer_id: str = Form(...),
    text: str = Form(""),
    file: Optional[UploadFile] = File(None),
    request: Request = None
):
    """
    Send a P2P message.
    1. Sign message with my DID key.
    2. Publish to receiver's inbox.
    3. Store in local DB.
    """
    # Auth Check
    did = request.headers.get("X-DID")
    if not did:
        # Check if local (no header but calling from same machine?)
        # For now strict
        raise HTTPException(status_code=401, detail="Unauthorized")
        
    conn = get_db_connection()
    user = conn.execute("SELECT secret_key, peer_id FROM users WHERE did = ?", (did,)).fetchone()
    conn.close()
    
    if not user:
        raise HTTPException(status_code=401, detail="User not found")
        
    secret_key = user["secret_key"]
    my_peer_id = user["peer_id"] or get_my_peer_id() # fallback
    
    try:
        # Resolve Recipient Peer ID (if DID provided in peer_id field)
        receiver_peer_id = peer_id
        if peer_id.startswith("did:key:"):
             # It's a DID, let's try to resolve it to PeerID for DB storage (and for topic if using peer_id topic)
             # But our new plan is to use DID as topic?
             # Wait, existing code uses peer_id for everything.
             # We should probably resolve logic:
             # If startswith did:key, convert to peer_id using helper.
             resolved = did_to_peer_id(peer_id)
             if resolved:
                 print(f"Resolved DID {peer_id} to PeerID {resolved}")
                 receiver_peer_id = resolved
             else:
                 print(f"Warning: Could not resolve DID {peer_id} to PeerID")
                 # We might still use the DID as the 'peer_id' in DB if we want?
                 # But IPFS commands need PeerID.
        
        # Topic selection:
        # If receiver is using new app version, they subscribe to /app/inbox/{did} ?
        # Or /app/inbox/{peer_id} ?
        # We stuck with /app/inbox/{peer_id} in startup_event.
        # So we MUST send to {peer_id}.
        
        # Prepare Payload
        timestamp = datetime.now().isoformat()
        content = {"text": text, "timestamp": timestamp}
        
        cid = None
        filename = None
        mime_type = None
        
        if file:
            try:
                # Save temp file
                suffix = os.path.splitext(file.filename)[1] if file.filename else ""
                with tempfile.NamedTemporaryFile(delete=False, suffix=suffix) as tmp:
                    shutil.copyfileobj(file.file, tmp)
                    tmp_path = tmp.name
                
                # Add to IPFS
                ipfs_res = await run_command_async([IPFS_BIN, "add", "-Q", tmp_path])
                if ipfs_res:
                    cid = ipfs_res.strip()
                    filename = file.filename
                    mime_type = file.content_type
                    
                    # Update content payload
                    content["cid"] = cid
                    content["filename"] = filename
                    content["mime_type"] = mime_type
                    
                    # Pin it
                    await run_command_async([CLUSTER_CTL, "pin", "add", cid])
                
                # Cleanup
                os.unlink(tmp_path)
            except Exception as e:
                print(f"File upload error in message: {e}")

        payload_str = json.dumps(content)
        
        # Sign
        signature = sign_message(payload_str, secret_key)
        
        # Wrap
        message = {
            "payload": payload_str,
            "signature": signature,
            "did": did,
            "_from_peer_id": my_peer_id # For receiver to know who we are (IPFS level)
        }
        
        # Publish P2P
        if p2p_client:
            topic = f"/app/inbox/{receiver_peer_id}"
            success = await p2p_client.publish(topic, message)
            if not success:
               print(f"Warning: Failed to publish to {topic}")
        else:
            print("P2P Client not initialized")

        # Store locally
        conn = get_db_connection()
        conn.execute("""
            INSERT INTO messages (sender_peer_id, receiver_peer_id, text, timestamp, cid, filename, mime_type, is_read)
            VALUES (?, ?, ?, ?, ?, ?, ?, 1)
        """, (my_peer_id, receiver_peer_id, text, timestamp, cid, filename, mime_type))
        conn.commit()
        conn.close()
        
        return {"success": True, "timestamp": timestamp, "cid": cid}
        
    except Exception as e:
        print(f"Send error: {e}")
        raise HTTPException(status_code=500, detail="Failed to send message")

@app.post("/api/contacts/add")
async def add_contact(did: str = Form(...), request: Request = None):
    """
    Add a new contact by DID.
    Resolves DID to PeerID and adds to following.
    """
    my_did = request.headers.get("X-DID")
    if not my_did:
        raise HTTPException(status_code=401, detail="Unauthorized")
        
    # Get my peer id
    my_peer_id = get_my_peer_id()
    
    # Resolve DID
    peer_id = did_to_peer_id(did)
    if not peer_id:
        raise HTTPException(status_code=400, detail="Invalid DID or could not resolve to PeerID")
        
    conn = get_db_connection()
    try:
        # Check if already exists
        exists = conn.execute("SELECT 1 FROM following WHERE user_peer_id = ? AND following_peer_id = ?", (my_peer_id, peer_id)).fetchone()
        
        if not exists:
            # Add to following
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            conn.execute("""
                INSERT INTO following (user_peer_id, following_peer_id, relationship_type, timestamp)
                VALUES (?, ?, 'contact', ?)
            """, (my_peer_id, peer_id, timestamp))
            conn.commit()
            
        return {"success": True, "peer_id": peer_id, "did": did}
    except Exception as e:
        print(f"Add contact error: {e}")
        raise HTTPException(status_code=500, detail="Failed to add contact")
    finally:
        conn.close()


@app.get("/api/profile")
async def get_profile(request: Request):
    """Get logged in user profile from SQLite"""
    did = get_current_did(request)
    if did == "anonymous":
        return {"username": "Guest", "bio": "Please log in."}
    
    conn = get_db_connection()
    user = conn.execute("SELECT * FROM users WHERE did = ?", (did,)).fetchone()
    conn.close()
    
    if not user:
        # Return onboarding defaults
        return {
            "username": f"New User {did[:8]}",
            "handle": f"@{did[:8]}",
            "avatar": "",
            "bio": "New to the swarm.",
            "location": "",
            "tags": [],
            "stats": {"syncs": 0, "contacts": 0, "posts": 0, "likes": 0},
            "peer_id": get_my_peer_id(),
            "did": did,
            "onboarding": True
        }
    return dict(user)

@app.get("/api/network-info")
async def get_network_info():
    """Get the host's LAN IP address"""
    try:
        # Get host LAN IP
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0)
        try:
            # doesn't even have to be reachable
            s.connect(('10.254.254.254', 1))
            ip = s.getsockname()[0]
        except Exception:
            ip = '127.0.0.1'
        finally:
            s.close()
        return {"ip": ip, "port": 3000}
    except Exception as e:
        return {"error": str(e), "ip": "127.0.0.1"}

@app.post("/api/profile")
async def update_profile_endpoint(
    request: Request,
    username: str = Form(None),
    handle: str = Form(None),
    bio: str = Form(None),
    location: str = Form(None),
    avatar: str = Form(None),
    banner: str = Form(None)
):
    """Update user profile in SQLite"""
    did = get_current_did(request)
    if did == "anonymous":
        return {"success": False, "message": "Not authenticated"}
    
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check if user exists
    user = c.execute("SELECT * FROM users WHERE did = ?", (did,)).fetchone()
    
    if not user:
        # Insert new user
        peer_id = did # Use DID as peer_id for new users
        c.execute("""
            INSERT INTO users (peer_id, username, handle, bio, location, avatar, banner, did)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            peer_id,
            username or f"New User {did[:8]}",
            handle or f"@{did[:8]}",
            bio or "",
            location or "",
            avatar or "",
            banner or "",
            did
        ))
        profile = {
            "peer_id": peer_id,
            "username": username or f"New User {did[:8]}",
            "handle": handle or f"@{did[:8]}",
            "bio": bio or "",
            "location": location or "",
            "avatar": avatar or "",
            "banner": banner or "",
            "did": did,
            "onboarding": not (username or handle)
        }
    else:
        # Update existing
        profile = dict(user)
        # Construct update query dynamically? Or just update fields if provided
        # Since logic says "if username: profile[username] = username", it implies partial updates.
        
        updates = []
        params = []
        
        if username: 
            updates.append("username = ?")
            params.append(username)
            profile["username"] = username
        if handle:
            updates.append("handle = ?")
            params.append(handle)
            profile["handle"] = handle
        if bio:
            updates.append("bio = ?")
            params.append(bio)
            profile["bio"] = bio
        if location:
            updates.append("location = ?")
            params.append(location)
            profile["location"] = location
        if avatar:
            updates.append("avatar = ?")
            params.append(avatar)
            profile["avatar"] = avatar
        if banner:
            updates.append("banner = ?")
            params.append(banner)
            profile["banner"] = banner
            
        if updates:
            sql = f"UPDATE users SET {', '.join(updates)} WHERE did = ?"
            params.append(did)
            c.execute(sql, tuple(params))
            
    conn.commit()
    conn.close()
    
    return {"success": True, "profile": profile}

# ==================== Social Recovery System ====================

@app.get("/api/guardians")
async def get_guardians(request: Request):
    """Get list of user's guardians"""
    did = get_current_did(request) # We might need DID or peer_id. 
                                   # Guardians are tied to the profile which is tied to DID/PeerID.
                                   # Current implementation used USER_PROFILE_FILE which was DID-based manifest.
                                   # BUT local app usually runs as one peer.
                                   # I'll use get_my_peer_id() for now as guardians are for the local node recovery?
                                   # Or DID? Key management... 
                                   # User profile file was keyed by DID.
                                   # But guardians table uses `user_peer_id`.
                                   # I'll use get_my_peer_id().
    peer_id = get_my_peer_id()
    conn = get_db_connection()
    guardians = conn.execute("SELECT guardian_peer_id FROM guardians WHERE user_peer_id = ?", (peer_id,)).fetchall()
    conn.close()
    return {"guardians": [g["guardian_peer_id"] for g in guardians]}

@app.post("/api/guardians/add")
async def add_guardian(peer_id: str = Form(...)):
    """Add a guardian (max 7)"""
    my_peer_id = get_my_peer_id()
    conn = get_db_connection()
    c = conn.cursor()
    
    # Check count
    count = c.execute("SELECT COUNT(*) FROM guardians WHERE user_peer_id = ?", (my_peer_id,)).fetchone()[0]
    if count >= 7:
        conn.close()
        raise HTTPException(status_code=400, detail="Maximum 7 guardians allowed")
        
    # Check if exists
    exists = c.execute("SELECT 1 FROM guardians WHERE user_peer_id = ? AND guardian_peer_id = ?", (my_peer_id, peer_id)).fetchone()
    if exists:
        conn.close()
        raise HTTPException(status_code=400, detail="Peer already a guardian")
        
    c.execute("INSERT INTO guardians (user_peer_id, guardian_peer_id) VALUES (?, ?)", (my_peer_id, peer_id))
    conn.commit()
    
    # Return updated list
    guardians = c.execute("SELECT guardian_peer_id FROM guardians WHERE user_peer_id = ?", (my_peer_id,)).fetchall()
    guardian_list = [g["guardian_peer_id"] for g in guardians]
    conn.close()
    
    return {"success": True, "guardians": guardian_list}

# --- Standard SSSS Recovery Endpoints ---

@app.post("/api/recovery/setup")
async def setup_recovery(threshold: int = Form(3), shares: int = Form(5), request: Request = None):
    """
    Generate SSSS shards for the current user's secret key.
    User must securely distribute these to guardians.
    """
    # Verify auth by getting current user's secret
    did = request.headers.get("X-DID")
    if not did:
        raise HTTPException(status_code=401, detail="Unauthorized")
        
    conn = get_db_connection()
    user = conn.execute("SELECT secret_key FROM users WHERE did = ?", (did,)).fetchone()
    conn.close()
    
    if not user or not user["secret_key"]:
        raise HTTPException(status_code=404, detail="User not found")
        
    secret = user["secret_key"]
    
    # Generate shards
    # secret_key is hex string
    shards = split_secret(secret, threshold=threshold, share_count=shares)
    
    if not shards:
        raise HTTPException(status_code=500, detail="Failed to generate shards")
        
    return {
        "success": True,
        "shards": shards,
        "instructions": f"Distribute these {shares} shards to trusted guardians. You need {threshold} to recover."
    }

@app.post("/api/recovery/restore")
async def restore_identity(shards: List[str] = Form(...)):
    """
    Recover identity from shards.
    Combines shards -> Secret Key -> Derives DID -> Updates/Restores DB.
    """
    try:
        # 1. Reconstruct Secret
        secret = combine_shards(shards)
        if not secret:
            raise HTTPException(status_code=400, detail="Failed to reconstruct secret. Invalid shards or insufficient threshold.")
            
        # 2. Derive DID from Secret
        # We need to regenerate the keypair to get the DID
        # Wait, our generate_keypair makes a NEW random key.
        # We need a way to derive DID from Private Key.
        # Ed25519: Private K -> Public K -> DID
        
        # We need a helper for this in crypto.py
        # For now, let's assume we can't easily re-derive without `nacl.signing.SigningKey(secret)`.
        # Let's add that helper or use inline logic if simple.
        
        # Inline re-derivation (using logic from crypto.py)
        import nacl.signing
        import nacl.encoding
        import multibase
        
        signing_key = nacl.signing.SigningKey(secret, encoder=nacl.encoding.HexEncoder)
        verify_key = signing_key.verify_key
        public_bytes = verify_key.encode(encoder=nacl.encoding.RawEncoder)
        
        # Reconstruct DID
        header = bytes([0xed, 0x01])
        pub_key_bytes_with_header = header + public_bytes
        multibase_key = multibase.encode('base58btc', pub_key_bytes_with_header).decode('utf-8')
        did = f"did:key:{multibase_key}"
        
        # 3. Restore to DB
        # Check if exists
        conn = get_db_connection()
        exists = conn.execute("SELECT 1 FROM users WHERE did = ?", (did,)).fetchone()
        
        if not exists:
            # Create new user entry with recovered ID
            short_id = did.split(":")[-1][-6:]
            username = f"Recovered_{short_id}"
            avatar = "files/avatar_placeholder.png"
            
            conn.execute("""
                INSERT INTO users (peer_id, username, handle, avatar, did, secret_key) 
                VALUES (?, ?, ?, ?, ?, ?)
            """, (did, username, f"@{username.lower()}", avatar, did, secret))
            conn.commit()
            msg = "Identity recovered and registered."
        else:
            # Update secret just in case (though it should match)
            conn.execute("UPDATE users SET secret_key = ? WHERE did = ?", (secret, did))
            conn.commit()
            msg = "Identity recovered. Welcome back."
            
        conn.close()
        
        return {
            "success": True,
            "message": msg,
            "did": did,
            "secret": secret
        }
        
    except Exception as e:
        print(f"Recovery error: {e}")
        raise HTTPException(status_code=400, detail="Recovery failed")

# --- Legacy Approval Endpoints (Optional/Deprecated) ---

@app.post("/api/recovery/request")
async def start_recovery(old_peer_id: str = Form(...), new_peer_id: str = Form(...)):
    """Initiate an account recovery request"""
    # Generate request ID
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    
    conn = get_db_connection()
    c = conn.cursor()
    c.execute("""
        INSERT INTO recovery_requests (old_peer_id, new_peer_id, timestamp, status)
        VALUES (?, ?, ?, 'pending')
    """, (old_peer_id, new_peer_id, timestamp))
    conn.commit()
    req_id = c.lastrowid
    conn.close()
    
    return {"success": True, "request_id": str(req_id)}

@app.post("/api/recovery/approve")
async def approve_recovery(request_id: str = Form(...), guardian_peer_id: str = Form(...)):
    """Guardian approves a recovery request"""
    conn = get_db_connection()
    c = conn.cursor()
    status = "pending"
    
    try:
        rid = int(request_id)
        # Check request exists
        exists = c.execute("SELECT 1 FROM recovery_requests WHERE id = ?", (rid,)).fetchone()
        if not exists:
            conn.close()
            raise HTTPException(status_code=404, detail="Recovery request not found")
            
        # Add approval
        # Check if already approved by this guardian
        has_approved = c.execute("SELECT 1 FROM recovery_approvals WHERE request_id = ? AND guardian_peer_id = ?", (rid, guardian_peer_id)).fetchone()
        if not has_approved:
            c.execute("INSERT INTO recovery_approvals (request_id, guardian_peer_id) VALUES (?, ?)", (rid, guardian_peer_id))
            conn.commit()
            
        # Check count
        count = c.execute("SELECT COUNT(*) FROM recovery_approvals WHERE request_id = ?", (rid,)).fetchone()[0]
        if count >= 3: # Threshold
             c.execute("UPDATE recovery_requests SET status = 'approved' WHERE id = ?", (rid,))
             conn.commit()
             status = "approved"
             
    except ValueError:
        conn.close()
        raise HTTPException(status_code=400, detail="Invalid request ID")
        
    conn.close()
    return {"success": True, "status": status}

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
