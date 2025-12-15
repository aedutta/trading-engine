#!/bin/bash
set -e

echo "========================================"
echo "  AWS Network Setup for DPDK"
echo "========================================"

# Check for root
if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root (sudo ./scripts/setup_aws_network.sh)"
  exit 1
fi

# 1. Enable Hugepages
echo "[1/4] Enabling Hugepages..."
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
grep Huge /proc/meminfo

# 2. Load VFIO-PCI Driver
echo "[2/4] Loading VFIO-PCI Driver..."
modprobe vfio-pci
echo "vfio-pci loaded."

# 3. Identify Secondary Interface
# We assume eth0 is management, and the next available interface is for DPDK.
# In AWS, this is usually eth1 if attached.
DPDK_IFACE="eth1"

if ! ip link show "$DPDK_IFACE" > /dev/null 2>&1; then
    echo "Error: Interface $DPDK_IFACE not found. Please attach a second ENI to this instance."
    exit 1
fi

echo "[3/4] Preparing Interface $DPDK_IFACE..."
ip link set "$DPDK_IFACE" down

# 4. Bind to VFIO-PCI
echo "[4/4] Binding $DPDK_IFACE to vfio-pci..."

# Get PCI Address
PCI_ADDR=$(ethtool -i "$DPDK_IFACE" | grep bus-info | awk '{print $2}')
echo "Found PCI Address: $PCI_ADDR"

if [ -z "$PCI_ADDR" ]; then
    echo "Error: Could not determine PCI address for $DPDK_IFACE"
    exit 1
fi

# Use dpdk-devbind.py if available, otherwise manual bind
if command -v dpdk-devbind.py &> /dev/null; then
    dpdk-devbind.py --bind=vfio-pci "$PCI_ADDR"
else
    # Manual bind
    echo "dpdk-devbind.py not found, attempting manual bind..."
    echo "vfio-pci" > /sys/bus/pci/devices/"$PCI_ADDR"/driver_override
    echo "$PCI_ADDR" > /sys/bus/pci/drivers/vfio-pci/bind
fi

echo "========================================"
echo "  Setup Complete."
echo "  $DPDK_IFACE ($PCI_ADDR) is ready for DPDK."
echo "========================================"
