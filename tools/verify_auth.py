import json
import time
import jwt
import requests
import os
from cryptography.hazmat.primitives import serialization

# Load credentials
KEY_FILE_PATH = os.path.join(os.path.dirname(__file__), '../private/cdp_api_key.json')

try:
    with open(KEY_FILE_PATH, 'r') as f:
        key_data = json.load(f)
    
    KEY_NAME = key_data['name']
    PRIVATE_KEY = key_data['privateKey']
except Exception as e:
    print(f"Error loading key file: {e}")
    exit(1)

REQUEST_METHOD = "GET"
REQUEST_HOST = "api.cdp.coinbase.com"
REQUEST_PATH = "/platform/v2/evm/token-balances/base-sepolia/0x8fddcc0c5c993a1968b46787919cc34577d6dc5c"

def build_jwt():
    try:
        private_key_bytes = PRIVATE_KEY.encode('utf-8')
        private_key = serialization.load_pem_private_key(private_key_bytes, password=None)
    except Exception as e:
        print(f"❌ Error loading Key: {e}")
        return None

    payload = {
        "iss": "cdp", 
        "nbf": int(time.time()),
        "exp": int(time.time()) + 120,
        "sub": KEY_NAME,
        "uri": f"{REQUEST_METHOD} {REQUEST_HOST}{REQUEST_PATH}"
    }
    
    token = jwt.encode(
        payload,
        private_key,
        algorithm="ES256",
        headers={"kid": KEY_NAME, "nonce": str(int(time.time() * 1000000))}
    )
    return token

def send_request():
    token = build_jwt()
    if not token:
        return

    url = f"https://{REQUEST_HOST}{REQUEST_PATH}"
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
        "Accept": "application/json"
    }

    print(f"Sending {REQUEST_METHOD} request to {url}...")
    try:
        response = requests.request(REQUEST_METHOD, url, headers=headers)
        print(f"Status Code: {response.status_code}")
        print(f"Response Body: {response.text}")
    except Exception as e:
        print(f"Request failed: {e}")

if __name__ == "__main__":
    send_request()
