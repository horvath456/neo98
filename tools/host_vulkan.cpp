// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_vulkan.h"
#include "../src/render_profile.h"
#include "vulkan_fixed.h"
#include "vulkan_program.h"
#include "vulkan_texture.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#ifndef NEO98_VULKAN_SHADERS_HEADER
#define NEO98_VULKAN_SHADERS_HEADER "../build/generated/vulkan_shaders.h"
#endif
#include NEO98_VULKAN_SHADERS_HEADER
#include "vulkan_present.h"
namespace HostVulkan
{
namespace
{
using Vertex = Fixed::Vertex;
struct Uniforms
{
    unsigned stages[8][4] = {}, args[8][4] = {};
    Fixed::Vec bumps[8], luminance[8], factor, fog_color, fog;
    unsigned tests[4] = {}, flags[4] = {};
    Fixed::Vec w_range;
};
static_assert(sizeof(Uniforms) % 16 == 0);
struct GlideUniforms
{
    unsigned color[4] = {}, alpha[4] = {}, tmu_rgb[2][4] = {}, tmu_alpha[2][4] = {};
    Fixed::Vec tex_scale[2], detail[2], constant_color, constant_other, fog_color;
    unsigned flags[4] = {}, tests[4] = {};
    int depth[4] = {};
    float fog_table[64] = {};
};
static_assert(sizeof(GlideUniforms) == 512);
struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    VkDeviceSize capacity = 0;
};
struct Image
{
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
class Vulkan final : public Gpu::Backend
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties = {};
    VkPhysicalDeviceFeatures features = {};
    VkQueue queue = VK_NULL_HANDLE;
    bool presentation_support = false;
    std::unique_ptr<Presentation::Desktop> desktop;
    unsigned family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkShaderModule clip_vertex = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkShaderModule glide_vertex = VK_NULL_HANDLE, glide_fragment = VK_NULL_HANDLE;
    VkShaderModule w_fragment = VK_NULL_HANDLE;
    VkShaderModule flat_vertex = VK_NULL_HANDLE, flat_fragment = VK_NULL_HANDLE,
                   flat_w_fragment = VK_NULL_HANDLE;
    std::map<std::vector<unsigned>, std::pair<VkShaderModule, unsigned>> programs;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
    Image color, depth_image;
    Buffer pixels, depth_pixels;
    struct CachedColor
    {
        Image image;
        Buffer pixels;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        Gpu::Surface surface;
        bool invalid = true, initialized = false, dirty = false, staging_current = false;
    } spare;
    std::vector<Buffer> draws, uploads;
    size_t draw_at = 0, upload_at = 0;
    static constexpr unsigned FixedConstants = 70;
    std::map<std::array<unsigned, 29 + FixedConstants>, VkPipeline> pipelines;
    std::map<std::array<unsigned, 9>, VkSampler> samplers;
    std::unordered_map<std::string, VkDescriptorSet> descriptor_sets;
    struct CachedTexture
    {
        Image image;
        Gpu::Surface surface;
        bool current = false;
    };
    std::vector<std::unique_ptr<CachedTexture>> textures;
    struct TextureKey
    {
        unsigned owner, handle;
        const unsigned char *bits;
        bool operator==(const TextureKey &other) const
        { return owner == other.owner && handle == other.handle && bits == other.bits; }
    };
    struct TextureHash
    {
        size_t operator()(const TextureKey &key) const
        { return std::hash<const void *>{}(key.bits) ^ (size_t(key.owner) * size_t(0x9e3779b9u)) ^ key.handle; }
    };
    std::unordered_map<TextureKey, CachedTexture *, TextureHash> texture_lookup;
    std::vector<Image> retired;
    Gpu::Surface bound[8], target, depth;
    bool textured[8] = {};
    int width = 0, height = 0;
    bool active = false, recording = false, failed = false, pending = false, dirty = false, depth_dirty = false,
         has_depth = false;
    bool color_invalid = true, depth_invalid = true;
    bool color_initialized = false;
    bool staging_current = false;
    char name[320] = {};
    unsigned long long submitted = 0, draw_count = 0, pipeline_count = 0, texture_uploads = 0, readbacks = 0,
                       descriptor_count = 0;

    bool Good(VkResult result, const char *operation)
    {
        if (result == VK_SUCCESS)
            return true;
        fprintf(stderr, "Vulkan %s failed: %d\n", operation, result);
        failed = true;
        return false;
    }
    unsigned Memory(unsigned types, VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties p;
        vkGetPhysicalDeviceMemoryProperties(physical, &p);
        for (unsigned i = 0; i < p.memoryTypeCount; ++i)
            if ((types & (1u << i)) && (p.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        return ~0u;
    }
    void Drop(Buffer &b)
    {
        if (b.mapped)
            vkUnmapMemory(device, b.memory);
        if (b.buffer)
            vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory)
            vkFreeMemory(device, b.memory, nullptr);
        b = {};
    }
    void Drop(Image &i)
    {
        if (i.view)
            vkDestroyImageView(device, i.view, nullptr);
        if (i.image)
            vkDestroyImage(device, i.image, nullptr);
        if (i.memory)
            vkFreeMemory(device, i.memory, nullptr);
        i = {};
    }
    bool MakeBuffer(Buffer &b, VkDeviceSize bytes, VkBufferUsageFlags usage)
    {
        if (b.capacity >= bytes)
            return true;
        Drop(b);
        b.capacity = std::max<VkDeviceSize>(bytes, 4096);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = b.capacity;
        info.usage = usage;
        if (!Good(vkCreateBuffer(device, &info, nullptr, &b.buffer), "buffer"))
            return false;
        VkMemoryRequirements needs;
        vkGetBufferMemoryRequirements(device, b.buffer, &needs);
        const auto host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        // Readback is scanned by the CPU. Uncached coherent memory makes those
        // reads much more expensive on discrete GPUs and AMD integrated GPUs.
        unsigned type = Memory(needs.memoryTypeBits, host | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
        if (type == ~0u)
            type = Memory(needs.memoryTypeBits, host);
        if (type == ~0u)
            return false;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = needs.size;
        allocation.memoryTypeIndex = type;
        return Good(vkAllocateMemory(device, &allocation, nullptr, &b.memory), "buffer memory") &&
               Good(vkBindBufferMemory(device, b.buffer, b.memory, 0), "buffer bind") &&
               Good(vkMapMemory(device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "buffer map");
    }
    Buffer *Acquire(std::vector<Buffer> &list, size_t &at, VkDeviceSize bytes, VkBufferUsageFlags usage)
    {
        if (&list == &uploads)
            usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (&list == &draws)
            usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (at == list.size())
            list.emplace_back();
        auto &b = list[at++];
        return MakeBuffer(b, bytes, usage) ? &b : nullptr;
    }
    bool MakeImage(Image &out, int w, int h, VkFormat format, VkImageUsageFlags usage,
                   VkImageAspectFlags aspect, unsigned layers = 1, unsigned levels = 1, bool array = false,
                   unsigned volume_depth = 0)
    {
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = volume_depth ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {unsigned(w), unsigned(h), volume_depth ? volume_depth : 1};
        info.arrayLayers = layers;
        info.mipLevels = levels;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        if (!Good(vkCreateImage(device, &info, nullptr, &out.image), "image"))
            return false;
        VkMemoryRequirements needs;
        vkGetImageMemoryRequirements(device, out.image, &needs);
        unsigned type = Memory(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == ~0u)
            return false;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = needs.size;
        allocation.memoryTypeIndex = type;
        if (!Good(vkAllocateMemory(device, &allocation, nullptr, &out.memory), "image memory") ||
            !Good(vkBindImageMemory(device, out.image, out.memory, 0), "image bind"))
            return false;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = out.image;
        view.viewType = volume_depth ? VK_IMAGE_VIEW_TYPE_3D
                        : array      ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                     : VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {aspect, 0, levels, 0, layers};
        return Good(vkCreateImageView(device, &view, nullptr, &out.view), "image view");
    }
    void Barrier(const Image &i, VkImageLayout old, VkImageLayout next, VkImageAspectFlags aspect,
                 unsigned layers = 1, unsigned levels = 1)
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = old;
        b.newLayout = next;
        b.srcAccessMask =
            old == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = i.image;
        b.subresourceRange = {aspect, 0, levels, 0, layers};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
    }
    bool Wait()
    {
        RenderProfile::Scope profile(RenderProfile::GpuWait);
        Submit();
        if (!pending)
            return !failed;
        if (!Good(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "fence wait"))
            return false;
        pending = false;
        return true;
    }
    void DownloadColor()
    {
        RenderProfile::Scope profile(RenderProfile::GpuReadback);
        Submit(true);
        if (!dirty || !target.bits || !Wait())
            return;
        if (!staging_current)
        {
            if (!StartCommand()) return;
            CopyColorToHost();
            if (!FinishCommand() || !Wait()) return;
            staging_current = true;
        }
        unsigned stride = (target.bit_count + 7) / 8;
        const auto *input = static_cast<unsigned char *>(pixels.mapped);
        if (stride == 2 && target.b_mask == 0x1f && !target.a_mask &&
            ((target.r_mask == 0xf800 && target.g_mask == 0x7e0) ||
             (target.r_mask == 0x7c00 && target.g_mask == 0x3e0)))
        {
            if (target.r_mask == 0xf800)
                Texture::Encode16<6>(input, target);
            else
                Texture::Encode16<5>(input, target);
            dirty = false;
            ++readbacks;
            return;
        }
        if ((stride == 3 || stride == 4) && target.r_mask == 0xff0000 && target.g_mask == 0xff00 &&
            target.b_mask == 0xff && (!target.a_mask || target.a_mask == 0xff000000))
        {
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const auto *p = input + (size_t(y) * width + x) * 4;
                    auto *q = target.bits + y * target.pitch + x * stride;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    if (stride == 4)
                        q[3] = target.a_mask ? p[3] : 0;
                }
            dirty = false;
            ++readbacks;
            return;
        }
        auto pack = [](unsigned c, unsigned m) -> unsigned
        {
            if (!m)
                return 0;
            unsigned shift = __builtin_ctz(m), max = m >> shift;
            return unsigned((uint64_t(c) * max + 127) / 255) << shift;
        };
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto *p = input + (size_t(y) * width + x) * 4;
                unsigned v = pack(p[0], target.r_mask) | pack(p[1], target.g_mask) |
                             pack(p[2], target.b_mask) | pack(p[3], target.a_mask);
                memcpy(target.bits + y * target.pitch + x * stride, &v, stride);
            }
        dirty = false;
        ++readbacks;
    }
    void DownloadDepth()
    {
        Submit();
        if (!depth_dirty || !depth.bits || !Wait())
            return;
        // Most frames only need color on the CPU. Keep depth tiled on the
        // GPU until a Z lock, surface change or saved state reads it.
        if (!StartCommand())
            return;
        Barrier(depth_image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        VkBufferImageCopy copy = {};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {unsigned(width), unsigned(height), 1};
        vkCmdCopyImageToBuffer(command, depth_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               depth_pixels.buffer, 1, &copy);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        copy.bufferOffset = VkDeviceSize(width) * height * 4;
        vkCmdCopyImageToBuffer(command, depth_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               depth_pixels.buffer, 1, &copy);
        Barrier(depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        HostReadBarrier();
        if (!FinishCommand() || !Wait())
            return;
        const unsigned zb = depth.z_bits ? depth.z_bits : depth.bit_count, bytes = (depth.bit_count + 7) / 8;
        const uint64_t mask = zb == 32 ? 0xffffffffull : (1ull << zb) - 1;
        auto *values = static_cast<float *>(depth_pixels.mapped);
        const auto *stencil = static_cast<unsigned char *>(depth_pixels.mapped) + size_t(width) * height * 4;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                unsigned value =
                    unsigned(std::clamp(values[size_t(y) * width + x], 0.0f, 1.0f) * double(mask) + 0.5);
                if (depth.stencil_bits)
                    value |= unsigned(stencil[size_t(y) * width + x]) << zb;
                memcpy(depth.bits + y * depth.pitch + x * bytes, &value, bytes);
            }
        depth_dirty = false;
    }
    void DropSpare()
    {
        if (spare.framebuffer)
            vkDestroyFramebuffer(device, spare.framebuffer, nullptr);
        Drop(spare.image);
        Drop(spare.pixels);
        spare = {};
    }
    void SwapColor()
    {
        std::swap(color, spare.image);
        std::swap(pixels, spare.pixels);
        std::swap(framebuffer, spare.framebuffer);
        std::swap(target, spare.surface);
        std::swap(color_invalid, spare.invalid);
        std::swap(color_initialized, spare.initialized);
        std::swap(dirty, spare.dirty);
        std::swap(staging_current, spare.staging_current);
    }
    bool SelectColor(const Gpu::Surface &surface)
    {
        auto same = [&](const Gpu::Surface &s) {
            return s.bits == surface.bits && s.owner == surface.owner && s.handle == surface.handle;
        };
        if (same(target))
            return true;
        Submit();
        if (!Wait())
            return false;
        if (!same(spare.surface))
        {
            // Eviction must not discard bytes that a later Lock may read.
            SwapColor();
            DownloadColor();
            SwapColor();
            DropSpare();
        }
        SwapColor();
        return color.image || MakeColor(width, height);
    }
    void DropTarget()
    {
        DownloadColor();
        SwapColor();
        DownloadColor();
        SwapColor();
        Wait();
        DropSpare();
        if (framebuffer)
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
        Drop(color);
        Drop(depth_image);
        Drop(pixels);
        Drop(depth_pixels);
        width = height = 0;
        color_initialized = false;
    }
    bool MakeColor(int w, int h)
    {
        if (!MakeImage(color, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT))
            return false;
        VkImageView attachments[] = {color.view, depth_image.view};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = pass;
        fb.attachmentCount = 2;
        fb.pAttachments = attachments;
        fb.width = w;
        fb.height = h;
        fb.layers = 1;
        if (!Good(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer), "framebuffer") ||
            !MakeBuffer(pixels, VkDeviceSize(w) * h * 4,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            return false;
        color_initialized = false;
        return true;
    }
    bool MakeTarget(int w, int h)
    {
        DropTarget();
        if (!MakeImage(depth_image, w, h, VK_FORMAT_D32_SFLOAT_S8_UINT,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) ||
            !MakeColor(w, h) ||
            !MakeBuffer(depth_pixels, VkDeviceSize(w) * h * 8,
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT))
            return false;
        width = w;
        height = h;
        return true;
    }
    void RenderPass()
    {
        VkRenderPassBeginInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        render.renderPass = pass;
        render.framebuffer = framebuffer;
        render.renderArea.extent = {unsigned(width), unsigned(height)};
        vkCmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
    }
    bool StartCommand()
    {
        if (!Wait() || !Good(vkResetCommandBuffer(command, 0), "command reset") ||
            !Good(vkResetFences(device, 1, &fence), "fence reset"))
            return false;
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        return Good(vkBeginCommandBuffer(command, &info), "command begin");
    }
    static VkCompareOp Compare(unsigned op)
    {
        return op >= 1 && op <= 8 ? VkCompareOp(op - 1) : VK_COMPARE_OP_ALWAYS;
    }
    static VkStencilOp Stencil(unsigned op)
    {
        static const VkStencilOp ops[] = {VK_STENCIL_OP_KEEP,
                                          VK_STENCIL_OP_KEEP,
                                          VK_STENCIL_OP_ZERO,
                                          VK_STENCIL_OP_REPLACE,
                                          VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                          VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                          VK_STENCIL_OP_INVERT,
                                          VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                          VK_STENCIL_OP_DECREMENT_AND_WRAP};
        return op < 9 ? ops[op] : VK_STENCIL_OP_KEEP;
    }
    static VkBlendFactor Blend(unsigned op, bool alpha = false)
    {
        static const VkBlendFactor ops[] = {VK_BLEND_FACTOR_ONE,
                                            VK_BLEND_FACTOR_ZERO,
                                            VK_BLEND_FACTOR_ONE,
                                            VK_BLEND_FACTOR_SRC_COLOR,
                                            VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                            VK_BLEND_FACTOR_SRC_ALPHA,
                                            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                            VK_BLEND_FACTOR_DST_ALPHA,
                                            VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                            VK_BLEND_FACTOR_DST_COLOR,
                                            VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                            VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
                                            VK_BLEND_FACTOR_SRC_ALPHA,
                                            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA};
        VkBlendFactor r = op < 14 ? ops[op] : VK_BLEND_FACTOR_ONE;
        if (alpha)
        {
            if (r == VK_BLEND_FACTOR_SRC_COLOR)
                r = VK_BLEND_FACTOR_SRC_ALPHA;
            if (r == VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR)
                r = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            if (r == VK_BLEND_FACTOR_DST_COLOR)
                r = VK_BLEND_FACTOR_DST_ALPHA;
            if (r == VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR)
                r = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        }
        return r;
    }
    VkShaderModule ShaderModule(const Gpu::Shader &shader, bool pixel, bool wbuffer, bool flat, unsigned &id)
    {
        id = 0;
        if (!shader.tokens || shader.count < 2 || shader.count > 65536)
            return VK_NULL_HANDLE;
        std::vector<unsigned> key(shader.tokens, shader.tokens + shader.count);
        key.push_back(pixel);
        key.push_back(wbuffer);
        key.push_back(flat);
        auto found = programs.find(key);
        if (found != programs.end())
        {
            id = found->second.second;
            return found->second.first;
        }
        std::string source, error;
        std::vector<unsigned> words;
        VkShaderModule module = VK_NULL_HANDLE;
        if (Program::Translate(shader.tokens, shader.count, pixel, wbuffer, source, error, flat) &&
            Program::Compile(source, pixel, words, error))
        {
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = words.size() * 4;
            info.pCode = words.data();
            Good(vkCreateShaderModule(device, &info, nullptr, &module), "D3D shader module");
        }
        else
            fprintf(stderr, "Vulkan D3D shader: %s\n", error.c_str());
        id = unsigned(programs.size()) + 1;
        programs.emplace(std::move(key), std::make_pair(module, id));
        return module;
    }
    VkPipeline Pipeline(VkPrimitiveTopology topology, const Gpu::State &state, bool glide = false, bool packed = false)
    {
        std::array<unsigned, 29 + FixedConstants> key = {unsigned(topology), unsigned(has_depth)};
        key[24] = glide;
        const bool flat = state.render[9] == 1;
        key[27] = flat;
        key[28 + FixedConstants] = packed;
        VkShaderModule vs = glide ? glide_vertex : packed ? clip_vertex : flat ? flat_vertex : vertex;
        VkShaderModule ps = glide                  ? glide_fragment
                            : state.render[7] == 2 ? (flat ? flat_w_fragment : w_fragment)
                                                   : (flat ? flat_fragment : fragment);
        if (!glide && state.vertex_shader.tokens)
            vs = ShaderModule(state.vertex_shader, false, false, flat, key[25]);
        if (!glide && state.pixel_shader.tokens)
            ps = ShaderModule(state.pixel_shader, true, state.render[7] == 2, flat, key[26]);
        if (!vs || !ps)
            return VK_NULL_HANDLE;
        constexpr unsigned states[] = {7,  8,  14, 19, 20, 22,  23,  27,  47,  52,  53,
                                       54, 55, 56, 58, 59, 168, 171, 206, 207, 208, 209};
        for (unsigned i = 0; i < 22; ++i)
            key[i + 2] = state.render[states[i]];
        auto *fixed = key.data() + 28;
        const bool specialize = !glide && !state.pixel_shader.tokens;
        if (specialize)
        {
            for (unsigned i = 0; i < 8 && state.stage[i][1] > 1; ++i)
            {
                ++fixed[0];
                const auto *s = state.stage[i];
                fixed[1 + i * 4] = s[1]; fixed[2 + i * 4] = s[4];
                fixed[3 + i * 4] = textured[i] ? (bound[i].slices > 0 ? 3 : bound[i].faces ? 2 : 1) : 0;
                if (textured[i] && bound[i].color_key && state.render[41]) fixed[3 + i * 4] |= 256;
                fixed[4 + i * 4] = (s[24] & 256) ? s[24] & 255 : 0;
                fixed[33 + i * 4] = s[2]; fixed[34 + i * 4] = s[3];
                fixed[35 + i * 4] = s[5]; fixed[36 + i * 4] = s[6];
            }
            fixed[65] = state.render[15]; fixed[66] = state.render[25];
            fixed[67] = state.render[29]; fixed[68] = state.render[28]; fixed[69] = state.render[35];
        }
        auto found = pipelines.find(key);
        if (found != pipelines.end())
            return found->second;
        VkPipelineShaderStageCreateInfo stages[2] = {};
        for (auto &s : stages)
        {
            s.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            s.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = ps;
        VkSpecializationMapEntry constants[FixedConstants];
        for (unsigned i = 0; i < FixedConstants; ++i) constants[i] = {i, i * 4, 4};
        VkSpecializationInfo specialization{FixedConstants, constants, FixedConstants * 4, fixed};
        if (specialize) stages[1].pSpecializationInfo = &specialization;
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[16] = {
            {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, unsigned(offsetof(Vertex, position))},
            {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, unsigned(offsetof(Vertex, color))},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, unsigned(offsetof(Vertex, specular))}};
        for (unsigned i = 0; i < 8; ++i)
            attrs[3 + i] = {3 + i, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                            unsigned(offsetof(Vertex, tex) + i * 16)};
        attrs[11] = {11, 0, VK_FORMAT_R32G32_SFLOAT, unsigned(offsetof(Vertex, depth))};
        attrs[12] = {12, 0, VK_FORMAT_R32G32B32A32_SFLOAT, unsigned(offsetof(Vertex, clip))};
        attrs[13] = {13, 0, VK_FORMAT_R32G32_SFLOAT, unsigned(offsetof(Vertex, clip) + 16)};
        const bool programmable_vertex = !glide && state.vertex_shader.tokens;
        if (programmable_vertex)
        {
            binding.stride = 256;
            for (unsigned i = 0; i < 16; ++i)
                attrs[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
        }
        if (packed)
        {
            binding.stride = 56;
            attrs[0] = {0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0};
            attrs[1] = {1,0,VK_FORMAT_R8G8B8A8_UNORM,16};
            attrs[2] = {2,0,VK_FORMAT_R8G8B8A8_UNORM,20};
            attrs[3] = {3,0,VK_FORMAT_R32G32B32A32_SFLOAT,24};
            attrs[4] = {4,0,VK_FORMAT_R32G32B32A32_SFLOAT,40};
        }
        VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = packed ? 5 : programmable_vertex ? 16 : 14;
        input.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = topology;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.lineWidth = 1;
        raster.polygonMode = features.fillModeNonSolid && state.render[8] == 1   ? VK_POLYGON_MODE_POINT
                             : features.fillModeNonSolid && state.render[8] == 2 ? VK_POLYGON_MODE_LINE
                                                                                 : VK_POLYGON_MODE_FILL;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.cullMode = state.render[22] == 2   ? VK_CULL_MODE_FRONT_BIT
                          : state.render[22] == 3 ? VK_CULL_MODE_BACK_BIT
                                                  : VK_CULL_MODE_NONE;
        raster.depthBiasEnable = state.render[47] != 0;
        VkPipelineMultisampleStateCreateInfo samples{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment = {};
        attachment.colorWriteMask = state.render[168] & 15;
        attachment.blendEnable = state.render[27] != 0;
        attachment.srcColorBlendFactor = Blend(state.render[19]);
        attachment.dstColorBlendFactor = state.render[19] == 12   ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
                                         : state.render[19] == 13 ? VK_BLEND_FACTOR_SRC_ALPHA
                                                                  : Blend(state.render[20]);
        attachment.srcAlphaBlendFactor =
            Blend(state.render[206] ? state.render[207] : state.render[19], true);
        attachment.dstAlphaBlendFactor =
            Blend(state.render[206] ? state.render[208] : state.render[20], true);
        if (state.render[19] == 12)
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        if (state.render[19] == 13)
            attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        auto blendop = [](unsigned v) { return v >= 1 && v <= 5 ? VkBlendOp(v - 1) : VK_BLEND_OP_ADD; };
        attachment.colorBlendOp = blendop(state.render[171]);
        attachment.alphaBlendOp = blendop(state.render[206] ? state.render[209] : state.render[171]);
        VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        VkPipelineDepthStencilStateCreateInfo z{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        z.depthTestEnable = has_depth && state.render[7];
        z.depthWriteEnable = has_depth && state.render[14];
        z.depthCompareOp = Compare(state.render[23]);
        z.stencilTestEnable = has_depth && depth.stencil_bits && state.render[52];
        z.front = {Stencil(state.render[53]),
                   Stencil(state.render[55]),
                   Stencil(state.render[54]),
                   Compare(state.render[56]),
                   state.render[58],
                   state.render[59],
                   0};
        z.back = z.front;
        VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                 VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 4;
        dyn.pDynamicStates = dyns;
        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pColorBlendState = &blend;
        info.pDepthStencilState = &z;
        info.pDynamicState = &dyn;
        info.layout = layout;
        info.renderPass = pass;
        VkPipeline result = VK_NULL_HANDLE;
        if (!Good(vkCreateGraphicsPipelines(device, pipeline_cache, 1, &info, nullptr, &result), "pipeline"))
            return VK_NULL_HANDLE;
        pipelines.emplace(key, result);
        ++pipeline_count;
        return result;
    }
    VkSampler Sampler(const Gpu::State &state, unsigned stage)
    {
        const auto *s = state.stage[stage];
        std::array<unsigned, 9> key = {s[13] ? s[13] : s[12],
                                       s[14] ? s[14] : s[12],
                                       s[16],
                                       s[17],
                                       s[18],
                                       s[21],
                                       s[25] ? s[25] : s[12],
                                       s[19],
                                       s[20]};
        auto found = samplers.find(key);
        if (found != samplers.end())
            return found->second;
        auto address = [](unsigned a)
        {
            return a == 2   ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
                   : a == 3 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
                   : a == 4 ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                            : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        };
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = key[2] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.minFilter = key[3] >= 2 ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = key[4] == 3 ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.maxLod = key[4] >= 2 ? 12.0f : 0.0f;
        info.addressModeU = address(key[0]);
        info.addressModeV = address(key[1]);
        info.addressModeW = address(key[6]);
        info.mipLodBias = std::clamp(Fixed::Float(key[7]), -properties.limits.maxSamplerLodBias,
                                     properties.limits.maxSamplerLodBias);
        info.minLod = std::min(float(key[8]), info.maxLod);
        info.anisotropyEnable = features.samplerAnisotropy && (key[3] == 3 || key[2] == 5);
        info.maxAnisotropy =
            std::clamp(float(key[5] ? key[5] : 1), 1.0f, properties.limits.maxSamplerAnisotropy);
        VkSampler result = VK_NULL_HANDLE;
        if (!Good(vkCreateSampler(device, &info, nullptr, &result), "sampler"))
            return VK_NULL_HANDLE;
        samplers.emplace(key, result);
        return result;
    }
    CachedTexture *TextureImage(const Gpu::Surface &given)
    {
        RenderProfile::Scope profile(RenderProfile::GpuTextures);
        const bool volume = given.slices > 0;
        if (given.width <= 0 || given.height <= 0 || given.width > 4096 || given.height > 4096 ||
            given.slices < 0 ||
            (volume && (given.faces || unsigned(std::max({given.width, given.height, given.slices})) >
                                           properties.limits.maxImageDimension3D)))
            return nullptr;
        const unsigned layers = given.faces ? 6 : 1, levels = given.level_count + 1;
        unsigned maximum_levels = 1;
        for (unsigned size = unsigned(std::max({given.width, given.height, given.slices})); size > 1;
             size >>= 1)
            ++maximum_levels;
        if (levels > maximum_levels || given.level_count > Gpu::Surface::MaxLevels)
            return nullptr;
        const TextureKey key{given.owner, given.handle, given.bits};
        auto found = texture_lookup.find(key);
        CachedTexture *entry = found == texture_lookup.end() ? nullptr : found->second;
        if (!entry)
        {
            textures.push_back(std::make_unique<CachedTexture>());
            entry = textures.back().get();
            texture_lookup.emplace(key, entry);
        }
        bool fresh = !entry->image.image || entry->surface.width != given.width ||
                     entry->surface.height != given.height || entry->surface.slices != given.slices ||
                     entry->surface.level_count != given.level_count ||
                     bool(entry->surface.faces) != bool(given.faces);
        if (!fresh && entry->current && entry->surface.generation == given.generation &&
            entry->surface.color_key == given.color_key && entry->surface.key_low == given.key_low &&
            entry->surface.key_high == given.key_high)
            return entry;
        std::vector<std::vector<unsigned char>> decoded(layers * levels);
        for (unsigned layer = 0; layer < layers; ++layer)
            for (unsigned level = 0; level < levels; ++level)
            {
                Gpu::Surface source = given.faces ? given.faces[layer] : given;
                if (source.level_count + 1 < levels)
                    return nullptr;
                if (level)
                {
                    const auto &l = source.levels[level - 1];
                    source.bits = l.bits;
                    source.width = l.width;
                    source.height = l.height;
                    source.pitch = l.pitch;
                    source.slices = l.slices;
                }
                if (source.width != std::max(given.width >> level, 1) ||
                    source.height != std::max(given.height >> level, 1) ||
                    (volume && source.slices != std::max(given.slices >> level, 1)) ||
                    !Texture::DecodeVolume(source, decoded[layer * levels + level]))
                    return nullptr;
            }
        if (fresh)
        {
            if (entry->image.image)
            {
                retired.push_back(entry->image);
                entry->image = {};
            }
            if (!MakeImage(entry->image, given.width, given.height, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, layers, levels, true,
                           volume ? unsigned(given.slices) : 0))
                return nullptr;
        }
        vkCmdEndRenderPass(command);
        Barrier(entry->image, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, layers, levels);
        for (unsigned layer = 0; layer < layers; ++layer)
            for (unsigned level = 0; level < levels; ++level)
            {
                Gpu::Surface source = given.faces ? given.faces[layer] : given;
                if (level)
                {
                    const auto &l = source.levels[level - 1];
                    source.bits = l.bits;
                    source.width = l.width;
                    source.height = l.height;
                    source.pitch = l.pitch;
                }
                const auto &converted = decoded[layer * levels + level];
                Buffer *buffer =
                    Acquire(uploads, upload_at, converted.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                if (!buffer)
                {
                    RenderPass();
                    return nullptr;
                }
                memcpy(buffer->mapped, converted.data(), converted.size());
                VkBufferImageCopy copy = {};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
                copy.imageExtent = {unsigned(source.width), unsigned(source.height),
                                    volume ? unsigned(std::max(given.slices >> level, 1)) : 1};
                vkCmdCopyBufferToImage(command, buffer->buffer, entry->image.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            }
        Barrier(entry->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT, layers, levels);
        RenderPass();
        entry->surface = given;
        entry->current = true;
        ++texture_uploads;
        return entry;
    }
    VkDescriptorSet BindTextures(const Gpu::State &state, const GlideUniforms *glide = nullptr)
    {
        RenderProfile::Scope profile(RenderProfile::GpuBindings);
        static unsigned white = 0xffffffff;
        Gpu::Surface blank;
        blank.bits = reinterpret_cast<unsigned char *>(&white);
        blank.width = blank.height = 1;
        blank.pitch = 4;
        blank.bit_count = 32;
        blank.r_mask = 0xff0000;
        blank.g_mask = 0xff00;
        blank.b_mask = 0xff;
        blank.a_mask = 0xff000000;
        static unsigned volume_white = 0xffffffff;
        Gpu::Surface volume_blank = blank;
        volume_blank.bits = reinterpret_cast<unsigned char *>(&volume_white);
        volume_blank.slices = 1;
        VkDescriptorImageInfo images[8] = {}, volumes[8] = {};
        Uniforms u;
        unsigned stages = 8;
        if (!glide && !state.pixel_shader.tokens)
        {
            stages = 0;
            while (stages < 8 && state.stage[stages][1] > 1) ++stages;
        }
        auto *white_texture = TextureImage(blank);
        auto *white_volume = TextureImage(volume_blank);
        const auto white_sampler = Sampler(state, 0);
        if (!white_texture || !white_volume || !white_sampler) return VK_NULL_HANDLE;
        for (unsigned i = 0; i < 8; ++i)
        {
            if (i >= stages)
            {
                images[i] = {white_sampler, white_texture->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                volumes[i] = {white_sampler, white_volume->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                continue;
            }
            const auto *s = state.stage[i];
            Gpu::Surface source = textured[i] ? bound[i] : blank;
            source.color_key = source.color_key && state.render[41];
            const bool volume = source.slices > 0;
            CachedTexture *texture = volume || !textured[i] ? white_texture : TextureImage(source);
            if (!texture)
                return VK_NULL_HANDLE;
            images[i] = {Sampler(state, i), texture->image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if (!images[i].sampler)
                return VK_NULL_HANDLE;
            CachedTexture *volume_texture = volume ? TextureImage(source) : white_volume;
            if (!volume_texture)
                return VK_NULL_HANDLE;
            volumes[i] = {images[i].sampler, volume_texture->image.view,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            u.stages[i][0] = s[1];
            u.stages[i][1] = s[4];
            u.stages[i][2] = textured[i] ? (volume ? 3 : bound[i].faces ? 2 : 1) : 0;
            if (textured[i] && bound[i].bump)
                u.stages[i][2] |= 512;
            if (textured[i] && bound[i].color_key && state.render[41])
                u.stages[i][2] |= 256;
            u.stages[i][3] = (s[24] & 256) ? s[24] & 255 : 0;
            u.args[i][0] = s[2];
            u.args[i][1] = s[3];
            u.args[i][2] = s[5];
            u.args[i][3] = s[6];
            for (unsigned j = 0; j < 4; ++j)
                u.bumps[i][j] = Fixed::Float(s[7 + j]);
            u.luminance[i][0] = Fixed::Float(s[22]);
            u.luminance[i][1] = Fixed::Float(s[23]);
        }
        u.factor = Fixed::Color(state.render[60]);
        u.fog_color = Fixed::Color(state.render[34]);
        u.fog = {{Fixed::Float(state.render[36]), Fixed::Float(state.render[37]),
                  Fixed::Float(state.render[38]), float(state.render[35])}};
        u.tests[0] = state.render[25];
        u.tests[1] = state.render[24] & 255;
        u.tests[2] = state.render[15];
        u.tests[3] = state.render[29];
        u.flags[0] = state.render[28];
        u.w_range = {{state.w_near, state.w_far > state.w_near ? state.w_far : state.w_near + 1, 0, 0}};
        const size_t uniform_size = glide ? sizeof *glide : sizeof u;
        std::string key;
        auto append = [&](const void *data, size_t size)
        { key.append(static_cast<const char *>(data), size); };
        const unsigned kind[] = {unsigned(glide != nullptr), unsigned(state.vertex_shader.tokens != nullptr),
                                 unsigned(state.pixel_shader.tokens != nullptr)};
        append(kind, sizeof kind);
        for (unsigned i = 0; i < 8; ++i)
        {
            append(&images[i].sampler, sizeof images[i].sampler);
            append(&images[i].imageView, sizeof images[i].imageView);
            append(&volumes[i].imageView, sizeof volumes[i].imageView);
        }
        append(glide ? static_cast<const void *>(glide) : &u, uniform_size);
        if (!glide && state.vertex_shader.tokens)
        {
            append(state.vs_constants, sizeof state.vs_constants);
            append(state.vs_integers, sizeof state.vs_integers);
            append(state.vs_booleans, sizeof state.vs_booleans);
            append(&state.viewport_width, sizeof state.viewport_width);
            append(&state.viewport_height, sizeof state.viewport_height);
        }
        if (!glide && state.pixel_shader.tokens)
        {
            append(state.ps_constants, sizeof state.ps_constants);
            append(state.ps_integers, sizeof state.ps_integers);
            append(state.ps_booleans, sizeof state.ps_booleans);
        }
        auto cached = descriptor_sets.find(key);
        if (cached != descriptor_sets.end())
            return cached->second;
        Buffer *buffer = Acquire(uploads, upload_at, uniform_size,
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        if (!buffer)
            return VK_NULL_HANDLE;
        memcpy(buffer->mapped, glide ? static_cast<const void *>(glide) : &u, uniform_size);
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = descriptor_pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptors;
        VkDescriptorSet set;
        if (!Good(vkAllocateDescriptorSets(device, &allocation, &set), "descriptor set"))
            return VK_NULL_HANDLE;
        VkDescriptorBufferInfo uniform{buffer->buffer, 0, uniform_size};
        VkWriteDescriptorSet writes[3] = {};
        for (auto &w : writes)
        {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
        }
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 8;
        writes[0].pImageInfo = images;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &uniform;
        writes[2].dstBinding = 4;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 8;
        writes[2].pImageInfo = volumes;
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        if (!glide && (state.vertex_shader.tokens || state.pixel_shader.tokens))
        {
            for (unsigned stage = 0; stage < 2; ++stage)
            {
                if (stage ? !state.pixel_shader.tokens : !state.vertex_shader.tokens)
                    continue;
                const unsigned constants = stage ? Gpu::State::PixelConstants : Gpu::State::VertexConstants;
                const size_t size = constants * 16 + 256 + 64 + (stage ? 0 : 32);
                Buffer *storage = Acquire(uploads, upload_at, size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
                if (!storage)
                    return VK_NULL_HANDLE;
                auto *data = static_cast<unsigned char *>(storage->mapped);
                memcpy(data, stage ? state.ps_constants : state.vs_constants, constants * 16);
                memcpy(data + constants * 16, stage ? state.ps_integers : state.vs_integers, 256);
                memcpy(data + constants * 16 + 256, stage ? state.ps_booleans : state.vs_booleans, 64);
                if (!stage)
                {
                    const float viewport[] = {float(state.viewport_width ? state.viewport_width : width),
                                              float(state.viewport_height ? state.viewport_height : height),
                                              features.largePoints ? properties.limits.pointSizeRange[0] : 1,
                                              features.largePoints ? properties.limits.pointSizeRange[1] : 1};
                    memcpy(data + size - 32, viewport, 16);
                    unsigned flags[] = {unsigned(state.render[28] && !state.render[35]), 0, 0, 0};
                    memcpy(data + size - 16, flags, 16);
                }
                VkDescriptorBufferInfo info{storage->buffer, 0, size};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = stage ? 3 : 2;
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.descriptorCount = 1;
                write.pBufferInfo = &info;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
        }
        descriptor_sets.emplace(std::move(key), set);
        ++descriptor_count;
        return set;
    }

  public:
    ~Vulkan() override
    {
        if (device)
        {
            Wait();
            desktop.reset();
            DropTarget();
            for (auto &t : textures)
                Drop(t->image);
            for (auto &image : retired)
                Drop(image);
            for (auto &b : draws)
                Drop(b);
            for (auto &b : uploads)
                Drop(b);
            for (auto &p : pipelines)
                vkDestroyPipeline(device, p.second, nullptr);
            for (auto &s : samplers)
                vkDestroySampler(device, s.second, nullptr);
            if (framebuffer)
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (pass)
                vkDestroyRenderPass(device, pass, nullptr);
            if (layout)
                vkDestroyPipelineLayout(device, layout, nullptr);
            if (descriptor_pool)
                vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            if (descriptors)
                vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
            if (clip_vertex) vkDestroyShaderModule(device, clip_vertex, nullptr);
            if (vertex)
                vkDestroyShaderModule(device, vertex, nullptr);
            if (fragment)
                vkDestroyShaderModule(device, fragment, nullptr);
            if (glide_vertex)
                vkDestroyShaderModule(device, glide_vertex, nullptr);
            if (glide_fragment)
                vkDestroyShaderModule(device, glide_fragment, nullptr);
            if (w_fragment)
                vkDestroyShaderModule(device, w_fragment, nullptr);
            if (flat_vertex)
                vkDestroyShaderModule(device, flat_vertex, nullptr);
            if (flat_fragment)
                vkDestroyShaderModule(device, flat_fragment, nullptr);
            if (flat_w_fragment)
                vkDestroyShaderModule(device, flat_w_fragment, nullptr);
            for (auto &p : programs)
                if (p.second.first)
                    vkDestroyShaderModule(device, p.second.first, nullptr);
            if (pipeline_cache)
                vkDestroyPipelineCache(device, pipeline_cache, nullptr);
            if (fence)
                vkDestroyFence(device, fence, nullptr);
            if (pool)
                vkDestroyCommandPool(device, pool, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance)
            vkDestroyInstance(instance, nullptr);
        if (getenv("NEO98_GPU_STATS"))
            fprintf(stderr,
                    "VULKAN STATS batches=%llu draws=%llu pipelines=%llu texture_uploads=%llu readbacks=%llu "
                    "descriptors=%llu\n",
                    submitted, draw_count, pipeline_count, texture_uploads, readbacks, descriptor_count);
    }
    bool Open()
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "NEO98";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        unsigned extension_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> available(extension_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available.data());
        auto extension = [&](const char *name) { return std::any_of(available.begin(), available.end(),
            [&](auto &e) { return !strcmp(e.extensionName, name); }); };
        std::vector<const char *> extensions;
        if (extension(VK_KHR_SURFACE_EXTENSION_NAME) && extension(VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
        {
            extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
            presentation_support = true;
        }
        const char *validation = "VK_LAYER_KHRONOS_validation";
        VkValidationFeatureEnableEXT synchronization =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT checks{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
        const char *validation_extension = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
        if (getenv("NEO98_VULKAN_VALIDATION"))
        {
            info.enabledLayerCount = 1;
            info.ppEnabledLayerNames = &validation;
            if (!strcmp(getenv("NEO98_VULKAN_VALIDATION"), "sync"))
            {
                checks.enabledValidationFeatureCount = 1;
                checks.pEnabledValidationFeatures = &synchronization;
                info.pNext = &checks;
                extensions.push_back(validation_extension);
            }
        }
        info.enabledExtensionCount = unsigned(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        // An empty list of drivers (VK_ICD_FILENAMES= in a shell profile)
        // makes the loader find none at all; it means the installed ones.
        for (const char *name : {"VK_ICD_FILENAMES", "VK_DRIVER_FILES"})
            if (const char *files = getenv(name); files && !*files)
                unsetenv(name);
        if (!Good(vkCreateInstance(&info, nullptr, &instance), "instance"))
            return false;
        unsigned count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());
        for (auto candidate : devices)
        {
            VkPhysicalDeviceFeatures f;
            vkGetPhysicalDeviceFeatures(candidate, &f);
            if (!f.shaderClipDistance)
                continue;
            VkFormatProperties format;
            vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_D32_SFLOAT_S8_UINT, &format);
            if (!(format.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
                continue;
            unsigned queues = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues, nullptr);
            std::vector<VkQueueFamilyProperties> families(queues);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queues, families.data());
            for (unsigned i = 0; i < queues && !physical; ++i)
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    physical = candidate;
                    family = i;
                    features.shaderClipDistance = VK_TRUE;
                    features.fillModeNonSolid = f.fillModeNonSolid;
                    features.largePoints = f.largePoints;
                    features.samplerAnisotropy = f.samplerAnisotropy;
                }
            if (physical)
                break;
        }
        if (!physical)
            return false;
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        di.pEnabledFeatures = &features;
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, nullptr);
        available.resize(extension_count);
        vkEnumerateDeviceExtensionProperties(physical, nullptr, &extension_count, available.data());
        presentation_support &= extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        const char *swapchain_extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        if (presentation_support) { di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &swapchain_extension; }
        if (!Good(vkCreateDevice(physical, &di, nullptr, &device), "device"))
            return false;
        vkGetDeviceQueue(device, family, 0, &queue);
        vkGetPhysicalDeviceProperties(physical, &properties);
        snprintf(name, sizeof name, "Vulkan: %s", properties.deviceName);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pi.queueFamilyIndex = family;
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (!Good(vkCreateCommandPool(device, &pi, nullptr, &pool), "command pool"))
            return false;
        VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ci.commandPool = pool;
        ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ci.commandBufferCount = 1;
        if (!Good(vkAllocateCommandBuffers(device, &ci, &command), "command buffer"))
            return false;
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!Good(vkCreateFence(device, &fi, nullptr, &fence), "fence"))
            return false;
        VkPipelineCacheCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (!Good(vkCreatePipelineCache(device, &pci, nullptr, &pipeline_cache), "pipeline cache"))
            return false;
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = sizeof Vulkan_clip_vert;
        si.pCode = Vulkan_clip_vert;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &clip_vertex), "clip vertex shader")) return false;
        si.codeSize = sizeof Vulkan_vert;
        si.pCode = Vulkan_vert;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &vertex), "vertex shader"))
            return false;
        si.codeSize = sizeof Vulkan_frag;
        si.pCode = Vulkan_frag;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &fragment), "fragment shader"))
            return false;
        si.codeSize = sizeof Vulkan_w_frag;
        si.pCode = Vulkan_w_frag;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &w_fragment), "W-buffer shader"))
            return false;
        si.codeSize = sizeof Vulkan_flat_vert;
        si.pCode = Vulkan_flat_vert;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &flat_vertex), "flat vertex shader"))
            return false;
        si.codeSize = sizeof Vulkan_flat_frag;
        si.pCode = Vulkan_flat_frag;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &flat_fragment), "flat fragment shader"))
            return false;
        si.codeSize = sizeof Vulkan_flat_w_frag;
        si.pCode = Vulkan_flat_w_frag;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &flat_w_fragment), "flat W-buffer shader"))
            return false;
        si.codeSize = sizeof Vulkan_glide_vert;
        si.pCode = Vulkan_glide_vert;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &glide_vertex), "Glide vertex shader"))
            return false;
        si.codeSize = sizeof Vulkan_glide_frag;
        si.pCode = Vulkan_glide_frag;
        if (!Good(vkCreateShaderModule(device, &si, nullptr, &glide_fragment), "Glide fragment shader"))
            return false;
        VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = 5;
        dli.pBindings = bindings;
        if (!Good(vkCreateDescriptorSetLayout(device, &dli, nullptr, &descriptors), "descriptor layout"))
            return false;
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16384},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3072}};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets = 1024;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = sizes;
        if (!Good(vkCreateDescriptorPool(device, &dpi, nullptr, &descriptor_pool), "descriptor pool"))
            return false;
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount = 1;
        li.pSetLayouts = &descriptors;
        if (!Good(vkCreatePipelineLayout(device, &li, nullptr, &layout), "pipeline layout"))
            return false;
        VkAttachmentDescription attachments[2] = {};
        for (auto &a : attachments)
        {
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        }
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].initialLayout = attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1].format = VK_FORMAT_D32_SFLOAT_S8_UINT;
        attachments[1].initialLayout = attachments[1].finalLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            zr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub = {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &cr;
        sub.pDepthStencilAttachment = &zr;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = 2;
        ri.pAttachments = attachments;
        ri.subpassCount = 1;
        ri.pSubpasses = &sub;
        return Good(vkCreateRenderPass(device, &ri, nullptr, &pass), "render pass");
    }
    const char *Name() const override { return name; }
    unsigned Limit(Capability kind, unsigned value) const override
    {
        if (kind == Capability::Filter && !features.samplerAnisotropy)
            return value & ~(0x400u | 0x4000000u);
        if (kind == Capability::Anisotropy)
            return features.samplerAnisotropy
                       ? std::min(value, unsigned(properties.limits.maxSamplerAnisotropy))
                       : 1;
        return value;
    }
    bool CompressedTextures() const override { return true; }
    bool TransformAndLight() const override { return true; }
    bool DriverModel8() const override { return !getenv("NEO98_D3D_DX7_DRIVER"); }
    bool DriverModel9() const override { return DriverModel8() && !getenv("NEO98_D3D_DX8_DRIVER"); }
    bool PixelShaders() const override
    {
        return !getenv("NEO98_D3D_NO_PIXEL_SHADERS") && Program::Available();
    }
    bool VertexShaders() const override
    {
        return !getenv("NEO98_D3D_NO_VERTEX_SHADERS") && Program::Available();
    }
    bool Glide() const override { return !getenv("NEO98_NO_GLIDE"); }
    bool Begin(const Gpu::Surface &surface, const Gpu::Surface *z) override
    {
        if (active || failed || !surface.bits || surface.width <= 0 || surface.height <= 0 ||
            surface.width > 4096 || surface.height > 4096 ||
            (surface.bit_count != 16 && surface.bit_count != 24 && surface.bit_count != 32) ||
            surface.pitch < surface.width * int((surface.bit_count + 7) / 8) || !surface.r_mask ||
            !surface.g_mask || !surface.b_mask)
            return false;
        if (z && (!z->bits || z->width != surface.width || z->height != surface.height || !z->bit_count ||
                  z->bit_count > 32 || z->pitch < z->width * int((z->bit_count + 7) / 8) ||
                  (z->z_bits && (z->z_bits > z->bit_count || z->z_bits + z->stencil_bits > z->bit_count)) ||
                  z->stencil_bits > 8 || (z->stencil_bits && !z->z_bits)))
            return false;
        if (width == surface.width && height == surface.height && !SelectColor(surface))
            return false;
        const bool keep_color = !color_invalid && width == surface.width && height == surface.height &&
                                target.bits == surface.bits && target.owner == surface.owner &&
                                target.handle == surface.handle && target.generation == surface.generation &&
                                target.bit_count == surface.bit_count && target.pitch == surface.pitch &&
                                target.r_mask == surface.r_mask && target.g_mask == surface.g_mask &&
                                target.b_mask == surface.b_mask && target.a_mask == surface.a_mask;
        const bool keep_depth = !depth_invalid && width == surface.width && height == surface.height &&
                                (z ? depth.bits == z->bits && depth.generation == z->generation : !has_depth);
        // DP2 command buffers often continue drawing the same unchanged
        // attachments. Keep their commands together until a CPU access,
        // attachment change or descriptor limit requires a submission.
        if (recording && keep_color && keep_depth)
        {
            for (bool &b : textured)
                b = false;
            active = true;
            return true;
        }
        if (!keep_color)
            DownloadColor();
        if (!keep_depth)
            DownloadDepth();
        if (!Wait())
            return false;
        const bool fresh = width != surface.width || height != surface.height;
        if (fresh && !MakeTarget(surface.width, surface.height))
            return false;
        target = surface;
        depth = z ? *z : Gpu::Surface{};
        has_depth = z != nullptr;
        if (!keep_color && !Texture::Decode(surface, scratch))
            return false;
        if (!keep_color)
            memcpy(pixels.mapped, scratch.data(), scratch.size());
        auto *depth_values = static_cast<float *>(depth_pixels.mapped);
        auto *stencil = static_cast<unsigned char *>(depth_pixels.mapped) + size_t(width) * height * 4;
        unsigned zb = depth.z_bits ? depth.z_bits : depth.bit_count, bytes = (depth.bit_count + 7) / 8;
        uint64_t mask = zb == 32 ? 0xffffffffull : zb ? (1ull << zb) - 1 : 0;
        if (!keep_depth)
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    unsigned value = 0;
                    if (z)
                        memcpy(&value, z->bits + y * z->pitch + x * bytes, bytes);
                    depth_values[size_t(y) * width + x] = z ? float(double(value & mask) / double(mask)) : 1;
                    stencil[size_t(y) * width + x] =
                        z && z->stencil_bits ? static_cast<unsigned char>(value >> zb) : 0;
                }
        if (!StartCommand() || !Good(vkResetDescriptorPool(device, descriptor_pool, 0), "descriptor reset"))
            return false;
        descriptor_sets.clear();
        for (auto &image : retired)
            Drop(image);
        retired.clear();
        draw_at = upload_at = 0;
        for (bool &b : textured)
            b = false;
        Barrier(color, color_initialized ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                keep_color ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT);
        Barrier(depth_image, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                keep_depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                           : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        VkBufferImageCopy copy = {};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {unsigned(width), unsigned(height), 1};
        if (!keep_color)
            vkCmdCopyBufferToImage(command, pixels.buffer, color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &copy);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (!keep_depth)
            vkCmdCopyBufferToImage(command, depth_pixels.buffer, depth_image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
        copy.bufferOffset = VkDeviceSize(width) * height * 4;
        if (!keep_depth)
            vkCmdCopyBufferToImage(command, depth_pixels.buffer, depth_image.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        if (!keep_color)
            Barrier(color, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT);
        if (!keep_depth)
            Barrier(depth_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
        RenderPass();
        color_initialized = true;
        color_invalid = depth_invalid = false;
        recording = active = true;
        return true;
    }
    void Clear(unsigned flags, unsigned c, float z, unsigned stencil, const int *rects, unsigned count,
               const Gpu::State &state) override
    {
        if (!active)
            return;
        VkClearAttachment attachments[2] = {};
        unsigned n = 0;
        if (flags & 1)
        {
            auto &a = attachments[n++];
            a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            auto color = Fixed::Color(c);
            memcpy(a.clearValue.color.float32, color.v, 16);
        }
        if (flags & 6)
        {
            auto &a = attachments[n++];
            a.aspectMask = ((flags & 2) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
                           ((flags & 4) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
            a.clearValue.depthStencil = {std::clamp(z, 0.0f, 1.0f), stencil & 255};
        }
        if (!n)
            return;
        for (unsigned i = 0; i < (count ? count : 1); ++i)
        {
            int x = count && rects ? rects[i * 4] : state.viewport_x,
                y = count && rects ? rects[i * 4 + 1] : state.viewport_y;
            int right = count && rects ? rects[i * 4 + 2]
                                       : x + (state.viewport_width ? state.viewport_width : width),
                bottom = count && rects ? rects[i * 4 + 3]
                                        : y + (state.viewport_height ? state.viewport_height : height);
            x = std::clamp(x, 0, width);
            y = std::clamp(y, 0, height);
            right = std::clamp(right, x, width);
            bottom = std::clamp(bottom, y, height);
            if (x < right && y < bottom)
            {
                VkClearRect r{{{x, y}, {unsigned(right - x), unsigned(bottom - y)}}, 0, 1};
                vkCmdClearAttachments(command, n, attachments, 1, &r);
            }
        }
    }
    void SetTexture(unsigned stage, const Gpu::Surface *s) override
    {
        bool read_target = false, read_spare = false;
        auto inspect = [&](const unsigned char *bits) {
            read_target |= bits && bits == target.bits && (dirty || recording);
            read_spare |= bits && bits == spare.surface.bits && spare.dirty;
        };
        if (s) for (unsigned face = 0; face < (s->faces ? 6u : 1u); ++face)
        {
            const auto &image = s->faces ? s->faces[face] : *s;
            inspect(image.bits);
            for (unsigned level = 0; level < std::min(image.level_count, Gpu::Surface::MaxLevels); ++level)
                inspect(image.levels[level].bits);
        }
        if (read_target || read_spare)
        {
            const auto saved_target = target, saved_depth = depth;
            const bool resume = active, saved_has_depth = has_depth;
            Gpu::Surface saved_bound[8]; bool saved_textured[8];
            memcpy(saved_bound, bound, sizeof bound); memcpy(saved_textured, textured, sizeof textured);
            if (read_target) Sync(saved_target, false);
            if (read_spare) Sync(spare.surface, false);
            if (resume) Begin(saved_target, saved_has_depth ? &saved_depth : nullptr);
            memcpy(bound, saved_bound, sizeof bound); memcpy(textured, saved_textured, sizeof textured);
        }
        if (stage < 8)
        {
            textured[stage] = s != nullptr;
            if (s)
                bound[stage] = *s;
        }
    }
    void Draw(const Gpu::Draw &draw, const Gpu::State &state) override
    {
        RenderProfile::Scope profile(RenderProfile::GpuDraw);
        auto f = Gpu::VertexLayout::Of(draw.fvf);
        const bool programmable_vertex = state.vertex_shader.tokens != nullptr;
        if (!active || failed || !draw.vertices ||
            (programmable_vertex
                 ? !draw.shader_inputs || draw.stride < 16 || draw.stride > 256 || draw.stride % 16
                 : draw.stride < f.stride))
            return;
        if (!ContinueBatch())
            return;
        unsigned count = draw.index_count;
        if (!draw.indices)
        {
            if (!draw.primitive_count || draw.first_vertex >= draw.vertex_count)
                return;
            uint64_t requested = draw.primitive_count;
            switch (draw.primitive)
            {
            case Gpu::PointList: break;
            case Gpu::LineList: requested *= 2; break;
            case Gpu::LineStrip: requested += 1; break;
            case Gpu::TriangleList: requested *= 3; break;
            case Gpu::TriangleStrip:
            case Gpu::TriangleFan: requested += 2; break;
            default: return;
            }
            // DP2 draws share a vertex buffer and each names only its own primitives.
            count = unsigned(std::min<uint64_t>(requested, draw.vertex_count - draw.first_vertex));
        }
        if (!count || count > 1000000)
            return;
        VkPrimitiveTopology topology;
        switch (draw.primitive)
        {
        case Gpu::PointList:
            topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case Gpu::LineList:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case Gpu::LineStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            break;
        case Gpu::TriangleList:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        case Gpu::TriangleStrip:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            break;
        case Gpu::TriangleFan:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
            break;
        default:
            return;
        }
        const bool packed = draw.fvf == (0x4002 | 0x40 | 0x80 | 0x200 | (2u << 16) | (2u << 18)) &&
                            draw.stride == 56 && draw.vertex_count <= 1000000 && draw.vertex_count <= count * 4 &&
                            !programmable_vertex && !state.pixel_shader.tokens && state.render[9] != 1 &&
                            state.render[7] != 2 && !state.render[136] &&
                            state.stage[2][1] <= 1 && state.stage[0][11] == 0 &&
                            (state.stage[1][1] <= 1 || state.stage[1][11] == 1);
        bool packed_matrices = packed;
        if (packed)
            for (unsigned unit = 0; unit < 2; ++unit)
                if (state.stage[unit][24] & 255)
                    for (unsigned i = 0; i < 16; ++i)
                        if (state.texture[unit][i] != (i % 5 == 0 ? 1.0f : 0.0f)) packed_matrices = false;
        if (packed_matrices)
        {
            for (unsigned i = 0; draw.indices && i < count; ++i)
                if (uint64_t(draw.indices[i]) + draw.base_vertex >= draw.vertex_count) return;
            RenderVertices(topology, state, draw.indices != nullptr, false, nullptr, &draw, count);
            return;
        }
        converted.clear();
        converted.reserve(count);
        shader_vertices.clear();
        if (programmable_vertex)
            shader_vertices.reserve(count);
        index_data.clear();
        const bool indexed = draw.indices && (programmable_vertex || state.render[9] != 1);
        if (draw.vertex_count > 1000000)
            return;
        if (indexed)
        {
            vertex_remap.assign(draw.vertex_count, ~0u);
            index_data.reserve(count);
        }
        std::unique_ptr<Fixed::Prepared> prepared;
        if (!programmable_vertex && !f.transformed && !f.clip_space)
            prepared = std::make_unique<Fixed::Prepared>(state);
        unsigned texture_stages = 8, texture_transforms = 0;
        if (!state.pixel_shader.tokens)
        {
            texture_stages = 0;
            while (texture_stages < 8 && state.stage[texture_stages][1] > 1) ++texture_stages;
        }
        for (unsigned stage = 0; stage < texture_stages; ++stage)
            if (state.stage[stage][24] & 255)
                for (unsigned i = 0; i < 16; ++i)
                    if (state.texture[stage][i] != (i % 5 == 0 ? 1.0f : 0.0f))
                    { texture_transforms |= 1u << stage; break; }
        for (unsigned i = 0; i < count; ++i)
        {
            const uint64_t raw_index =
                draw.indices ? uint64_t(draw.indices[i]) + draw.base_vertex : uint64_t(i) + draw.first_vertex;
            if (raw_index >= draw.vertex_count)
                return;
            const unsigned index = unsigned(raw_index);
            if (indexed)
            {
                if (vertex_remap[index] != ~0u)
                {
                    index_data.push_back(vertex_remap[index]);
                    continue;
                }
                vertex_remap[index] = unsigned(converted.size());
                index_data.push_back(unsigned(converted.size()));
            }
            if (programmable_vertex)
            {
                ShaderVertex v;
                for (auto &r : v.inputs)
                    r = {{0, 0, 0, 1}};
                memcpy(v.inputs, draw.vertices + size_t(index) * draw.stride, draw.stride);
                shader_vertices.push_back(v);
                converted.emplace_back();
                continue;
            }
            auto v = Fixed::Convert(draw.vertices + size_t(index) * draw.stride, f, state, width, height,
                                    prepared.get(), texture_stages, texture_transforms);
            v.point_size = features.largePoints
                               ? std::clamp(v.point_size, properties.limits.pointSizeRange[0],
                                            properties.limits.pointSizeRange[1])
                               : 1;
            converted.push_back(v);
        }
        if (state.render[9] == 1 && !programmable_vertex)
        {
            flat_vertices.clear();
            auto emit = [&](unsigned first, unsigned second, unsigned third, bool triangle)
            {
                const unsigned indices[] = {first, second, third};
                for (unsigned j = 0; j < (triangle ? 3u : 2u); ++j)
                {
                    auto v = converted[indices[j]];
                    v.color = converted[first].color;
                    v.specular = converted[first].specular;
                    flat_vertices.push_back(v);
                }
            };
            if (draw.primitive == Gpu::TriangleList)
                for (unsigned i = 0; i + 2 < count; i += 3)
                    emit(i, i + 1, i + 2, true);
            else if (draw.primitive == Gpu::TriangleFan)
                for (unsigned i = 1; i + 1 < count; ++i)
                    emit(0, i, i + 1, true);
            else if (draw.primitive == Gpu::TriangleStrip)
                for (unsigned i = 0; i + 2 < count; ++i)
                {
                    const size_t start = flat_vertices.size();
                    emit(i, i + 1, i + 2, true);
                    if (i & 1)
                        std::swap(flat_vertices[start + 1], flat_vertices[start + 2]);
                }
            else if (draw.primitive == Gpu::LineList)
                for (unsigned i = 0; i + 1 < count; i += 2)
                    emit(i, i + 1, 0, false);
            else if (draw.primitive == Gpu::LineStrip)
                for (unsigned i = 0; i + 1 < count; ++i)
                    emit(i, i + 1, 0, false);
            if (draw.primitive != Gpu::PointList)
            {
                converted.swap(flat_vertices);
                topology = draw.primitive <= Gpu::LineStrip ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                            : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }
        if (converted.empty())
            return;
        RenderVertices(topology, state, indexed);
    }
    void RenderVertices(VkPrimitiveTopology topology, const Gpu::State &state, bool indexed = false,
                        bool glide = false, const GlideUniforms *uniforms = nullptr,
                        const Gpu::Draw *packed = nullptr, unsigned packed_count = 0)
    {
        VkPipeline pipeline = Pipeline(topology, state, glide, packed != nullptr);
        if (!pipeline)
            return;
        VkDescriptorSet set = BindTextures(state, uniforms);
        if (!set)
            return;
        const bool shader_input = !glide && state.vertex_shader.tokens;
        const size_t vertex_bytes = packed ? size_t(packed->vertex_count) * packed->stride
                                  : converted.size() * (shader_input ? sizeof(ShaderVertex) : sizeof(Vertex));
        Buffer *buffer = Acquire(draws, draw_at, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!buffer)
            return;
        memcpy(buffer->mapped,
               packed ? static_cast<const void *>(packed->vertices)
                      : shader_input ? static_cast<const void *>(shader_vertices.data()) : converted.data(),
               vertex_bytes);
        const VkBuffer vertex_buffer = buffer->buffer;
        VkBuffer index_buffer = VK_NULL_HANDLE;
        if (indexed)
        {
            Buffer *indices =
                Acquire(draws, draw_at, packed ? packed->index_count * 2 : index_data.size() * 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
            if (!indices)
                return;
            memcpy(indices->mapped, packed ? static_cast<const void *>(packed->indices) : index_data.data(),
                   packed ? packed->index_count * 2 : index_data.size() * 4);
            index_buffer = indices->buffer;
        }
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        vkCmdBindVertexBuffers(command, 0, 1, &vertex_buffer, &offset);
        if (indexed)
            vkCmdBindIndexBuffer(command, index_buffer, 0, packed ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
        VkViewport viewport{float(state.viewport_x),
                            float(state.viewport_y),
                            float(state.viewport_width ? state.viewport_width : width),
                            float(state.viewport_height ? state.viewport_height : height),
                            state.z_min,
                            state.z_max};
        vkCmdSetViewport(command, 0, 1, &viewport);
        int left = std::clamp(state.viewport_x, 0, width), top = std::clamp(state.viewport_y, 0, height),
            right = std::clamp(state.viewport_x + int(viewport.width), left, width),
            bottom = std::clamp(state.viewport_y + int(viewport.height), top, height);
        if (state.render[174])
        {
            left = std::clamp(state.scissor[0], left, right);
            top = std::clamp(state.scissor[1], top, bottom);
            right = std::clamp(state.scissor[2], left, right);
            bottom = std::clamp(state.scissor[3], top, bottom);
        }
        VkRect2D scissor{{left, top}, {unsigned(right - left), unsigned(bottom - top)}};
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdSetDepthBias(command, -float(int(state.render[47])), 0, 0);
        vkCmdSetStencilReference(command, VK_STENCIL_FACE_FRONT_AND_BACK, state.render[57] & 255);
        if (indexed)
            vkCmdDrawIndexed(command, packed ? packed->index_count : unsigned(index_data.size()), 1, 0, packed ? packed->base_vertex : 0, 0);
        else
            vkCmdDraw(command, packed ? packed_count : unsigned(converted.size()), 1, packed ? packed->first_vertex : 0, 0);
        ++draw_count;
    }
    bool ContinueBatch()
    {
        if (descriptor_sets.size() < 1024)
            return true;
        const auto saved_target = target, saved_depth = depth;
        Gpu::Surface saved_textures[8];
        bool saved_bound[8];
        memcpy(saved_textures, bound, sizeof bound);
        memcpy(saved_bound, textured, sizeof textured);
        const bool depth_present = has_depth;
        End();
        Submit();
        if (!Begin(saved_target, depth_present ? &saved_depth : nullptr))
            return false;
        for (unsigned i = 0; i < 8; ++i)
            SetTexture(i, saved_bound[i] ? &saved_textures[i] : nullptr);
        return true;
    }
    void End() override
    {
        active = false;
    }
  private:
    void CopyColorToHost()
    {
        VkBufferImageCopy copy = {};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {unsigned(width), unsigned(height), 1};
        vkCmdCopyImageToBuffer(command, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixels.buffer, 1, &copy);
        HostReadBarrier();
    }
    void Submit(bool readback = false)
    {
        if (!recording)
            return;
        recording = active = false;
        vkCmdEndRenderPass(command);
        Barrier(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_ASPECT_COLOR_BIT);
        staging_current = readback;
        if (readback) CopyColorToHost();
        MarkRendered();
        FinishCommand();
    }
    void MarkRendered()
    {
        dirty = true;
        depth_dirty = has_depth;
        for (auto &t : textures)
            if (t->surface.bits == target.bits) t->current = false;
    }
    void HostReadBarrier()
    {
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                             &barrier, 0, nullptr, 0, nullptr);
    }
    bool FinishCommand(VkSemaphore available = VK_NULL_HANDLE, VkSemaphore finished = VK_NULL_HANDLE)
    {
        if (!Good(vkEndCommandBuffer(command), "command end"))
            return false;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        if (available) { submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &available; submit.pWaitDstStageMask = &stage; }
        if (finished) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &finished; }
        if (!Good(vkQueueSubmit(queue, 1, &submit, fence), "submit"))
            return false;
        pending = true;
        ++submitted;
        return true;
    }
  public:
    bool AttachX11(void *display, unsigned long window)
    {
        if (!presentation_support || failed) return false;
        auto made = std::make_unique<Presentation::Desktop>();
        if (!made->Open(instance, physical, device, queue, family, display, window)) return false;
        desktop = std::move(made);
        return true;
    }
    void Detach() { Wait(); desktop.reset(); }
    bool CanScanout(const unsigned char *bits) const override
    {
        auto usable = [&](const Gpu::Surface &s, bool invalid, bool initialized) {
            return s.bits && s.bits == bits && !invalid && initialized &&
                ((s.bit_count == 16 && s.r_mask == 0x7c00 && s.g_mask == 0x3e0 && s.b_mask == 0x1f) ||
                 (s.bit_count == 32 && s.r_mask == 0xff0000 && s.g_mask == 0xff00 && s.b_mask == 0xff));
        };
        return desktop && !failed && (usable(target, color_invalid, color_initialized) ||
            usable(spare.surface, spare.invalid, spare.initialized));
    }
    bool Present(const unsigned char *bits, int w, int h, int canvas_width, int canvas_height)
    {
        if (!CanScanout(bits)) return false;
        // A recording already owns an idle command buffer. Append the desktop
        // pass to it, so presentation needs no intermediate submit/fence wait.
        const bool append = recording;
        if (!append && !Wait()) return false;
        if (!desktop->Acquire(w, h)) return false;
        if (append)
        {
            recording = active = false;
            vkCmdEndRenderPass(command);
            Barrier(color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_ASPECT_COLOR_BIT);
            staging_current = false;
            MarkRendered();
        }
        else if (!StartCommand()) return false;
        const bool cached = bits == spare.surface.bits;
        const auto &s = cached ? spare.surface : target;
        auto &image = cached ? spare.image : color;
        Barrier(image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        unsigned maximum[] = {255, 255, 255, 0, unsigned(canvas_width ? canvas_width : s.width),
                              unsigned(canvas_height ? canvas_height : s.height), 0, 0};
        if (s.bit_count == 16) maximum[0] = maximum[1] = maximum[2] = 31;
        desktop->Record(command, image.view, int(maximum[4]), int(maximum[5]), maximum);
        Barrier(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!FinishCommand(desktop->Available(), desktop->Finished())) return false;
        return desktop->Present();
    }
    void Sync(const Gpu::Surface &surface, bool will_write) override
    {
        Submit(surface.bits == target.bits);
        if (surface.bits == target.bits)
        {
            DownloadColor();
            if (will_write)
                color_invalid = true;
        }
        if (surface.bits && surface.bits == spare.surface.bits)
        {
            Wait(); SwapColor(); DownloadColor();
            if (will_write) color_invalid = true;
            SwapColor();
        }
        if (surface.bits == depth.bits)
        {
            DownloadDepth();
            if (will_write)
                depth_invalid = true;
        }
        if (will_write)
            for (auto &t : textures)
                if (t->surface.bits == surface.bits)
                    t->current = false;
    }
    void CpuAccess(const unsigned char *bits, bool write) override
    {
        if (!bits || (bits != target.bits && bits != spare.surface.bits && bits != depth.bits)) return;
        Gpu::Surface surface; surface.bits = const_cast<unsigned char *>(bits);
        Sync(surface, write);
    }
    void SyncDepth(const Gpu::Surface &s) override
    {
        Submit();
        if (s.bits == depth.bits)
        {
            DownloadDepth();
            depth_invalid = true;
        }
    }
    void Forget(unsigned owner, unsigned handle) override
    {
        if (active)
            End();
        Wait();
        for (auto &t : textures)
            if (t->surface.owner == owner && t->surface.handle == handle)
            {
                Drop(t->image);
                t->current = false;
            }
        if (target.owner == owner && target.handle == handle)
        {
            dirty = false;
            target.bits = nullptr;
            color_invalid = true;
        }
        if (depth.owner == owner && depth.handle == handle)
        {
            depth_dirty = false;
            depth_invalid = true;
        }
        if (spare.surface.owner == owner && spare.surface.handle == handle)
            DropSpare();
    }
    void Release(const unsigned char *bits, unsigned long bytes) override
    {
        if (active)
            End();
        Wait();
        auto contains = [&](const unsigned char *p)
        { return uintptr_t(p) >= uintptr_t(bits) && uintptr_t(p) - uintptr_t(bits) < bytes; };
        if (contains(target.bits))
        {
            dirty = false;
            target.bits = nullptr;
            color_invalid = true;
        }
        if (contains(depth.bits))
        {
            depth_dirty = false;
            depth_invalid = true;
        }
        if (contains(spare.surface.bits))
            DropSpare();
        for (auto &t : textures)
            if (contains(t->surface.bits))
            {
                Drop(t->image);
                t->current = false;
            }
    }
    void FillDepth(const Gpu::Surface &s, const int *rect, float value) override
    {
        SyncDepth(s);
        if (!s.bits || !s.bit_count || s.bit_count > 32)
            return;
        unsigned bits = s.z_bits ? s.z_bits : s.bit_count;
        if (bits > 32)
            return;
        uint64_t mask = bits == 32 ? 0xffffffffull : (1ull << bits) - 1;
        unsigned packed = unsigned(std::clamp(value, 0.0f, 1.0f) * double(mask) + 0.5),
                 stride = (s.bit_count + 7) / 8;
        int left = rect ? std::clamp(rect[0], 0, s.width) : 0,
            top = rect ? std::clamp(rect[1], 0, s.height) : 0,
            right = rect ? std::clamp(rect[2], left, s.width) : s.width,
            bottom = rect ? std::clamp(rect[3], top, s.height) : s.height;
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
            {
                auto *p = s.bits + y * s.pitch + x * stride;
                unsigned old = 0;
                memcpy(&old, p, stride);
                unsigned v = (old & ~unsigned(mask)) | packed;
                memcpy(p, &v, stride);
            }
    }
    void DrawGlide(unsigned primitive, const Gpu::GlideVertex *vertices, unsigned count,
                   const Gpu::GlideState &g) override
    {
        if (!active || failed || !Glide() || !vertices || !count || count > 1000000)
            return;
        if (!ContinueBatch())
            return;
        VkPrimitiveTopology topology;
        switch (primitive)
        {
        case Gpu::PointList:
            topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case Gpu::LineList:
            topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case Gpu::TriangleList:
            topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        default:
            return;
        }
        Gpu::State state;
        state.viewport_width = width;
        state.viewport_height = height;
        state.render[7] = g.depth_mode != 0;
        state.render[14] = g.depth_mask;
        state.render[23] = (g.depth_function & 7) + 1;
        state.render[22] = 1;
        state.render[168] = (g.rgb_mask ? 7 : 0) | (g.alpha_mask ? 8 : 0);
        state.render[174] = 1;
        memcpy(state.scissor, g.clip, sizeof state.scissor);
        auto blend = [](unsigned v, bool destination)
        {
            // GR_BLEND_ to D3DBLEND_, with separate color/alpha factors.
            static const unsigned factors[] = {1, 5, 9, 7, 2, 6, 10, 8};
            if (v == 2)
                return destination ? 3u : 9u;
            if (v == 6)
                return destination ? 4u : 10u;
            if (v == 15)
                return destination ? 3u : 11u;
            return v < 8 ? factors[v] : 1u;
        };
        state.render[27] =
            !(g.rgb_source == 4 && g.rgb_destination == 0 && g.alpha_source == 4 && g.alpha_destination == 0);
        state.render[19] = blend(g.rgb_source, false);
        state.render[20] = blend(g.rgb_destination, true);
        state.render[206] = 1;
        state.render[207] = blend(g.alpha_source, false);
        state.render[208] = blend(g.alpha_destination, true);
        GlideUniforms u;
        const unsigned color[] = {g.color_function, g.color_factor, g.color_local, g.color_other};
        const unsigned alpha[] = {g.alpha_function, g.alpha_factor, g.alpha_local, g.alpha_other};
        memcpy(u.color, color, sizeof color);
        memcpy(u.alpha, alpha, sizeof alpha);
        for (unsigned i = 0; i < 2; ++i)
        {
            const auto &t = g.tmu[i];
            u.tmu_rgb[i][0] = t.rgb_function;
            u.tmu_rgb[i][1] = t.rgb_factor;
            u.tmu_rgb[i][2] = unsigned(t.rgb_invert) | (unsigned(t.alpha_invert) << 1);
            u.tmu_rgb[i][3] = t.textured && textured[i];
            u.tmu_alpha[i][0] = t.alpha_function;
            u.tmu_alpha[i][1] = t.alpha_factor;
            u.tmu_alpha[i][3] = t.mipmap != 0;
            const float w = float(std::max(t.width, 1)), h = float(std::max(t.height, 1));
            u.tex_scale[i] = {{(w >= h ? 1 : h / w) / 256, (h >= w ? 1 : w / h) / 256, t.lod_bias,
                               g.scale > 1 ? std::log2(g.scale) : 0}};
            u.detail[i] = {{float(t.detail_bias), float(t.detail_scale), t.detail_max, 0}};
            state.stage[i][13] = t.clamp_s ? 3 : 1;
            state.stage[i][14] = t.clamp_t ? 3 : 1;
            state.stage[i][16] = t.mag_bilinear ? 2 : 1;
            state.stage[i][17] = t.min_bilinear ? 2 : 1;
            state.stage[i][18] = t.mipmap ? 2 : 1;
        }
        u.constant_color = Fixed::Color(g.constant_color);
        u.constant_other = Fixed::Color(g.constant_split ? g.constant_other : g.constant_color);
        u.fog_color = Fixed::Color(g.fog_color);
        u.flags[0] = g.color_invert;
        u.flags[1] = g.alpha_invert;
        u.flags[2] = unsigned(g.chroma_key) | (unsigned(g.alpha_bit_test) << 1);
        u.flags[3] = g.chroma_value & 0xffffff;
        u.tests[0] = g.alpha_test & 7;
        u.tests[1] = g.alpha_reference & 255;
        u.tests[2] = g.fog_mode;
        u.tests[3] = g.depth_mode;
        u.depth[0] = g.depth_bias;
        for (unsigned i = 0; i < 64; ++i)
            u.fog_table[i] = float(g.fog_table[i]);
        converted.clear();
        converted.reserve(count);
        for (unsigned i = 0; i < count; ++i)
        {
            const auto &v = vertices[i];
            Vertex out;
            out.position = {{v.x * g.scale * 2 / width - 1, v.y * g.scale * 2 / height - 1, .5f, 1}};
            out.color = {{v.r, v.g, v.b, v.a}};
            out.specular[0] = v.oow;
            out.depth = v.ooz;
            out.point_size = features.largePoints ? std::clamp(g.scale, properties.limits.pointSizeRange[0],
                                                               properties.limits.pointSizeRange[1])
                                                  : 1;
            out.tex[0] = {{v.s0, v.t0, v.q0, 1}};
            out.tex[1] = {{v.s1, v.t1, v.q1, 1}};
            converted.push_back(out);
        }
        RenderVertices(topology, state, false, true, &u);
    }

  private:
    struct ShaderVertex
    {
        Fixed::Vec inputs[16];
    };
    std::vector<ShaderVertex> shader_vertices;
    std::vector<unsigned char> scratch;
    std::vector<Vertex> converted;
    std::vector<Vertex> flat_vertices;
    std::vector<unsigned> vertex_remap, index_data;
};
Vulkan *installed = nullptr;
} // namespace
Gpu::Backend *Install()
{
    // Register our destructor after the loader/layers initialized their globals.
    // Otherwise RADV's mapping support can shut down before vkUnmapMemory.
    static std::unique_ptr<Vulkan> backend = []
    {
        auto value = std::make_unique<Vulkan>();
        if (!value->Open())
            value.reset();
        return value;
    }();
    installed = backend.get();
    return installed;
}
bool AttachX11(void *display, unsigned long window)
{
    auto *backend = static_cast<Vulkan *>(Install());
    return backend && backend->AttachX11(display, window);
}
void Detach() { if (installed) installed->Detach(); }
bool CanPresent(const unsigned char *bits)
{
    return installed && installed->CanScanout(bits);
}
bool Present(const unsigned char *bits, int width, int height, int canvas_width, int canvas_height)
{
    return installed && installed->Present(bits, width, height, canvas_width, canvas_height);
}
void ReadScanout(const unsigned char *bits)
{
    if (auto *b = installed)
    { Gpu::Surface s; s.bits = const_cast<unsigned char *>(bits); b->Sync(s, false); }
}
} // namespace HostVulkan
