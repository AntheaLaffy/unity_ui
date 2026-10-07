#!/usr/bin/env bash
set -euo pipefail

echo '== PCI GPU =='
lspci -k | grep -EA3 'VGA|3D|Display' || true

echo
echo '== Vulkan summary =='
if command -v vulkaninfo >/dev/null 2>&1; then
    vulkaninfo --summary
else
    echo 'vulkaninfo not found. On Arch: sudo pacman -S vulkan-intel vulkan-tools'
fi

echo
echo '== Intel Vulkan ICD files =='
ls -1 /usr/share/vulkan/icd.d/*intel* 2>/dev/null || true
