#include "asmui.h"
#include "asmui_builder.h"
#include "asmui_vulkan.h"
#include "asmui_vulkan_texture.h"
#include "asmui_text.h"
#include "asmui_text_hb.h"
#include "asmui_font_sdf_ft.h"
#include "asmui_image_png.h"
#include "asmui_interaction.h"
#include "asmui_asset_cache.h"
#include "demo/song_select_scene_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>



#define ASMUI_LIVE_JACKET_CACHE_CAP 12u
#define ASMUI_LIVE_JACKET_RESOURCE_BASE 16u

typedef struct LiveJacketAsset {
    AsmUiVkTexture texture;
    AsmUiVkTextureUpload upload;
    uint64_t pending_key;
    uint32_t width;
    uint32_t height;
    int descriptor_allocated;
} LiveJacketAsset;

typedef struct LiveJacketLoader {
    const char *directory;
    uint32_t repeat_modulo;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    VkCommandPool command_pool;
    VkDescriptorPool descriptor_pool;
    VkSampler sampler;
    AsmUiVkBackend *backend;
    LiveJacketAsset *assets;
} LiveJacketLoader;

static int live_jacket_load(void *user, uint32_t slot, uint64_t key,
                            uint32_t resource_id, uint64_t old_key, int had_old) {
    LiveJacketLoader *l = (LiveJacketLoader *)user;
    if (!l || !l->directory || !l->assets || slot >= ASMUI_LIVE_JACKET_CACHE_CAP)
        return ASMUI_ASSET_LOADER_FAILED;
    LiveJacketAsset *a = &l->assets[slot];
    if (a->upload.submitted) return ASMUI_ASSET_LOADER_FAILED;
    if (had_old) asmui_vk_texture_destroy(l->device, &a->texture);
    (void)old_key;

    uint64_t file_key = l->repeat_modulo ? key % l->repeat_modulo : key;
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%llu.png", l->directory,
                     (unsigned long long)file_key);
    if (n <= 0 || (size_t)n >= sizeof(path)) return ASMUI_ASSET_LOADER_FAILED;

    AsmUiImageRgba8 image = {0};
    if (!asmui_image_png_load(path, &image)) return ASMUI_ASSET_LOADER_FAILED;
    a->width = image.width;
    a->height = image.height;
    a->pending_key = key;
    int ok = asmui_vk_texture_upload_rgba8_async_begin(
        l->physical, l->device, l->queue, l->command_pool,
        image.pixels, image.width, image.height, &a->upload);
    asmui_image_rgba8_destroy(&image);
    if (!ok) {
        a->width = a->height = 0u;
        a->pending_key = 0u;
        return ASMUI_ASSET_LOADER_FAILED;
    }
    (void)resource_id;
    return ASMUI_ASSET_LOADER_PENDING;
}

static void live_jacket_poll_uploads(AsmUiAssetCache *cache, LiveJacketLoader *l) {
    if (!cache || !l || !l->assets) return;
    for (uint32_t slot = 0; slot < cache->capacity; ++slot) {
        AsmUiAssetCacheEntry *e = &cache->entries[slot];
        if (e->state != ASMUI_ASSET_PENDING) continue;
        LiveJacketAsset *a = &l->assets[slot];
        if (!a->upload.submitted) {
            (void)asmui_asset_cache_complete(cache, slot, e->key, 0);
            continue;
        }
        AsmUiVkTexture completed = {0};
        int pr = asmui_vk_texture_upload_async_poll(&a->upload, &completed);
        if (pr == 0) continue;
        if (pr < 0) {
            asmui_vk_texture_upload_async_shutdown(&a->upload);
            a->width = a->height = 0u;
            a->pending_key = 0u;
            (void)asmui_asset_cache_complete(cache, slot, e->key, 0);
            continue;
        }
        a->texture = completed;
        int ok = a->descriptor_allocated
               ? asmui_vk_update_texture_set(l->backend, e->resource_id, a->texture.view,
                                             l->sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
               : asmui_vk_alloc_texture_set(l->backend, l->descriptor_pool, e->resource_id,
                                            a->texture.view, l->sampler,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (ok) a->descriptor_allocated = 1;
        else {
            asmui_vk_texture_destroy(l->device, &a->texture);
            a->width = a->height = 0u;
        }
        a->pending_key = 0u;
        (void)asmui_asset_cache_complete(cache, slot, e->key, ok);
    }
}

static void live_jacket_apply_node(AsmUiNode *node,
                                   uint32_t resource_id,
                                   const LiveJacketAsset *asset,
                                   float box_w, float box_h,
                                   uint32_t mode) {
    if (!node || !asset || !asset->width || !asset->height) return;
    AsmUiImagePlacement p;
    if (!asmui_image_place((float)asset->width, (float)asset->height,
                           box_w, box_h, mode, &p)) return;
    node->flags |= ASMUI_VISIBLE;
    node->resource_id = resource_id;
    asmui_apply_image_placement(node, &p);
}

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


static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, NULL);
    if (!n) { VkSurfaceFormatKHR z = {0}; return z; }
    VkSurfaceFormatKHR *f = calloc(n, sizeof(*f));
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, f);
    VkSurfaceFormatKHR out = f[0];
    for (uint32_t i = 0; i < n; ++i) {
        if ((f[i].format == VK_FORMAT_B8G8R8A8_UNORM || f[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { out = f[i]; break; }
    }
    free(f);
    return out;
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, NULL);
    VkPresentModeKHR *m = n ? calloc(n, sizeof(*m)) : NULL;
    if (n) vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, m);
    VkPresentModeKHR out = VK_PRESENT_MODE_FIFO_KHR;
    const char *env = getenv("ASMUI_PRESENT_MODE");
    if (env && strcmp(env, "mailbox") == 0) {
        for (uint32_t i = 0; i < n; ++i) if (m[i] == VK_PRESENT_MODE_MAILBOX_KHR) out = m[i];
    }
    free(m);
    return out;
}

static VkCompositeAlphaFlagBitsKHR choose_composite_alpha(VkCompositeAlphaFlagsKHR f) {
    const VkCompositeAlphaFlagBitsKHR pref[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (size_t i = 0; i < sizeof(pref)/sizeof(pref[0]); ++i) if (f & pref[i]) return pref[i];
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static uint64_t now_ns(void) {
    return SDL_GetTicksNS();
}

static int cmp_u64_live(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

#define ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT 3u

typedef struct LiveFrame {
    VkCommandBuffer cmd;
    VkSemaphore image_available;
    VkFence fence;
    VkQueryPool query_pool;
    int has_query;
    int targets_initialized;

    AsmUiVkRenderTarget *source;
    AsmUiVkRenderTarget *extract;
    AsmUiVkRenderTarget *ping;
    AsmUiVkRenderTarget *pong;
    VkDescriptorSet source_set;
    VkDescriptorSet extract_set;
    VkDescriptorSet ping_set;
    VkDescriptorSet pong_set;
} LiveFrame;

static uint32_t choose_frames_in_flight(uint32_t swap_images) {
    uint32_t n = ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT;
    const char *env = getenv("ASMUI_FRAMES_IN_FLIGHT");
    if (env && *env) {
        unsigned long v = strtoul(env, NULL, 10);
        if (v >= 1ul && v <= ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT) n = (uint32_t)v;
    }
    if (swap_images && n > swap_images) n = swap_images;
    if (!n) n = 1u;
    return n;
}

static void accumulate_gpu_queries(VkDevice device,
                                   LiveFrame *f,
                                   double timestamp_us,
                                   double gpu_acc[5],
                                   uint32_t *gpu_samples) {
    if (!f->query_pool || !f->has_query) return;
    uint64_t q[10] = {0};
    VkResult r = vkGetQueryPoolResults(device, f->query_pool, 0, 10, sizeof(q), q,
                                       sizeof(uint64_t),
                                       VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (r == VK_SUCCESS) {
        for (int i = 0; i < 5; ++i)
            gpu_acc[i] += (double)(q[i * 2 + 1] - q[i * 2]) * timestamp_us;
        ++*gpu_samples;
    }
    f->has_query = 0;
}

static void print_quantiles(const char *name, uint64_t *samples, uint32_t count) {
    if (!count) return;
    qsort(samples, count, sizeof(*samples), cmp_u64_live);
    uint32_t i50 = count / 2u;
    uint32_t i95 = (count * 95u) / 100u;
    uint32_t i99 = (count * 99u) / 100u;
    if (i95 >= count) i95 = count - 1u;
    if (i99 >= count) i99 = count - 1u;
    printf("%s p50/p95/p99=%.2f/%.2f/%.2f us (%u samples)\n",
           name, samples[i50] / 1000.0, samples[i95] / 1000.0,
           samples[i99] / 1000.0, count);
}

int main(int argc, char **argv) {
    uint32_t frame_limit = 0;
    int stream_bench_mode = 0;
    const char *stream_bench_env = getenv("ASMUI_STREAM_BENCH");
    if (stream_bench_env && *stream_bench_env && strcmp(stream_bench_env, "0") != 0)
        stream_bench_mode = 1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frame_limit = (uint32_t)strtoul(argv[++i], NULL, 10);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window *window = SDL_CreateWindow("ASMUI v0.16 - Live Song Select",
                                          (int)ASMUI_SONG_W, (int)ASMUI_SONG_H,
                                          SDL_WINDOW_VULKAN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit(); return 2;
    }

    Uint32 ext_count = 0;
    const char * const *exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    if (!exts) {
        fprintf(stderr, "SDL_Vulkan_GetInstanceExtensions failed: %s\n", SDL_GetError());
        return 2;
    }
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "asmui-live-song-select",
        .applicationVersion = VK_MAKE_VERSION(0, 16, 0),
        .pEngineName = "ASMUI",
        .engineVersion = VK_MAKE_VERSION(0, 16, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
        .enabledExtensionCount = ext_count,
        .ppEnabledExtensionNames = exts,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) die_vk("vkCreateInstance", vr);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window, instance, NULL, &surface)) {
        fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return 3;
    }

    uint32_t devn = 0;
    vkEnumeratePhysicalDevices(instance, &devn, NULL);
    VkPhysicalDevice *devs = calloc(devn, sizeof(*devs));
    vkEnumeratePhysicalDevices(instance, &devn, devs);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t queue_family = UINT32_MAX, timestamp_valid_bits = 0;
    int best = -1;
    VkPhysicalDeviceProperties props = {0};
    for (uint32_t d = 0; d < devn; ++d) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[d], &p);
        if (VK_API_VERSION_MAJOR(p.apiVersion) < 1 ||
            (VK_API_VERSION_MAJOR(p.apiVersion) == 1 && VK_API_VERSION_MINOR(p.apiVersion) < 3)) continue;
        VkPhysicalDeviceVulkan13Features f13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f13 };
        vkGetPhysicalDeviceFeatures2(devs[d], &f2);
        if (!f13.dynamicRendering) continue;
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, NULL);
        VkQueueFamilyProperties *qps = calloc(qn, sizeof(*qps));
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, qps);
        for (uint32_t q = 0; q < qn; ++q) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], q, surface, &present);
            if ((qps[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qps[q].queueCount && present) {
                int score = (p.vendorID == 0x8086u ? 100 : 0) +
                            (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 20 : 0);
                if (score > best) {
                    best = score; physical = devs[d]; queue_family = q;
                    timestamp_valid_bits = qps[q].timestampValidBits; props = p;
                }
                break;
            }
        }
        free(qps);
    }
    free(devs);
    if (!physical) { fprintf(stderr, "No Vulkan 1.3 graphics+present device found.\n"); return 3; }
    printf("GPU: %s (vendor=0x%04x device=0x%04x)\n", props.deviceName, props.vendorID, props.deviceID);

    const char *dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
        .enabledExtensionCount = 1, .ppEnabledExtensionNames = dev_exts,
    };
    VkDevice device = VK_NULL_HANDLE;
    if ((vr = vkCreateDevice(physical, &dci, NULL, &device)) != VK_SUCCESS) die_vk("vkCreateDevice", vr);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    VkSurfaceFormatKHR sf = choose_surface_format(physical, surface);
    if (sf.format == VK_FORMAT_UNDEFINED) { fprintf(stderr, "No surface format.\n"); return 4; }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        int pw = ASMUI_SONG_W, ph = ASMUI_SONG_H;
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
        extent.width = (uint32_t)pw; extent.height = (uint32_t)ph;
        if (extent.width < caps.minImageExtent.width) extent.width = caps.minImageExtent.width;
        if (extent.height < caps.minImageExtent.height) extent.height = caps.minImageExtent.height;
        if (extent.width > caps.maxImageExtent.width) extent.width = caps.maxImageExtent.width;
        if (extent.height > caps.maxImageExtent.height) extent.height = caps.maxImageExtent.height;
    }
    uint32_t image_count = caps.minImageCount + 1u;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    VkPresentModeKHR present_mode = choose_present_mode(physical, surface);
    VkSwapchainCreateInfoKHR sci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface, .minImageCount = image_count,
        .imageFormat = sf.format, .imageColorSpace = sf.colorSpace, .imageExtent = extent,
        .imageArrayLayers = 1, .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = choose_composite_alpha(caps.supportedCompositeAlpha),
        .presentMode = present_mode, .clipped = VK_TRUE,
    };
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    if ((vr = vkCreateSwapchainKHR(device, &sci, NULL, &swapchain)) != VK_SUCCESS) die_vk("vkCreateSwapchainKHR", vr);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL);
    VkImage *swap_images = calloc(image_count, sizeof(*swap_images));
    VkImageView *swap_views = calloc(image_count, sizeof(*swap_views));
    uint8_t *swap_initialized = calloc(image_count, 1);
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, swap_images);
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo vi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swap_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = sf.format,
            .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 },
        };
        if ((vr = vkCreateImageView(device, &vi, NULL, &swap_views[i])) != VK_SUCCESS) die_vk("vkCreateImageView(swap)", vr);
    }
    uint32_t frames_in_flight = choose_frames_in_flight(image_count);
    printf("swapchain: %ux%u images=%u format=%d present=%s frames-in-flight=%u\n",
           extent.width, extent.height, image_count, (int)sf.format,
           present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo",
           frames_in_flight);

    AsmUiVkBackend backend;
    if (!asmui_vk_init_streaming_ex(&backend, physical, device,
                                    ASMUI_SONG_MAX_NODES * 2u, frames_in_flight)) return 5;
    size_t uvs_n=0, ufs_n=0, pvs_n=0, pfs_n=0;
    uint32_t *uvs=read_file("build/shaders/ui.vert.spv",&uvs_n), *ufs=read_file("build/shaders/ui.frag.spv",&ufs_n);
    uint32_t *pvs=read_file("build/shaders/post.vert.spv",&pvs_n), *pfs=read_file("build/shaders/post.frag.spv",&pfs_n);
    if (!uvs || !ufs || !pvs || !pfs) { fprintf(stderr, "Run make shaders first.\n"); return 5; }
    if (!asmui_vk_create_pipeline_pack(&backend, sf.format, uvs, uvs_n, ufs, ufs_n)) return 5;
    AsmUiVkPostBackend post;
    if (!asmui_vk_post_init(&post, device, sf.format, pvs, pvs_n, pfs, pfs_n)) return 5;
    free(uvs); free(ufs); free(pvs); free(pfs);

    VkDescriptorPoolSize dps = { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 96 };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 96, .poolSizeCount = 1, .pPoolSizes = &dps };
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    if ((vr = vkCreateDescriptorPool(device, &dpci, NULL, &descriptor_pool)) != VK_SUCCESS) die_vk("vkCreateDescriptorPool", vr);
    VkSamplerCreateInfo sampci = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f,
    };
    VkSampler sampler = VK_NULL_HANDLE;
    if ((vr = vkCreateSampler(device, &sampci, NULL, &sampler)) != VK_SUCCESS) die_vk("vkCreateSampler", vr);

    /* v0.12: build only the glyphs needed by the scene.  If no suitable font
       is installed, the demo falls back to the deterministic 5x7 scene. */
    AsmUiFontSdfAtlas font_atlas = {0};
    AsmUiHbShaper font_shaper = {0};
    AsmUiVkTexture font_texture = {0};
    AsmUiImageRgba8 jacket_image = {0};
    AsmUiVkTexture jacket_texture = {0};
    int real_text = 0;
    int real_jacket = 0;
    uint32_t jacket_aspect_mode = ASMUI_ASPECT_COVER;
    AsmUiImagePlacement jacket_placement = {0};
    AsmUiAssetCacheEntry jacket_cache_entries[ASMUI_LIVE_JACKET_CACHE_CAP];
    AsmUiAssetCache jacket_cache;
    LiveJacketAsset jacket_assets[ASMUI_LIVE_JACKET_CACHE_CAP];
    memset(jacket_assets, 0, sizeof(jacket_assets));
    LiveJacketLoader jacket_loader;
    memset(&jacket_loader, 0, sizeof(jacket_loader));
    int stream_jackets = 0;
    const char *font_path = asmui_font_find_default();
    if (font_path) {
        size_t sample_count = 0;
        const char *const *samples = asmui_song_text_samples(&sample_count);
        if (asmui_font_sdf_build(&font_atlas, font_path, 1u, 64u, samples, sample_count)) {
            font_shaper = asmui_font_sdf_shaper(&font_atlas);
            real_text = 1;
        }
    }

    VkImage white_image = VK_NULL_HANDLE; VkDeviceMemory white_memory = VK_NULL_HANDLE; VkImageView white_view = VK_NULL_HANDLE;
    if (!create_image(physical, device, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      &white_image, &white_memory, &white_view)) return 6;
    if (!asmui_vk_alloc_texture_set(&backend, descriptor_pool, 0, white_view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) return 6;
    asmui_vk_set_white_texture_set(&backend, backend.texture_sets[0]);

    const uint32_t W = extent.width, H = extent.height;
    const uint32_t GW = W > 1 ? W / 2u : 1u;
    const uint32_t GH = H > 1 ? H / 2u : 1u;

    AsmUiVkTargetPool target_pool;
    asmui_vk_target_pool_init(&target_pool, physical, device);

    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };
    VkCommandPool command_pool = VK_NULL_HANDLE;
    if ((vr = vkCreateCommandPool(device, &cpci, NULL, &command_pool)) != VK_SUCCESS)
        die_vk("vkCreateCommandPool", vr);

    if (real_text) {
        if (!asmui_vk_texture_upload_rgba8(physical, device, queue, command_pool,
                                            font_atlas.rgba, font_atlas.width,
                                            font_atlas.height, &font_texture) ||
            !asmui_vk_alloc_texture_set(&backend, descriptor_pool, font_atlas.texture_id,
                                         font_texture.view, sampler,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
            fprintf(stderr, "real SDF font upload failed; using 5x7 fallback\n");
            asmui_vk_texture_destroy(device, &font_texture);
            asmui_font_sdf_destroy(&font_atlas);
            memset(&font_shaper, 0, sizeof(font_shaper));
            real_text = 0;
        }
    }

    const char *jacket_path = getenv("ASMUI_JACKET");
    const char *jacket_mode_env = getenv("ASMUI_JACKET_MODE");
    if (jacket_mode_env && strcmp(jacket_mode_env, "contain") == 0) jacket_aspect_mode = ASMUI_ASPECT_CONTAIN;
    else if (jacket_mode_env && strcmp(jacket_mode_env, "stretch") == 0) jacket_aspect_mode = ASMUI_ASPECT_STRETCH;
    if (jacket_path && *jacket_path) {
        if (asmui_image_png_load(jacket_path, &jacket_image) &&
            asmui_vk_texture_upload_rgba8(physical, device, queue, command_pool,
                                          jacket_image.pixels, jacket_image.width,
                                          jacket_image.height, &jacket_texture) &&
            asmui_vk_alloc_texture_set(&backend, descriptor_pool, 2u,
                                       jacket_texture.view, sampler,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)) {
            real_jacket = 1;
        } else {
            fprintf(stderr, "jacket PNG load/upload failed: %s; using procedural artwork\n", jacket_path);
            asmui_vk_texture_destroy(device, &jacket_texture);
            asmui_image_rgba8_destroy(&jacket_image);
        }
    }

    const char *jacket_dir = getenv("ASMUI_JACKET_DIR");
    uint32_t jacket_repeat = 0u;
    const char *jacket_repeat_env = getenv("ASMUI_JACKET_REPEAT");
    if (jacket_repeat_env && *jacket_repeat_env) {
        unsigned long n = strtoul(jacket_repeat_env, NULL, 10);
        if (n > 0ul && n <= 1000000ul) jacket_repeat = (uint32_t)n;
    }
    uint32_t jacket_load_budget = 1u;
    const char *jacket_budget_env = getenv("ASMUI_ASSET_LOADS_PER_FRAME");
    if (jacket_budget_env && *jacket_budget_env) {
        unsigned long n = strtoul(jacket_budget_env, NULL, 10);
        if (n > 0ul && n <= 8ul) jacket_load_budget = (uint32_t)n;
    }
    if (jacket_dir && *jacket_dir) {
        asmui_asset_cache_init(&jacket_cache, jacket_cache_entries,
                               ASMUI_LIVE_JACKET_CACHE_CAP,
                               ASMUI_LIVE_JACKET_RESOURCE_BASE,
                               frames_in_flight, jacket_load_budget);
        jacket_loader.directory = jacket_dir;
        jacket_loader.repeat_modulo = jacket_repeat;
        jacket_loader.physical = physical;
        jacket_loader.device = device;
        jacket_loader.queue = queue;
        jacket_loader.command_pool = command_pool;
        jacket_loader.descriptor_pool = descriptor_pool;
        jacket_loader.sampler = sampler;
        jacket_loader.backend = &backend;
        jacket_loader.assets = jacket_assets;
        stream_jackets = 1;
    }

    LiveFrame frames[ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT];
    memset(frames, 0, sizeof(frames));
    VkCommandBuffer cmds[ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT] = {0};
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = frames_in_flight,
    };
    if ((vr = vkAllocateCommandBuffers(device, &cbai, cmds)) != VK_SUCCESS)
        die_vk("vkAllocateCommandBuffers", vr);

    VkSemaphoreCreateInfo semci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (uint32_t i = 0; i < frames_in_flight; ++i) {
        LiveFrame *f = &frames[i];
        f->cmd = cmds[i];
        if ((vr = vkCreateSemaphore(device, &semci, NULL, &f->image_available)) != VK_SUCCESS)
            die_vk("vkCreateSemaphore(image_available)", vr);
        if ((vr = vkCreateFence(device, &fci, NULL, &f->fence)) != VK_SUCCESS)
            die_vk("vkCreateFence", vr);
        if (timestamp_valid_bits) {
            VkQueryPoolCreateInfo qpi = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = 10,
            };
            if ((vr = vkCreateQueryPool(device, &qpi, NULL, &f->query_pool)) != VK_SUCCESS)
                die_vk("vkCreateQueryPool", vr);
        }

        f->source = asmui_vk_target_acquire(&target_pool, W, H, sf.format);
        f->extract = asmui_vk_target_acquire(&target_pool, GW, GH, sf.format);
        f->ping = asmui_vk_target_acquire(&target_pool, GW, GH, sf.format);
        f->pong = asmui_vk_target_acquire(&target_pool, GW, GH, sf.format);
        if (!f->source || !f->extract || !f->ping || !f->pong) {
            fprintf(stderr, "render-target pool exhausted for frame slot %u\n", i);
            return 6;
        }
        if (!asmui_vk_post_alloc_texture_set(&post, descriptor_pool, f->source->view, sampler,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &f->source_set) ||
            !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, f->extract->view, sampler,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &f->extract_set) ||
            !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, f->ping->view, sampler,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &f->ping_set) ||
            !asmui_vk_post_alloc_texture_set(&post, descriptor_pool, f->pong->view, sampler,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &f->pong_set)) {
            fprintf(stderr, "post descriptor allocation failed for frame slot %u\n", i);
            return 6;
        }
    }

    /* Presentation waits may outlive the graphics fence. Keep render-finished
       semaphores per swapchain image so a semaphore is only reused after that
       image is acquired again. */
    VkSemaphore *render_done_by_image = calloc(image_count, sizeof(*render_done_by_image));
    if (!render_done_by_image) return 7;
    for (uint32_t i = 0; i < image_count; ++i) {
        if ((vr = vkCreateSemaphore(device, &semci, NULL, &render_done_by_image[i])) != VK_SUCCESS)
            die_vk("vkCreateSemaphore(render_done_by_image)", vr);
    }

    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    /* Initialize the white texture once before any frame overlaps. */
    VkCommandBuffer init_cmd = frames[0].cmd;
    if ((vr = vkBeginCommandBuffer(init_cmd, &cbbi)) != VK_SUCCESS)
        die_vk("vkBeginCommandBuffer(init)", vr);
    image_barrier(init_cmd, white_image,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT);
    VkClearColorValue white = { .float32 = {1, 1, 1, 1} };
    VkImageSubresourceRange wr = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = 1,
        .layerCount = 1,
    };
    vkCmdClearColorImage(init_cmd, white_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &white, 1, &wr);
    image_barrier(init_cmd, white_image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    if ((vr = vkEndCommandBuffer(init_cmd)) != VK_SUCCESS) die_vk("vkEndCommandBuffer(init)", vr);
    VkSubmitInfo init_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &init_cmd,
    };
    if ((vr = vkQueueSubmit(queue, 1, &init_submit, VK_NULL_HANDLE)) != VK_SUCCESS)
        die_vk("vkQueueSubmit(init)", vr);
    vkQueueWaitIdle(queue);
    vkResetCommandBuffer(init_cmd, 0);

    static AsmUiNode nodes[ASMUI_SONG_MAX_NODES];
    static AsmUiDrawCmd raw_draws[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiDrawCmd expanded_draws[ASMUI_SONG_MAX_NODES * 2u];
    static AsmUiGlyphQuad glyph_scratch[2048];
    static AsmUiTween tweens[16];
    static AsmUiBatch batches[ASMUI_SONG_MAX_NODES * 2u];
    AsmUiSongSceneInfo scene;
    AsmUiSongTextSceneInfo text_scene = {0};
    int has_text_scene = 0;
    uint32_t scene_text_nodes = 0;
    uint32_t carousel_count = 0;
    uint32_t carousel_roots[ASMUI_SONG_CAROUSEL_COUNT];
    for (uint32_t i = 0; i < ASMUI_SONG_CAROUSEL_COUNT; ++i) carousel_roots[i] = UINT32_MAX;
    if (real_text) {
        text_scene = asmui_song_select_build_text(
            nodes, ASMUI_SONG_MAX_NODES, font_atlas.px_range);
        has_text_scene = 1;
        scene = text_scene.base;
        scene_text_nodes = text_scene.text_node_count;
        carousel_count = text_scene.carousel_count;
        for (uint32_t i = 0; i < carousel_count; ++i) carousel_roots[i] = text_scene.carousel_root[i];
    } else {
        scene = asmui_song_select_build(nodes, ASMUI_SONG_MAX_NODES);
    }
    if (real_jacket && scene.jacket_image_node < scene.node_count) {
        AsmUiNode *jacket_node = &nodes[scene.jacket_image_node];
        jacket_node->flags |= ASMUI_VISIBLE;
        jacket_node->resource_id = 2u;
        if (asmui_image_place((float)jacket_image.width, (float)jacket_image.height,
                              320.0f, 320.0f, jacket_aspect_mode, &jacket_placement)) {
            asmui_apply_image_placement(jacket_node, &jacket_placement);
        }
    }
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, ASMUI_SONG_MAX_NODES, raw_draws, ASMUI_SONG_MAX_NODES * 2u,
                tweens, 16u);
    asm_ui_set_viewport(&ctx, (float)W, (float)H);
    ctx.node_count = scene.node_count;

    uint32_t sample_cap = frame_limit ? frame_limit : 4096u;
    if (sample_cap > 20000u) sample_cap = 20000u;
    uint64_t *wait_samples = calloc(sample_cap, sizeof(*wait_samples));
    uint64_t *acquire_samples = calloc(sample_cap, sizeof(*acquire_samples));
    uint64_t *encode_samples = calloc(sample_cap, sizeof(*encode_samples));
    uint64_t *present_samples = calloc(sample_cap, sizeof(*present_samples));
    uint64_t *total_samples = calloc(sample_cap, sizeof(*total_samples));
    if (!wait_samples || !acquire_samples || !encode_samples || !present_samples || !total_samples)
        return 7;
    uint32_t sample_count = 0;
    double gpu_acc[5] = {0};
    uint32_t gpu_samples = 0;
    const double timestamp_us = (double)props.limits.timestampPeriod / 1000.0;
    uint64_t start_wall = now_ns();
    uint64_t last_tick = start_wall;
    uint32_t frame = 0;
    int running = 1;
    int selected_nav = 2;
    AsmUiSnapScroll nav_scroll;
    asmui_scroll_init(&nav_scroll, 2.0f, 0.0f, 4.0f, 110.0f);
    uint32_t logical_song_count = carousel_count;
    const char *virtual_env = getenv("ASMUI_VIRTUAL_SONGS");
    if (has_text_scene) {
        logical_song_count = 256u;
        if (virtual_env && *virtual_env) {
            unsigned long n = strtoul(virtual_env, NULL, 10);
            if (n >= carousel_count && n <= 1000000ul) logical_song_count = (uint32_t)n;
        }
    }
    AsmUiVirtualCarousel song_carousel;
    asmui_virtual_carousel_init(&song_carousel, logical_song_count,
                                logical_song_count ? logical_song_count / 2u : 0u,
                                carousel_count,
                                90.0f, -318.0f,
                                0.0f, 0.0f, 156.0f, 125.0f);
    if (has_text_scene &&
        !asmui_song_select_bind_logical(nodes, &text_scene, song_carousel.motion.selected)) {
        fprintf(stderr, "initial song detail binding failed\n");
        return 7;
    }
    if (real_text) {
        printf("text: FreeType SDF + HarfBuzz font=%s atlas=%ux%u glyphs=%u missing=%u text-nodes=%u\n",
               font_path, font_atlas.width, font_atlas.height, font_atlas.glyph_count,
               font_atlas.missing_glyph_count, scene_text_nodes);
    } else {
        puts("text: 5x7 fallback (set ASMUI_FONT=/path/to/font.ttf to force a font)");
    }
    if (real_jacket) {
        const char *mode_name = jacket_aspect_mode == ASMUI_ASPECT_CONTAIN ? "contain" :
                                (jacket_aspect_mode == ASMUI_ASPECT_STRETCH ? "stretch" : "cover");
        printf("jacket: PNG %s (%ux%u) resource=2 mode=%s draw=%.1fx%.1f uv=[%.3f %.3f %.3f %.3f]\n",
               jacket_path, jacket_image.width, jacket_image.height, mode_name,
               jacket_placement.width, jacket_placement.height, jacket_placement.u0,
               jacket_placement.v0, jacket_placement.u1, jacket_placement.v1);
    } else {
        puts("jacket: procedural fallback (set ASMUI_JACKET=/path/to/jacket.png)");
    }
    if (stream_jackets) {
        printf("jacket-stream: async-gpu dir=%s cache=%u submits/frame=%u repeat=%u resources=%u..%u\n",
               jacket_dir, ASMUI_LIVE_JACKET_CACHE_CAP, jacket_load_budget, jacket_repeat,
               ASMUI_LIVE_JACKET_RESOURCE_BASE,
               ASMUI_LIVE_JACKET_RESOURCE_BASE + ASMUI_LIVE_JACKET_CACHE_CAP - 1u);
    } else {
        puts("jacket-stream: disabled (set ASMUI_JACKET_DIR=/path/to/numbered/pngs)");
    }
    printf("controls: ESC quit | LEFT/RIGHT virtual song carousel | UP/DOWN nav selection | "
           "mouse click/drag/wheel carousel | logical-songs=%u retained-slots=%u | "
           "env ASMUI_VIRTUAL_SONGS=N ASMUI_PRESENT_MODE=mailbox ASMUI_FRAMES_IN_FLIGHT=1..%u\n",
           logical_song_count, carousel_count, ASMUI_LIVE_MAX_FRAMES_IN_FLIGHT);

    while (running && (!frame_limit || frame < frame_limit)) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = 0;
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) {
                if (ev.key.key == SDLK_ESCAPE) running = 0;
                else if (ev.key.key == SDLK_LEFT) {
                    asmui_virtual_carousel_nudge(&song_carousel, -1);
                } else if (ev.key.key == SDLK_RIGHT) {
                    asmui_virtual_carousel_nudge(&song_carousel, +1);
                } else if (ev.key.key == SDLK_UP) {
                    if (selected_nav > 0) --selected_nav;
                    asmui_scroll_set_target(&nav_scroll, (float)selected_nav);
                } else if (ev.key.key == SDLK_DOWN) {
                    if (selected_nav < 4) ++selected_nav;
                    asmui_scroll_set_target(&nav_scroll, (float)selected_nav);
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL_BUTTON_LEFT) {
                float cx = 0.0f, cy = 0.0f;
                asmui_pointer_to_canvas(ev.button.x, ev.button.y, (float)W, (float)H, &cx, &cy);
                int hit = asmui_virtual_carousel_hit_test(&song_carousel, cx, cy, 142.0f, 66.0f);
                if (hit >= 0) {
                    asmui_virtual_carousel_select(&song_carousel, (uint32_t)hit);
                    asmui_virtual_carousel_pointer_begin(&song_carousel, cx, ((double)SDL_GetTicksNS() / 1e9));
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION && song_carousel.motion.pointer_dragging) {
                float cx = 0.0f, cy = 0.0f;
                asmui_pointer_to_canvas(ev.motion.x, ev.motion.y, (float)W, (float)H, &cx, &cy);
                (void)cy;
                asmui_virtual_carousel_pointer_move(&song_carousel, cx, ((double)SDL_GetTicksNS() / 1e9));
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP && ev.button.button == SDL_BUTTON_LEFT &&
                song_carousel.motion.pointer_dragging) {
                asmui_virtual_carousel_pointer_end(&song_carousel);
            }
            if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                float wheel_y = ev.wheel.y;
                if (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) wheel_y = -wheel_y;
                asmui_virtual_carousel_wheel(&song_carousel, wheel_y);
            }
        }
        if (!running) break;

        LiveFrame *f = &frames[frame % frames_in_flight];
        uint64_t total0 = now_ns();
        uint64_t wait0 = total0;
        if ((vr = vkWaitForFences(device, 1, &f->fence, VK_TRUE, UINT64_MAX)) != VK_SUCCESS)
            die_vk("vkWaitForFences", vr);
        uint64_t wait1 = now_ns();
        accumulate_gpu_queries(device, f, timestamp_us, gpu_acc, &gpu_samples);
        if (stream_jackets) {
            asmui_asset_cache_begin_epoch(&jacket_cache, frame);
            live_jacket_poll_uploads(&jacket_cache, &jacket_loader);
        }

        uint64_t acquire0 = now_ns();
        uint32_t image_index = 0;
        vr = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                   f->image_available, VK_NULL_HANDLE, &image_index);
        uint64_t acquire1 = now_ns();
        if (vr == VK_ERROR_OUT_OF_DATE_KHR) {
            puts("swapchain out of date; close/restart live demo after resize");
            break;
        }
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR)
            die_vk("vkAcquireNextImageKHR", vr);

        vkResetCommandBuffer(f->cmd, 0);
        uint64_t tick = now_ns();
        float dt = (float)((double)(tick - last_tick) / 1e9);
        last_tick = tick;
        if (!(dt > 0.0f) || dt > 0.1f) dt = 1.0f / 60.0f;

        uint64_t encode0 = now_ns();
        asm_ui_begin_frame(&ctx, dt);
        /* Bench mode deliberately moves the carousel so live-bench measures
           the animated path instead of 600 identical static frames. */
        if (frame_limit && frame > 0u) {
            if (stream_bench_mode && (frame % 30u) == 0u) {
                /* Walk forward aggressively so a finite benchmark exercises
                   pending uploads, completions and LRU eviction instead of
                   oscillating between the same two logical songs. */
                asmui_virtual_carousel_nudge(&song_carousel, +3);
            } else if (!stream_bench_mode && (frame % 120u) == 0u) {
                int dir = ((frame / 120u) & 1u) ? 1 : -1;
                asmui_virtual_carousel_nudge(&song_carousel, dir);
            }
        }
        asmui_scroll_step(&nav_scroll, dt);
        asmui_virtual_carousel_step(&song_carousel, dt);
        asmui_song_select_update(nodes, &scene, ctx.time, 0);
        /* Selection binding is intentionally allocation-free and runs after the
           generic demo animation so per-song gauge/accent values remain authoritative. */
        if (has_text_scene &&
            !asmui_song_select_bind_logical(nodes, &text_scene, song_carousel.motion.selected)) {
            fprintf(stderr, "song detail binding failed for %u\n", song_carousel.motion.selected);
            running = 0;
            break;
        }
        uint32_t logical_by_slot[ASMUI_SONG_CAROUSEL_COUNT] = {0};
        for (uint32_t i = 0; i < carousel_count; ++i) {
            if (carousel_roots[i] < scene.node_count) {
                uint32_t logical = 0;
                asmui_virtual_carousel_apply_slot(&song_carousel, i,
                                                  &nodes[carousel_roots[i]], &logical);
                logical_by_slot[i] = logical;
                if (has_text_scene &&
                    !asmui_song_carousel_bind_slot(nodes, &text_scene, i, logical)) {
                    fprintf(stderr, "virtual slot binding failed slot=%u logical=%u\n", i, logical);
                    running = 0;
                    break;
                }
                if (has_text_scene && text_scene.carousel_jacket[i] < scene.node_count)
                    nodes[text_scene.carousel_jacket[i]].flags &= ~ASMUI_VISIBLE;
            }
        }
        if (stream_jackets && has_text_scene) {
            static const uint32_t order[ASMUI_SONG_CAROUSEL_COUNT] = {3u,2u,4u,1u,5u,0u,6u};
            for (uint32_t oi = 0; oi < carousel_count; ++oi) {
                uint32_t i = order[oi];
                if (i >= carousel_count) continue;
                uint32_t resource_id = 0u, cache_slot = 0u;
                int ar = asmui_asset_cache_request(&jacket_cache, logical_by_slot[i],
                                                   live_jacket_load, &jacket_loader,
                                                   &resource_id, &cache_slot);
                if ((ar == ASMUI_ASSET_HIT || ar == ASMUI_ASSET_LOADED) &&
                    text_scene.carousel_jacket[i] < scene.node_count) {
                    live_jacket_apply_node(&nodes[text_scene.carousel_jacket[i]], resource_id,
                                           &jacket_assets[cache_slot], 42.0f, 42.0f,
                                           ASMUI_ASPECT_COVER);
                }
            }
            if (!real_jacket && text_scene.base.jacket_image_node < scene.node_count) {
                AsmUiNode *main_jacket = &nodes[text_scene.base.jacket_image_node];
                main_jacket->flags &= ~ASMUI_VISIBLE;
                uint32_t resource_id = 0u, cache_slot = 0u;
                int found = asmui_asset_cache_lookup(&jacket_cache, song_carousel.motion.selected,
                                                     &resource_id, &cache_slot);
                if (found == ASMUI_ASSET_HIT)
                    live_jacket_apply_node(main_jacket, resource_id, &jacket_assets[cache_slot],
                                           320.0f, 320.0f, jacket_aspect_mode);
            }
        }
        if (!running) break;
        if (scene.nav_selected_node < scene.node_count) {
            float nav_y = 170.0f - nav_scroll.position * 86.0f;
            nodes[scene.nav_selected_node].anchored_y = nav_y;
            if (scene.nav_selected_icon_node < scene.node_count) {
                nodes[scene.nav_selected_icon_node].anchored_x = nodes[scene.nav_selected_node].anchored_x;
                nodes[scene.nav_selected_icon_node].anchored_y = nav_y;
            }
        }
        asm_ui_tick_tweens(&ctx);
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);

        AsmUiContext render_ctx = ctx;
        if (real_text) {
            uint32_t expanded = asmui_expand_text_stream(
                &ctx, expanded_draws, ASMUI_SONG_MAX_NODES * 2u,
                asmui_hb_shape, &font_shaper, glyph_scratch,
                (uint32_t)(sizeof(glyph_scratch) / sizeof(glyph_scratch[0])));
            if (!expanded) {
                fprintf(stderr, "live text expansion failed\n");
                break;
            }
            render_ctx.draws = expanded_draws;
            render_ctx.draw_count = expanded;
            render_ctx.draw_capacity = ASMUI_SONG_MAX_NODES * 2u;
        }
        uint32_t batch_count = asm_ui_build_batches(&render_ctx, batches,
                                                     ASMUI_SONG_MAX_NODES * 2u);
        uint32_t verts = asmui_vk_upload_slot(&render_ctx, &backend, frame % frames_in_flight);
        if (!batch_count || verts != render_ctx.draw_count * 4u) {
            fprintf(stderr, "live stream mismatch\n");
            break;
        }

        if ((vr = vkBeginCommandBuffer(f->cmd, &cbbi)) != VK_SUCCESS)
            die_vk("vkBeginCommandBuffer", vr);
        if (f->query_pool) vkCmdResetQueryPool(f->cmd, f->query_pool, 0, 10);

        VkImageLayout rt_old = f->targets_initialized
                             ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                             : VK_IMAGE_LAYOUT_UNDEFINED;
        image_barrier(f->cmd, f->source->image, rt_old,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      f->targets_initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      f->targets_initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo a = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = f->source->view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { .float32 = {0, 0, 0, 1} } },
        };
        VkRenderingInfo ri = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .offset = {0, 0}, .extent = {W, H} },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &a,
        };
        if (f->query_pool)
            vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f->query_pool, 0);
        vkCmdBeginRendering(f->cmd, &ri);
        asmui_vk_record_images(f->cmd, &render_ctx, batches, batch_count, &backend, W, H);
        vkCmdEndRendering(f->cmd);
        if (f->query_pool)
            vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->query_pool, 1);
        image_barrier(f->cmd, f->source->image,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      VK_ACCESS_SHADER_READ_BIT);

        AsmUiVkRenderTarget *halfs[3] = {f->extract, f->ping, f->pong};
        VkDescriptorSet insets[3] = {f->source_set, f->extract_set, f->ping_set};
        for (int pass = 0; pass < 3; ++pass) {
            image_barrier(f->cmd, halfs[pass]->image, rt_old,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          f->targets_initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                 : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          f->targets_initialized ? VK_ACCESS_SHADER_READ_BIT : 0,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
            VkRenderingAttachmentInfo ha = a;
            ha.imageView = halfs[pass]->view;
            VkRenderingInfo hri = ri;
            hri.renderArea.extent.width = GW;
            hri.renderArea.extent.height = GH;
            hri.pColorAttachments = &ha;
            if (f->query_pool)
                vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    f->query_pool, 2u + (uint32_t)pass * 2u);
            vkCmdBeginRendering(f->cmd, &hri);
            if (pass == 0)
                asmui_vk_post_record(f->cmd, &post, insets[pass], GW, GH,
                                     ASMUI_POST_EXTRACT, 0, 0, 0.75f, 1.0f);
            else if (pass == 1)
                asmui_vk_post_record(f->cmd, &post, insets[pass], GW, GH,
                                     ASMUI_POST_BLUR, 1.0f / (float)GW, 0, 2.0f, 1.0f);
            else
                asmui_vk_post_record(f->cmd, &post, insets[pass], GW, GH,
                                     ASMUI_POST_BLUR, 0, 1.0f / (float)GH, 2.0f, 1.0f);
            vkCmdEndRendering(f->cmd);
            if (f->query_pool)
                vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    f->query_pool, 3u + (uint32_t)pass * 2u);
            image_barrier(f->cmd, halfs[pass]->image,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                          VK_ACCESS_SHADER_READ_BIT);
        }
        f->targets_initialized = 1;

        image_barrier(f->cmd, swap_images[image_index],
                      swap_initialized[image_index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                    : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkRenderingAttachmentInfo oa = a;
        oa.imageView = swap_views[image_index];
        VkRenderingInfo ori = ri;
        ori.pColorAttachments = &oa;
        if (f->query_pool)
            vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                f->query_pool, 8);
        vkCmdBeginRendering(f->cmd, &ori);
        asmui_vk_post_record(f->cmd, &post, f->source_set, W, H,
                             ASMUI_POST_COPY, 0, 0, 1, 1);
        asmui_vk_post_record_additive(f->cmd, &post, f->pong_set, W, H,
                                      ASMUI_POST_COPY, 0, 0, 1, 0.85f);
        vkCmdEndRendering(f->cmd);
        if (f->query_pool)
            vkCmdWriteTimestamp(f->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                f->query_pool, 9);
        image_barrier(f->cmd, swap_images[image_index],
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
        if ((vr = vkEndCommandBuffer(f->cmd)) != VK_SUCCESS)
            die_vk("vkEndCommandBuffer", vr);
        uint64_t encode1 = now_ns();

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &f->image_available,
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &f->cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_done_by_image[image_index],
        };
        vkResetFences(device, 1, &f->fence);
        uint64_t present0 = now_ns();
        if ((vr = vkQueueSubmit(queue, 1, &submit, f->fence)) != VK_SUCCESS)
            die_vk("vkQueueSubmit", vr);
        VkPresentInfoKHR pi = {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_done_by_image[image_index],
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &image_index,
        };
        vr = vkQueuePresentKHR(queue, &pi);
        uint64_t present1 = now_ns();
        if (vr == VK_ERROR_OUT_OF_DATE_KHR) {
            puts("swapchain out of date during present; exiting cleanly");
            break;
        }
        if (vr != VK_SUCCESS && vr != VK_SUBOPTIMAL_KHR)
            die_vk("vkQueuePresentKHR", vr);
        swap_initialized[image_index] = 1;
        f->has_query = f->query_pool != VK_NULL_HANDLE;

        uint64_t total1 = now_ns();
        if (sample_count < sample_cap) {
            wait_samples[sample_count] = wait1 - wait0;
            acquire_samples[sample_count] = acquire1 - acquire0;
            encode_samples[sample_count] = encode1 - encode0;
            present_samples[sample_count] = present1 - present0;
            total_samples[sample_count] = total1 - total0;
            ++sample_count;
        }
        ++frame;
    }

    vkDeviceWaitIdle(device);
    for (uint32_t i = 0; i < frames_in_flight; ++i)
        accumulate_gpu_queries(device, &frames[i], timestamp_us, gpu_acc, &gpu_samples);
    uint64_t end_wall = now_ns();

    print_quantiles("live CPU fence-wait", wait_samples, sample_count);
    print_quantiles("live CPU acquire", acquire_samples, sample_count);
    print_quantiles("live CPU encode", encode_samples, sample_count);
    print_quantiles("live CPU submit+present", present_samples, sample_count);
    print_quantiles("live CPU total-loop", total_samples, sample_count);
    if (gpu_samples) {
        printf("live GPU avg: base=%.2f extract=%.2f blurH=%.2f blurV=%.2f "
               "composite=%.2f total=%.2f us (%u frames)\n",
               gpu_acc[0] / gpu_samples, gpu_acc[1] / gpu_samples,
               gpu_acc[2] / gpu_samples, gpu_acc[3] / gpu_samples,
               gpu_acc[4] / gpu_samples,
               (gpu_acc[0] + gpu_acc[1] + gpu_acc[2] + gpu_acc[3] + gpu_acc[4]) / gpu_samples,
               gpu_samples);
    }
    double seconds = (double)(end_wall - start_wall) / 1e9;
    if (seconds > 0.0)
        printf("presented %u frames in %.2fs (wall %.1f fps, frames-in-flight=%u)\n",
               frame, seconds, (double)frame / seconds, frames_in_flight);

    free(total_samples);
    free(present_samples);
    free(encode_samples);
    free(acquire_samples);
    free(wait_samples);

    for (uint32_t i = 0; i < frames_in_flight; ++i) {
        LiveFrame *f = &frames[i];
        if (f->query_pool) vkDestroyQueryPool(device, f->query_pool, NULL);
        if (f->fence) vkDestroyFence(device, f->fence, NULL);
        if (f->image_available) vkDestroySemaphore(device, f->image_available, NULL);
    }
    for (uint32_t i = 0; i < image_count; ++i)
        if (render_done_by_image[i]) vkDestroySemaphore(device, render_done_by_image[i], NULL);
    free(render_done_by_image);
    /* Async upload command buffers belong to command_pool, so retire them
       before destroying the pool. vkDeviceWaitIdle above guarantees that no
       transfer is still executing. */
    for (uint32_t i = 0; i < ASMUI_LIVE_JACKET_CACHE_CAP; ++i) {
        asmui_vk_texture_upload_async_shutdown(&jacket_assets[i].upload);
        asmui_vk_texture_destroy(device, &jacket_assets[i].texture);
    }
    vkDestroyCommandPool(device, command_pool, NULL);
    asmui_vk_target_pool_destroy(&target_pool);
    vkDestroyImageView(device, white_view, NULL);
    vkDestroyImage(device, white_image, NULL);
    vkFreeMemory(device, white_memory, NULL);
    if (stream_jackets) {
        printf("jacket-stream stats: resident=%u pending=%u hits=%llu misses=%llu loads=%llu evictions=%llu deferred=%llu failures=%llu async-submits=%llu async-completions=%llu\n",
               asmui_asset_cache_resident(&jacket_cache),
               asmui_asset_cache_pending(&jacket_cache),
               (unsigned long long)jacket_cache.stats.hits,
               (unsigned long long)jacket_cache.stats.misses,
               (unsigned long long)jacket_cache.stats.loads,
               (unsigned long long)jacket_cache.stats.evictions,
               (unsigned long long)jacket_cache.stats.deferred,
               (unsigned long long)jacket_cache.stats.failures,
               (unsigned long long)jacket_cache.stats.pending_submits,
               (unsigned long long)jacket_cache.stats.pending_completions);
    }
    asmui_vk_texture_destroy(device, &jacket_texture);
    asmui_image_rgba8_destroy(&jacket_image);
    asmui_vk_texture_destroy(device, &font_texture);
    asmui_font_sdf_destroy(&font_atlas);
    vkDestroySampler(device, sampler, NULL);
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    asmui_vk_post_destroy(&post);
    asmui_vk_destroy_pipeline_pack(&backend);
    asmui_vk_destroy_streaming(&backend);
    for (uint32_t i = 0; i < image_count; ++i)
        vkDestroyImageView(device, swap_views[i], NULL);
    free(swap_initialized);
    free(swap_views);
    free(swap_images);
    vkDestroySwapchainKHR(device, swapchain, NULL);
    vkDestroyDevice(device, NULL);
    SDL_Vulkan_DestroySurface(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
