#ifndef ASMUI_VULKAN_TEXTURE_H
#define ASMUI_VULKAN_TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AsmUiVkTexture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    uint32_t width;
    uint32_t height;
    VkFormat format;
} AsmUiVkTexture;

typedef struct AsmUiVkTextureUpload {
    VkDevice device;
    VkCommandPool command_pool;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    VkCommandBuffer command_buffer;
    VkFence fence;
    AsmUiVkTexture texture;
    int submitted;
} AsmUiVkTextureUpload;

/* Startup/blocking upload retained for fonts and one-shot assets. */
int asmui_vk_texture_upload_rgba8(VkPhysicalDevice physical,
                                  VkDevice device,
                                  VkQueue queue,
                                  VkCommandPool command_pool,
                                  const uint8_t *rgba,
                                  uint32_t width,
                                  uint32_t height,
                                  AsmUiVkTexture *out);

/* Non-blocking GPU upload. begin() copies pixels into a staging allocation and
 * submits transfer work without vkQueueWaitIdle. poll() returns 0 while the
 * fence is unsignaled, 1 on completion and transfers texture ownership to out,
 * or -1 on a Vulkan error. shutdown() is safe for pending jobs and waits only
 * for that upload fence, never the whole graphics queue. */
int asmui_vk_texture_upload_rgba8_async_begin(VkPhysicalDevice physical,
                                              VkDevice device,
                                              VkQueue queue,
                                              VkCommandPool command_pool,
                                              const uint8_t *rgba,
                                              uint32_t width,
                                              uint32_t height,
                                              AsmUiVkTextureUpload *upload);
int asmui_vk_texture_upload_async_poll(AsmUiVkTextureUpload *upload,
                                       AsmUiVkTexture *out);
void asmui_vk_texture_upload_async_shutdown(AsmUiVkTextureUpload *upload);

void asmui_vk_texture_destroy(VkDevice device, AsmUiVkTexture *texture);

#ifdef __cplusplus
}
#endif
#endif
