#!/bin/bash
set -e

echo "========================================"
echo "  HFT Engine System Setup (Amazon Linux 2023)"
echo "========================================"

if [ "$EUID" -ne 0 ]; then 
  echo "Please run as root (sudo ./scripts/setup_amazon_linux.sh)"
  exit 1
fi

# 1. Install Dependencies
echo "[1/3] Installing Dependencies..."
dnf update -y
dnf groupinstall "Development Tools" -y
dnf install -y cmake git ethtool
dnf install -y dpdk dpdk-devel dpdk-tools

# 2. Configure Kernel Tuning (GRUB)
echo "[2/3] Configuring Kernel Parameters..."
# We need: isolcpus=1 default_hugepagesz=2M hugepagesz=2M hugepages=1024 iommu=pt intel_iommu=on
# Amazon Linux uses grubby for this
grubby --update-kernel=ALL --args="default_hugepagesz=2M hugepagesz=2M hugepages=1024 isolcpus=1 rcu_nocbs=1 nohz_full=1 intel_iommu=on iommu=pt"

# 3. Disable IRQ Balance
echo "[3/3] Disabling IRQ Balance..."
systemctl stop irqbalance 2>/dev/null || true
systemctl disable irqbalance 2>/dev/null || true

# 4. Network Tuning (Low Latency)
echo "[4/4] Tuning Network Latency..."
# Disable interrupt coalescing on primary interface (eth0)
ethtool -C eth0 rx-usecs 0 tx-usecs 0 2>/dev/null || echo "Warning: Could not tune eth0 coalescing"

# Persist Sysctl Settings
cat <<EOF > /etc/sysctl.d/99-hft-tuning.conf
net.core.busy_read=50
net.core.busy_poll=50
net.core.rmem_max=16777216
net.core.wmem_max=16777216
EOF
sysctl -p /etc/sysctl.d/99-hft-tuning.conf

echo "========================================"
echo "  System Setup Complete."
echo "  PLEASE REBOOT YOUR INSTANCE NOW."
echo "  (sudo reboot)"
echo "========================================"
