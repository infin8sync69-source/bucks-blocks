import asyncio
from asyncio import subprocess
import json
import logging
import binascii
from typing import Callable, Awaitable

class P2PClient:
    def __init__(self, ipfs_bin: str = "ipfs"):
        self.ipfs_bin = ipfs_bin
        self.subscriptions = {} # topic -> subprocess
        self.logger = logging.getLogger("P2PClient")

    async def publish(self, topic: str, message: dict) -> bool:
        """
        Publish a JSON message to a PubSub topic.
        """
        try:
            payload = json.dumps(message)
            process = await asyncio.create_subprocess_exec(
                self.ipfs_bin, "pubsub", "pub", topic,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            stdout, stderr = await process.communicate(input=payload.encode())
            
            if process.returncode != 0:
                self.logger.error(f"Publish failed: {stderr.decode()}")
                return False
            return True
        except Exception as e:
            self.logger.error(f"Publish error: {e}")
            return False

    async def subscribe(self, topic: str, callback: Callable[[dict], Awaitable[None]]):
        """
        Subscribe to a topic and trigger callback on new messages.
        Uses --enc=json to get 'from' and 'data' fields.
        """
        if topic in self.subscriptions:
            self.logger.warning(f"Already subscribed to {topic}")
            return

        self.logger.info(f"Subscribing to {topic}...")
        
        try:
            # Use --enc=json to get structured data
            process = await asyncio.create_subprocess_exec(
                self.ipfs_bin, "pubsub", "sub", topic, "--enc=json",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE
            )
            self.subscriptions[topic] = process
            
            # Start a background task to read stdout
            asyncio.create_task(self._read_stream(process, callback))
            
        except Exception as e:
             self.logger.error(f"Subscribe error: {e}")

    async def _read_stream(self, process, callback):
        """
        Continuously read lines from the PubSub process stdout.
        """
        import base64
        try:
            while True:
                line = await process.stdout.readline()
                if not line:
                    break
                
                try:
                    decoded_line = line.decode().strip()
                    if not decoded_line:
                        continue
                        
                    # Parse outer JSON (IPFS envelope)
                    # { "from": "...", "data": "base64...", ... }
                    envelope = json.loads(decoded_line)
                    
                    # Decode inner data
                    if "data" in envelope:
                        b64_data = envelope["data"]
                        # IPFS standard base64 might be URL safe or standard? 
                        # Usually standard.
                        try:
                            # Try standard b64 decode
                            # Handle padding just in case
                            missing_padding = len(b64_data) % 4
                            if missing_padding:
                                b64_data += '=' * (4 - missing_padding)
                            
                            raw_data = base64.b64decode(b64_data)
                            message_str = raw_data.decode("utf-8")
                            
                            # Parse inner JSON (Application payload)
                            payload = json.loads(message_str)
                            
                            # Inject 'from' peer_id for context if needed
                            payload["_from_peer_id"] = envelope.get("from")
                            
                            if asyncio.iscoroutinefunction(callback):
                                await callback(payload)
                            else:
                                callback(payload)
                                
                        except (binascii.Error, UnicodeDecodeError, json.JSONDecodeError) as e:
                            self.logger.debug(f"Failed to parse inner payload: {e}")
                            pass
                            
                except json.JSONDecodeError:
                    pass
                except Exception as e:
                    self.logger.error(f"Error processing message loop: {e}")
                    
        except Exception as e:
            self.logger.error(f"Stream reader crashed: {e}")
        finally:
            self.logger.info("Subscription stream ended")
            
    def close(self):
        for topic, process in self.subscriptions.items():
            try:
                process.terminate()
            except:
                pass
        self.subscriptions.clear()
