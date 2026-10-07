#define _POSIX_C_SOURCE 200809L
#include "asmui_vulkan_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define JOBS 4u
#define W 128u
#define H 128u

static void die_vk(const char *what, VkResult r) {
    fprintf(stderr, "%s failed: VkResult=%d\n", what, (int)r);
    exit(1);
}

int main(void) {
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "asmui-async-texture-smoke",
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance instance = VK_NULL_HANDLE;
    VkResult vr = vkCreateInstance(&ici, NULL, &instance);
    if (vr != VK_SUCCESS) die_vk("vkCreateInstance", vr);

    uint32_t dc = 0;
    if (vkEnumeratePhysicalDevices(instance, &dc, NULL) != VK_SUCCESS || !dc) return 2;
    VkPhysicalDevice *devs = calloc(dc, sizeof(*devs));
    if (!devs) return 2;
    vkEnumeratePhysicalDevices(instance, &dc, devs);
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    uint32_t qf = UINT32_MAX;
    VkPhysicalDeviceProperties props = {0};
    int best = -1;
    for (uint32_t d = 0; d < dc; ++d) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, NULL);
        VkQueueFamilyProperties *qp = calloc(qn, sizeof(*qp));
        if (!qp) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &qn, qp);
        uint32_t q = UINT32_MAX;
        for (uint32_t i = 0; i < qn; ++i) {
            if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && qp[i].queueCount) { q = i; break; }
        }
        free(qp);
        if (q == UINT32_MAX) continue;
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[d], &p);
        int score = (p.vendorID == 0x8086u ? 100 : 0) +
                    (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 20 : 0);
        if (score > best) { best = score; physical = devs[d]; qf = q; props = p; }
    }
    free(devs);
    if (!physical) return 3;

    float pri = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qf,
        .queueCount = 1,
        .pQueuePriorities = &pri,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qci,
    };
    VkDevice device = VK_NULL_HANDLE;
    if ((vr = vkCreateDevice(physical, &dci, NULL, &device)) != VK_SUCCESS)
        die_vk("vkCreateDevice", vr);
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, qf, 0, &queue);
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = qf,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    if ((vr = vkCreateCommandPool(device, &cpci, NULL, &pool)) != VK_SUCCESS)
        die_vk("vkCreateCommandPool", vr);

    uint8_t *pixels = malloc((size_t)W * H * 4u);
    if (!pixels) return 4;
    AsmUiVkTextureUpload jobs[JOBS];
    AsmUiVkTexture textures[JOBS];
    memset(jobs, 0, sizeof(jobs));
    memset(textures, 0, sizeof(textures));
    for (uint32_t j = 0; j < JOBS; ++j) {
        for (uint32_t i = 0; i < W * H; ++i) {
            pixels[i*4u+0u] = (uint8_t)(32u + j * 50u);
            pixels[i*4u+1u] = (uint8_t)(i & 255u);
            pixels[i*4u+2u] = (uint8_t)(255u - j * 40u);
            pixels[i*4u+3u] = 255u;
        }
        if (!asmui_vk_texture_upload_rgba8_async_begin(
                physical, device, queue, pool, pixels, W, H, &jobs[j])) return 5;
    }
    free(pixels);

    uint32_t done = 0;
    uint32_t polls = 0;
    uint8_t finished[JOBS] = {0};
    struct timespec nap = { .tv_sec = 0, .tv_nsec = 1000000L };
    while (done < JOBS && polls < 2000u) {
        for (uint32_t j = 0; j < JOBS; ++j) {
            if (finished[j]) continue;
            int r = asmui_vk_texture_upload_async_poll(&jobs[j], &textures[j]);
            if (r < 0) return 6;
            if (r == 1) { finished[j] = 1u; ++done; }
        }
        ++polls;
        if (done < JOBS) nanosleep(&nap, NULL);
    }
    if (done != JOBS) return 7;
    for (uint32_t j = 0; j < JOBS; ++j) {
        if (!textures[j].image || !textures[j].view || textures[j].width != W || textures[j].height != H)
            return 8;
        asmui_vk_texture_destroy(device, &textures[j]);
    }

    printf("GPU: %s (vendor=0x%04x device=0x%04x)\n",
           props.deviceName, props.vendorID, props.deviceID);
    printf("async texture upload OK: submitted=%u completed=%u poll-rounds=%u queueWaitIdle=0\n",
           JOBS, done, polls);
    vkDestroyCommandPool(device, pool, NULL);
    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
