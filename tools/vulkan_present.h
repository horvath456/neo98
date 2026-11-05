// SPDX-License-Identifier: GPL-2.0-or-later
// Included by host_vulkan.cpp. WSI resources share the renderer's queue and
// command fence. A semaphore per swapchain image stays alive until reacquire.
#pragma once
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#undef None
#undef Status
namespace HostVulkan::Presentation
{
class Desktop
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    unsigned family = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> frames;
    std::vector<VkSemaphore> finished;
    VkExtent2D extent{};
    unsigned index = 0;
    bool Good(VkResult result, const char *operation)
    {
        if (result == VK_SUCCESS) return true;
        fprintf(stderr, "Vulkan presentation %s: %d\n", operation, result);
        return false;
    }
    void DropSwapchain()
    {
        if (!device) return;
        vkQueueWaitIdle(queue);
        for (auto f : frames) vkDestroyFramebuffer(device, f, nullptr);
        for (auto v : views) vkDestroyImageView(device, v, nullptr);
        for (auto s : finished) vkDestroySemaphore(device, s, nullptr);
        frames.clear(); views.clear(); finished.clear();
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pass) vkDestroyRenderPass(device, pass, nullptr);
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        pipeline = VK_NULL_HANDLE; pass = VK_NULL_HANDLE; swapchain = VK_NULL_HANDLE;
    }
    bool MakeSwapchain(int w, int h)
    {
        DropSwapchain();
        VkSurfaceCapabilitiesKHR caps{};
        if (!Good(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps), "capabilities")) return false;
        extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
            extent = {std::clamp(unsigned(w), caps.minImageExtent.width, caps.maxImageExtent.width),
                      std::clamp(unsigned(h), caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (!extent.width || !extent.height) return false;
        unsigned count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
        auto format = std::find_if(formats.begin(), formats.end(), [](auto f) {
            return (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (format == formats.end() || !(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) return false;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR})
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) mode = preferred;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface;
        info.minImageCount = std::max(3u, caps.minImageCount);
        if (caps.maxImageCount) info.minImageCount = std::min(info.minImageCount, caps.maxImageCount);
        info.imageFormat = format->format; info.imageColorSpace = format->colorSpace;
        info.imageExtent = extent; info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & info.compositeAlpha))
            info.compositeAlpha = VkCompositeAlphaFlagBitsKHR(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
        info.presentMode = mode; info.clipped = VK_TRUE;
        if (!Good(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "swapchain")) return false;
        VkAttachmentDescription attachment{};
        attachment.format = format->format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &reference;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL; dependency.dstSubpass = 0;
        dependency.srcStageMask = dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &sub;
        rp.dependencyCount = 1; rp.pDependencies = &dependency;
        if (!Good(vkCreateRenderPass(device, &rp, nullptr, &pass), "render pass")) return false;
        VkShaderModule shaders[2]{};
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize = sizeof Vulkan_present_vert; sm.pCode = Vulkan_present_vert;
        if (!Good(vkCreateShaderModule(device, &sm, nullptr, &shaders[0]), "vertex shader")) return false;
        sm.codeSize = sizeof Vulkan_present_frag; sm.pCode = Vulkan_present_frag;
        if (!Good(vkCreateShaderModule(device, &sm, nullptr, &shaders[1]), "fragment shader"))
        { vkDestroyShaderModule(device, shaders[0], nullptr); return false; }
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
            stages[i].module = shaders[i]; stages[i].pName = "main";
        }
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{}; blend.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &blend;
        VkDynamicState values[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dy.dynamicStateCount = 2; dy.pDynamicStates = values;
        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount = 2; pi.pStages = stages; pi.pVertexInputState = &vi; pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp; pi.pRasterizationState = &raster; pi.pMultisampleState = &ms;
        pi.pColorBlendState = &cb; pi.pDynamicState = &dy; pi.layout = layout; pi.renderPass = pass;
        const bool made = Good(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), "pipeline");
        for (auto shader : shaders) vkDestroyShaderModule(device, shader, nullptr);
        if (!made) return false;
        vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        std::vector<VkImage> images(count);
        vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
        views.resize(count); frames.resize(count); finished.resize(count);
        for (unsigned i = 0; i < count; ++i) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format->format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (!Good(vkCreateImageView(device, &view, nullptr, &views[i]), "image view")) return false;
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = pass; fb.attachmentCount = 1; fb.pAttachments = &views[i];
            fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
            if (!Good(vkCreateFramebuffer(device, &fb, nullptr, &frames[i]), "framebuffer")) return false;
            VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (!Good(vkCreateSemaphore(device, &si, nullptr, &finished[i]), "semaphore")) return false;
        }
        fprintf(stderr, "NEO98 Vulkan direct presentation %ux%u mode=%u\n", extent.width, extent.height, mode);
        return true;
    }
  public:
    bool Open(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, VkQueue q, unsigned fam, void *display, unsigned long window)
    {
        instance = inst; physical = phys; device = dev; queue = q; family = fam;
        VkXlibSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
        info.dpy = static_cast<Display *>(display); info.window = window;
        if (!Good(vkCreateXlibSurfaceKHR(instance, &info, nullptr, &surface), "surface")) return false;
        VkBool32 supported = VK_FALSE;
        if (!Good(vkGetPhysicalDeviceSurfaceSupportKHR(physical, family, surface, &supported), "queue") || !supported) return false;
        VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = 1; dl.pBindings = &binding;
        if (!Good(vkCreateDescriptorSetLayout(device, &dl, nullptr, &descriptors), "descriptor layout")) return false;
        VkPushConstantRange constants{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1; pl.pSetLayouts = &descriptors; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &constants;
        if (!Good(vkCreatePipelineLayout(device, &pl, nullptr, &layout), "pipeline layout")) return false;
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = 1; dp.poolSizeCount = 1; dp.pPoolSizes = &size;
        if (!Good(vkCreateDescriptorPool(device, &dp, nullptr, &pool), "descriptor pool")) return false;
        VkDescriptorSetAllocateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ds.descriptorPool = pool; ds.descriptorSetCount = 1; ds.pSetLayouts = &descriptors;
        if (!Good(vkAllocateDescriptorSets(device, &ds, &set), "descriptor set")) return false;
        VkSamplerCreateInfo sp{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sp.magFilter = sp.minFilter = VK_FILTER_NEAREST;
        sp.addressModeU = sp.addressModeV = sp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (!Good(vkCreateSampler(device, &sp, nullptr, &sampler), "sampler")) return false;
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        return Good(vkCreateSemaphore(device, &si, nullptr, &acquired), "acquire semaphore");
    }
    ~Desktop()
    {
        DropSwapchain();
        if (device) {
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (sampler) vkDestroySampler(device, sampler, nullptr);
            if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
            if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
            if (descriptors) vkDestroyDescriptorSetLayout(device, descriptors, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    }
    bool Acquire(int w, int h)
    {
        if ((!swapchain || extent.width != unsigned(w) || extent.height != unsigned(h)) && !MakeSwapchain(w, h)) return false;
        auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!MakeSwapchain(w, h)) return false;
            result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        }
        return result == VK_SUBOPTIMAL_KHR || Good(result, "acquire");
    }
    void Record(VkCommandBuffer command, VkImageView source, int w, int h, const unsigned *maximum)
    {
        VkDescriptorImageInfo image{sampler, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set; write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        VkClearValue clear{}; clear.color.float32[3] = 1;
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = pass; rp.framebuffer = frames[index]; rp.renderArea.extent = extent;
        rp.clearValueCount = 1; rp.pClearValues = &clear;
        vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE);
        const float scale = std::min(float(extent.width) / w, float(extent.height) / h);
        VkViewport viewport{std::floor((extent.width - w * scale) / 2),
                            std::floor((extent.height - h * scale) / 2), w * scale, h * scale, 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(command, 0, 1, &viewport); vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(command, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, maximum);
        vkCmdDraw(command, 3, 1, 0, 0);
        vkCmdEndRenderPass(command);
    }
    VkSemaphore Available() const { return acquired; }
    VkSemaphore Finished() const { return finished[index]; }
    bool Present()
    {
        auto semaphore = Finished();
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        info.waitSemaphoreCount = 1; info.pWaitSemaphores = &semaphore;
        info.swapchainCount = 1; info.pSwapchains = &swapchain; info.pImageIndices = &index;
        const auto result = vkQueuePresentKHR(queue, &info);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) { DropSwapchain(); return true; }
        return Good(result, "present");
    }
};
}
