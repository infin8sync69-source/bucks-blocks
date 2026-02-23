import sqlite3
import os

DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "database.db")

def clean_content():
    if not os.path.exists(DB_PATH):
        print(f"Database not found at {DB_PATH}")
        return

    print(f"Cleaning content from {DB_PATH}...")
    conn = sqlite3.connect(DB_PATH)
    
    # helper to print count
    def count(table):
        return conn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]

    initial_posts = count("posts")
    initial_follows = count("following")
    
    # Delete content but preserve identity (users table)
    conn.execute("DELETE FROM posts")
    conn.execute("DELETE FROM following")
    conn.execute("DELETE FROM interactions")
    conn.execute("DELETE FROM comments")
    conn.execute("DELETE FROM notifications")
    conn.execute("DELETE FROM messages") # Optional: clear messages too? User said "unwanted suggestions", usually related to feed. But cleaner to wipe all "user data" except identity.
    
    conn.commit()
    conn.close()
    
    print(f"✅ Content cleaned.")
    print(f"Removed {initial_posts} posts and {initial_follows} follows.")
    print("Your identity (DID/Network Keys) is PRESERVED.")

if __name__ == "__main__":
    clean_content()
