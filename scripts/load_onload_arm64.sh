#!/bin/bash
# Helper script to load onload on ARM64 kernel 6.8+ with syscall table address
set -e

echo "ARM64 Onload Loader for Kernel 6.8+"
echo "===================================="

# Get the syscall table address from /proc/kallsyms
SYSCALL_ADDR=$(grep " sys_call_table" /proc/kallsyms | awk '{print "0x" $1}')

if [ -z "$SYSCALL_ADDR" ]; then
    echo "ERROR: Could not find sys_call_table in /proc/kallsyms"
    echo "Make sure CONFIG_KALLSYMS is enabled and you have root privileges"
    exit 1
fi

echo "Found sys_call_table at: $SYSCALL_ADDR"

# Pass the hex address directly as the parameter (kernel modules accept hex)
SYSCALL_PARAM=$SYSCALL_ADDR

echo "Loading sfc driver..."
modprobe sfc

echo "Loading sfc_resource with syscall_table_addr=$SYSCALL_PARAM..."
insmod /root/onload/onload/build/aarch64_linux-6.8.0-60-generic/src/driver/linux_resource/sfc_resource.ko syscall_table_addr=$SYSCALL_PARAM

echo "Loading sfc_char..."
insmod /root/onload/onload/build/aarch64_linux-6.8.0-60-generic/src/driver/linux_char/sfc_char.ko

echo "Loading onload..."
insmod /root/onload/onload/build/aarch64_linux-6.8.0-60-generic/src/driver/linux_onload/onload.ko

echo "SUCCESS: Onload loaded successfully!"
echo ""
echo "Checking kernel logs..."
dmesg | tail -5

echo ""
echo "Usage: To reload, run:"
echo "  sudo rmmod onload sfc_char sfc_resource sfc"
echo "  sudo $0"