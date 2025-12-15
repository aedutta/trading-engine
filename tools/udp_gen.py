import socket
import struct
import time
import argparse

def send_traffic(ip, port, rate, duration):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # SbeHeader: block_length(H), template_id(H), schema_id(H), version(H)
    # template_id = 202
    # block_length = 0 (for simplicity in this demo)
    sbe_header = struct.pack('<HHHH', 0, 202, 1, 1)
    
    # MDEntry: price_mantissa(q), order_qty(I), price_exponent(b), side(B)
    # price = 50000 * 10^-2 = 500.00
    md_entry = struct.pack('<qIbB', 5000000, 100, -2, 1)
    
    packet = sbe_header + md_entry
    
    print(f"Sending traffic to {ip}:{port} for {duration} seconds at {rate} pps...")
    
    start_time = time.time()
    count = 0
    delay = 1.0 / rate
    
    while time.time() - start_time < duration:
        sock.sendto(packet, (ip, port))
        count += 1
        time.sleep(delay)
        
    print(f"Sent {count} packets.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate UDP traffic for HFT engine benchmark')
    parser.add_argument('--ip', type=str, required=True, help='Target IP address')
    parser.add_argument('--port', type=int, default=1234, help='Target port')
    parser.add_argument('--rate', type=int, default=1000, help='Packets per second')
    parser.add_argument('--duration', type=int, default=10, help='Duration in seconds')
    
    args = parser.parse_args()
    
    send_traffic(args.ip, args.port, args.rate, args.duration)
