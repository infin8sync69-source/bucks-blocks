"""
Reset the database completely (drop all tables) and reinitialize.
Run this script from the project root for a fresh start.
Usage: python scripts/reset_db.py
"""
import sys
import os
import sqlite3

# Add backend to path so we can use init_db
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'backend'))

DB_PATH = os.path.join(os.path.dirname(__file__), '..', 'backend', 'database.db')

TABLES = [
    "notifications",
    "recovery_approvals",
    "recovery_requests",
    "guardians",
    "messages",
    "comments",
    "interactions",
    "following",
    "users",
    "posts",
]

def reset_db(full: bool = True):
    """Drop and recreate the entire database."""
    if not os.path.exists(DB_PATH):
        print(f"Database not found at {DB_PATH} — will be created fresh on backend start.")
        return

    print(f"Resetting database at {DB_PATH}...")
    try:
        conn = sqlite3.connect(DB_PATH)
        c = conn.cursor()
        c.execute("PRAGMA foreign_keys = OFF;")   # disable FK checks during drop

        if full:
            # Drop all tables
            for table in TABLES:
                c.execute(f"DROP TABLE IF EXISTS {table}")
                print(f"  Dropped {table}")
        else:
            # Only clear user data, keep schema
            for table in TABLES:
                c.execute(f"DELETE FROM {table}")
                print(f"  Cleared {table}")

        conn.commit()
        conn.close()
        print("Reset complete. Restart the backend to reinitialize the schema.")

    except Exception as e:
        print(f"Error resetting DB: {e}")

if __name__ == "__main__":
    mode = "--clear" if "--clear" in sys.argv else "--full"
    auto_yes = "--yes" in sys.argv or "-y" in sys.argv
    print(f"Mode: {'Clear data only' if mode == '--clear' else 'Full schema drop'}")
    if auto_yes:
        confirm = 'y'
    else:
        confirm = input("This will DELETE ALL DATA. Are you sure? (y/N): ")
    if confirm.lower() == 'y':
        reset_db(full=(mode == "--full"))
    else:
        print("Cancelled.")
