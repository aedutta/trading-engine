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
echo "[1/5] Enabling Hugepages..."
echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
grep Huge /proc/meminfo | grep Total

# 2. Load VFIO-PCI Driver
echo "[2/5] Loading VFIO-PCI Driver..."
modprobe vfio-pci
echo "vfio-pci loaded."

# 2.1 FIX: Enable Unsafe No-IOMMU Mode (Required for AWS EC2)
echo "[2.1/5] Enabling VFIO No-IOMMU Mode..."
echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode
cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode

# 3. Identify Secondary Interface
# Automatically detect the interface that is NOT the default management interface
MGMT_IFACE=$(ip route show default | awk '/default/ {print $5}' | head -n 1)
DPDK_IFACE=$(ip -o link show | awk -F': ' '{print $2}' | grep -v "lo" | grep -v "$MGMT_IFACE" | head -n 1)

echo "Management Interface: $MGMT_IFACE"
echo "DPDK Interface:       $DPDK_IFACE"

if [ -z "$DPDK_IFACE" ]; then
    echo "Error: No secondary interface found. Please attach a second ENI to this instance."
    exit 1
fi

# 4. Bind to VFIO-PCI (Manual, Robust Method)
echo "[4/5] Binding $DPDK_IFACE to vfio-pci..."

# Get PCI Address (e.g., 0000:28:00.0)
PCI_ADDR=$(ethtool -i "$DPDK_IFACE" | grep bus-info | awk '{print $2}')
echo "Found PCI Address: $PCI_ADDR"

if [ -z "$PCI_ADDR" ]; then
    echo "Error: Could not determine PCI address for $DPDK_IFACE"
    exit 1
fi

# Bring interface down
ip link set "$DPDK_IFACE" down

# Unbind from current driver (usually 'ena') if attached
if [ -e /sys/bus/pci/devices/"$PCI_ADDR"/driver ]; then
    echo "Unbinding from current driver..."
    echo "$PCI_ADDR" > /sys/bus/pci/devices/"$PCI_ADDR"/driver/unbind
fi

# Force driver override to vfio-pci
echo "vfio-pci" > /sys/bus/pci/devices/"$PCI_ADDR"/driver_override

# Bind to vfio-pci
echo "$PCI_ADDR" > /sys/bus/pci/drivers/vfio-pci/bind

echo "========================================"
echo "  Setup Complete."
echo "  $DPDK_IFACE ($PCI_ADDR) is successfully bound to VFIO-PCI."
echo "========================================"