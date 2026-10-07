#include "asmui.h"
#include "asmui_builder.h"
#include "asmui_vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256u
#define H 256u
#define GW (W / 2u)
#define GH (H / 2u)

static void die_vk(const char *what, VkResult r) {
    fprintf(stderr, "%s failed: VkResult=%d\n", what, (int)r);
    exit(1);
}

static void *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || (n & 3L)) { fclose(f); return NULL; }
    rewind(f);
    void *p = malloc((size_t)n);
    if (!p) { fclose(f); return NULL; }
    if (fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p); fclose(f); return NULL;
    }
    fclose(f);
    *out_size = (size_t)n;
    return p;
}

static int find_memory_type(VkPhysicalDevice physical,
                            uint32_t type_bits,
                            VkMemoryPropertyFlags required,
                            uint32_t *out_index,
                            VkMemoryPropertyFlags *out_flags) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if (!(type_bits & (1u << i))) continue;
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & required) == required) {
            *out_index = i;
            if (out_flags) *out_flags = f;
            return 1;
        }
    }
    return 0;
}

static int create_image(VkPhysicalDevice physical,
                        VkDevice device,
                        uint32_t width,
                        uint32_t height,
                        VkFormat format,
                        VkImageUsageFlags usage,
                        VkImage *out_image,
                        VkDeviceMemory *out_memory,
                        VkImageView *out_view) {
    VkImageCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &ci, NULL, out_image) != VK_SUCCESS) return 0;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, *out_image, &req);
    uint32_t mt = 0;
    if (!find_memory_type(physical, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &mt, NULL)) {
        vkDestroyImage(device, *out_image, NULL);
        *out_image = VK_NULL_HANDLE;
        return 0;
    }

    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mt,
    };
    if (vkAllocateMemory(device, &ai, NULL, out_memory) != VK_SUCCESS) {
        vkDestroyImage(device, *out_image, NULL);
        *out_image = VK_NULL_HANDLE;
        return 0;
    }
    if (vkBindImageMemory(device, *out_image, *out_memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, *out_memory, NULL);
        vkDestroyImage(device, *out_image, NULL);
        *out_memory = VK_NULL_HANDLE;
        *out_image = VK_NULL_HANDLE;
        return 0;
    }

    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *out_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(device, &vi, NULL, out_view) != VK_SUCCESS) {
        vkFreeMemory(device, *out_memory, NULL);
        vkDestroyImage(device, *out_image, NULL);
        *out_memory = VK_NULL_HANDLE;
        *out_image = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

static int create_readback(VkPhysicalDevice physical,
                           VkDevice device,
                           VkDeviceSize size,
                           VkBuffer *out_buffer,
                           VkDeviceMemory *out_memory,
                           VkMemoryPropertyFlags *out_flags) {
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &bi, NULL, out_buffer) != VK_SUCCESS) return 0;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, *out_buffer, &req);
    uint32_t mt = 0;
    if (!find_memory_type(physical, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &mt, out_flags)) {
        vkDestroyBuffer(device, *out_buffer, NULL);
        return 0;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = mt,
    };
    if (vkAllocateMemory(device, &ai, NULL, out_memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, *out_buffer, NULL);
        return 0;
    }
    if (vkBindBufferMemory(device, *out_buffer, *out_memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, *out_memory, NULL);
        vkDestroyBuffer(device, *out_buffer, NULL);
        return 0;
    }
    return 1;
}

static void image_barrier(VkCommandBuffer cmd,
                          VkImage image,
                          VkImageLayout old_layout,
                          VkImageLayout new_layout,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage,
                          VkAccessFlags src_access,
                          VkAccessFlags dst_access) {
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
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &b);
}


#include "asmui_post_vulkan.h"

int main(void) {
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "asmui-vulkan-blur-smoke",
        .applicationVersion = VK_MAKE_VERSION(0, 7, 0),
        .pEngineName = "ASMUI",
        .engineVersion = VK_MAKE_VERSION(0, 7, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) die_vk("vkCreateInstance", vr);

    uint32_t device_count = 0;
    vr = vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if (vr != VK_SUCCESS || device_count == 0) {
        fprintf(stderr, "No Vulkan physical device found.\n");
        return 2;
    }
    VkPhysicalDevice *devices = calloc(device_count, sizeof(*devices));
    if (!devices) return 2;
    vr = vkEnumeratePhysicalDevices(instance, &device_count, devices);
    if (vr != VK_SUCCESS) die_vk("vkEnumeratePhysicalDevices", vr);

    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX;
    uint32_t timestamp_valid_bits = 0;
    int best_score = -1;
    VkPhysicalDeviceProperties chosen_props = {0};
    for (uint32_t d = 0; d < device_count; ++d) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[d], &props);
        if (VK_API_VERSION_MAJOR(props.apiVersion) < 1 ||
            (VK_API_VERSION_MAJOR(props.apiVersion) == 1 &&
             VK_API_VERSION_MINOR(props.apiVersion) < 3)) continue;

        VkPhysicalDeviceVulkan13Features f13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        };
        VkPhysicalDeviceFeatures2 f2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &f13,
        };
        vkGetPhysicalDeviceFeatures2(devices[d], &f2);
        if (!f13.dynamicRendering) continue;

        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, NULL);
        VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
        if (!qp) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, qp);
        uint32_t q = UINT32_MAX;
        uint32_t q_timestamp_bits = 0;
        for (uint32_t i = 0; i < qn; ++i) {
            if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qp[i].queueCount) {
                q = i;
                q_timestamp_bits = qp[i].timestampValidBits;
                break;
            }
        }
        free(qp);
        if (q == UINT32_MAX) continue;

        int score = 0;
        if (props.vendorID == 0x8086u) score += 100;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 20;
        if (score > best_score) {
            best_score = score;
            physical = devices[d];
            queue_family = q;
            timestamp_valid_bits = q_timestamp_bits;
            chosen_props = props;
        }
    }
    free(devices);
    if (!physical) {
        fprintf(stderr, "No Vulkan 1.3 + dynamicRendering graphics device found.\n");
        vkDestroyInstance(instance, NULL);
        return 3;
    }
    printf("GPU: %s (vendor=0x%04x device=0x%04x)\n",
           chosen_props.deviceName, chosen_props.vendorID, chosen_props.deviceID);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan13Features enable13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enable13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    };
    VkDevice device = VK_NULL_HANDLE;
    vr = vkCreateDevice(physical, &dci, NULL, &device);
    if (vr != VK_SUCCESS) die_vk("vkCreateDevice", vr);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    AsmUiVkBackend backend;
    if (!asmui_vk_init_streaming(&backend, physical, device, 32)) {
        fprintf(stderr, "asmui_vk_init_streaming failed\n");
        return 4;
    }

    size_t uvs_bytes = 0, ufs_bytes = 0, pvs_bytes = 0, pfs_bytes = 0;
    uint32_t *uvs = read_file("build/shaders/ui.vert.spv", &uvs_bytes);
    uint32_t *ufs = read_file("build/shaders/ui.frag.spv", &ufs_bytes);
    uint32_t *pvs = read_file("build/shaders/post.vert.spv", &pvs_bytes);
    uint32_t *pfs = read_file("build/shaders/post.frag.spv", &pfs_bytes);
    if (!uvs || !ufs || !pvs || !pfs) {
        fprintf(stderr, "Could not read SPIR-V. Run `make shaders` first.\n");
        return 5;
    }
    const VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (!asmui_vk_create_pipeline_pack(&backend, color_format,
                                       uvs, uvs_bytes, ufs, ufs_bytes)) {
        fprintf(stderr, "asmui_vk_create_pipeline_pack failed\n");
        return 6;
    }
    AsmUiVkPostBackend post;
    if (!asmui_vk_post_init(&post, device, color_format,
                            pvs, pvs_bytes, pfs, pfs_bytes)) {
        fprintf(stderr, "asmui_vk_post_init failed\n");
        return 6;
    }
    free(pfs); free(pvs); free(ufs); free(uvs);

    VkDescriptorPoolSize ps = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 16,
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 16,
        .poolSizeCount = 1,
        .pPoolSizes = &ps,
    };
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    vr = vkCreateDescriptorPool(device, &dpci, NULL, &descriptor_pool);
    if (vr != VK_SUCCESS) die_vk("vkCreateDescriptorPool", vr);

    VkSamplerCreateInfo sci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    vr = vkCreateSampler(device, &sci, NULL, &sampler);
    if (vr != VK_SUCCESS) die_vk("vkCreateSampler", vr);

    VkImage white_image = VK_NULL_HANDLE, color_image = VK_NULL_HANDLE;
    VkDeviceMemory white_memory = VK_NULL_HANDLE, color_memory = VK_NULL_HANDLE;
    VkImageView white_view = VK_NULL_HANDLE, color_view = VK_NULL_HANDLE;
    if (!create_image(physical, device, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      &white_image, &white_memory, &white_view)) {
        fprintf(stderr, "create white image failed\n"); return 7;
    }
    if (!create_image(physical, device, W, H, color_format,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      &color_image, &color_memory, &color_view)) {
        fprintf(stderr, "create color image failed\n"); return 8;
    }

    AsmUiVkTargetPool target_pool;
    asmui_vk_target_pool_init(&target_pool, physical, device);
    AsmUiVkRenderTarget *source = asmui_vk_target_acquire(&target_pool, W, H, color_format);
    AsmUiVkRenderTarget *extract = asmui_vk_target_acquire(&target_pool, GW, GH, color_format);
    AsmUiVkRenderTarget *ping = asmui_vk_target_acquire(&target_pool, GW, GH, color_format);
    AsmUiVkRenderTarget *pong = asmui_vk_target_acquire(&target_pool, GW, GH, color_format);
    if (!source || !extract || !ping || !pong) {
        fprintf(stderr, "glow render-target acquire failed\n"); return 8;
    }

    if (!asmui_vk_alloc_texture_set(&backend, descriptor_pool, 0,
                                    white_view, sampler,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
        fprintf(stderr, "white descriptor allocation failed\n"); return 9;
    }
    asmui_vk_set_white_texture_set(&backend, backend.texture_sets[0]);

    VkDescriptorSet source_set = VK_NULL_HANDLE;
    VkDescriptorSet extract_set = VK_NULL_HANDLE;
    VkDescriptorSet ping_set = VK_NULL_HANDLE;
    VkDescriptorSet pong_set = VK_NULL_HANDLE;
    if (!asmui_vk_post_alloc_texture_set(&post, descriptor_pool, source->view, sampler,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &source_set) ||
        !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, extract->view, sampler,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &extract_set) ||
        !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, ping->view, sampler,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &ping_set) ||
        !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, pong->view, sampler,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &pong_set)) {
        fprintf(stderr, "post descriptor allocation failed\n"); return 9;
    }

    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkMemoryPropertyFlags readback_flags = 0;
    if (!create_readback(physical, device, W * H * 4u,
                         &readback, &readback_memory, &readback_flags)) {
        fprintf(stderr, "readback buffer creation failed\n"); return 10;
    }

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_family,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    };
    VkCommandPool command_pool = VK_NULL_HANDLE;
    vr = vkCreateCommandPool(device, &cpci, NULL, &command_pool);
    if (vr != VK_SUCCESS) die_vk("vkCreateCommandPool", vr);
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vr = vkAllocateCommandBuffers(device, &cbai, &cmd);
    if (vr != VK_SUCCESS) die_vk("vkAllocateCommandBuffers", vr);

    VkQueryPool query_pool = VK_NULL_HANDLE;
    if (timestamp_valid_bits) {
        VkQueryPoolCreateInfo qpci = {
            .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP,
            .queryCount = 8,
        };
        vr = vkCreateQueryPool(device, &qpci, NULL, &query_pool);
        if (vr != VK_SUCCESS) die_vk("vkCreateQueryPool", vr);
    }

    AsmUiContext ctx;
    AsmUiNode nodes[3] = {0};
    AsmUiDrawCmd draws[8] = {0};
    AsmUiTween tweens[1] = {0};
    AsmUiBatch batches[8] = {0};
    asm_ui_init(&ctx, nodes, 3, draws, 8, tweens, 1);
    asm_ui_set_viewport(&ctx, (float)W, (float)H);

    /* Bright source: must bloom. */
    nodes[0] = asmui_node(ASMUI_NODE_IMAGE, -1, 0xFF4040FFu);
    nodes[0].resource_id = 0;
    nodes[0].size_delta_x = 64.0f;
    nodes[0].size_delta_y = 64.0f;

    /* Dim source: visible in base image but intentionally below extraction threshold. */
    nodes[1] = asmui_node(ASMUI_NODE_IMAGE, -1, 0x307030FFu);
    nodes[1].resource_id = 0;
    nodes[1].size_delta_x = 28.0f;
    nodes[1].size_delta_y = 28.0f;
    nodes[1].anchored_x = -80.0f;

    ctx.node_count = 2;
    asm_ui_begin_frame(&ctx, 1.0f / 60.0f);
    asm_ui_layout(&ctx);
    asm_ui_build_drawlist(&ctx);
    uint32_t batch_count = asm_ui_build_batches(&ctx, batches, 8);
    uint32_t vertex_count = asmui_vk_upload(&ctx, &backend);
    if (ctx.draw_count != 2 || batch_count != 1 || vertex_count != 8) {
        fprintf(stderr, "ASMUI stream mismatch: draws=%u batches=%u vertices=%u\n",
                ctx.draw_count, batch_count, vertex_count);
        return 11;
    }

    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vr = vkBeginCommandBuffer(cmd, &cbbi);
    if (vr != VK_SUCCESS) die_vk("vkBeginCommandBuffer", vr);
    if (query_pool) vkCmdResetQueryPool(cmd, query_pool, 0, 8);

    image_barrier(cmd, white_image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue white = { .float32 = {1.0f, 1.0f, 1.0f, 1.0f} };
    VkImageSubresourceRange white_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
    };
    vkCmdClearColorImage(cmd, white_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &white, 1, &white_range);
    image_barrier(cmd, white_image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    image_barrier(cmd, source->image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo src_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = source->view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} } },
    };
    VkRenderingInfo src_ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = {W, H} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &src_att,
    };
    vkCmdBeginRendering(cmd, &src_ri);
    asmui_vk_record_images(cmd, &ctx, batches, 1, &backend, W, H);
    vkCmdEndRendering(cmd);
    image_barrier(cmd, source->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    /* Bright-pass extraction + 2x downsample: source -> half-resolution extract. */
    image_barrier(cmd, extract->image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 0);
    VkRenderingAttachmentInfo extract_att = src_att;
    extract_att.imageView = extract->view;
    VkRenderingInfo half_ri = src_ri;
    half_ri.renderArea.extent.width = GW;
    half_ri.renderArea.extent.height = GH;
    half_ri.pColorAttachments = &extract_att;
    vkCmdBeginRendering(cmd, &half_ri);
    asmui_vk_post_record(cmd, &post, source_set, GW, GH, ASMUI_POST_EXTRACT,
                         0.0f, 0.0f, 0.75f, 1.0f);
    vkCmdEndRendering(cmd);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 1);
    image_barrier(cmd, extract->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    /* Half-resolution horizontal Gaussian pass. */
    image_barrier(cmd, ping->image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 2);
    VkRenderingAttachmentInfo ping_att = src_att;
    ping_att.imageView = ping->view;
    half_ri.pColorAttachments = &ping_att;
    vkCmdBeginRendering(cmd, &half_ri);
    asmui_vk_post_record(cmd, &post, extract_set, GW, GH, ASMUI_POST_BLUR,
                         1.0f / (float)GW, 0.0f, 2.0f, 1.0f);
    vkCmdEndRendering(cmd);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 3);
    image_barrier(cmd, ping->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    /* Half-resolution vertical Gaussian pass. */
    image_barrier(cmd, pong->image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 4);
    VkRenderingAttachmentInfo pong_att = src_att;
    pong_att.imageView = pong->view;
    half_ri.pColorAttachments = &pong_att;
    vkCmdBeginRendering(cmd, &half_ri);
    asmui_vk_post_record(cmd, &post, ping_set, GW, GH, ASMUI_POST_BLUR,
                         0.0f, 1.0f / (float)GH, 2.0f, 1.0f);
    vkCmdEndRendering(cmd);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 5);
    image_barrier(cmd, pong->image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    /* Base image + additive upsampled glow into final image. */
    image_barrier(cmd, color_image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool, 6);
    VkRenderingAttachmentInfo out_att = src_att;
    out_att.imageView = color_view;
    VkRenderingInfo out_ri = src_ri;
    out_ri.pColorAttachments = &out_att;
    vkCmdBeginRendering(cmd, &out_ri);
    asmui_vk_post_record(cmd, &post, source_set, W, H, ASMUI_POST_COPY,
                         0.0f, 0.0f, 1.0f, 1.0f);
    asmui_vk_post_record_additive(cmd, &post, pong_set, W, H, ASMUI_POST_COPY,
                                  0.0f, 0.0f, 1.0f, 0.85f);
    vkCmdEndRendering(cmd);
    if (query_pool) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool, 7);

    image_barrier(cmd, color_image,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = {W, H, 1},
    };
    vkCmdCopyImageToBuffer(cmd, color_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback, 1, &copy);
    VkBufferMemoryBarrier rb = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = readback,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         0, NULL, 1, &rb, 0, NULL);

    vr = vkEndCommandBuffer(cmd);
    if (vr != VK_SUCCESS) die_vk("vkEndCommandBuffer", vr);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    vr = vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    if (vr != VK_SUCCESS) die_vk("vkQueueSubmit", vr);
    vr = vkQueueWaitIdle(queue);
    if (vr != VK_SUCCESS) die_vk("vkQueueWaitIdle", vr);

    void *mapped = NULL;
    vr = vkMapMemory(device, readback_memory, 0, VK_WHOLE_SIZE, 0, &mapped);
    if (vr != VK_SUCCESS) die_vk("vkMapMemory(readback)", vr);
    if (!(readback_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange mr = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = readback_memory,
            .size = VK_WHOLE_SIZE,
        };
        vr = vkInvalidateMappedMemoryRanges(device, 1, &mr);
        if (vr != VK_SUCCESS) die_vk("vkInvalidateMappedMemoryRanges", vr);
    }
    const unsigned char *pixels = mapped;
    size_t center = ((size_t)(H / 2u) * W + W / 2u) * 4u;
    size_t bright_halo = ((size_t)(H / 2u) * W + (W / 2u + 40u)) * 4u;
    size_t dim_halo = ((size_t)(H / 2u) * W + (W / 2u - 80u + 20u)) * 4u;
    size_t farp = ((size_t)16u * W + 16u) * 4u;
    unsigned center_r = pixels[center + 0];
    unsigned halo_r = pixels[bright_halo + 0];
    unsigned dim_halo_g = pixels[dim_halo + 1];
    unsigned far_r = pixels[farp + 0];
    printf("glow pixels: center.r=%u bright-halo.r=%u dim-halo.g=%u far.r=%u\n",
           center_r, halo_r, dim_halo_g, far_r);
    int ok = center_r > 220u && halo_r > 3u && dim_halo_g < 8u && far_r < 8u;
    vkUnmapMemory(device, readback_memory);

    if (query_pool) {
        uint64_t q[8] = {0};
        vr = vkGetQueryPoolResults(device, query_pool, 0, 8, sizeof(q), q, sizeof(uint64_t),
                                   VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (vr != VK_SUCCESS) die_vk("vkGetQueryPoolResults", vr);
        double period_ns = (double)chosen_props.limits.timestampPeriod;
        double extract_us = (double)(q[1] - q[0]) * period_ns / 1000.0;
        double h_us = (double)(q[3] - q[2]) * period_ns / 1000.0;
        double v_us = (double)(q[5] - q[4]) * period_ns / 1000.0;
        double c_us = (double)(q[7] - q[6]) * period_ns / 1000.0;
        double total_us = extract_us + h_us + v_us + c_us;
        printf("GPU glow timings (half-res blur): extract=%.2f us blurH=%.2f us blurV=%.2f us composite=%.2f us total=%.2f us\n",
               extract_us, h_us, v_us, c_us, total_us);
        double max_us = 2000.0;
        const char *env = getenv("ASMUI_GPU_GLOW_MAX_US");
        if (env && *env) max_us = strtod(env, NULL);
        if (total_us > max_us) {
            fprintf(stderr, "GPU GLOW PERF REGRESSION: %.2f us > %.2f us guardrail\n", total_us, max_us);
            ok = 0;
        }
    } else {
        puts("GPU timestamps unavailable on selected queue; glow correctness still verified.");
    }

    /* Mixed-size pool regression: exact half-resolution target must be reusable. */
    AsmUiVkRenderTarget *old_extract = extract;
    asmui_vk_target_release(&target_pool, extract);
    AsmUiVkRenderTarget *extract_reuse = asmui_vk_target_acquire(&target_pool, GW, GH, color_format);
    printf("render-target pool: created=%u reused=%u half-res-reused=%s\n",
           target_pool.created_count, target_pool.reuse_count,
           extract_reuse == old_extract ? "yes" : "no");
    if (extract_reuse != old_extract) ok = 0;

    vkDeviceWaitIdle(device);
    if (query_pool) vkDestroyQueryPool(device, query_pool, NULL);
    vkDestroyCommandPool(device, command_pool, NULL);
    vkDestroyBuffer(device, readback, NULL);
    vkFreeMemory(device, readback_memory, NULL);
    vkDestroyImageView(device, color_view, NULL);
    vkDestroyImage(device, color_image, NULL);
    vkFreeMemory(device, color_memory, NULL);
    vkDestroyImageView(device, white_view, NULL);
    vkDestroyImage(device, white_image, NULL);
    vkFreeMemory(device, white_memory, NULL);
    asmui_vk_target_pool_destroy(&target_pool);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    asmui_vk_post_destroy(&post);
    asmui_vk_destroy_pipeline_pack(&backend);
    asmui_vk_destroy_streaming(&backend);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);

    if (!ok) {
        fprintf(stderr, "Glow/post-process output did not meet correctness/perf guardrails.\n");
        return 12;
    }
    puts("Vulkan glow smoke OK: bright-pass/downsample -> blur H/V -> additive upsample -> timestamp/readback");
    return 0;
}
