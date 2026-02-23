import sqlite3
import json
import os
from typing import List, Dict, Optional, Any

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "database.db")

def get_db_connection():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    conn = get_db_connection()
    c = conn.cursor()
    
    # Enable Foreign Keys
    c.execute("PRAGMA foreign_keys = ON;")

    # 1. Posts Table (Library)
    # 1. Posts Table (Library)
    c.execute("""
        CREATE TABLE IF NOT EXISTS posts (
            id TEXT PRIMARY KEY,
            name TEXT,
            description TEXT,
            filename TEXT,
            type TEXT,
            author TEXT,
            avatar TEXT,
            timestamp TEXT,
            peer_id TEXT,
            size INTEGER,
            is_pinned BOOLEAN DEFAULT 0,
            content TEXT,
            visibility TEXT,
            original_cid TEXT,
            tag TEXT
        );
    """)
    # Index for fast search
    c.execute("CREATE INDEX IF NOT EXISTS idx_posts_name ON posts(name);")
    c.execute("CREATE INDEX IF NOT EXISTS idx_posts_description ON posts(description);")

    # 2. Users Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS users (
            peer_id TEXT PRIMARY KEY,
            username TEXT,
            handle TEXT,
            avatar TEXT,
            banner TEXT,
            bio TEXT,
            location TEXT,
            did TEXT,
            secret_key TEXT,
            dag_root TEXT
        );
    """)

    # 3. Interactions Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS interactions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            post_cid TEXT,
            user_peer_id TEXT,
            type TEXT, -- 'like', 'view'
            timestamp TEXT,
            FOREIGN KEY(post_cid) REFERENCES posts(id)
        );
    """)

    # 4. Comments Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS comments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            post_cid TEXT,
            user_peer_id TEXT,
            username TEXT,
            text TEXT,
            timestamp TEXT,
            FOREIGN KEY(post_cid) REFERENCES posts(id)
        );
    """)
    
    # 5. Messages Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender_peer_id TEXT,
            receiver_peer_id TEXT,
            text TEXT,
            timestamp TEXT,
            cid TEXT,
            is_read BOOLEAN DEFAULT 0
        );
    """)
    # Migrate messages table: add filename and mime_type if not present
    for col, coltype in [("filename", "TEXT"), ("mime_type", "TEXT")]:
        try:
            c.execute(f"ALTER TABLE messages ADD COLUMN {col} {coltype}")
        except Exception:
            pass  # Column already exists

    # 6. Guardians Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS guardians (
            user_peer_id TEXT,
            guardian_peer_id TEXT,
            PRIMARY KEY (user_peer_id, guardian_peer_id)
        );
    """)

    # 7. Recovery Requests Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS recovery_requests (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            old_peer_id TEXT,
            new_peer_id TEXT,
            timestamp TEXT,
            status TEXT DEFAULT 'pending'
        );
    """)

    # 8. Recovery Approvals Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS recovery_approvals (
            request_id INTEGER,
            guardian_peer_id TEXT,
            PRIMARY KEY (request_id, guardian_peer_id)
        );
    """)

    # 9. Following Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS following (
            user_peer_id TEXT,
            following_peer_id TEXT,
            relationship_type TEXT DEFAULT 'following',
            timestamp TEXT,
            library_cid TEXT,
            vouched_cid TEXT,
            last_synced TEXT,
            username TEXT,
            PRIMARY KEY (user_peer_id, following_peer_id)
        );
    """)

    # 10. Notifications Table
    c.execute("""
        CREATE TABLE IF NOT EXISTS notifications (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_peer_id TEXT,
            type TEXT, -- 'message', 'connection', 'mention', 'follow'
            title TEXT,
            message TEXT,
            link TEXT,
            timestamp TEXT,
            is_read BOOLEAN DEFAULT 0
        );
    """)
    # 11. Discovered Peers Table (Global Index)
    c.execute("""
        CREATE TABLE IF NOT EXISTS discovered_peers (
            peer_id TEXT PRIMARY KEY,
            username TEXT,
            avatar TEXT,
            dag_root TEXT,
            last_seen TEXT,
            discovery_type TEXT -- 'pubsub', 'sync', 'manual'
        );
    """)
    
    # 9.1 Migrations for Following Table (Add columns if missing)
    # Check if columns exist
    try:
        c.execute("SELECT library_cid FROM following LIMIT 1")
    except sqlite3.OperationalError:
        print("Migrating following table...")
        c.execute("ALTER TABLE following ADD COLUMN library_cid TEXT")
        c.execute("ALTER TABLE following ADD COLUMN vouched_cid TEXT")
        c.execute("ALTER TABLE following ADD COLUMN last_synced TEXT")
        c.execute("ALTER TABLE following ADD COLUMN username TEXT")
        
    # 9.2 Migration for Users Table (Add secret_key)
    try:
        c.execute("SELECT secret_key FROM users LIMIT 1")
    except sqlite3.OperationalError:
        print("Migrating users table...")
        c.execute("ALTER TABLE users ADD COLUMN secret_key TEXT")
        
    # 9.4 Migration for Users Table (Add dag_root)
    try:
        c.execute("SELECT dag_root FROM users LIMIT 1")
    except sqlite3.OperationalError:
        print("Migrating users table (adding dag_root)...")
        c.execute("ALTER TABLE users ADD COLUMN dag_root TEXT")
        
    # 9.3 Migration for Posts Table (Encryption) - REMOVED

    conn.commit()
    conn.close()
    print(f"✅ Database initialized at {DB_PATH}")

if __name__ == "__main__":
    init_db()
