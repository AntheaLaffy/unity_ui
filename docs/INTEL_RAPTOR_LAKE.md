# Intel Raptor Lake-P / i915 Vulkan path

The reference target used while designing ASMUI v0.4 is an Intel Raptor Lake-P integrated GPU on Linux with `i915` bound as the kernel driver.

ASMUI does **not** talk to `i915` directly. The Vulkan path is:

```text
ASM UI core -> Vulkan API -> Mesa ANV / vulkan-intel -> DRM i915 -> Intel iGPU
```

`xe` may also be present as a kernel module on current kernels. ASMUI does not require switching to it; the active driver reported by `lspci -k` is the one that matters for the current session.

## Why the v0.4 upload path is UMA-friendly

For UI geometry the amount of vertex data is small and regenerated every frame. On an integrated GPU, a persistently mapped HOST_VISIBLE allocation avoids blindly copying dynamic vertices into a second DEVICE_LOCAL buffer each frame. The allocator prefers memory that is both HOST_COHERENT and DEVICE_LOCAL when Vulkan exposes such a memory type, but requires only HOST_VISIBLE and correctly flushes non-coherent memory.

The design is deliberately conservative:

- no vendor-specific extension is required;
- no descriptor indexing is required;
- one texture descriptor set is bound per atlas/resource batch;
- draw order is never globally sorted;
- only adjacent commands with identical state are merged;
- Vulkan 1.3 dynamic rendering is used for the pipeline pack;
- vertex/index buffers are persistent rather than created per frame.

## Arch setup / probe

```bash
sudo pacman -S vulkan-intel vulkan-tools vulkan-headers shaderc
./tools/probe_intel_vulkan.sh
```

Compile shaders:

```bash
make shaders
```

The important checks in `vulkaninfo --summary` are that the Intel physical device is visible and that the driver/API versions are sane. Do not infer Vulkan availability solely from `lspci`; `lspci` shows the kernel driver, while Vulkan also needs the user-space ICD.
