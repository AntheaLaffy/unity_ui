#ifndef ASMUI_POST_VULKAN_H
#define ASMUI_POST_VULKAN_H

#include <vulkan/vulkan.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASMUI_POST_COPY = 0,
    ASMUI_POST_BLUR = 1,
    ASMUI_POST_EXTRACT = 2,
};

typedef struct AsmUiVkPostPush {
    float texel_x;
    float texel_y;
    float radius;
    float strength;
    uint32_t mode;
    uint32_t pad0;
    uint32_t pad1;
    uint32_t pad2;
} AsmUiVkPostPush;

typedef struct AsmUiVkPostBackend {
    VkDevice device;
    VkDescriptorSetLayout texture_set_layout;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkPipeline additive_pipeline;
} AsmUiVkPostBackend;

int asmui_vk_post_init(AsmUiVkPostBackend *post,
                       VkDevice device,
                       VkFormat color_format,
                       const uint32_t *vert_spv, size_t vert_spv_bytes,
                       const uint32_t *frag_spv, size_t frag_spv_bytes);
void asmui_vk_post_destroy(AsmUiVkPostBackend *post);

int asmui_vk_post_alloc_texture_set(AsmUiVkPostBackend *post,
                                    VkDescriptorPool pool,
                                    VkImageView image_view,
                                    VkSampler sampler,
                                    VkImageLayout layout,
                                    VkDescriptorSet *out_set);

void asmui_vk_post_record(VkCommandBuffer cmd,
                          const AsmUiVkPostBackend *post,
                          VkDescriptorSet texture_set,
                          uint32_t framebuffer_w,
                          uint32_t framebuffer_h,
                          uint32_t mode,
                          float texel_x,
                          float texel_y,
                          float radius,
                          float strength);

/* Same shader, but using additive RGB/alpha blending. Intended for glow. */
void asmui_vk_post_record_additive(VkCommandBuffer cmd,
                                   const AsmUiVkPostBackend *post,
                                   VkDescriptorSet texture_set,
                                   uint32_t framebuffer_w,
                                   uint32_t framebuffer_h,
                                   uint32_t mode,
                                   float texel_x,
                                   float texel_y,
                                   float radius,
                                   float strength);

#ifdef __cplusplus
}
#endif
#endif
