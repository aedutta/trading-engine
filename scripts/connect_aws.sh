#!/bin/bash
KEY_FILE="private/new-key.pem"
IP="3.236.194.30"
USER="ec2-user"

if [ ! -f "$KEY_FILE" ]; then
    echo "Error: Key file $KEY_FILE not found!"
    exit 1
fi

# Ensure key permissions are correct (AWS requirement)
chmod 400 "$KEY_FILE"

echo "Connecting to AWS Instance ($IP)..."
ssh -i "$KEY_FILE" $USER@$IP