/* Desktop rendering service derived from the existing editor's Vulkan path. */
#include "asmui_desktop.h"
#include "asmui_font_sdf_ft.h"
#include "asmui_text.h"
#include "asmui_vulkan.h"
#include "asmui_vulkan_texture.h"
#include <SDL3/SDL_vulkan.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INPUT_FONT_PAGES 64u
typedef struct FontBank {
    AsmUiFontSdfAtlas* base;
    AsmUiFontSdfAtlas pages[INPUT_FONT_PAGES];
    uint32_t count;
} FontBank;
typedef struct Host {
    SDL_Window* window;
    SDL_InitFlags sdl_flags;
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkSurfaceKHR surface;
    VkQueue queue;
    uint32_t family;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore acquired, done[16];
    VkSwapchainKHR swapchain;
    VkImage images[16];
    uint32_t image_count;
    uint32_t width, height;
    int initialized[16], target_initialized;
    VkFormat swap_format;
    AsmUiVkBackend backend;
    AsmUiVkTargetPool targets;
    AsmUiVkRenderTarget* target;
    VkDescriptorPool descriptors;
    VkSampler sampler;
    AsmUiVkTexture font_texture, white_texture;
    AsmUiVkTexture input_fonts[INPUT_FONT_PAGES];
    VkBuffer readback;
    VkDeviceMemory memory;
    VkMemoryPropertyFlags memory_flags;
} Host;
static void vk_check(VkResult r, const char* s) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "%s: Vulkan %d\n", s, r);
        exit(1);
    }
}
#define VK(C) vk_check((C), #C)
static void fail(const char* s) {
    fprintf(stderr, "%s\n", s);
    exit(1);
}
static uint32_t* read_spv(const char* path, size_t* bytes) {
    FILE* f = fopen(path, "rb");
    if (!f)
        fail(path);
    if (fseek(f, 0, SEEK_END))
        fail(path);
    long n = ftell(f);
    if (n <= 0 || (n & 3))
        fail(path);
    rewind(f);
    uint32_t* p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n)
        fail(path);
    fclose(f);
    *bytes = (size_t)n;
    return p;
}
static void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before,
                    VkImageLayout after, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                    VkAccessFlags sa, VkAccessFlags da) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                              .srcAccessMask = sa,
                              .dstAccessMask = da,
                              .oldLayout = before,
                              .newLayout = after,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image = image,
                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, NULL, 0, NULL, 1, &b);
}
static void destroy_size(Host* h) {
    if (h->readback)
        vkDestroyBuffer(h->device, h->readback, NULL);
    if (h->memory)
        vkFreeMemory(h->device, h->memory, NULL);
    h->readback = VK_NULL_HANDLE;
    h->memory = VK_NULL_HANDLE;
    asmui_vk_target_pool_destroy(&h->targets);
    for (uint32_t i = 0; i < h->image_count; ++i)
        if (h->done[i])
            vkDestroySemaphore(h->device, h->done[i], NULL);
    memset(h->done, 0, sizeof(h->done));
    h->image_count = 0;
    if (h->swapchain)
        vkDestroySwapchainKHR(h->device, h->swapchain, NULL);
    h->swapchain = VK_NULL_HANDLE;
}
static void create_size(Host* h, uint32_t width, uint32_t height) {
    VK(vkDeviceWaitIdle(h->device));
    destroy_size(h);
    if (h->surface) {
        VkSurfaceCapabilitiesKHR cap;
        VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(h->physical, h->surface, &cap));
        if (!(cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            fail("Surface cannot receive a blit");
        if (cap.currentExtent.width != UINT32_MAX) {
            width = cap.currentExtent.width;
            height = cap.currentExtent.height;
        } else {
            width = width < cap.minImageExtent.width   ? cap.minImageExtent.width
                    : width > cap.maxImageExtent.width ? cap.maxImageExtent.width
                                                       : width;
            height = height < cap.minImageExtent.height   ? cap.minImageExtent.height
                     : height > cap.maxImageExtent.height ? cap.maxImageExtent.height
                                                          : height;
        }
        uint32_t nf = 0;
        VK(vkGetPhysicalDeviceSurfaceFormatsKHR(h->physical, h->surface, &nf, NULL));
        if (!nf)
            fail("No surface formats");
        VkSurfaceFormatKHR* fmts = calloc(nf, sizeof(*fmts));
        if (!fmts)
            fail("format allocation");
        VK(vkGetPhysicalDeviceSurfaceFormatsKHR(h->physical, h->surface, &nf, fmts));
        VkSurfaceFormatKHR fmt = fmts[0];
        for (uint32_t i = 0; i < nf; ++i)
            if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
                fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
                fmt = fmts[i];
                break;
            }
        free(fmts);
        h->swap_format = fmt.format;
        uint32_t mode_count = 0;
        VK(vkGetPhysicalDeviceSurfacePresentModesKHR(h->physical, h->surface, &mode_count,
                                                     NULL));
        VkPresentModeKHR* modes = calloc(mode_count, sizeof(*modes));
        if (!modes)
            fail("Present mode allocation");
        VK(vkGetPhysicalDeviceSurfacePresentModesKHR(h->physical, h->surface, &mode_count,
                                                     modes));
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (uint32_t i = 0; i < mode_count; ++i)
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        free(modes);
        printf("ASMUI presentation: %s\n",
               present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo");
        uint32_t count = cap.minImageCount + 1;
        if (cap.maxImageCount && count > cap.maxImageCount)
            count = cap.maxImageCount;
        VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(cap.supportedCompositeAlpha & alpha)) {
            for (unsigned a = 1; a <= 8; a <<= 1)
                if (cap.supportedCompositeAlpha & a) {
                    alpha = (VkCompositeAlphaFlagBitsKHR)a;
                    break;
                }
        }
        VkSwapchainCreateInfoKHR s = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                      .surface = h->surface,
                                      .minImageCount = count,
                                      .imageFormat = fmt.format,
                                      .imageColorSpace = fmt.colorSpace,
                                      .imageExtent = {width, height},
                                      .imageArrayLayers = 1,
                                      .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
                                      .preTransform = cap.currentTransform,
                                      .compositeAlpha = alpha,
                                      .presentMode = present_mode,
                                      .clipped = VK_TRUE};
        VK(vkCreateSwapchainKHR(h->device, &s, NULL, &h->swapchain));
        VK(vkGetSwapchainImagesKHR(h->device, h->swapchain, &h->image_count, NULL));
        if (h->image_count > 16)
            fail("Too many swapchain images");
        VK(vkGetSwapchainImagesKHR(h->device, h->swapchain, &h->image_count, h->images));
        VkSemaphoreCreateInfo sem = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (uint32_t i = 0; i < h->image_count; ++i)
            VK(vkCreateSemaphore(h->device, &sem, NULL, &h->done[i]));
        memset(h->initialized, 0, sizeof(h->initialized));
    }
    h->width = width;
    h->height = height;
    h->target_initialized = 0;
    asmui_vk_target_pool_init(&h->targets, h->physical, h->device);
    h->target = asmui_vk_target_acquire(&h->targets, width, height, VK_FORMAT_R8G8B8A8_UNORM);
    if (!h->target)
        fail("Offscreen allocation");
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = (VkDeviceSize)width * height * 4,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VK(vkCreateBuffer(h->device, &bi, NULL, &h->readback));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(h->device, h->readback, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(h->physical, &mp);
    uint32_t mt = UINT32_MAX;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            mt = i;
            if (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                break;
        }
    if (mt == UINT32_MAX)
        fail("No readback memory");
    h->memory_flags = mp.memoryTypes[mt].propertyFlags;
    VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = req.size,
                               .memoryTypeIndex = mt};
    VK(vkAllocateMemory(h->device, &ma, NULL, &h->memory));
    VK(vkBindBufferMemory(h->device, h->readback, h->memory, 0));
}
static void init_host(Host* h, int headless, uint32_t w, uint32_t ht, AsmUiFontSdfAtlas* atlas,
                      const AsmUiDesktopConfig* config) {
    const char* const* exts = NULL;
    uint32_t ext_count = 0;
    h->sdl_flags = SDL_INIT_EVENTS | (headless ? 0 : SDL_INIT_VIDEO);
    if (!SDL_InitSubSystem(h->sdl_flags))
        fail(SDL_GetError());
    if (!headless) {
        h->window = SDL_CreateWindow(config->title, (int)w, (int)ht,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!h->window)
            fail(SDL_GetError());
        SDL_SetWindowMinimumSize(h->window, config->min_width ? (int)config->min_width : 640,
                                 config->min_height ? (int)config->min_height : 480);
        exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
        if (!exts)
            fail(SDL_GetError());
    }
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = config->title,
                            .applicationVersion = VK_MAKE_VERSION(0, 21, 0),
                            .pEngineName = "ASMUI",
                            .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                .pApplicationInfo = &ai,
                                .enabledExtensionCount = ext_count,
                                .ppEnabledExtensionNames = exts};
    VK(vkCreateInstance(&ici, NULL, &h->instance));
    if (h->window && !SDL_Vulkan_CreateSurface(h->window, h->instance, NULL, &h->surface))
        fail(SDL_GetError());
    uint32_t n = 0;
    VK(vkEnumeratePhysicalDevices(h->instance, &n, NULL));
    if (!n)
        fail("No Vulkan device");
    VkPhysicalDevice* devs = calloc(n, sizeof(*devs));
    if (!devs)
        fail("device allocation");
    VK(vkEnumeratePhysicalDevices(h->instance, &n, devs));
    for (uint32_t d = 0; d < n && !h->physical; ++d) {
        VkPhysicalDeviceVulkan13Features f13 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                        .pNext = &f13};
        vkGetPhysicalDeviceFeatures2(devs[d], &f2);
        if (!f13.dynamicRendering)
            continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, NULL);
        VkQueueFamilyProperties* qs = calloc(qn, sizeof(*qs));
        if (!qs)
            fail("queue allocation");
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, qs);
        for (uint32_t q = 0; q < qn; ++q) {
            VkBool32 present = VK_TRUE;
            if (h->surface)
                VK(vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], q, h->surface, &present));
            if ((qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                h->physical = devs[d];
                h->family = q;
                break;
            }
        }
        free(qs);
    }
    free(devs);
    if (!h->physical)
        fail("Vulkan 1.3 dynamic rendering required");
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(h->physical, &props);
    printf("ASMUI GPU: %s\n", props.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = h->family,
                                  .queueCount = 1,
                                  .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE};
    const char* swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .pNext = &f13,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qi,
                             .enabledExtensionCount = h->surface ? 1u : 0u,
                             .ppEnabledExtensionNames = h->surface ? &swap_ext : NULL};
    VK(vkCreateDevice(h->physical, &di, NULL, &h->device));
    vkGetDeviceQueue(h->device, h->family, 0, &h->queue);
    VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                  .queueFamilyIndex = h->family};
    VK(vkCreateCommandPool(h->device, &pi, NULL, &h->pool));
    VkCommandBufferAllocateInfo ca = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                      .commandPool = h->pool,
                                      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                      .commandBufferCount = 1};
    VK(vkAllocateCommandBuffers(h->device, &ca, &h->cmd));
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                            .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VK(vkCreateFence(h->device, &fi, NULL, &h->fence));
    VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK(vkCreateSemaphore(h->device, &si, NULL, &h->acquired));
    if (!asmui_vk_init_streaming(&h->backend, h->physical, h->device,
                                 config->max_draws + config->max_nodes))
        fail("ASM vertex stream init");
    char vert[4096], frag[4096];
    snprintf(vert, sizeof(vert), "%s/ui.vert.spv", config->shader_dir);
    snprintf(frag, sizeof(frag), "%s/ui.frag.spv", config->shader_dir);
    size_t vb, fb;
    uint32_t *vs = read_spv(vert, &vb), *fs = read_spv(frag, &fb);
    if (!asmui_vk_create_pipeline_pack(&h->backend, VK_FORMAT_R8G8B8A8_UNORM, vs, vb, fs, fb))
        fail("Editor pipelines");
    free(vs);
    free(fs);
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               ASMUI_VK_MAX_TEXTURES};
    VkDescriptorPoolCreateInfo dp = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                     .maxSets = ASMUI_VK_MAX_TEXTURES,
                                     .poolSizeCount = 1,
                                     .pPoolSizes = &ps};
    VK(vkCreateDescriptorPool(h->device, &dp, NULL, &h->descriptors));
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_LINEAR,
                               .minFilter = VK_FILTER_LINEAR,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    VK(vkCreateSampler(h->device, &sci, NULL, &h->sampler));
    const unsigned char white[] = {255, 255, 255, 255};
    if (!asmui_vk_texture_upload_rgba8(h->physical, h->device, h->queue, h->pool, white, 1, 1,
                                       &h->white_texture) ||
        !asmui_vk_texture_upload_rgba8(h->physical, h->device, h->queue, h->pool, atlas->rgba,
                                       atlas->width, atlas->height, &h->font_texture))
        fail("Editor texture upload");
    if (!asmui_vk_alloc_texture_set(&h->backend, h->descriptors, 0, h->white_texture.view,
                                    h->sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ||
        !asmui_vk_alloc_texture_set(&h->backend, h->descriptors, 1, h->font_texture.view,
                                    h->sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        fail("Editor descriptors");
    asmui_vk_set_white_texture_set(&h->backend, h->backend.texture_sets[0]);
    create_size(h, w, ht);
}
static void capture(Host* h, const char* path) {
    VK(vkWaitForFences(h->device, 1, &h->fence, VK_TRUE, UINT64_MAX));
    void* p = NULL;
    VK(vkMapMemory(h->device, h->memory, 0, VK_WHOLE_SIZE, 0, &p));
    if (!(h->memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange r = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                 .memory = h->memory,
                                 .size = VK_WHOLE_SIZE};
        VK(vkInvalidateMappedMemoryRanges(h->device, 1, &r));
    }
    png_image im;
    memset(&im, 0, sizeof(im));
    im.version = PNG_IMAGE_VERSION;
    im.width = h->width;
    im.height = h->height;
    im.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&im, path, 0, p, 0, NULL))
        fail(im.message);
    png_image_free(&im);
    vkUnmapMemory(h->device, h->memory);
    printf("ASMUI screenshot: %s (%ux%u)\n", path, h->width, h->height);
}

static int font_lookup(uint32_t id, AsmUiHbAtlasGlyph* out, void* user) {
    FontBank* bank = user;
    if (asmui_font_sdf_lookup(id, out, bank->base))
        return 1;
    for (uint32_t i = 0; i < bank->count; ++i)
        if (asmui_font_sdf_lookup(id, out, &bank->pages[i]))
            return 1;
    return 0;
}
static int draw(Host* h, AsmUiContext* ctx, AsmUiBatch* batches, uint32_t count,
                int screenshot) {
    VK(vkWaitForFences(h->device, 1, &h->fence, VK_TRUE, UINT64_MAX));
    uint32_t index = 0;
    if (h->swapchain) {
        VkResult r = vkAcquireNextImageKHR(h->device, h->swapchain, UINT64_MAX, h->acquired,
                                           VK_NULL_HANDLE, &index);
        if (r == VK_ERROR_OUT_OF_DATE_KHR)
            return -1;
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
            vk_check(r, "acquire");
    }
    if (asmui_vk_upload(ctx, &h->backend) != ctx->draw_count * 4u)
        fail("Editor vertex capacity");
    VK(vkResetCommandBuffer(h->cmd, 0));
    VkCommandBufferBeginInfo cb = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK(vkBeginCommandBuffer(h->cmd, &cb));
    barrier(h->cmd, h->target->image,
            h->target_initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                  : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            h->target_initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                  : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            h->target_initialized ? VK_ACCESS_TRANSFER_READ_BIT : 0,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    VkRenderingAttachmentInfo a = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                   .imageView = h->target->view,
                                   .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                   .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                   .clearValue = {.color = {.float32 = {0, 0, 0, 1}}}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {h->width, h->height}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &a};
    vkCmdBeginRendering(h->cmd, &ri);
    asmui_vk_record_images(h->cmd, ctx, batches, count, &h->backend, h->width, h->height);
    vkCmdEndRendering(h->cmd);
    barrier(h->cmd, h->target->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);
    h->target_initialized = 1;
    if (screenshot) {
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageExtent = {h->width, h->height, 1}};
        vkCmdCopyImageToBuffer(h->cmd, h->target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               h->readback, 1, &copy);
        VkBufferMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                   .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .buffer = h->readback,
                                   .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(h->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 0, NULL, 1, &b, 0, NULL);
    }
    if (h->swapchain) {
        barrier(h->cmd, h->images[index],
                h->initialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                      : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit b = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         .srcOffsets = {{0, 0, 0}, {(int)h->width, (int)h->height, 1}},
                         .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         .dstOffsets = {{0, 0, 0}, {(int)h->width, (int)h->height, 1}}};
        vkCmdBlitImage(h->cmd, h->target->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       h->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                       VK_FILTER_NEAREST);
        barrier(h->cmd, h->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
        h->initialized[index] = 1;
    }
    VK(vkEndCommandBuffer(h->cmd));
    VK(vkResetFences(h->device, 1, &h->fence));
    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                       .waitSemaphoreCount = h->swapchain ? 1u : 0u,
                       .pWaitSemaphores = h->swapchain ? &h->acquired : NULL,
                       .pWaitDstStageMask = &stage,
                       .commandBufferCount = 1,
                       .pCommandBuffers = &h->cmd,
                       .signalSemaphoreCount = h->swapchain ? 1u : 0u,
                       .pSignalSemaphores = h->swapchain ? &h->done[index] : NULL};
    VK(vkQueueSubmit(h->queue, 1, &si, h->fence));
    if (h->swapchain) {
        VkPresentInfoKHR p = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                              .waitSemaphoreCount = 1,
                              .pWaitSemaphores = &h->done[index],
                              .swapchainCount = 1,
                              .pSwapchains = &h->swapchain,
                              .pImageIndices = &index};
        VkResult r = vkQueuePresentKHR(h->queue, &p);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            return 0;
        vk_check(r, "present");
    }
    return 1;
}
static void destroy_host(Host* h) {
    VK(vkDeviceWaitIdle(h->device));
    destroy_size(h);
    asmui_vk_texture_destroy(h->device, &h->font_texture);
    for (uint32_t i = 0; i < INPUT_FONT_PAGES; ++i)
        asmui_vk_texture_destroy(h->device, &h->input_fonts[i]);
    asmui_vk_texture_destroy(h->device, &h->white_texture);
    vkDestroySampler(h->device, h->sampler, NULL);
    vkDestroyDescriptorPool(h->device, h->descriptors, NULL);
    asmui_vk_destroy_pipeline_pack(&h->backend);
    asmui_vk_destroy_streaming(&h->backend);
    vkDestroyFence(h->device, h->fence, NULL);
    vkDestroySemaphore(h->device, h->acquired, NULL);
    vkDestroyCommandPool(h->device, h->pool, NULL);
    vkDestroyDevice(h->device, NULL);
    if (h->surface)
        vkDestroySurfaceKHR(h->instance, h->surface, NULL);
    vkDestroyInstance(h->instance, NULL);
    if (h->window) {
        SDL_DestroyWindow(h->window);
    }
    SDL_QuitSubSystem(h->sdl_flags);
}

struct AsmUiDesktop {
    Host host;
    AsmUiDesktopConfig config;
    AsmUiFontSdfAtlas atlas;
    FontBank fonts;
    AsmUiHbShaper shaper;
    AsmUiContext context;
    AsmUiDrawCmd *draws, *expanded;
    AsmUiBatch* batches;
    AsmUiGlyphQuad* glyphs;
    uint64_t corpus_hash;
};
AsmUiDesktop* asmui_desktop_open(const AsmUiDesktopConfig* config) {
    AsmUiDesktop* d = calloc(1, sizeof(*d));
    if (!d)
        fail("Desktop allocation");
    d->config = *config;
    if (!d->config.max_nodes)
        d->config.max_nodes = 8192;
    if (!d->config.max_draws)
        d->config.max_draws = 32768;
    if (!d->config.font)
        d->config.font = asmui_font_find_desktop();
    if (!asmui_font_sdf_build(&d->atlas, d->config.font, 1, 64, config->texts,
                              config->text_count))
        fail("Desktop font atlas");
    if (d->atlas.missing_glyph_count)
        fail("The desktop font is missing required glyphs");
    init_host(&d->host, config->headless, config->width, config->height, &d->atlas, &d->config);
    d->fonts.base = &d->atlas;
    d->shaper = asmui_font_sdf_shaper(&d->atlas);
    d->shaper.lookup = font_lookup;
    d->shaper.lookup_user = &d->fonts;
    d->draws = calloc(d->config.max_draws, sizeof(*d->draws));
    d->expanded = calloc(d->config.max_draws, sizeof(*d->expanded));
    d->batches = calloc(d->config.max_draws, sizeof(*d->batches));
    d->glyphs = calloc(4096, sizeof(*d->glyphs));
    if (!d->draws || !d->expanded || !d->batches || !d->glyphs)
        fail("Desktop stream allocation");
    return d;
}
SDL_Window* asmui_desktop_window(AsmUiDesktop* d) { return d->host.window; }
void asmui_desktop_size(AsmUiDesktop* d, float* w, float* h) {
    *w = d->host.width;
    *h = d->host.height;
}
void asmui_desktop_resize(AsmUiDesktop* d) {
    if (!d->host.window)
        return;
    int w, h;
    SDL_GetWindowSizeInPixels(d->host.window, &w, &h);
    if (w > 0 && h > 0 && (w != (int)d->host.width || h != (int)d->host.height))
        create_size(&d->host, (uint32_t)w, (uint32_t)h);
}
static void canvas_fonts(AsmUiDesktop* d, const AsmUiCanvas* c) {
    uint64_t hash = 1469598103934665603ull;
    const char** texts = malloc(c->count * sizeof(*texts));
    if (!texts)
        fail("Font corpus allocation");
    size_t count = 0;
    for (uint32_t i = 0; i < c->count; i++)
        if (c->nodes[i].type == ASMUI_NODE_TEXT) {
            const char* s = c->nodes[i].payload;
            texts[count++] = s;
            do {
                hash = (hash ^ (unsigned char)*s) * 1099511628211ull;
            } while (*s++);
        }
    if (hash != d->corpus_hash) {
        if (d->fonts.count == INPUT_FONT_PAGES) {
            /* present() has already waited for the previous frame's fence.
             * Rebuild the current corpus instead of terminating long sessions. */
            for (unsigned i = 0; i < d->fonts.count; i++) {
                asmui_font_sdf_destroy(&d->fonts.pages[i]);
                asmui_vk_texture_destroy(d->host.device, &d->host.input_fonts[i]);
            }
            d->fonts.count = 0;
            fprintf(stderr, "ASMUI fonts: recycled %u input pages\n", INPUT_FONT_PAGES);
        }
        AsmUiFontSdfAtlas* a = &d->fonts.pages[d->fonts.count];
        unsigned resource = 2 + d->fonts.count;
        if (asmui_font_sdf_build_missing(a, d->config.font, resource, 64, texts, count,
                                         font_lookup, &d->fonts)) {
            if (a->missing_glyph_count)
                fprintf(stderr,
                        "Desktop font: %u missing input glyphs; using replacement glyphs\n",
                        a->missing_glyph_count);
            AsmUiVkTexture* t = &d->host.input_fonts[d->fonts.count];
            if (!asmui_vk_texture_upload_rgba8(d->host.physical, d->host.device, d->host.queue,
                                               d->host.pool, a->rgba, a->width, a->height, t) ||
                !(d->host.backend.texture_sets[resource]
                      ? asmui_vk_update_texture_set(&d->host.backend, resource, t->view,
                                                    d->host.sampler,
                                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                      : asmui_vk_alloc_texture_set(&d->host.backend, d->host.descriptors,
                                                   resource, t->view, d->host.sampler,
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)))
                fail("Desktop glyph upload");
            d->fonts.count++;
        }
        d->corpus_hash = hash;
    }
    free(texts);
}
int asmui_desktop_present(AsmUiDesktop* d, AsmUiCanvas* c, float dt, const char* path) {
    if (c->overflow || c->count > d->config.max_nodes)
        fail("Canvas capacity exceeded");
    VK(vkWaitForFences(d->host.device, 1, &d->host.fence, VK_TRUE, UINT64_MAX));
    canvas_fonts(d, c);
    asm_ui_init(&d->context, c->nodes, c->capacity, d->draws, d->config.max_draws, NULL, 0);
    d->context.node_count = c->count;
    asm_ui_set_viewport(&d->context, c->width, c->height);
    asm_ui_begin_frame(&d->context, dt);
    asm_ui_layout(&d->context);
    asm_ui_build_drawlist(&d->context);
    AsmUiContext render = d->context;
    render.draws = d->expanded;
    render.draw_count = asmui_expand_text_stream(&d->context, d->expanded, d->config.max_draws,
                                                 asmui_hb_shape, &d->shaper, d->glyphs, 4096);
    if (!render.draw_count || render.draw_count == d->config.max_draws)
        fail("Desktop draw capacity exceeded");
    unsigned batches = asm_ui_build_batches(&render, d->batches, d->config.max_draws);
    int result = draw(&d->host, &render, d->batches, batches, path != NULL);
    /* SUBOPTIMAL follows a submitted frame: its readback is valid. OUT_OF_DATE
     * at acquire has no frame. In both cases recreate even when the SDL size
     * is unchanged, since surface configuration can change independently. */
    if (result >= 0 && path)
        capture(&d->host, path);
    if (result <= 0 && d->host.window) {
        int w, h;
        SDL_GetWindowSizeInPixels(d->host.window, &w, &h);
        if (w > 0 && h > 0)
            create_size(&d->host, (uint32_t)w, (uint32_t)h);
    }
    return result >= 0;
}
void asmui_desktop_close(AsmUiDesktop* d) {
    if (!d)
        return;
    destroy_host(&d->host);
    for (unsigned i = 0; i < d->fonts.count; i++)
        asmui_font_sdf_destroy(&d->fonts.pages[i]);
    asmui_font_sdf_destroy(&d->atlas);
    free(d->draws);
    free(d->expanded);
    free(d->batches);
    free(d->glyphs);
    free(d);
}
