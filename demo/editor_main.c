#include "asmui_editor.h"
#include "asmui_application.h"
#include "application_sound.h"
#include "asmui_font_sdf_ft.h"
#include "asmui_image_png.h"
#include "asmui_text.h"
#include "asmui_vulkan.h"
#include "asmui_vulkan_texture.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <png.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_FONT_PAGES 64u
#define APP_IMAGE_BASE 196u
#define APP_IMAGE_LIMIT (ASMUI_VK_MAX_TEXTURES - APP_IMAGE_BASE)
typedef struct ApplicationImage {
    char *path;
    AsmUiVkTexture texture;
    uint64_t last_used;
} ApplicationImage;
typedef struct FontBank {
    AsmUiFontSdfAtlas *base;
    AsmUiFontSdfAtlas pages[INPUT_FONT_PAGES];
    uint32_t count;
} FontBank;
typedef struct ApplicationFonts {
    AsmUiFontSdfAtlas atlas[2];
    FontBank bank[2];
    AsmUiHbShaper shaper[3];
} ApplicationFonts;
typedef struct DialogBridge {
    SDL_Mutex *mutex;
    SDL_AtomicInt references, pending;
    uint32_t event_type;
    int active;
} DialogBridge;
typedef struct DialogRequest { DialogBridge *bridge; uint32_t kind; } DialogRequest;
static void dialog_release(DialogBridge *d) {
    if (SDL_AtomicDecRef(&d->references)) { SDL_DestroyMutex(d->mutex); free(d); }
}
static void SDLCALL file_dialog_result(void *user, const char *const *paths, int filter) {
    (void)filter;
    DialogRequest *request = user;
    DialogBridge *d = request->bridge;
    SDL_LockMutex(d->mutex);
    if (d->active) {
        SDL_Event event = {0};
        event.type = d->event_type;
        event.user.code = (int)request->kind | (paths ? 0 : 0x100);
        event.user.data1 = SDL_strdup(!paths ? SDL_GetError() : paths[0] ? paths[0] : "");
        if (!SDL_PushEvent(&event)) {
            SDL_free(event.user.data1);
            SDL_SetAtomicInt(&d->pending, 0);
        }
    }
    SDL_UnlockMutex(d->mutex);
    free(request);
    dialog_release(d);
}
static DialogBridge *dialog_create(void) {
    DialogBridge *d = calloc(1, sizeof(*d));
    if (!d) return NULL;
    d->mutex = SDL_CreateMutex();
    d->event_type = SDL_RegisterEvents(1);
    if (!d->mutex || !d->event_type) {
        if (d->mutex) SDL_DestroyMutex(d->mutex);
        free(d); return NULL;
    }
    d->active = 1;
    SDL_SetAtomicInt(&d->references, 1);
    return d;
}
static void dialog_close(DialogBridge *d) {
    if (!d) return;
    SDL_LockMutex(d->mutex);
    d->active = 0;
    SDL_UnlockMutex(d->mutex);
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, d->event_type, d->event_type) > 0)
        SDL_free(event.user.data1);
    dialog_release(d);
}
static void application_dialog(AsmUiAppHost *app, SDL_Window *window, DialogBridge *bridge) {
    if (!bridge || SDL_GetAtomicInt(&bridge->pending)) return;
    uint32_t kind = app->api->take_request(app->instance);
    if (!kind) return;
    static const SDL_DialogFileFilter filters[] = {
        {"ZIP", "zip"}, {"Audio", "ogg;mp3;wav"}, {"Jacket", "png;jpg;jpeg"},
        {"Chart", "json;sus;txt"}, {"2DMV", "mp4"}, {"ZIP", "zip"}};
    if (kind > 6) return;
    DialogRequest *request = malloc(sizeof(*request));
    if (!request) return;
    *request = (DialogRequest){bridge, kind};
    SDL_SetAtomicInt(&bridge->pending, 1);
    SDL_AtomicIncRef(&bridge->references);
    if (kind == 6)
        SDL_ShowSaveFileDialog(file_dialog_result, request, window, &filters[kind - 1], 1,
                               "OpenSekai.zip");
    else SDL_ShowOpenFileDialog(file_dialog_result, request, window, &filters[kind - 1], 1,
                                 NULL, false);
}
static void app_smoke_pointer(SDL_Window *window, const AsmUiAppHost *app,
                               float x, float y, uint32_t width, uint32_t height) {
    int ww, hh;
    SDL_GetWindowSize(window, &ww, &hh);
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = (app->canvas[0] + x * app->scale) * ww / width;
    event.button.y = (app->canvas[1] + y * app->scale) * hh / height;
    SDL_PushEvent(&event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    SDL_PushEvent(&event);
}
static void app_smoke_key(SDL_Window *window, SDL_Keycode key, SDL_Keymod modifiers) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(window);
    event.key.key = key;
    event.key.mod = modifiers;
    SDL_PushEvent(&event);
}
typedef struct ScriptedKey {
    double seconds;
    uint32_t key, down, click, music_time, capture, done;
    float x, y;
} ScriptedKey;
static void fail(const char *s);
static unsigned read_input_script(const char *path, ScriptedKey *keys, unsigned capacity) {
    FILE *file=fopen(path,"r");
    if (!file) fail("Cannot open application input script");
    unsigned count=0;
    double previous[2]={-1.,-1.};
    char line[256];
    while (fgets(line,sizeof(line),file)) {
        if (line[0]=='#' || line[0]=='\n') continue;
        ScriptedKey key={0};
        const char *command=line;
        if (!strncmp(command,"music ",6)) {key.music_time=1;command+=6;}
        int consumed=0;
        int valid=sscanf(command,"%lf capture %n",&key.seconds,&consumed)==1 && consumed>0;
        if (valid) key.capture=1;
        else valid=sscanf(command,"%lf click %f %f",&key.seconds,&key.x,&key.y)==3;
        if (valid && !key.capture) key.click=1;
        else if (!valid) valid=sscanf(command,"%lf %u %u",&key.seconds,&key.key,&key.down)==3;
        if (!valid || !isfinite(key.x) || !isfinite(key.y) ||
            !isfinite(key.seconds) || key.seconds<0 || key.down>1 || count==capacity ||
            key.seconds<previous[key.music_time]) fail("Invalid application input script");
        previous[key.music_time]=key.seconds;
        keys[count++]=key;
    }
    fclose(file);
    return count;
}

typedef struct Host {
    SDL_Window *window;
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
    AsmUiVkRenderTarget *target;
    VkDescriptorPool descriptors;
    VkSampler sampler;
    AsmUiVkTexture font_texture, white_texture;
    AsmUiVkTexture input_fonts[INPUT_FONT_PAGES];
    AsmUiVkTexture application_fonts[2][INPUT_FONT_PAGES + 1];
    ApplicationImage application_images[APP_IMAGE_LIMIT];
    uint32_t application_image_count;
    uint64_t application_image_epoch;
    VkBuffer readback;
    VkDeviceMemory memory;
    VkMemoryPropertyFlags memory_flags;
} Host;
static void vk_check(VkResult r, const char *s) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "%s: Vulkan %d\n", s, r);
        exit(1);
    }
}
#define VK(C) vk_check((C), #C)
static void fail(const char *s) {
    fprintf(stderr, "%s\n", s);
    exit(1);
}
static uint32_t *read_spv(const char *path, size_t *bytes) {
    FILE *f = fopen(path, "rb");
    if (!f)
        fail(path);
    if (fseek(f, 0, SEEK_END))
        fail(path);
    long n = ftell(f);
    if (n <= 0 || (n & 3))
        fail(path);
    rewind(f);
    uint32_t *p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n)
        fail(path);
    fclose(f);
    *bytes = (size_t)n;
    return p;
}
static void barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after,
                    VkPipelineStageFlags src, VkPipelineStageFlags dst, VkAccessFlags sa,
                    VkAccessFlags da) {
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
static void destroy_size(Host *h) {
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
static void create_size(Host *h, uint32_t width, uint32_t height) {
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
        VkSurfaceFormatKHR *fmts = calloc(nf, sizeof(*fmts));
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
        uint32_t mode_count=0;
        VK(vkGetPhysicalDeviceSurfacePresentModesKHR(h->physical,h->surface,&mode_count,NULL));
        VkPresentModeKHR *modes=calloc(mode_count,sizeof(*modes));
        if (!modes) fail("Present mode allocation");
        VK(vkGetPhysicalDeviceSurfacePresentModesKHR(h->physical,h->surface,&mode_count,modes));
        VkPresentModeKHR present_mode=VK_PRESENT_MODE_FIFO_KHR;
        for (uint32_t i=0;i<mode_count;++i)
            if (modes[i]==VK_PRESENT_MODE_MAILBOX_KHR) present_mode=VK_PRESENT_MODE_MAILBOX_KHR;
        free(modes);
        printf("Editor presentation: %s\n",present_mode==VK_PRESENT_MODE_MAILBOX_KHR?"mailbox":"fifo");
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
static void init_host(Host *h, int headless, uint32_t w, uint32_t ht, AsmUiFontSdfAtlas *atlas) {
    const char *const *exts = NULL;
    uint32_t ext_count = 0;
    if (!headless) {
        if (!SDL_Init(SDL_INIT_VIDEO))
            fail(SDL_GetError());
        h->window = SDL_CreateWindow("unity_ui - Unity Editor", (int)w, (int)ht,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!h->window)
            fail(SDL_GetError());
        SDL_SetWindowMinimumSize(h->window, 1024, 680);
        exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);
        if (!exts)
            fail(SDL_GetError());
    }
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = "ASMUI Editor",
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
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
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
        VkQueueFamilyProperties *qs = calloc(qn, sizeof(*qs));
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
    printf("Editor GPU: %s\n", props.deviceName);
    float priority = 1;
    VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  .queueFamilyIndex = h->family,
                                  .queueCount = 1,
                                  .pQueuePriorities = &priority};
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE};
    const char *swap_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
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
                                 EDITOR_MAX_DRAWS + EDITOR_MAX_NODES))
        fail("ASM vertex stream init");
    size_t vb, fb;
    uint32_t *vs = read_spv("build/shaders/ui.vert.spv", &vb),
             *fs = read_spv("build/shaders/ui.frag.spv", &fb);
    if (!asmui_vk_create_pipeline_pack(&h->backend, VK_FORMAT_R8G8B8A8_UNORM, vs, vb, fs, fb))
        fail("Editor pipelines");
    free(vs);
    free(fs);
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ASMUI_VK_MAX_TEXTURES};
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
static void capture(Host *h, const char *path) {
    VK(vkWaitForFences(h->device, 1, &h->fence, VK_TRUE, UINT64_MAX));
    void *p = NULL;
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
    printf("Editor screenshot: %s (%ux%u)\n", path, h->width, h->height);
}

static int font_lookup(uint32_t id, AsmUiHbAtlasGlyph *out, void *user) {
    FontBank *bank = user;
    if (asmui_font_sdf_lookup(id, out, bank->base))
        return 1;
    for (uint32_t i = 0; i < bank->count; ++i)
        if (asmui_font_sdf_lookup(id, out, &bank->pages[i]))
            return 1;
    return 0;
}
static int atlas_has_text(FontBank *bank, const char *text) {
    AsmUiFontSdfAtlas *atlas = bank->base;
    hb_buffer_t *b = hb_buffer_create();
    hb_buffer_add_utf8(b, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(b);
    hb_shape(atlas->hb_font, b, NULL, 0);
    unsigned n;
    hb_glyph_info_t *g = hb_buffer_get_glyph_infos(b, &n);
    int ok = 1;
    for (unsigned i = 0; i < n; ++i) {
        hb_glyph_extents_t ext;
        AsmUiHbAtlasGlyph glyph;
        if (hb_font_get_glyph_extents(atlas->hb_font, g[i].codepoint, &ext) && ext.width &&
            ext.height && !font_lookup(g[i].codepoint, &glyph, bank)) {
            ok = 0;
            break;
        }
    }
    hb_buffer_destroy(b);
    return ok;
}
static void extend_font(Host *h, FontBank *bank, const EditorState *e) {
    const char *s = e->focus == 100 ? e->search : e->edit;
    char shown[sizeof(e->search) + sizeof(e->composition)];
    snprintf(shown, sizeof(shown), "%s%s", s, e->composition);
    if (atlas_has_text(bank, shown))
        return;
    uint64_t start = SDL_GetTicksNS();
    if (bank->count == INPUT_FONT_PAGES) {
        fprintf(stderr, "Input glyph pages full; missing glyphs use a box\n");
        return;
    }
    const char *samples[] = {shown};
    AsmUiFontSdfAtlas *next = &bank->pages[bank->count];
    uint32_t resource = 2 + bank->count;
    int built = asmui_font_sdf_build_missing(next, editor_font(), resource, 64, samples, 1,
                                             font_lookup, bank);
    if (!built) {
        fprintf(stderr, "Input glyph atlas extension failed\n");
        return;
    }
    AsmUiVkTexture *texture = &h->input_fonts[bank->count];
    if (!asmui_vk_texture_upload_rgba8(h->physical, h->device, h->queue, h->pool, next->rgba,
                                       next->width, next->height, texture))
        fail("Input glyph texture upload");
    if (!asmui_vk_alloc_texture_set(&h->backend, h->descriptors, resource, texture->view,
                                    h->sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        fail("Input glyph descriptor allocation");
    ++bank->count;
    printf("Input glyph page %u: %u new glyphs, missing=%u, build+upload=%.3f ms\n", bank->count,
           next->glyph_count, next->missing_glyph_count, (SDL_GetTicksNS() - start) / 1e6);
}
static void upload_application_font(Host *h, AsmUiVkTexture *texture,
                                     const AsmUiFontSdfAtlas *atlas, uint32_t resource) {
    if (!asmui_vk_texture_upload_rgba8(h->physical, h->device, h->queue, h->pool,
                                       atlas->rgba, atlas->width, atlas->height, texture) ||
        !asmui_vk_alloc_texture_set(&h->backend, h->descriptors, resource, texture->view,
                                    h->sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
        fail("Application font texture upload");
}
static void application_fonts_init(Host *h, AsmUiAppHost *app, ApplicationFonts *fonts) {
    /* Source Han's ellipsis uses different GSUB glyphs in Latin and Han runs.
       Ellipsis is inserted after the normal frame glyph collection. */
    const char *samples[] = {app->api->font_samples(app->instance), "A…", "文…"};
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t resource = 66 + i * 65;
        const char *path = app->api->font_path(app->instance, i + 1);
        if (!path || !asmui_font_sdf_build(&fonts->atlas[i], path, resource, 64, samples, 3))
            fail("Original application font build");
        printf("Application font: %s, glyphs=%u, missing=%u\n", path,
               fonts->atlas[i].glyph_count, fonts->atlas[i].missing_glyph_count);
        fonts->bank[i].base = &fonts->atlas[i];
        fonts->shaper[i + 1] = asmui_font_sdf_shaper(&fonts->atlas[i]);
        fonts->shaper[i + 1].lookup = font_lookup;
        fonts->shaper[i + 1].lookup_user = &fonts->bank[i];
        upload_application_font(h, &h->application_fonts[i][0], &fonts->atlas[i], resource);
    }
}
static uint32_t application_image(void *user, const char *path) {
    Host *h = user;
    for (uint32_t i = 0; i < h->application_image_count; ++i)
        if (!strcmp(h->application_images[i].path, path)) {
            h->application_images[i].last_used=h->application_image_epoch;
            return h->application_images[i].texture.view ? APP_IMAGE_BASE + i : 0;
        }
    uint32_t index;
    if (h->application_image_count < APP_IMAGE_LIMIT) index=h->application_image_count++;
    else {
        index=APP_IMAGE_LIMIT;
        uint64_t oldest=UINT64_MAX;
        for (uint32_t i=0;i<APP_IMAGE_LIMIT;i++)
            if (h->application_images[i].last_used<h->application_image_epoch &&
                h->application_images[i].last_used<oldest) {
                index=i; oldest=h->application_images[i].last_used;
            }
        if (index==APP_IMAGE_LIMIT) fail("Application frame exceeds image cache capacity");
        /* Current-frame images are protected. Wait before replacing a view
           that may still be referenced by the preceding GPU submission. */
        VK(vkDeviceWaitIdle(h->device));
        asmui_vk_texture_destroy(h->device,&h->application_images[index].texture);
        SDL_free(h->application_images[index].path);
    }
    uint64_t start=SDL_GetTicksNS();
    ApplicationImage *entry = &h->application_images[index];
    entry->last_used=h->application_image_epoch;
    entry->path = SDL_strdup(path);
    if (!entry->path) fail("Application image path allocation");
    AsmUiImageRgba8 pixels = {0};
    if (!asmui_image_png_load(path, &pixels)) {
        fprintf(stderr, "Application image could not be loaded: %s\n", path);
        return 0;
    }
    uint32_t resource = APP_IMAGE_BASE + index;
    int uploaded = asmui_vk_texture_upload_rgba8(h->physical, h->device, h->queue, h->pool,
                                                  pixels.pixels, pixels.width, pixels.height,
                                                  &entry->texture);
    asmui_image_rgba8_destroy(&pixels);
    int bound=uploaded && (h->backend.texture_sets[resource]
        ? asmui_vk_update_texture_set(&h->backend,resource,entry->texture.view,h->sampler,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        : asmui_vk_alloc_texture_set(&h->backend,h->descriptors,resource,entry->texture.view,
                                     h->sampler,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    if (!bound)
        fail("Application image upload");
    printf("Application image: %s (%.3f ms)\n", path,(SDL_GetTicksNS()-start)/1e6);
    return resource;
}
static void application_fonts_update(Host *h, AsmUiAppHost *app, ApplicationFonts *fonts) {
    for (unsigned i = 0; i < 2; ++i) {
        FontBank *bank = &fonts->bank[i];
        size_t sample_count = 0;
        for (uint32_t j = 0; j < app->frame.count; ++j) {
            const AsmUiAppDraw *d = &app->frame.draws[j];
            if (d->kind == APP_TEXT && d->font == i + 1 && d->text && !atlas_has_text(bank, d->text))
                ++sample_count;
        }
        if (!sample_count) continue;
        uint64_t start=SDL_GetTicksNS();
        if (bank->count == INPUT_FONT_PAGES) fail("Application glyph pages exhausted");
        const char **samples = calloc(sample_count, sizeof(*samples));
        if (!samples) fail("Application glyph sample allocation");
        size_t index = 0;
        for (uint32_t j = 0; j < app->frame.count; ++j) {
            const AsmUiAppDraw *d = &app->frame.draws[j];
            if (d->kind != APP_TEXT || d->font != i + 1 || !d->text || atlas_has_text(bank, d->text)) continue;
            samples[index++] = d->text;
        }
        AsmUiFontSdfAtlas *next = &bank->pages[bank->count];
        uint32_t resource = 67 + i * 65 + bank->count;
        /* Preserve each draw's shaping context. Joining Han and digit-only
           labels changes HarfBuzz's script and can miss the requested glyphs. */
        if (!asmui_font_sdf_build_missing(next, app->api->font_path(app->instance, i + 1),
                                          resource, 64, samples, sample_count, font_lookup, bank))
            fail("Application glyph extension");
        free(samples);
        upload_application_font(h, &h->application_fonts[i][bank->count + 1], next, resource);
        ++bank->count;
        printf("Application font %u: added %u glyphs, missing=%u, build+upload=%.3f ms\n", i + 1,
               next->glyph_count, next->missing_glyph_count,(SDL_GetTicksNS()-start)/1e6);
    }
}
static float measure_text(const char *text, float pixels, void *user);
static uint32_t shape_application_text(const AsmUiDrawCmd *draw, AsmUiGlyphQuad *glyphs,
                                       uint32_t capacity, void *user) {
    ApplicationFonts *fonts = user;
    unsigned index = draw->resource_id == 66 ? 1 : draw->resource_id == 131 ? 2 : 0;
    AsmUiDrawCmd fitted = *draw;
    if (index && draw->payload && draw->material3 > 0) {
        float width = measure_text(draw->payload, draw->font_size, fonts->shaper[index].font);
        if (width > draw->local_w && width > 0) {
            fitted.font_size = fmaxf(draw->material3, draw->font_size * draw->local_w / width);
            draw = &fitted;
        }
    }
    if (index && draw->payload && measure_text(draw->payload, draw->font_size,
                                               fonts->shaper[index].font) > draw->local_w) {
        size_t length = strlen(draw->payload);
        char *text = malloc(length + 4);
        if (!text) fail("Text ellipsis allocation");
        memcpy(text, draw->payload, length + 1);
        while (length) {
            do { --length; } while (length && ((unsigned char)text[length] & 0xc0) == 0x80);
            memcpy(text + length, "…", 4);
            if (measure_text(text, draw->font_size, fonts->shaper[index].font) <= draw->local_w) break;
        }
        AsmUiDrawCmd shortened = *draw;
        shortened.payload = text;
        uint32_t count = asmui_hb_shape(&shortened, glyphs, capacity, &fonts->shaper[index]);
        free(text);
        return count;
    }
    return asmui_hb_shape(draw, glyphs, capacity, &fonts->shaper[index]);
}
static uint32_t application_build(void *user, AsmUiNode *nodes, uint32_t start,
                                   uint32_t capacity, int32_t parent, EditorRect r) {
    float viewport[] = {r.x, r.y, r.w, r.h};
    return asmui_app_build(user, nodes, start, capacity, parent, viewport);
}
typedef struct ApplicationAudio {
    SDL_AudioStream *stream;
    uint32_t epoch, frames, state;
    int error, end_logged;
    uint64_t opened_ns, last_log_ns;
    float position;
} ApplicationAudio;
static void application_audio_clock(ApplicationAudio *audio, AsmUiAppHost *app) {
    if (!app->instance) return;
    if (audio->error) {
        app->api->event(app->instance, &(AsmUiAppEvent){.kind=APP_AUDIO_CLOCK,.key=2});
        audio->error=0;
    }
    if (!audio->stream || audio->state!=1) return;
    int queued=SDL_GetAudioStreamQueued(audio->stream);
    if (queued<0) {audio->error=1;return;}
    float elapsed=(audio->frames-(uint32_t)queued/8)/48000.f;
    audio->position=elapsed;
    if (!queued && !audio->end_logged) {
        printf("Application music complete: frames=%u sample=%.3f\n", audio->frames, elapsed);
        audio->end_logged=1;
    }
    uint64_t now=SDL_GetTicksNS();
    if (getenv("ASMUI_PROFILE_AUDIO") && now-audio->last_log_ns>=5000000000ull) {
        printf("Application music clock: wall=%.3f sample=%.3f queued=%d\n",(now-audio->opened_ns)/1e9,elapsed,queued);
        audio->last_log_ns=now;
    }
    app->api->event(app->instance, &(AsmUiAppEvent){.kind=APP_AUDIO_CLOCK,.x=elapsed,.key=queued==0});
}
static void application_audio_sync(ApplicationAudio *audio, const AsmUiAppFrame *frame) {
    if (audio->stream && (frame->audio_epoch!=audio->epoch || !frame->audio_state)) {
        SDL_DestroyAudioStream(audio->stream);audio->stream=NULL;
    }
    if (!frame->audio_state || !frame->audio_pcm || !frame->audio_frames) {audio->state=0;return;}
    if (!audio->stream && audio->epoch!=frame->audio_epoch) {
        audio->epoch=frame->audio_epoch;audio->frames=frame->audio_frames;audio->state=2;audio->end_logged=0;
        audio->position=0;
        audio->opened_ns=SDL_GetTicksNS();audio->last_log_ns=audio->opened_ns;
        SDL_AudioSpec spec={.format=SDL_AUDIO_F32,.channels=2,.freq=48000};
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO) ||
            !(audio->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL)) ||
            frame->audio_frames>INT32_MAX/8 ||
            !SDL_PutAudioStreamData(audio->stream,frame->audio_pcm,(int)(frame->audio_frames*8)) ||
            !SDL_FlushAudioStream(audio->stream)) {
            fprintf(stderr,"Application audio: %s\n",SDL_GetError());
            if(audio->stream) SDL_DestroyAudioStream(audio->stream);
            audio->stream=NULL;audio->error=1;return;
        }
        printf("Application audio: %u stereo frames at 48000 Hz, epoch=%u\n",audio->frames,audio->epoch);
        if (getenv("ASMUI_PROFILE_AUDIO")) {
            SDL_AudioSpec device;int samples;
            if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio->stream),&device,&samples))
                printf("Application audio device: driver=%s rate=%d channels=%d buffer=%d\n",
                    SDL_GetCurrentAudioDriver(),device.freq,device.channels,samples);
        }
    }
    if (audio->stream && audio->state!=frame->audio_state) {
        bool ok=frame->audio_state==1 ? SDL_ResumeAudioStreamDevice(audio->stream) : SDL_PauseAudioStreamDevice(audio->stream);
        if (!ok) audio->error=1;
        audio->state=frame->audio_state;
        if (getenv("ASMUI_PROFILE_AUDIO")) printf("Application music state: wall=%.3f state=%u\n",
            (SDL_GetTicksNS()-audio->opened_ns)/1e9,audio->state);
    }
}
static int application_event(AsmUiAppHost *app, const EditorState *editor, AsmUiAppEvent event) {
    if (!app->instance || !editor->game || editor->menu >= 0) return 0;
    if (editor->focus>=0 && (event.kind==APP_KEY || event.kind==APP_KEY_UP || event.kind==APP_TEXT_INPUT || event.kind==APP_COMPOSITION)) return 0;
    return asmui_app_event(app, &event);
}
static float measure_text(const char *text, float pixels, void *user) {
    hb_font_t *font = user;
    hb_buffer_t *buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text, -1, 0, -1);
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, NULL, 0);
    unsigned n;
    hb_glyph_position_t *p = hb_buffer_get_glyph_positions(buffer, &n);
    float advance = 0;
    int scale = 1;
    hb_font_get_scale(font, &scale, NULL);
    for (unsigned i = 0; i < n; ++i)
        advance += p[i].x_advance * pixels / (float)scale;
    hb_buffer_destroy(buffer);
    return advance;
}
static void text_input_area(Host *h, const EditorState *e) {
    if (!h->window)
        return;
    if (e->focus < 0) {
        if (SDL_TextInputActive(h->window))
            SDL_StopTextInput(h->window);
        return;
    }
    int ww, hh;
    SDL_GetWindowSize(h->window, &ww, &hh);
    float sx = (float)ww / h->width, sy = (float)hh / h->height;
    SDL_Rect r = {(int)(e->input_rect.x * sx), (int)(e->input_rect.y * sy),
                  (int)(e->input_rect.w * sx), (int)(e->input_rect.h * sy)};
    SDL_SetTextInputArea(h->window, &r, (int)(e->caret_offset * sx));
    if (!SDL_TextInputActive(h->window))
        SDL_StartTextInput(h->window);
}
/* A shared render path produces both the native window and lossless readback.
   Headless visual checks therefore exercise exactly the same ASM/UI renderer. */
static int draw(Host *h, AsmUiContext *ctx, AsmUiBatch *batches, uint32_t count, int screenshot) {
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
    barrier(
        h->cmd, h->target->image,
        h->target_initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        h->target_initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
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
        vkCmdPipelineBarrier(h->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                             0, NULL, 1, &b, 0, NULL);
    }
    if (h->swapchain) {
        barrier(h->cmd, h->images[index],
                h->initialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
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
static void destroy_host(Host *h) {
    VK(vkDeviceWaitIdle(h->device));
    destroy_size(h);
    for (uint32_t i = 0; i < h->application_image_count; ++i) {
        asmui_vk_texture_destroy(h->device, &h->application_images[i].texture);
        SDL_free(h->application_images[i].path);
    }
    asmui_vk_texture_destroy(h->device, &h->font_texture);
    for (uint32_t i = 0; i < INPUT_FONT_PAGES; ++i)
        asmui_vk_texture_destroy(h->device, &h->input_fonts[i]);
    for (unsigned f = 0; f < 2; ++f)
        for (unsigned i = 0; i <= INPUT_FONT_PAGES; ++i)
            asmui_vk_texture_destroy(h->device, &h->application_fonts[f][i]);
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
        SDL_Quit();
    }
}
static void preview_state(EditorState *e, const char *state) {
    if (!strcmp(state, "camera"))
        e->selected = 0;
    else if (!strcmp(state, "console"))
        e->console = 1;
    else if (!strcmp(state, "play"))
        e->playing = 1;
    else if (!strcmp(state, "menu"))
        e->menu = 0;
    else if (!strcmp(state, "search")) {
        e->focus = 100;
        editor_text(e, "测试立方体");
    } else if (!strcmp(state, "ime")) {
        e->focus = 100;
        editor_composition(e, "测试输入");
    } else if (!strcmp(state, "rotated")) {
        e->rotation[0] = 25;
        e->rotation[1] = 35;
        e->rotation[2] = 15;
    } else if (!strcmp(state, "game"))
        e->game = 1;
    else if (!strcmp(state, "2d"))
        e->two_d = 1;
    else if (strcmp(state, "default"))
        fail("Unknown editor state");
}
typedef struct Preview {
    const char *name, *state;
    EditorLocale locale;
    uint32_t width, height;
} Preview;
static const Preview previews[] = {
    {"editor-zh", "default", EDITOR_ZH_CN, 1440, 900},
    {"editor-en", "default", EDITOR_EN_US, 1440, 900},
    {"editor-compact", "default", EDITOR_ZH_CN, 1024, 680},
    {"editor-compact-en", "default", EDITOR_EN_US, 1024, 680},
    {"editor-menu", "menu", EDITOR_ZH_CN, 1440, 900},
    {"editor-camera", "camera", EDITOR_ZH_CN, 1440, 900},
    {"editor-console", "console", EDITOR_ZH_CN, 1440, 900},
    {"editor-2d", "2d", EDITOR_ZH_CN, 1440, 900},
    {"editor-game", "game", EDITOR_ZH_CN, 1440, 900},
    {"editor-rotated", "rotated", EDITOR_ZH_CN, 1440, 900},
    {"editor-ime", "ime", EDITOR_ZH_CN, 1440, 900},
    {"editor-search", "search", EDITOR_ZH_CN, 1440, 900},
    {"editor-large", "default", EDITOR_ZH_CN, 1920, 1080},
};
int main(int argc, char **argv) {
    int headless = 0, smoke = 0;
    int app_only = 0, app_smoke = 0;
    const char *project = NULL, *app_data = "", *app_screen = "manager", *app_chart = NULL, *app_input_script = NULL;
    const char *app_captures = NULL;
    EditorLocale locale = EDITOR_ZH_CN;
    const char *locale_env = getenv("ASMUI_LANG");
    if (locale_env && *locale_env && !editor_locale_parse(locale_env, &locale))
        fail("ASMUI_LANG must be zh-CN or en-US");
    uint32_t width = 1440, height = 900, limit = 0;
    double duration = 0;
    int exit_on_result = 0;
    const char *output = "build/editor.png", *state = "default", *gallery = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--app-chart") && i+1<argc) app_chart=argv[++i];
        else if (!strcmp(argv[i], "--headless"))
            headless = 1;
        else if (!strcmp(argv[i], "--lang") && i + 1 < argc) {
            if (!editor_locale_parse(argv[++i], &locale))
                fail("--lang must be zh-CN or en-US");
        } else if (!strcmp(argv[i], "--smoke"))
            smoke = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            limit = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--app-exit-on-result")) exit_on_result = 1;
        else if (!strcmp(argv[i], "--duration") && i + 1 < argc) {
            duration = strtod(argv[++i], NULL);
            if (!isfinite(duration) || duration <= 0) fail("--duration must be positive seconds");
        }
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--project") && i + 1 < argc)
            project = argv[++i];
        else if (!strcmp(argv[i], "--data-dir") && i + 1 < argc)
            app_data = argv[++i];
        else if (!strcmp(argv[i], "--app-screen") && i + 1 < argc)
            app_screen = argv[++i];
        else if (!strcmp(argv[i], "--app-only"))
            app_only = 1;
        else if (!strcmp(argv[i], "--app-smoke"))
            app_smoke = 1;
        else if (!strcmp(argv[i], "--app-input-script") && i + 1 < argc)
            app_input_script = argv[++i];
        else if (!strcmp(argv[i], "--app-captures") && i + 1 < argc)
            app_captures = argv[++i];
        else if (!strcmp(argv[i], "--gallery") && i + 1 < argc)
            gallery = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc)
            state = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            width = (uint32_t)strtoul(argv[++i], NULL, 10);
            height = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else {
            fprintf(stderr,
                    "Usage: %s [--headless] [--size W H] [--frames N] [--duration SECONDS] [--app-exit-on-result] [--smoke] [--lang "
                    "zh-CN|en-US] [--state "
                    "default|camera|console|play|menu|search|ime|rotated|game|2d] [--output "
                    "FILE.png] [--gallery DIR] [--project DIR] [--data-dir DIR] [--app-screen manager|maker|play|auto] [--app-chart ID] [--app-only] [--app-smoke] [--app-input-script FILE] [--app-captures DIR]\n",
                    argv[0]);
            return 2;
        }
    }
    if (width < 1024 || height < 680 || width > 3840 || height > 2160)
        fail("Editor size must be 1024x680 .. 3840x2160");
    if (app_only && !project) fail("--app-only requires --project");
    if (strcmp(app_screen, "manager") && strcmp(app_screen, "maker") && strcmp(app_screen,"play") && strcmp(app_screen,"auto"))
        fail("--app-screen must be manager, maker, play or auto");
    if ((strcmp(app_screen, "manager") || app_chart) && !project) fail("Application navigation requires --project");
    if (app_smoke && (!project || !*app_data || headless))
        fail("--app-smoke requires --project, an explicit --data-dir and a native window");
    if (app_smoke && !limit) limit = 110;
    if (app_input_script && (!project || !*app_data || headless))
        fail("--app-input-script requires a native window and explicit --data-dir");
    ScriptedKey scripted_keys[512];
    unsigned scripted_count=app_input_script?read_input_script(app_input_script,scripted_keys,512):0;
    if (project && (gallery || smoke)) fail("Editor gallery/smoke cannot be combined with --project");
    AsmUiAppHost app = {0};
    if (project && !asmui_app_open(&app, project, app_data))
        fail("Application load failed; run make opensekai first");
    if (app_chart && !app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_NAVIGATE,.key=10,.text=app_chart}))
        fail("Requested chart ID was not found");
    if (strcmp(app_screen,"manager") && !app.api->event(app.instance,
        &(AsmUiAppEvent){.kind=APP_NAVIGATE,.key=!strcmp(app_screen,"maker")?2:!strcmp(app_screen,"play")?3:4}))
        fail("Application screen could not open; check the selected chart");
    if (headless && !limit)
        limit = 1;
    if (smoke && !limit)
        limit = 90;
    if (gallery) {
        headless = 1;
        smoke = 0;
        limit = sizeof(previews) / sizeof(previews[0]);
    }
    AsmUiFontSdfAtlas atlas = {0};
    size_t ns;
    const char *const *samples = editor_font_samples(&ns);
    const char *font = editor_font();
    if (!font || !asmui_font_sdf_build(&atlas, font, 1, 64, samples, ns) ||
        atlas.missing_glyph_count)
        fail("Editor font build failed; set ASMUI_FONT");
    printf("Editor font: %s (%u glyphs)\n", font, atlas.glyph_count);
    AsmUiHbShaper shaper = asmui_font_sdf_shaper(&atlas);
    FontBank fonts = {.base = &atlas};
    shaper.lookup = font_lookup;
    shaper.lookup_user = &fonts;
    Host host = {0};
    init_host(&host, headless, width, height, &atlas);
    app.resolve_image = application_image;
    app.image_user = &host;
    ApplicationFonts application_fonts = {0};
    ApplicationAudio application_audio = {0};
    ApplicationSound application_sound = {0};
    application_fonts.shaper[0] = shaper;
    if (app.instance) application_fonts_init(&host, &app, &application_fonts);
    DialogBridge *dialog = app.instance && !headless ? dialog_create() : NULL;
    if (app.instance && !headless && !dialog) fail("Application dialog initialization");
    EditorState e;
    editor_init(&e, (float)host.width, (float)host.height);
    e.locale = locale;
    printf("Editor language: %s\n", editor_locale_name(e.locale));
    preview_state(&e, state);
    if (app.instance) {
        e.game = 1;
        e.playing = 1;
        e.application_build = application_build;
        e.application_user = &app;
    }
    static AsmUiNode nodes[EDITOR_MAX_NODES];
    static AsmUiDrawCmd raw[EDITOR_MAX_DRAWS], expanded[EDITOR_MAX_DRAWS];
    static AsmUiBatch batches[EDITOR_MAX_DRAWS];
    static AsmUiGlyphQuad glyphs[2048];
    AsmUiContext ctx;
    asm_ui_init(&ctx, nodes, EDITOR_MAX_NODES, raw, EDITOR_MAX_DRAWS, NULL, 0);
    int running = 1;
    int font_dirty = e.focus >= 0;
    uint32_t frames = 0, last_draws = 0, last_batches = 0;
    uint64_t start = SDL_GetTicksNS();
    uint64_t previous_frame=start;
    int previous_play=1;
    int profile_frames=getenv("ASMUI_PROFILE_FRAMES")!=NULL;
    while (running && (!limit || frames < limit)) {
        ++host.application_image_epoch;
        uint64_t frame_now=SDL_GetTicksNS();
        float app_elapsed=headless?1.f/60.f:(float)((frame_now-previous_frame)/1e9);
        previous_frame=frame_now;
        if (app.instance) {
            application_audio_clock(&application_audio,&app);
            if (SDL_SetAtomicInt(&application_sound.error,0))
                app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_AUDIO_CLOCK,.key=2});
            app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_TICK,.delta=e.playing && !e.paused?app_elapsed:0});
        }
        char gallery_path[4096];
        const char *frame_output = output;
        if (gallery) {
            const Preview *p = &previews[frames];
            if (host.width != p->width || host.height != p->height)
                create_size(&host, p->width, p->height);
            editor_init(&e, host.width, host.height);
            e.locale = p->locale;
            preview_state(&e, p->state);
            font_dirty = e.focus >= 0;
            int n = snprintf(gallery_path, sizeof(gallery_path), "%s/%s.png", gallery, p->name);
            if (n < 0 || (size_t)n >= sizeof(gallery_path))
                fail("Gallery path too long");
            frame_output = gallery_path;
        }
        int timed_end = duration > 0 && (frame_now-start)/1e9 >= duration;
        int resize = 0, screenshot = gallery || timed_end || (limit && frames + 1 == limit);
        SDL_Event ev;
        double script_time=(frame_now-start)/1e9;
        for (unsigned script_index=0;script_index<scripted_count;++script_index) {
            ScriptedKey *key=&scripted_keys[script_index];
            double clock=key->music_time?application_audio.position:script_time;
            if (key->done || (key->music_time && application_audio.state!=1) || key->seconds>clock) continue;
            key->done=1;
            if (key->capture) {
                screenshot=1;
                printf("Application capture: wall=%.6f music=%.6f requested=%.6f\n",script_time,
                       application_audio.position,key->seconds);
                continue;
            }
            if (key->click) {
                app_smoke_pointer(host.window,&app,key->x,key->y,host.width,host.height);
                printf("Application pointer: %.6f x=%.3f y=%.3f\n",script_time,key->x,key->y);
                continue;
            }
            SDL_Event input={0};
            input.type=key->down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
            input.key.windowID=SDL_GetWindowID(host.window);
            input.key.key=key->key;
            if (!SDL_PushEvent(&input)) fail("Scripted SDL input failed");
            printf("Application input: %.6f key=%u down=%u\n",script_time,key->key,key->down);
        }
        if (!headless)
            while (SDL_PollEvent(&ev)) {
                if (dialog && ev.type == dialog->event_type) {
                    app.api->event(app.instance, &(AsmUiAppEvent){
                        .kind = ev.user.code & 0x100 ? APP_FILE_ERROR : APP_FILE_SELECTED,
                        .key = (uint32_t)ev.user.code & 0xff, .text = ev.user.data1});
                    SDL_free(ev.user.data1);
                    SDL_SetAtomicInt(&dialog->pending, 0);
                } else if (ev.type == SDL_EVENT_QUIT)
                    running = 0;
                else if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST && app.instance) {
                    if (getenv("ASMUI_PROFILE_AUDIO")) printf("Application focus lost: wall=%.3f\n",(SDL_GetTicksNS()-start)/1e9);
                    app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_FOCUS_LOST});
                }
                else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                    resize = 1;
                else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                    int ww, hh;
                    SDL_GetWindowSize(host.window, &ww, &hh);
                    editor_motion(&e, ev.motion.x * host.width / (float)ww,
                                  ev.motion.y * host.height / (float)hh);
                    application_event(&app, &e, (AsmUiAppEvent){.kind = APP_POINTER_MOVE,
                        .x = e.mouse_x, .y = e.mouse_y, .modifiers = (SDL_GetModState() & SDL_KMOD_CTRL) != 0});
                } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                           ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    int ww, hh;
                    SDL_GetWindowSize(host.window, &ww, &hh);
                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.focus >= 0)
                        SDL_ClearComposition(host.window);
                    int consumed = application_event(&app, &e, (AsmUiAppEvent){
                        .kind = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? APP_POINTER_DOWN : APP_POINTER_UP,
                        .key = ev.button.button, .x = ev.button.x * host.width / (float)ww,
                        .y = ev.button.y * host.height / (float)hh,
                        .modifiers = (SDL_GetModState() & SDL_KMOD_CTRL) != 0});
                    if (!consumed && app.instance && ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                        app.api->event(app.instance, &(AsmUiAppEvent){.kind=APP_KEY,.key=27});
                    if (!consumed && editor_pointer(&e, ev.button.x * host.width / (float)ww,
                                       ev.button.y * host.height / (float)hh, ev.button.button,
                                       ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN))
                        running = 0;
                } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
                    float delta = ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
                    if (!application_event(&app, &e, (AsmUiAppEvent){.kind=APP_WHEEL,
                        .x=e.mouse_x,.y=e.mouse_y,.delta=delta})) editor_wheel(&e, delta);
                }
                else if (ev.type == SDL_EVENT_TEXT_INPUT) {
                    if (!application_event(&app, &e, (AsmUiAppEvent){.kind=APP_TEXT_INPUT,.text=ev.text.text}))
                        editor_text(&e, ev.text.text);
                    font_dirty = 1;
                } else if (ev.type == SDL_EVENT_TEXT_EDITING) {
                    if (!application_event(&app, &e, (AsmUiAppEvent){.kind=APP_COMPOSITION,.text=ev.edit.text}))
                        editor_composition(&e, ev.edit.text);
                    font_dirty = 1;
                } else if (ev.type == SDL_EVENT_KEY_UP) {
                    application_event(&app,&e,(AsmUiAppEvent){.kind=APP_KEY_UP,.key=(uint32_t)ev.key.key});
                } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_F12)
                        screenshot = 1;
                    else {
                        if (ev.key.key == SDLK_ESCAPE)
                            SDL_ClearComposition(host.window);
                        if (!application_event(&app, &e, (AsmUiAppEvent){.kind=APP_KEY,
                            .key=(uint32_t)ev.key.key,.modifiers=(ev.key.mod & SDL_KMOD_CTRL) != 0}))
                            editor_key(&e, (int)ev.key.key, (ev.key.mod & SDL_KMOD_CTRL) != 0);
                    }
                }
            }
        if (!running)
            break;
        if (dialog) application_dialog(&app, host.window, dialog);
        if (app_smoke) {
            if (frames == 5) app_smoke_pointer(host.window, &app, 1600, 50, host.width, host.height);
            if (frames == 15) app_smoke_pointer(host.window, &app, 900, 565, host.width, host.height);
            if (frames == 20) {
                SDL_Event event = {0};
                event.type = SDL_EVENT_TEXT_EDITING;
                event.edit.windowID = SDL_GetWindowID(host.window);
                event.edit.text = "原生中文输入";
                SDL_PushEvent(&event);
            }
            if (frames == 24) {
                SDL_Event event = {0};
                event.type = SDL_EVENT_TEXT_INPUT;
                event.text.windowID = SDL_GetWindowID(host.window);
                event.text.text = "ASM 原生中文输入";
                SDL_PushEvent(&event);
            }
            if (frames == 28) app_smoke_key(host.window, SDLK_RETURN, SDL_KMOD_NONE);
            if (frames == 30) app_smoke_pointer(host.window, &app, 850, 990, host.width, host.height);
            if (frames == 40) {
                printf("Application resize: driver=%s flags=%llu\n", SDL_GetCurrentVideoDriver(),
                       (unsigned long long)SDL_GetWindowFlags(host.window));
                if (SDL_GetWindowFlags(host.window) & SDL_WINDOW_FULLSCREEN) {
                    SDL_SetWindowFullscreen(host.window, false);
                    SDL_SyncWindow(host.window);
                }
                if (SDL_GetWindowFlags(host.window) & SDL_WINDOW_MAXIMIZED)
                    SDL_RestoreWindow(host.window);
                if (!SDL_SetWindowSize(host.window, 1280, 800)) fail(SDL_GetError());
                SDL_SyncWindow(host.window);
                resize = 1;
            }
            if (frames == 50) app_smoke_pointer(host.window, &app, 1330, 50, host.width, host.height);
            if (frames == 60) app_smoke_key(host.window, SDLK_ESCAPE, SDL_KMOD_NONE);
            if (frames == 65) app_smoke_pointer(host.window, &app, 832, 420, host.width, host.height);
            if (frames == 70) app_smoke_pointer(host.window, &app, 100, 212, host.width, host.height);
            if (frames == 72) app_smoke_pointer(host.window, &app, 650, 599, host.width, host.height);
            if (frames == 76) app_smoke_key(host.window, SDLK_Z, SDL_KMOD_CTRL);
            if (frames == 78) app_smoke_key(host.window, SDLK_Y, SDL_KMOD_CTRL);
            if (frames == 82) app_smoke_key(host.window, SDLK_S, SDL_KMOD_CTRL);
            if (frames == 84) app_smoke_pointer(host.window, &app, 100, 356, host.width, host.height);
            if (frames == 86) app_smoke_pointer(host.window, &app, 1000, 599, host.width, host.height);
            if (frames == 90) app_smoke_key(host.window, SDLK_Z, SDL_KMOD_CTRL);
            if (frames == 94) app_smoke_key(host.window, SDLK_Y, SDL_KMOD_CTRL);
            if (frames == 98) app_smoke_key(host.window, SDLK_S, SDL_KMOD_CTRL);
        }
        if (smoke) {
            if (frames == 10) {
                editor_pointer(&e, 75, 165, 1, 1);
                editor_pointer(&e, 75, 165, 1, 0);
            }
            if (frames == 20)
                editor_key(&e, 'p', 1);
            if (frames == 30 && !headless) {
                SDL_SetWindowSize(host.window, 1280, 800);
                resize = 1;
            }
            if (frames == 40)
                e.console = 1;
            if (frames == 50) {
                e.console = 0;
                e.selected = 3;
                e.asset = -1;
            }
            if (frames == 60) {
                e.menu = 0;
            }
            if (frames == 70)
                e.menu = -1;
            if (frames == 74) {
                e.focus = 100;
                if (headless) {
                    editor_composition(&e, "测试输入");
                    font_dirty = 1;
                } else {
                    SDL_Event event = {0};
                    event.type = SDL_EVENT_TEXT_EDITING;
                    event.edit.windowID = SDL_GetWindowID(host.window);
                    event.edit.text = "测试输入";
                    if (!SDL_PushEvent(&event))
                        fail("Smoke preedit event");
                }
            }
            if (frames == 77) {
                if (strcmp(e.composition, "测试输入") || !atlas_has_text(&fonts, e.composition))
                    fail("Smoke preedit/glyph coverage");
            }
            if (frames == 78) {
                if (headless) {
                    editor_text(&e, "立方体");
                    font_dirty = 1;
                } else {
                    SDL_Event event = {0};
                    event.type = SDL_EVENT_TEXT_INPUT;
                    event.text.windowID = SDL_GetWindowID(host.window);
                    event.text.text = "立方体";
                    if (!SDL_PushEvent(&event))
                        fail("Smoke input event");
                }
            }
            if (frames == 81) {
                if (strcmp(e.search, "立方体") || e.composition[0])
                    fail("Smoke committed UTF-8 text");
                printf("SDL text input: preedit, dynamic glyphs and Chinese commit OK\n");
            }
            if (frames == 82) {
                editor_key(&e, 'a', 1);
                editor_key(&e, 13, 0);
            }
        }
        if (resize) {
            int ww, hh;
            SDL_GetWindowSizeInPixels(host.window, &ww, &hh);
            if (ww < 1 || hh < 1) {
                SDL_Delay(20);
                continue;
            }
            create_size(&host, (uint32_t)ww, (uint32_t)hh);
            editor_resize(&e, (float)host.width, (float)host.height);
            if (app_smoke) printf("Application framebuffer: %ux%u\n", host.width, host.height);
        }
        /* The fence also protects the persistently mapped ASM vertex stream. */
        VK(vkWaitForFences(host.device, 1, &host.fence, VK_TRUE, UINT64_MAX));
        if (font_dirty && e.focus >= 0)
            extend_font(&host, &fonts, &e);
        font_dirty = 0;
        e.measure_text = measure_text;
        e.measure_user = atlas.hb_font;
        app.locale = (uint32_t)e.locale;
        int play_state=e.playing && !e.paused;
        if (app.instance && previous_play!=play_state)
            app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_PLAY_STATE,.key=(uint32_t)play_state});
        previous_play=play_state;
        app.dt = 0; /* Clock is advanced before input dispatch, never by frame count. */
        if (app_only) {
            e.scene_rect = (EditorRect){0, 0, e.width, e.height};
            ctx.node_count = application_build(&app, nodes, 0, EDITOR_MAX_NODES, -1, e.scene_rect);
            e.node_count = ctx.node_count;
        } else ctx.node_count = editor_build(&e, nodes, EDITOR_MAX_NODES, atlas.px_range);
        if (app.instance) application_audio_sync(&application_audio,&app.frame);
        if (app.instance) application_sound_sync(&application_sound,&app.frame);
        if (app.instance && e.game) application_fonts_update(&host, &app, &application_fonts);
        uint64_t fonts_ready=SDL_GetTicksNS();
        if (!(host.window && app.instance && e.game && app.frame.wants_text))
            text_input_area(&host, &e);
        else {
            int ww, hh;
            SDL_GetWindowSize(host.window, &ww, &hh);
            float sx = (float)ww / host.width, sy = (float)hh / host.height;
            SDL_Rect r = {(int)((app.canvas[0] + app.frame.input_rect[0] * app.scale) * sx),
                          (int)((app.canvas[1] + app.frame.input_rect[1] * app.scale) * sy),
                          (int)(app.frame.input_rect[2] * app.scale * sx),
                          (int)(app.frame.input_rect[3] * app.scale * sy)};
            SDL_SetTextInputArea(host.window, &r, r.w);
            if (!SDL_TextInputActive(host.window)) SDL_StartTextInput(host.window);
        }
        if (ctx.node_count >= EDITOR_MAX_NODES)
            fail("Editor node capacity");
        asm_ui_set_viewport(&ctx, e.width, e.height);
        asm_ui_begin_frame(&ctx, 1.f / 60.f);
        asm_ui_layout(&ctx);
        asm_ui_build_drawlist(&ctx);
        editor_patch_quads(&e, &ctx);
        if (app.instance) asmui_app_patch_quads(&app, &ctx);
        AsmUiContext render = ctx;
        render.draws = expanded;
        render.draw_count = asmui_expand_text_stream(&ctx, expanded, EDITOR_MAX_DRAWS,
                                                     shape_application_text, &application_fonts, glyphs, 2048);
        if (!render.draw_count || render.draw_count >= EDITOR_MAX_DRAWS)
            fail("Editor text expansion");
        last_draws = render.draw_count;
        last_batches = asm_ui_build_batches(&render, batches, EDITOR_MAX_DRAWS);
        uint64_t draws_ready=SDL_GetTicksNS();
        int result_end=exit_on_result && app.instance && (app.frame.reserved & 2u);
        if (result_end) screenshot=1;
        if (app_captures && screenshot && !timed_end && !result_end && !gallery) {
            int n=snprintf(gallery_path,sizeof(gallery_path),"%s/frame-%06u.png",app_captures,frames);
            if (n<0 || (size_t)n>=sizeof(gallery_path)) fail("Application capture path too long");
            frame_output=gallery_path;
        }
        int presented = draw(&host, &render, batches, last_batches, screenshot);
        if (profile_frames) printf("Frame timing: index=%u update+assets=%.3f layout+text=%.3f GPU+present=%.3f ms\n",
            frames,(fonts_ready-frame_now)/1e6,(draws_ready-fonts_ready)/1e6,(SDL_GetTicksNS()-draws_ready)/1e6);
        if (screenshot && presented >= 0)
            capture(&host, frame_output);
        if (presented <= 0 && !headless) {
            int ww, hh;
            SDL_GetWindowSizeInPixels(host.window, &ww, &hh);
            if (ww > 0 && hh > 0) {
                create_size(&host, (uint32_t)ww, (uint32_t)hh);
                editor_resize(&e, (float)host.width, (float)host.height);
            }
            if (presented < 0) {
                SDL_Delay(20);
                continue;
            }
        }
        ++frames;
        if (app.instance && (app.frame.reserved & 1u)) {
            app.api->event(app.instance,&(AsmUiAppEvent){.kind=APP_RESOURCES_READY});
            previous_frame=SDL_GetTicksNS();
            /* Initial preparation is outside both gameplay and test input time. */
            if (frames==1) start=previous_frame;
            printf("Application resources ready before gameplay clock\n");
        }
        if (e.playing && !e.paused)
            ++e.frame;
        if (timed_end || result_end) running = 0;
        /* Mailbox keeps compositor throttling out of the input loop. Bound CPU
           work at 120 Hz; audio and judgement still use elapsed/sample time. */
        uint64_t frame_ns=SDL_GetTicksNS()-frame_now;
        if (!headless && frame_ns<1000000000u/120u) SDL_DelayNS(1000000000u/120u-frame_ns);
    }
    printf("Editor rendered: frames=%u nodes=%u text=%u draws=%u batches=%u elapsed=%.3f s\n",
           frames, e.node_count, e.text_count, last_draws, last_batches,
           (SDL_GetTicksNS() - start) / 1e9);
    dialog_close(dialog);
    if (application_audio.stream) SDL_DestroyAudioStream(application_audio.stream);
    application_sound_close(&application_sound);
    destroy_host(&host);
    for (uint32_t i = 0; i < fonts.count; ++i)
        asmui_font_sdf_destroy(&fonts.pages[i]);
    asmui_font_sdf_destroy(&atlas);
    for (unsigned f = 0; f < 2; ++f) {
        for (uint32_t i = 0; i < application_fonts.bank[f].count; ++i)
            asmui_font_sdf_destroy(&application_fonts.bank[f].pages[i]);
        asmui_font_sdf_destroy(&application_fonts.atlas[f]);
    }
    asmui_app_close(&app);
    return frames ? 0 : 1;
}
