#include "asmui_vulkan.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int find_memory_type(VkPhysicalDevice physical,
                            uint32_t type_bits,
                            VkMemoryPropertyFlags required,
                            VkMemoryPropertyFlags preferred,
                            uint32_t *out_index,
                            VkMemoryPropertyFlags *out_flags) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    int best = -1;
    unsigned best_score = 0;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if (!(type_bits & (1u << i))) continue;
        VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
        if ((f & required) != required) continue;
        unsigned score = 1;
        if ((f & preferred) == preferred) score += 4;
        if (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) score += 2;
        if (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) score += 1;
        if (best < 0 || score > best_score) {
            best = (int)i;
            best_score = score;
        }
    }
    if (best < 0) return 0;
    *out_index = (uint32_t)best;
    *out_flags = mp.memoryTypes[best].propertyFlags;
    return 1;
}

static int create_mapped_buffer(AsmUiVkBackend *b,
                                VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkBuffer *out_buffer,
                                VkDeviceMemory *out_memory,
                                VkMemoryPropertyFlags *out_flags,
                                void **out_mapped) {
    VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(b->device, &bi, NULL, out_buffer) != VK_SUCCESS) return 0;

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(b->device, *out_buffer, &req);
    uint32_t memory_type = 0;
    if (!find_memory_type(b->physical_device, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          &memory_type, out_flags)) {
        vkDestroyBuffer(b->device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return 0;
    }

    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type;
    if (vkAllocateMemory(b->device, &ai, NULL, out_memory) != VK_SUCCESS) {
        vkDestroyBuffer(b->device, *out_buffer, NULL);
        *out_buffer = VK_NULL_HANDLE;
        return 0;
    }
    if (vkBindBufferMemory(b->device, *out_buffer, *out_memory, 0) != VK_SUCCESS) {
        vkFreeMemory(b->device, *out_memory, NULL);
        vkDestroyBuffer(b->device, *out_buffer, NULL);
        *out_memory = VK_NULL_HANDLE;
        *out_buffer = VK_NULL_HANDLE;
        return 0;
    }
    if (vkMapMemory(b->device, *out_memory, 0, VK_WHOLE_SIZE, 0, out_mapped) != VK_SUCCESS) {
        vkFreeMemory(b->device, *out_memory, NULL);
        vkDestroyBuffer(b->device, *out_buffer, NULL);
        *out_memory = VK_NULL_HANDLE;
        *out_buffer = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

static void flush_range_if_needed(VkDevice device, VkDeviceMemory memory,
                                  VkMemoryPropertyFlags flags,
                                  VkDeviceSize offset, VkDeviceSize size,
                                  VkDeviceSize atom_size,
                                  VkDeviceSize allocation_size) {
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) return;
    if (!atom_size) atom_size = 1;
    VkDeviceSize begin = (offset / atom_size) * atom_size;
    VkDeviceSize end = ((offset + size + atom_size - 1u) / atom_size) * atom_size;
    if (allocation_size && end > allocation_size) end = allocation_size;
    VkMappedMemoryRange range = { .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
    range.memory = memory;
    range.offset = begin;
    range.size = end - begin;
    vkFlushMappedMemoryRanges(device, 1, &range);
}

int asmui_vk_init_streaming_ex(AsmUiVkBackend *b,
                               VkPhysicalDevice physical_device,
                               VkDevice device,
                               uint32_t max_quads,
                               uint32_t stream_frames) {
    if (!b || !physical_device || !device || !max_quads || !stream_frames) return 0;
    memset(b, 0, sizeof(*b));
    b->physical_device = physical_device;
    b->device = device;
    b->max_quads = max_quads;
    b->stream_frames = stream_frames;
    b->active_stream_slot = 0;
    b->vertex_segment_bytes = (VkDeviceSize)max_quads * 4u * sizeof(AsmUiGpuVertex);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device, &props);
    b->non_coherent_atom_size = props.limits.nonCoherentAtomSize;

    VkDeviceSize vb_size = b->vertex_segment_bytes * stream_frames;
    if (!create_mapped_buffer(b, vb_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              &b->vertex_buffer, &b->vertex_memory,
                              &b->vertex_memory_flags, &b->vertex_mapped)) {
        asmui_vk_destroy_streaming(b);
        return 0;
    }
    VkMemoryRequirements vertex_req;
    vkGetBufferMemoryRequirements(device, b->vertex_buffer, &vertex_req);
    b->vertex_allocation_size = vertex_req.size;

    void *index_mapped = NULL;
    VkDeviceSize ib_size = (VkDeviceSize)max_quads * 6u * sizeof(uint32_t);
    if (!create_mapped_buffer(b, ib_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              &b->index_buffer, &b->index_memory,
                              &b->index_memory_flags, &index_mapped)) {
        asmui_vk_destroy_streaming(b);
        return 0;
    }
    uint32_t *idx = (uint32_t *)index_mapped;
    for (uint32_t q = 0; q < max_quads; ++q) {
        uint32_t v = q * 4u;
        idx[q*6u+0] = v+0; idx[q*6u+1] = v+1; idx[q*6u+2] = v+2;
        idx[q*6u+3] = v+0; idx[q*6u+4] = v+2; idx[q*6u+5] = v+3;
    }
    VkMemoryRequirements index_req;
    vkGetBufferMemoryRequirements(device, b->index_buffer, &index_req);
    flush_range_if_needed(device, b->index_memory, b->index_memory_flags,
                          0, ib_size, b->non_coherent_atom_size, index_req.size);
    vkUnmapMemory(device, b->index_memory);
    return 1;
}

int asmui_vk_init_streaming(AsmUiVkBackend *b,
                            VkPhysicalDevice physical_device,
                            VkDevice device,
                            uint32_t max_quads) {
    return asmui_vk_init_streaming_ex(b, physical_device, device, max_quads, 1u);
}

void asmui_vk_destroy_streaming(AsmUiVkBackend *b) {
    if (!b || !b->device) return;
    if (b->vertex_mapped && b->vertex_memory)
        vkUnmapMemory(b->device, b->vertex_memory);
    if (b->index_buffer) vkDestroyBuffer(b->device, b->index_buffer, NULL);
    if (b->index_memory) vkFreeMemory(b->device, b->index_memory, NULL);
    if (b->vertex_buffer) vkDestroyBuffer(b->device, b->vertex_buffer, NULL);
    if (b->vertex_memory) vkFreeMemory(b->device, b->vertex_memory, NULL);
    b->vertex_mapped = NULL;
    b->vertex_buffer = VK_NULL_HANDLE;
    b->vertex_memory = VK_NULL_HANDLE;
    b->index_buffer = VK_NULL_HANDLE;
    b->index_memory = VK_NULL_HANDLE;
}

static VkShaderModule create_shader(VkDevice device, const uint32_t *spv, size_t bytes) {
    if (!spv || !bytes || (bytes & 3u)) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes;
    ci.pCode = spv;
    VkShaderModule m = VK_NULL_HANDLE;
    return vkCreateShaderModule(device, &ci, NULL, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
}

static VkPipeline create_pipeline(AsmUiVkBackend *b,
                                  VkFormat color_format,
                                  VkFormat stencil_format,
                                  VkShaderModule vs,
                                  VkShaderModule fs,
                                  VkBlendFactor src_color,
                                  VkBlendFactor dst_color,
                                  int stencil_mode) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
          VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
          VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL },
    };

    VkVertexInputBindingDescription binding = { 0, sizeof(AsmUiGpuVertex), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrs[6] = {
        {0,0,VK_FORMAT_R32G32_SFLOAT,          offsetof(AsmUiGpuVertex,x)},
        {1,0,VK_FORMAT_R32G32_SFLOAT,          offsetof(AsmUiGpuVertex,u)},
        {2,0,VK_FORMAT_R32G32_SFLOAT,          offsetof(AsmUiGpuVertex,local_x)},
        {3,0,VK_FORMAT_R32_UINT,                offsetof(AsmUiGpuVertex,rgba)},
        {4,0,VK_FORMAT_R32_SFLOAT,              offsetof(AsmUiGpuVertex,alpha)},
        {5,0,VK_FORMAT_R32G32B32A32_SFLOAT,     offsetof(AsmUiGpuVertex,material0)},
    };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 6; vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState att = {0};
    if (stencil_mode == 2 || stencil_mode == 3) {
        att.blendEnable = VK_FALSE;
        att.colorWriteMask = 0;
    } else {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = src_color;
        att.dstColorBlendFactor = dst_color;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
        att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                             VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1; cb.pAttachments = &att;

    VkPipelineDepthStencilStateCreateInfo ds = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    if (stencil_format != VK_FORMAT_UNDEFINED) {
        VkStencilOpState op = {0};
        op.failOp = VK_STENCIL_OP_KEEP;
        op.depthFailOp = VK_STENCIL_OP_KEEP;
        op.compareOp = VK_COMPARE_OP_EQUAL;
        op.compareMask = 0xffu;
        op.reference = 0;
        if (stencil_mode == 2) {
            op.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            op.writeMask = 0xffu;
        } else if (stencil_mode == 3) {
            op.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
            op.writeMask = 0xffu;
        } else {
            op.passOp = VK_STENCIL_OP_KEEP;
            op.writeMask = 0u;
        }
        ds.stencilTestEnable = VK_TRUE;
        ds.front = ds.back = op;
    }

    VkDynamicState dyns[3] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VkPipelineDynamicStateCreateInfo dyn = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = stencil_format == VK_FORMAT_UNDEFINED ? 2u : 3u;
    dyn.pDynamicStates = dyns;

    VkPipelineRenderingCreateInfo rendering = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &color_format;
    rendering.stencilAttachmentFormat = stencil_format;

    VkGraphicsPipelineCreateInfo gp = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    gp.pNext = &rendering;
    gp.stageCount = 2; gp.pStages = stages;
    gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp; gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms; gp.pColorBlendState = &cb;
    gp.pDepthStencilState = stencil_format == VK_FORMAT_UNDEFINED ? NULL : &ds;
    gp.pDynamicState = &dyn; gp.layout = b->pipeline_layout;
    VkPipeline out = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(b->device, VK_NULL_HANDLE, 1, &gp, NULL, &out) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return out;
}

int asmui_vk_create_pipeline_pack_ex(AsmUiVkBackend *b,
                                     VkFormat color_format,
                                     VkFormat stencil_format,
                                     const uint32_t *vert_spv, size_t vert_spv_bytes,
                                     const uint32_t *frag_spv, size_t frag_spv_bytes) {
    if (!b || !b->device) return 0;
    VkDescriptorSetLayoutBinding binding = {0};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dsl = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dsl.bindingCount = 1; dsl.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(b->device, &dsl, NULL, &b->texture_set_layout) != VK_SUCCESS)
        return 0;

    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(AsmUiVkPush) };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.setLayoutCount = 1; pli.pSetLayouts = &b->texture_set_layout;
    pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(b->device, &pli, NULL, &b->pipeline_layout) != VK_SUCCESS) {
        asmui_vk_destroy_pipeline_pack(b); return 0;
    }

    VkShaderModule vs = create_shader(b->device, vert_spv, vert_spv_bytes);
    VkShaderModule fs = create_shader(b->device, frag_spv, frag_spv_bytes);
    if (!vs || !fs) {
        if (vs) vkDestroyShaderModule(b->device, vs, NULL);
        if (fs) vkDestroyShaderModule(b->device, fs, NULL);
        asmui_vk_destroy_pipeline_pack(b); return 0;
    }

    b->stencil_format = stencil_format;
    b->pipeline_alpha = create_pipeline(b, color_format, stencil_format, vs, fs,
                                        VK_BLEND_FACTOR_SRC_ALPHA,
                                        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 1);
    b->pipeline_additive = create_pipeline(b, color_format, stencil_format, vs, fs,
                                           VK_BLEND_FACTOR_SRC_ALPHA,
                                           VK_BLEND_FACTOR_ONE, 1);
    b->pipeline_multiply = create_pipeline(b, color_format, stencil_format, vs, fs,
                                           VK_BLEND_FACTOR_DST_COLOR,
                                           VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, 1);
    if (stencil_format != VK_FORMAT_UNDEFINED) {
        b->pipeline_stencil_push = create_pipeline(b, color_format, stencil_format, vs, fs,
                                                   VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, 2);
        b->pipeline_stencil_pop = create_pipeline(b, color_format, stencil_format, vs, fs,
                                                  VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, 3);
    }
    vkDestroyShaderModule(b->device, fs, NULL);
    vkDestroyShaderModule(b->device, vs, NULL);
    if (!b->pipeline_alpha || !b->pipeline_additive || !b->pipeline_multiply ||
        (stencil_format != VK_FORMAT_UNDEFINED &&
         (!b->pipeline_stencil_push || !b->pipeline_stencil_pop))) {
        asmui_vk_destroy_pipeline_pack(b); return 0;
    }
    return 1;
}

int asmui_vk_create_pipeline_pack(AsmUiVkBackend *b,
                                  VkFormat color_format,
                                  const uint32_t *vert_spv, size_t vert_spv_bytes,
                                  const uint32_t *frag_spv, size_t frag_spv_bytes) {
    return asmui_vk_create_pipeline_pack_ex(b, color_format, VK_FORMAT_UNDEFINED,
                                            vert_spv, vert_spv_bytes,
                                            frag_spv, frag_spv_bytes);
}

void asmui_vk_destroy_pipeline_pack(AsmUiVkBackend *b) {
    if (!b || !b->device) return;
    if (b->pipeline_stencil_pop) vkDestroyPipeline(b->device, b->pipeline_stencil_pop, NULL);
    if (b->pipeline_stencil_push) vkDestroyPipeline(b->device, b->pipeline_stencil_push, NULL);
    if (b->pipeline_multiply) vkDestroyPipeline(b->device, b->pipeline_multiply, NULL);
    if (b->pipeline_additive) vkDestroyPipeline(b->device, b->pipeline_additive, NULL);
    if (b->pipeline_alpha) vkDestroyPipeline(b->device, b->pipeline_alpha, NULL);
    if (b->pipeline_layout) vkDestroyPipelineLayout(b->device, b->pipeline_layout, NULL);
    if (b->texture_set_layout) vkDestroyDescriptorSetLayout(b->device, b->texture_set_layout, NULL);
    b->pipeline_stencil_pop = b->pipeline_stencil_push = VK_NULL_HANDLE;
    b->pipeline_multiply = b->pipeline_additive = b->pipeline_alpha = VK_NULL_HANDLE;
    b->pipeline_layout = VK_NULL_HANDLE;
    b->texture_set_layout = VK_NULL_HANDLE;
    b->stencil_format = VK_FORMAT_UNDEFINED;
}

int asmui_vk_alloc_texture_set(AsmUiVkBackend *b,
                               VkDescriptorPool pool,
                               uint32_t resource_id,
                               VkImageView image_view,
                               VkSampler sampler,
                               VkImageLayout image_layout) {
    if (!b || resource_id >= ASMUI_VK_MAX_TEXTURES || !b->texture_set_layout) return 0;
    VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = pool; ai.descriptorSetCount = 1; ai.pSetLayouts = &b->texture_set_layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(b->device, &ai, &set) != VK_SUCCESS) return 0;
    VkDescriptorImageInfo ii = { sampler, image_view, image_layout };
    VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = set; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(b->device, 1, &w, 0, NULL);
    b->texture_sets[resource_id] = set;
    return 1;
}

int asmui_vk_update_texture_set(AsmUiVkBackend *b,
                                uint32_t resource_id,
                                VkImageView image_view,
                                VkSampler sampler,
                                VkImageLayout image_layout) {
    if (!b || resource_id >= ASMUI_VK_MAX_TEXTURES || !b->texture_sets[resource_id]) return 0;
    VkDescriptorImageInfo ii = { sampler, image_view, image_layout };
    VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet = b->texture_sets[resource_id];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &ii;
    vkUpdateDescriptorSets(b->device, 1, &w, 0, NULL);
    return 1;
}

void asmui_vk_set_white_texture_set(AsmUiVkBackend *b, VkDescriptorSet set) {
    if (b) b->white_texture_set = set;
}

uint32_t asmui_vk_upload_slot(const AsmUiContext *ctx,
                              AsmUiVkBackend *b,
                              uint32_t stream_slot) {
    if (!ctx || !b || !b->vertex_mapped || !b->stream_frames) return 0;
    if (stream_slot >= b->stream_frames) return 0;
    if (ctx->draw_count + ctx->node_count > b->max_quads) return 0;

    uint8_t *mapped = (uint8_t *)b->vertex_mapped;
    AsmUiGpuVertex *base = (AsmUiGpuVertex *)(mapped + b->vertex_segment_bytes * stream_slot);
    uint32_t draw_vertices = asm_ui_build_gpu_vertices(
        ctx, base, ctx->draw_count * 4u);
    uint32_t node_vertices = asm_ui_build_node_vertices(
        ctx, base + draw_vertices, ctx->node_count * 4u);
    if (draw_vertices != ctx->draw_count * 4u ||
        node_vertices != ctx->node_count * 4u) return 0;

    b->active_stream_slot = stream_slot;
    flush_range_if_needed(b->device, b->vertex_memory, b->vertex_memory_flags,
                          b->vertex_segment_bytes * stream_slot,
                          b->vertex_segment_bytes, b->non_coherent_atom_size,
                          b->vertex_allocation_size);
    return draw_vertices;
}

uint32_t asmui_vk_upload(const AsmUiContext *ctx, AsmUiVkBackend *b) {
    return asmui_vk_upload_slot(ctx, b, 0u);
}

static VkPipeline pipeline_for(const AsmUiVkBackend *b, uint32_t material) {
    if (material == ASMUI_MATERIAL_ADDITIVE) return b->pipeline_additive;
    if (material == ASMUI_MATERIAL_MULTIPLY) return b->pipeline_multiply;
    return b->pipeline_alpha;
}

static VkRect2D scissor_from_clip(float clip_x, float clip_y,
                                  float clip_w, float clip_h,
                                  uint32_t framebuffer_w,
                                  uint32_t framebuffer_h) {
    /* ASMUI uses a top-left origin with +Y downward. After the v0.9.1
       vertex fix, Vulkan framebuffer/scissor coordinates use the same convention. */
    float x0f = fmaxf(0.0f, clip_x);
    float y0f = fmaxf(0.0f, clip_y);
    float x1f = fminf((float)framebuffer_w, clip_x + clip_w);
    float y1f = fminf((float)framebuffer_h, clip_y + clip_h);
    int32_t sx = (int32_t)floorf(x0f), sy = (int32_t)floorf(y0f);
    int32_t ex = (int32_t)ceilf(x1f), ey = (int32_t)ceilf(y1f);
    if (ex <= sx || ey <= sy) return (VkRect2D){{0,0},{0,0}};
    return (VkRect2D){{sx,sy},{(uint32_t)(ex-sx),(uint32_t)(ey-sy)}};
}

static uint32_t build_stencil_chain(const AsmUiContext *ctx,
                                    uint32_t nearest_plus_one,
                                    uint32_t *out,
                                    uint32_t capacity) {
    uint32_t reverse[64];
    uint32_t n = 0;
    uint32_t cursor = nearest_plus_one;
    while (cursor && n < 64u) {
        uint32_t idx = cursor - 1u;
        if (idx >= ctx->node_count) break;
        const AsmUiNode *node = &ctx->nodes[idx];
        if (node->mask_mode != ASMUI_MASK_STENCIL) break;
        reverse[n++] = idx;

        int32_t p = node->parent;
        cursor = 0;
        uint32_t guard = 0;
        while (p >= 0 && (uint32_t)p < ctx->node_count && guard++ < 64u) {
            const AsmUiNode *pn = &ctx->nodes[(uint32_t)p];
            if (pn->mask_mode == ASMUI_MASK_STENCIL) {
                cursor = (uint32_t)p + 1u;
                break;
            }
            p = pn->parent;
        }
    }
    if (n > capacity) return 0;
    for (uint32_t i = 0; i < n; ++i) out[i] = reverse[n - 1u - i];
    return n;
}

static void push_common_constants(VkCommandBuffer cmd,
                                  const AsmUiVkBackend *b,
                                  uint32_t framebuffer_w,
                                  uint32_t framebuffer_h,
                                  const AsmUiBatch *bt) {
    AsmUiVkPush push = {0};
    push.viewport_w = (float)framebuffer_w;
    push.viewport_h = (float)framebuffer_h;
    if (bt) {
        push.material_id = bt->material_id;
        push.mask_x = bt->mask_x; push.mask_y = bt->mask_y;
        push.mask_w = bt->mask_w; push.mask_h = bt->mask_h;
        push.mask_radius = bt->mask_radius;
        push.mask_softness = bt->mask_softness;
        push.mask_flags = bt->mask_flags;
    }
    vkCmdPushConstants(cmd, b->pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
}

static int record_stencil_quad(VkCommandBuffer cmd,
                               const AsmUiContext *ctx,
                               AsmUiVkBackend *b,
                               uint32_t node_index,
                               uint32_t reference,
                               int push_mask,
                               uint32_t framebuffer_w,
                               uint32_t framebuffer_h) {
    if (node_index >= ctx->node_count || !b->white_texture_set) return 0;
    uint32_t quad_index = ctx->draw_count + node_index;
    if (quad_index >= b->max_quads) return 0;
    const AsmUiNode *node = &ctx->nodes[node_index];
    VkPipeline pipeline = push_mask ? b->pipeline_stencil_push : b->pipeline_stencil_pop;
    if (!pipeline) return 0;

    VkRect2D sc = scissor_from_clip(node->clip_x, node->clip_y,
                                    node->clip_w, node->clip_h,
                                    framebuffer_w, framebuffer_h);
    if (!sc.extent.width || !sc.extent.height) return 1;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipeline_layout,
                            0, 1, &b->white_texture_set, 0, NULL);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
    push_common_constants(cmd, b, framebuffer_w, framebuffer_h, NULL);
    vkCmdSetScissor(cmd, 0, 1, &sc);
    vkCmdDrawIndexed(cmd, 6u, 1, quad_index * 6u, 0, 0);
    return 1;
}

void asmui_vk_record_images(VkCommandBuffer cmd,
                            const AsmUiContext *ctx,
                            const AsmUiBatch *batches,
                            uint32_t batch_count,
                            AsmUiVkBackend *b,
                            uint32_t framebuffer_w,
                            uint32_t framebuffer_h) {
    if (!cmd || !ctx || !batches || !b || !framebuffer_w || !framebuffer_h) return;
    VkDeviceSize offset = b->vertex_segment_bytes * b->active_stream_slot;
    vkCmdBindVertexBuffers(cmd, 0, 1, &b->vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmd, b->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    VkViewport vp = {0.0f, 0.0f, (float)framebuffer_w, (float)framebuffer_h, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);

    uint32_t current_chain[64] = {0};
    uint32_t current_depth = 0;
    uint32_t target_chain[64] = {0};
    const int stencil_enabled = b->stencil_format != VK_FORMAT_UNDEFINED &&
                                b->pipeline_stencil_push && b->pipeline_stencil_pop;

    for (uint32_t i = 0; i < batch_count; ++i) {
        const AsmUiBatch *bt = &batches[i];
        if (bt->type != ASMUI_NODE_IMAGE && bt->type != ASMUI_NODE_GLYPH) continue;
        if (bt->first_draw >= ctx->draw_count || bt->first_draw >= b->max_quads) break;
        uint32_t count = bt->draw_count;
        if (bt->first_draw + count > ctx->draw_count) count = ctx->draw_count - bt->first_draw;
        if (!count) continue;

        uint32_t target_depth = 0;
        if (stencil_enabled && bt->stencil_parent) {
            target_depth = build_stencil_chain(ctx, bt->stencil_parent, target_chain, 64);
            if (!target_depth || target_depth != bt->stencil_depth) continue;
        }

        if (stencil_enabled) {
            uint32_t common = 0;
            while (common < current_depth && common < target_depth &&
                   current_chain[common] == target_chain[common]) common++;

            for (uint32_t d = current_depth; d > common; --d) {
                record_stencil_quad(cmd, ctx, b, current_chain[d-1u], d,
                                    0, framebuffer_w, framebuffer_h);
            }
            current_depth = common;
            for (uint32_t d = common; d < target_depth; ++d) {
                if (!record_stencil_quad(cmd, ctx, b, target_chain[d], d,
                                         1, framebuffer_w, framebuffer_h)) return;
                current_chain[d] = target_chain[d];
                current_depth = d + 1u;
            }
        }

        VkPipeline pipeline = pipeline_for(b, bt->material_id);
        if (!pipeline) continue;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        if (stencil_enabled)
            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, current_depth);

        VkDescriptorSet set = b->white_texture_set;
        if (bt->resource_id < ASMUI_VK_MAX_TEXTURES && b->texture_sets[bt->resource_id])
            set = b->texture_sets[bt->resource_id];
        if (!set) continue;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipeline_layout,
                                0, 1, &set, 0, NULL);
        push_common_constants(cmd, b, framebuffer_w, framebuffer_h, bt);

        VkRect2D sc = scissor_from_clip(bt->clip_x, bt->clip_y, bt->clip_w, bt->clip_h,
                                        framebuffer_w, framebuffer_h);
        if (!sc.extent.width || !sc.extent.height) continue;
        vkCmdSetScissor(cmd, 0, 1, &sc);
        vkCmdDrawIndexed(cmd, count * 6u, 1, bt->first_draw * 6u, 0, 0);
    }

    if (stencil_enabled) {
        for (uint32_t d = current_depth; d > 0; --d) {
            record_stencil_quad(cmd, ctx, b, current_chain[d-1u], d,
                                0, framebuffer_w, framebuffer_h);
        }
    }
}
