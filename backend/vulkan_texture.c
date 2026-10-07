#include "asmui_vulkan_texture.h"
#include <string.h>

static int find_memory_type(VkPhysicalDevice physical,
                            uint32_t bits,
                            VkMemoryPropertyFlags required,
                            uint32_t *out_index,
                            VkMemoryPropertyFlags *out_flags) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if (!(bits & (1u << i))) continue;
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & required) == required) {
            *out_index = i;
            if (out_flags) *out_flags = f;
            return 1;
        }
    }
    return 0;
}

static void barrier(VkCommandBuffer cmd, VkImage image,
                    VkImageLayout old_layout, VkImageLayout new_layout,
                    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
                    VkAccessFlags src_access, VkAccessFlags dst_access) {
    VkImageMemoryBarrier b = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
}

static void upload_cleanup_transient(AsmUiVkTextureUpload *u) {
    if (!u || !u->device) return;
    if (u->command_buffer && u->command_pool)
        vkFreeCommandBuffers(u->device, u->command_pool, 1, &u->command_buffer);
    if (u->fence) vkDestroyFence(u->device, u->fence, NULL);
    if (u->staging) vkDestroyBuffer(u->device, u->staging, NULL);
    if (u->staging_memory) vkFreeMemory(u->device, u->staging_memory, NULL);
    u->command_buffer = VK_NULL_HANDLE;
    u->fence = VK_NULL_HANDLE;
    u->staging = VK_NULL_HANDLE;
    u->staging_memory = VK_NULL_HANDLE;
    u->submitted = 0;
}

int asmui_vk_texture_upload_rgba8_async_begin(VkPhysicalDevice physical,
                                              VkDevice device,
                                              VkQueue queue,
                                              VkCommandPool command_pool,
                                              const uint8_t *rgba,
                                              uint32_t width,
                                              uint32_t height,
                                              AsmUiVkTextureUpload *u) {
    if (!physical || !device || !queue || !command_pool || !rgba || !width || !height || !u)
        return 0;
    memset(u, 0, sizeof(*u));
    u->device = device;
    u->command_pool = command_pool;
    VkDeviceSize bytes = (VkDeviceSize)width * height * 4u;

    VkMemoryPropertyFlags staging_flags = 0;
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &bi, NULL, &u->staging) != VK_SUCCESS) goto fail;
    VkMemoryRequirements br;
    vkGetBufferMemoryRequirements(device, u->staging, &br);
    uint32_t bmt = 0;
    if (!find_memory_type(physical, br.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                          &bmt, &staging_flags)) goto fail;
    VkMemoryAllocateInfo bai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = br.size,
        .memoryTypeIndex = bmt,
    };
    if (vkAllocateMemory(device, &bai, NULL, &u->staging_memory) != VK_SUCCESS) goto fail;
    if (vkBindBufferMemory(device, u->staging, u->staging_memory, 0) != VK_SUCCESS) goto fail;
    void *mapped = NULL;
    if (vkMapMemory(device, u->staging_memory, 0, bytes, 0, &mapped) != VK_SUCCESS) goto fail;
    memcpy(mapped, rgba, (size_t)bytes);
    if (!(staging_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange mr = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = u->staging_memory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        if (vkFlushMappedMemoryRanges(device, 1, &mr) != VK_SUCCESS) {
            vkUnmapMemory(device, u->staging_memory);
            goto fail;
        }
    }
    vkUnmapMemory(device, u->staging_memory);

    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &ii, NULL, &u->texture.image) != VK_SUCCESS) goto fail;
    VkMemoryRequirements ir;
    vkGetImageMemoryRequirements(device, u->texture.image, &ir);
    uint32_t imt = 0;
    if (!find_memory_type(physical, ir.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &imt, NULL)) goto fail;
    VkMemoryAllocateInfo iai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = ir.size,
        .memoryTypeIndex = imt,
    };
    if (vkAllocateMemory(device, &iai, NULL, &u->texture.memory) != VK_SUCCESS) goto fail;
    if (vkBindImageMemory(device, u->texture.image, u->texture.memory, 0) != VK_SUCCESS) goto fail;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = u->texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(device, &vi, NULL, &u->texture.view) != VK_SUCCESS) goto fail;
    u->texture.width = width;
    u->texture.height = height;
    u->texture.format = VK_FORMAT_R8G8B8A8_UNORM;

    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device, &cai, &u->command_buffer) != VK_SUCCESS) goto fail;
    VkCommandBufferBeginInfo cbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(u->command_buffer, &cbi) != VK_SUCCESS) goto fail;
    barrier(u->command_buffer, u->texture.image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1 },
        .imageExtent = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(u->command_buffer, u->staging, u->texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    barrier(u->command_buffer, u->texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    if (vkEndCommandBuffer(u->command_buffer) != VK_SUCCESS) goto fail;

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(device, &fci, NULL, &u->fence) != VK_SUCCESS) goto fail;
    VkSubmitInfo si = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &u->command_buffer,
    };
    if (vkQueueSubmit(queue, 1, &si, u->fence) != VK_SUCCESS) goto fail;
    u->submitted = 1;
    return 1;

fail:
    upload_cleanup_transient(u);
    asmui_vk_texture_destroy(device, &u->texture);
    memset(u, 0, sizeof(*u));
    return 0;
}

int asmui_vk_texture_upload_async_poll(AsmUiVkTextureUpload *u,
                                       AsmUiVkTexture *out) {
    if (!u || !u->submitted || !u->device) return -1;
    VkResult r = vkGetFenceStatus(u->device, u->fence);
    if (r == VK_NOT_READY) return 0;
    if (r != VK_SUCCESS) return -1;
    AsmUiVkTexture texture = u->texture;
    memset(&u->texture, 0, sizeof(u->texture));
    upload_cleanup_transient(u);
    if (out) *out = texture;
    else asmui_vk_texture_destroy(u->device, &texture);
    return 1;
}

void asmui_vk_texture_upload_async_shutdown(AsmUiVkTextureUpload *u) {
    if (!u || !u->device) return;
    if (u->submitted && u->fence)
        (void)vkWaitForFences(u->device, 1, &u->fence, VK_TRUE, UINT64_MAX);
    upload_cleanup_transient(u);
    asmui_vk_texture_destroy(u->device, &u->texture);
    memset(u, 0, sizeof(*u));
}

int asmui_vk_texture_upload_rgba8(VkPhysicalDevice physical,
                                  VkDevice device,
                                  VkQueue queue,
                                  VkCommandPool command_pool,
                                  const uint8_t *rgba,
                                  uint32_t width,
                                  uint32_t height,
                                  AsmUiVkTexture *out) {
    AsmUiVkTextureUpload u;
    if (!asmui_vk_texture_upload_rgba8_async_begin(physical, device, queue, command_pool,
                                                   rgba, width, height, &u)) return 0;
    if (vkWaitForFences(device, 1, &u.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        asmui_vk_texture_upload_async_shutdown(&u);
        return 0;
    }
    int r = asmui_vk_texture_upload_async_poll(&u, out);
    if (r != 1) {
        asmui_vk_texture_upload_async_shutdown(&u);
        return 0;
    }
    return 1;
}

void asmui_vk_texture_destroy(VkDevice device, AsmUiVkTexture *t) {
    if (!t || !device) return;
    if (t->view) vkDestroyImageView(device, t->view, NULL);
    if (t->image) vkDestroyImage(device, t->image, NULL);
    if (t->memory) vkFreeMemory(device, t->memory, NULL);
    memset(t, 0, sizeof(*t));
}
