import json
import base64
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.backends import default_backend

def convert_key():
    try:
        with open('private/cdp_api_key.json', 'r') as f:
            data = json.load(f)
        
        raw_key_b64 = data['privateKey']
        raw_bytes = base64.b64decode(raw_key_b64)
        
        # Assume first 32 bytes are the private key
        if len(raw_bytes) < 32:
            print("Error: Key too short")
            return

        private_value = int.from_bytes(raw_bytes[:32], byteorder='big')
        
        private_key = ec.derive_private_key(private_value, ec.SECP256R1(), default_backend())
        
        pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption()
        )
        
        print(pem.decode('utf-8'))
        
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    convert_key()
