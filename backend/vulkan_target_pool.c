#include "asmui_vulkan.h"
#include <string.h>

static int find_device_local(VkPhysicalDevice physical, uint32_t bits, uint32_t *out) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    int fallback = -1;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if (!(bits & (1u << i))) continue;
        if (fallback < 0) fallback = (int)i;
        if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            *out = i;
            return 1;
        }
    }
    if (fallback >= 0) { *out = (uint32_t)fallback; return 1; }
    return 0;
}

static void destroy_target(AsmUiVkTargetPool *pool, AsmUiVkRenderTarget *t) {
    if (t->view) vkDestroyImageView(pool->device, t->view, NULL);
    if (t->image) vkDestroyImage(pool->device, t->image, NULL);
    if (t->memory) vkFreeMemory(pool->device, t->memory, NULL);
    memset(t, 0, sizeof(*t));
}

static int create_target(AsmUiVkTargetPool *pool, AsmUiVkRenderTarget *t,
                         uint32_t width, uint32_t height, VkFormat format) {
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(pool->device, &ci, NULL, &t->image) != VK_SUCCESS) return 0;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(pool->device, t->image, &req);
    uint32_t type = 0;
    if (!find_device_local(pool->physical_device, req.memoryTypeBits, &type)) {
        destroy_target(pool, t); return 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = type,
    };
    if (vkAllocateMemory(pool->device, &ai, NULL, &t->memory) != VK_SUCCESS ||
        vkBindImageMemory(pool->device, t->image, t->memory, 0) != VK_SUCCESS) {
        destroy_target(pool, t); return 0;
    }

    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = t->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0, .levelCount = 1,
            .baseArrayLayer = 0, .layerCount = 1,
        },
    };
    if (vkCreateImageView(pool->device, &vi, NULL, &t->view) != VK_SUCCESS) {
        destroy_target(pool, t); return 0;
    }
    t->width = width; t->height = height; t->format = format;
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t->in_use = 1;
    t->generation = 1;
    return 1;
}

void asmui_vk_target_pool_init(AsmUiVkTargetPool *pool,
                               VkPhysicalDevice physical_device,
                               VkDevice device) {
    if (!pool) return;
    memset(pool, 0, sizeof(*pool));
    pool->physical_device = physical_device;
    pool->device = device;
}

void asmui_vk_target_pool_begin_frame(AsmUiVkTargetPool *pool) {
    if (!pool) return;
    for (uint32_t i = 0; i < ASMUI_VK_MAX_RENDER_TARGETS; ++i)
        pool->targets[i].in_use = 0;
}

AsmUiVkRenderTarget *asmui_vk_target_acquire(AsmUiVkTargetPool *pool,
                                             uint32_t width,
                                             uint32_t height,
                                             VkFormat format) {
    if (!pool || !pool->device || !width || !height || format == VK_FORMAT_UNDEFINED) return NULL;
    for (uint32_t i = 0; i < ASMUI_VK_MAX_RENDER_TARGETS; ++i) {
        AsmUiVkRenderTarget *t = &pool->targets[i];
        if (!t->in_use && t->image && t->width == width && t->height == height && t->format == format) {
            t->in_use = 1;
            ++t->generation;
            ++pool->reuse_count;
            return t;
        }
    }
    for (uint32_t i = 0; i < ASMUI_VK_MAX_RENDER_TARGETS; ++i) {
        AsmUiVkRenderTarget *t = &pool->targets[i];
        if (!t->image) {
            if (!create_target(pool, t, width, height, format)) return NULL;
            ++pool->created_count;
            return t;
        }
    }
    return NULL;
}

void asmui_vk_target_release(AsmUiVkTargetPool *pool, AsmUiVkRenderTarget *target) {
    (void)pool;
    if (target) target->in_use = 0;
}

void asmui_vk_target_pool_destroy(AsmUiVkTargetPool *pool) {
    if (!pool || !pool->device) return;
    for (uint32_t i = 0; i < ASMUI_VK_MAX_RENDER_TARGETS; ++i)
        destroy_target(pool, &pool->targets[i]);
    pool->created_count = pool->reuse_count = 0;
}
