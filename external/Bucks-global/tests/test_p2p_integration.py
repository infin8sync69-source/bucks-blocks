import pytest
from unittest.mock import AsyncMock, patch, MagicMock
from fastapi.testclient import TestClient
import sys
import os

# Add backend to path
sys.path.append(os.path.join(os.path.dirname(__file__), "../backend"))

from main import app, verify_signature, get_my_peer_id

client = TestClient(app)

@pytest.fixture
def mock_p2p_client():
    with patch("main.p2p_client", new_callable=AsyncMock) as mock:
        mock.publish.return_value = True
        yield mock

@pytest.fixture
def mock_db_user():
    with patch("main.get_db_connection") as mock_conn:
        mock_cursor = MagicMock()
        mock_conn.return_value.execute.return_value = mock_cursor
        
        # Setup specific user return for authentication
        # User with a did and valid secret key
        # secret_key for Ed25519 (hex) - purely random example for test
        # valid 32-byte hex key
        secret = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef"
        mock_cursor.fetchone.return_value = {
            "secret_key": secret,
            "peer_id": "test_peer_id",
            "relationship_type": "contact"
        }
        yield mock_conn

def test_send_message_unauthorized(mock_p2p_client):
    response = client.post("/api/messages/send", data={"peer_id": "receiver", "text": "hello"})
    assert response.status_code == 401, f"Expected 401, got {response.status_code}. Response: {response.text}"

def test_send_message_authorized(mock_p2p_client, mock_db_user):
    # We need to mock sign_message as well since we use a fake secret
    with patch("main.sign_message", return_value="fake_sig"):
        response = client.post(
            "/api/messages/send", 
            data={"peer_id": "receiver123", "text": "Hello P2P"},
            headers={"X-DID": "did:key:zTest"}
        )
        
        assert response.status_code == 200, f"Expected 200, got {response.status_code}. Response: {response.text}"
        assert response.json()["success"] is True
        
        # Verify P2P publish was called
        mock_p2p_client.publish.assert_called_once()
        args = mock_p2p_client.publish.call_args
        topic = args[0][0]
        message = args[0][1]
        
        assert topic == "/app/inbox/receiver123"
        assert message["did"] == "did:key:zTest"
        assert message["signature"] == "fake_sig"
        assert "text" in message["payload"]
        assert "Hello P2P" in message["payload"]

# Need to test handle_inbox_message
# Since it's an async function not exposed via API directly (it's a callback), 
# we can import and test it if we refactored validly.
# Use 'from main import handle_inbox_message' inside test
from main import handle_inbox_message
import json
import asyncio

@pytest.mark.asyncio
async def test_handle_inbox_message(mock_db_user):
    # Mock verify_message to return True
    with patch("main.verify_message", return_value=True):
        
        payload = json.dumps({"text": "Incoming!", "timestamp": "2023-01-01"})
        data = {
            "did": "did:key:zSender",
            "signature": "sig",
            "payload": payload,
            "_from_peer_id": "sender_peer"
        }
        
        await handle_inbox_message(data)
        
        # Verify DB insert
        # mock_db_user is the connection mock factory
        conn = mock_db_user.return_value
        # Check that execute was called with INSERT into messages
        # We need to check call args
        calls = conn.execute.call_args_list
        
        # Look for the INSERT statement
        insert_found = False
        for call in calls:
            sql = call[0][0]
            if "INSERT INTO messages" in sql:
                insert_found = True
                params = call[0][1]
                assert params[0] == "sender_peer" # sender_peer_id
                assert params[2] == "Incoming!" # text
                break
        
        assert insert_found
