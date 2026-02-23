import json
import os
import sqlite3
from database import init_db, get_db_connection, DB_PATH

# main.py defines BASE_DIR as os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# assuming main.py is in backend/, migrate.py is also in backend/
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def load_json(filename, default=None):
    path = os.path.join(BASE_DIR, filename)
    if not os.path.exists(path):
        print(f"⚠️ File not found: {path}")
        return default
    try:
        with open(path, 'r') as f:
            return json.load(f)
    except Exception as e:
        print(f"❌ Error loading {filename}: {e}")
        return default

def migrate():
    print("🔄 Starting Migration...")
    
    # 1. Initialize DB
    init_db()
    conn = get_db_connection()
    c = conn.cursor()

    # Load User Profile first to get my_peer_id
    profile = load_json("user_profile.json", {})
    my_peer_id = profile.get("peer_id", "local_user")
    
    # 2. Migrate User Profile
    if profile:
        try:
            c.execute("""
                INSERT OR IGNORE INTO users
                (peer_id, username, handle, avatar, banner, bio, location)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            """, (
                my_peer_id,
                profile.get("username"),
                profile.get("handle"),
                profile.get("avatar"),
                profile.get("banner"),
                profile.get("bio"),
                profile.get("location")
            ))
            print("👤 Migrated User Profile")
        except Exception as e:
             print(f"❌ Error migrating profile: {e}")

    # 3. Migrate Posts (library.json)
    library = load_json("library.json", [])
    print(f"📦 Found {len(library)} posts in library.json")
    
    count_posts = 0
    for item in library:
        try:
            # Schema: id, author, content, timestamp, visibility, original_cid
            # JSON: cid, author, description, timestamp, visibility
            c.execute("""
                INSERT OR IGNORE INTO posts 
                (id, author, content, timestamp, visibility, original_cid)
                VALUES (?, ?, ?, ?, ?, ?)
            """, (
                item.get("cid"),
                item.get("author", "Unknown"),
                item.get("description", item.get("name", "")), # Fallback to name if description missing
                item.get("timestamp"),
                item.get("visibility", "public"),
                None # original_cid
            ))
            count_posts += 1
        except Exception as e:
            print(f"❌ Error migrating post {item.get('name')}: {e}")
    print(f"✅ Migrated {count_posts} posts")

    # 4. Migrate Following (following.json)
    following = load_json("following.json", [])
    print(f"👥 Found {len(following)} followed peers")
    
    count_following = 0
    for item in following:
        try:
            # Schema: user_peer_id, following_peer_id, relationship_type, timestamp, library_cid, vouched_cid, last_synced, username
            c.execute("""
                INSERT OR IGNORE INTO following 
                (user_peer_id, following_peer_id, relationship_type, timestamp, library_cid, vouched_cid, last_synced, username)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                my_peer_id,
                item.get("peer_id"),
                item.get("relationship_type", "following"),
                item.get("followed_at", item.get("last_synced", "")), # Use followed_at or legacy
                item.get("library_cid"),
                item.get("vouched_cid"),
                item.get("last_synced"),
                item.get("username")
            ))
            count_following += 1
        except Exception as e:
            print(f"❌ Error migrating following {item.get('peer_id')}: {e}")
    print(f"✅ Migrated {count_following} connections")

    # 5. Migrate Vouched (vouched.json) -> Interactions
    vouched = load_json("vouched.json", [])
    print(f"👍 Found {len(vouched)} vouched posts")
    
    count_vouched = 0
    for cid in vouched:
        try:
            timestamp = "2024-01-01T00:00:00Z" # Dummy timestamp for migrated likes
            c.execute("""
                INSERT OR IGNORE INTO interactions 
                (user_peer_id, post_cid, type, timestamp)
                VALUES (?, ?, ?, ?)
            """, (
                my_peer_id,
                cid,
                "like",
                timestamp
            ))
            count_vouched += 1
        except Exception as e:
            print(f"❌ Error migrating vouched {cid}: {e}")
    print(f"✅ Migrated {count_vouched} likes")

    conn.commit()
    conn.close()
    print("✅ Migration Complete!")

if __name__ == "__main__":
    migrate()
