# ASMUI v0.11 live pipeline

v0.11 turns the live renderer from a single-frame serialized loop into a small
frames-in-flight pipeline (default 3, configurable with `ASMUI_FRAMES_IN_FLIGHT=1..3`).

Each frame slot owns:
- command buffer
- acquire semaphore
- fence
- timestamp query pool
- full-resolution source RT
- three half-resolution post-process RTs
- descriptor sets for those RTs

Render-finished semaphores are owned per **swapchain image**, not per CPU frame.
This follows the WSI lifetime more closely: reacquiring an image guarantees the
previous presentation wait for that image has completed before its semaphore is
reused.

The dynamic ASMUI vertex stream is one persistently-mapped Vulkan buffer split
into equal frame segments. `asmui_vk_upload_slot()` writes only the current
segment; `asmui_vk_record_images()` binds that segment as the vertex-buffer base.
On non-coherent memory only the active segment is flushed (atom-size aligned).

The live benchmark reports separate CPU distributions for:
- frame-slot fence wait
- swapchain image acquire
- UI update + command encoding
- queue submit + present call
- total CPU loop

This makes FIFO/MAILBOX stalls visible instead of hiding them inside one number.
