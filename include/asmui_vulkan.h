#ifndef ASMUI_VULKAN_H
#define ASMUI_VULKAN_H

#include "asmui.h"
#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ASMUI_VK_MAX_TEXTURES 512u
#define ASMUI_VK_MAX_RENDER_TARGETS 24u


typedef struct AsmUiVkRenderTarget {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t width;
    uint32_t height;
    VkFormat format;
    VkImageLayout layout;
    uint32_t in_use;
    uint32_t generation;
} AsmUiVkRenderTarget;

typedef struct AsmUiVkTargetPool {
    VkPhysicalDevice physical_device;
    VkDevice device;
    AsmUiVkRenderTarget targets[ASMUI_VK_MAX_RENDER_TARGETS];
    uint32_t created_count;
    uint32_t reuse_count;
} AsmUiVkTargetPool;

typedef struct AsmUiVkPush {
    float viewport_w;
    float viewport_h;
    uint32_t material_id;
    uint32_t flags;
    float mask_x, mask_y, mask_w, mask_h;
    float mask_radius, mask_softness;
    uint32_t mask_flags;
    uint32_t pad0;
} AsmUiVkPush;

typedef struct AsmUiVkBackend {
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t max_quads;
    uint32_t stream_frames;
    uint32_t active_stream_slot;
    VkDeviceSize vertex_segment_bytes;
    VkDeviceSize non_coherent_atom_size;
    VkDeviceSize vertex_allocation_size;

    VkBuffer vertex_buffer;
    VkDeviceMemory vertex_memory;
    void *vertex_mapped;
    VkMemoryPropertyFlags vertex_memory_flags;

    VkBuffer index_buffer;
    VkDeviceMemory index_memory;
    VkMemoryPropertyFlags index_memory_flags;

    VkDescriptorSetLayout texture_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline_alpha;
    VkPipeline pipeline_additive;
    VkPipeline pipeline_multiply;

    /* v0.5 optional nested stencil compositor. If stencil_format is not
       VK_FORMAT_UNDEFINED the caller must provide a cleared stencil attachment. */
    VkFormat stencil_format;
    VkPipeline pipeline_stencil_push;
    VkPipeline pipeline_stencil_pop;

    VkDescriptorSet white_texture_set;
    VkDescriptorSet texture_sets[ASMUI_VK_MAX_TEXTURES];
} AsmUiVkBackend;


/* v0.6 reusable offscreen render-target pool. Images are device-local and
 * created for COLOR_ATTACHMENT | SAMPLED | TRANSFER_SRC | TRANSFER_DST.
 * begin_frame marks all targets reusable; acquire reuses an exact size/format
 * match before allocating another slot. */
void asmui_vk_target_pool_init(AsmUiVkTargetPool *pool,
                               VkPhysicalDevice physical_device,
                               VkDevice device);
void asmui_vk_target_pool_begin_frame(AsmUiVkTargetPool *pool);
AsmUiVkRenderTarget *asmui_vk_target_acquire(AsmUiVkTargetPool *pool,
                                             uint32_t width,
                                             uint32_t height,
                                             VkFormat format);
void asmui_vk_target_release(AsmUiVkTargetPool *pool, AsmUiVkRenderTarget *target);
void asmui_vk_target_pool_destroy(AsmUiVkTargetPool *pool);

/* Creates host-visible persistent vertex/index streaming buffers.
 * This path is intentionally friendly to UMA iGPUs such as Intel Raptor Lake-P.
 */
int asmui_vk_init_streaming(AsmUiVkBackend *backend,
                            VkPhysicalDevice physical_device,
                            VkDevice device,
                            uint32_t max_quads);

/* v0.11 ring-stream variant for multiple frames in flight. The vertex buffer
 * is split into stream_frames equal segments; the static index buffer is shared. */
int asmui_vk_init_streaming_ex(AsmUiVkBackend *backend,
                               VkPhysicalDevice physical_device,
                               VkDevice device,
                               uint32_t max_quads,
                               uint32_t stream_frames);
void asmui_vk_destroy_streaming(AsmUiVkBackend *backend);

/* Creates descriptor layout, pipeline layout, and alpha/additive/multiply
 * graphics pipelines using Vulkan 1.3 dynamic rendering.
 */
int asmui_vk_create_pipeline_pack(AsmUiVkBackend *backend,
                                  VkFormat color_format,
                                  const uint32_t *vert_spv, size_t vert_spv_bytes,
                                  const uint32_t *frag_spv, size_t frag_spv_bytes);

/* Stencil-enabled variant. The stencil format should normally be VK_FORMAT_S8_UINT.
   Content compares stencil == nesting depth; mask push/pop use increment/decrement. */
int asmui_vk_create_pipeline_pack_ex(AsmUiVkBackend *backend,
                                     VkFormat color_format,
                                     VkFormat stencil_format,
                                     const uint32_t *vert_spv, size_t vert_spv_bytes,
                                     const uint32_t *frag_spv, size_t frag_spv_bytes);
void asmui_vk_destroy_pipeline_pack(AsmUiVkBackend *backend);

/* The descriptor pool remains owned by the caller. The set is stored in the
 * backend resource table after allocation/update.
 */
int asmui_vk_alloc_texture_set(AsmUiVkBackend *backend,
                               VkDescriptorPool pool,
                               uint32_t resource_id,
                               VkImageView image_view,
                               VkSampler sampler,
                               VkImageLayout image_layout);
int asmui_vk_update_texture_set(AsmUiVkBackend *backend,
                                uint32_t resource_id,
                                VkImageView image_view,
                                VkSampler sampler,
                                VkImageLayout image_layout);
void asmui_vk_set_white_texture_set(AsmUiVkBackend *backend, VkDescriptorSet set);

/* Packs ctx->draws plus one stable geometry quad per node into the persistent
 * stream using ASM. The return value remains the visual-draw vertex count. */
uint32_t asmui_vk_upload(const AsmUiContext *ctx, AsmUiVkBackend *backend);

/* Upload into one ring segment and make it active for subsequent record calls. */
uint32_t asmui_vk_upload_slot(const AsmUiContext *ctx,
                              AsmUiVkBackend *backend,
                              uint32_t stream_slot);

/* Record image and expanded-glyph batches. If the pipeline pack was created with
 * a stencil format, this also replays nested stencil push/pop operations in painter
 * order. The caller must already be inside compatible dynamic rendering and must
 * clear the stencil attachment to zero before the call.
 */
void asmui_vk_record_images(VkCommandBuffer cmd,
                            const AsmUiContext *ctx,
                            const AsmUiBatch *batches,
                            uint32_t batch_count,
                            AsmUiVkBackend *backend,
                            uint32_t framebuffer_w,
                            uint32_t framebuffer_h);

#ifdef __cplusplus
}
#endif
#endif
