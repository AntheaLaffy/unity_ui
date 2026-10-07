#include "asmui_post_vulkan.h"
#include <string.h>

_Static_assert(sizeof(AsmUiVkPostPush) == 32, "AsmUiVkPostPush must match GLSL push-constant ABI");

static VkShaderModule create_shader(VkDevice device, const uint32_t *spv, size_t bytes) {
    if (!device || !spv || !bytes || (bytes & 3u)) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = bytes,
        .pCode = spv,
    };
    VkShaderModule out = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &ci, NULL, &out) == VK_SUCCESS ? out : VK_NULL_HANDLE;
}

int asmui_vk_post_init(AsmUiVkPostBackend *post,
                       VkDevice device,
                       VkFormat color_format,
                       const uint32_t *vert_spv, size_t vert_spv_bytes,
                       const uint32_t *frag_spv, size_t frag_spv_bytes) {
    if (!post || !device || color_format == VK_FORMAT_UNDEFINED) return 0;
    memset(post, 0, sizeof(*post));
    post->device = device;

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding,
    };
    if (vkCreateDescriptorSetLayout(device, &dsl, NULL, &post->texture_set_layout) != VK_SUCCESS)
        goto fail;

    VkPushConstantRange pcr = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(AsmUiVkPostPush),
    };
    VkPipelineLayoutCreateInfo pli = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &post->texture_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(device, &pli, NULL, &post->pipeline_layout) != VK_SUCCESS)
        goto fail;

    VkShaderModule vs = create_shader(device, vert_spv, vert_spv_bytes);
    VkShaderModule fs = create_shader(device, frag_spv, frag_spv_bytes);
    if (!vs || !fs) {
        if (vs) vkDestroyShaderModule(device, vs, NULL);
        if (fs) vkDestroyShaderModule(device, fs, NULL);
        goto fail;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vs,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fs,
            .pName = "main",
        },
    };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState att = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &att,
    };
    const VkDynamicState states[2] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = states,
    };
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi,
        .pInputAssemblyState = &ia,
        .pViewportState = &vp,
        .pRasterizationState = &rs,
        .pMultisampleState = &ms,
        .pColorBlendState = &cb,
        .pDynamicState = &dyn,
        .layout = post->pipeline_layout,
    };
    VkResult vr = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL, &post->pipeline);
    if (vr == VK_SUCCESS) {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
        vr = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, NULL,
                                       &post->additive_pipeline);
    }
    vkDestroyShaderModule(device, fs, NULL);
    vkDestroyShaderModule(device, vs, NULL);
    if (vr != VK_SUCCESS) goto fail;
    return 1;

fail:
    asmui_vk_post_destroy(post);
    return 0;
}

void asmui_vk_post_destroy(AsmUiVkPostBackend *post) {
    if (!post || !post->device) return;
    if (post->additive_pipeline) vkDestroyPipeline(post->device, post->additive_pipeline, NULL);
    if (post->pipeline) vkDestroyPipeline(post->device, post->pipeline, NULL);
    if (post->pipeline_layout) vkDestroyPipelineLayout(post->device, post->pipeline_layout, NULL);
    if (post->texture_set_layout) vkDestroyDescriptorSetLayout(post->device, post->texture_set_layout, NULL);
    VkDevice device = post->device;
    memset(post, 0, sizeof(*post));
    (void)device;
}

int asmui_vk_post_alloc_texture_set(AsmUiVkPostBackend *post,
                                    VkDescriptorPool pool,
                                    VkImageView image_view,
                                    VkSampler sampler,
                                    VkImageLayout layout,
                                    VkDescriptorSet *out_set) {
    if (!post || !post->device || !post->texture_set_layout || !pool ||
        !image_view || !sampler || !out_set) return 0;
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &post->texture_set_layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(post->device, &ai, &set) != VK_SUCCESS) return 0;
    VkDescriptorImageInfo ii = {
        .sampler = sampler,
        .imageView = image_view,
        .imageLayout = layout,
    };
    VkWriteDescriptorSet w = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &ii,
    };
    vkUpdateDescriptorSets(post->device, 1, &w, 0, NULL);
    *out_set = set;
    return 1;
}

static void record_post(VkCommandBuffer cmd,
                        const AsmUiVkPostBackend *post,
                        VkPipeline pipeline,
                        VkDescriptorSet texture_set,
                        uint32_t framebuffer_w,
                        uint32_t framebuffer_h,
                        uint32_t mode,
                        float texel_x,
                        float texel_y,
                        float radius,
                        float strength) {
    if (!cmd || !post || !pipeline || !texture_set ||
        !framebuffer_w || !framebuffer_h) return;
    VkViewport vp = { 0.0f, 0.0f, (float)framebuffer_w, (float)framebuffer_h, 0.0f, 1.0f };
    VkRect2D sc = { {0, 0}, {framebuffer_w, framebuffer_h} };
    AsmUiVkPostPush push = {
        .texel_x = texel_x,
        .texel_y = texel_y,
        .radius = radius,
        .strength = strength,
        .mode = mode,
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            post->pipeline_layout, 0, 1, &texture_set, 0, NULL);
    vkCmdPushConstants(cmd, post->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void asmui_vk_post_record(VkCommandBuffer cmd,
                          const AsmUiVkPostBackend *post,
                          VkDescriptorSet texture_set,
                          uint32_t framebuffer_w,
                          uint32_t framebuffer_h,
                          uint32_t mode,
                          float texel_x,
                          float texel_y,
                          float radius,
                          float strength) {
    record_post(cmd, post, post ? post->pipeline : VK_NULL_HANDLE, texture_set,
                framebuffer_w, framebuffer_h, mode,
                texel_x, texel_y, radius, strength);
}

void asmui_vk_post_record_additive(VkCommandBuffer cmd,
                                   const AsmUiVkPostBackend *post,
                                   VkDescriptorSet texture_set,
                                   uint32_t framebuffer_w,
                                   uint32_t framebuffer_h,
                                   uint32_t mode,
                                   float texel_x,
                                   float texel_y,
                                   float radius,
                                   float strength) {
    record_post(cmd, post, post ? post->additive_pipeline : VK_NULL_HANDLE, texture_set,
                framebuffer_w, framebuffer_h, mode,
                texel_x, texel_y, radius, strength);
}
