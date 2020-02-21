#include "CommandBufferVK.h"
#include "ImageVK.h"
#include "DeviceVK.h"
#include "BufferVK.h"
#include "PipelineVK.h"
#include "RenderPassVK.h"
#include "FrameBufferVK.h"
#include "StagingBufferVK.h"
#include "DescriptorSetVK.h"
#include "PipelineLayoutVK.h"

CommandBufferVK::CommandBufferVK(DeviceVK* pDevice, VkCommandBuffer commandBuffer)
	: m_pDevice(pDevice),
	m_pStagingBuffer(nullptr),
	m_pStagingTexture(nullptr),
	m_CommandBuffer(commandBuffer),
	m_Fence(VK_NULL_HANDLE),
	m_DescriptorSets()
{
}

CommandBufferVK::~CommandBufferVK()
{
	if (m_Fence != VK_NULL_HANDLE)
	{
		vkDestroyFence(m_pDevice->getDevice(), m_Fence, nullptr);
		m_Fence = VK_NULL_HANDLE;
	}

	SAFEDELETE(m_pStagingBuffer);
	SAFEDELETE(m_pStagingTexture);
	m_pDevice = nullptr;
}

bool CommandBufferVK::finalize()
{
	// Create fence
	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK_RESULT_RETURN_FALSE(vkCreateFence(m_pDevice->getDevice(), &fenceInfo, nullptr, &m_Fence), "Create Fence for CommandBuffer Failed");
	D_LOG("--- CommandBuffer: Vulkan Fence created successfully");

	//Create staging-buffers
	m_pStagingBuffer = DBG_NEW StagingBufferVK(m_pDevice);
	if (!m_pStagingBuffer->init(MB(16)))
	{
		return false;
	}

	m_pStagingTexture = DBG_NEW StagingBufferVK(m_pDevice);
	if (!m_pStagingTexture->init(MB(32)))
	{
		return false;
	}

	return true;
}

void CommandBufferVK::reset()
{
	//Wait for GPU to finish with this commandbuffer and then reset it
	vkWaitForFences(m_pDevice->getDevice(), 1, &m_Fence, VK_TRUE, UINT64_MAX);
	vkResetFences(m_pDevice->getDevice(), 1, &m_Fence);

	m_pStagingBuffer->reset();
	m_pStagingTexture->reset();
}

void CommandBufferVK::begin()
{
	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = nullptr;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	VK_CHECK_RESULT(vkBeginCommandBuffer(m_CommandBuffer, &beginInfo), "Begin CommandBuffer Failed");
}

void CommandBufferVK::end()
{
	VK_CHECK_RESULT(vkEndCommandBuffer(m_CommandBuffer), "End CommandBuffer Failed");
}

void CommandBufferVK::beginRenderPass(RenderPassVK* pRenderPass, FrameBufferVK* pFrameBuffer, uint32_t width, uint32_t height, VkClearValue* pClearVales, uint32_t clearValueCount)
{
	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.pNext = nullptr;
	renderPassInfo.renderPass	= pRenderPass->getRenderPass();
	renderPassInfo.framebuffer	= pFrameBuffer->getFrameBuffer();
	renderPassInfo.renderArea.offset	= { 0, 0 };
	renderPassInfo.renderArea.extent	= { width, height };
	renderPassInfo.pClearValues			= pClearVales;
	renderPassInfo.clearValueCount		= clearValueCount;

	vkCmdBeginRenderPass(m_CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBufferVK::endRenderPass()
{
	vkCmdEndRenderPass(m_CommandBuffer);
}

void CommandBufferVK::bindVertexBuffers(const BufferVK* const* ppVertexBuffers, uint32_t vertexBufferCount, const VkDeviceSize* pOffsets)
{
	for (uint32_t i = 0; i < vertexBufferCount; i++)
	{
		m_VertexBuffers.emplace_back(ppVertexBuffers[i]->getBuffer());
	}

	vkCmdBindVertexBuffers(m_CommandBuffer, 0, vertexBufferCount, m_VertexBuffers.data(), pOffsets);
	m_VertexBuffers.clear();
}

void CommandBufferVK::bindIndexBuffer(const BufferVK* pIndexBuffer, VkDeviceSize offset, VkIndexType indexType)
{
	vkCmdBindIndexBuffer(m_CommandBuffer, pIndexBuffer->getBuffer(), offset, indexType);
}

void CommandBufferVK::bindGraphicsPipeline(PipelineVK* pPipeline)
{
	vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pPipeline->getPipeline());
}

void CommandBufferVK::bindDescriptorSet(VkPipelineBindPoint bindPoint, PipelineLayoutVK* pPipelineLayout, uint32_t firstSet, uint32_t count, const DescriptorSetVK* const* ppDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets)
{
	for (uint32_t i = 0; i < count; i++)
	{
		m_DescriptorSets.emplace_back(ppDescriptorSets[i]->getDescriptorSet());
	}

	vkCmdBindDescriptorSets(m_CommandBuffer, bindPoint, pPipelineLayout->getPipelineLayout(), firstSet, count, m_DescriptorSets.data(), dynamicOffsetCount, pDynamicOffsets);
	m_DescriptorSets.clear();
}

void CommandBufferVK::pushConstants(PipelineLayoutVK* pPipelineLayout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{
	vkCmdPushConstants(m_CommandBuffer, pPipelineLayout->getPipelineLayout(), stageFlags, offset, size, pValues);
}

void CommandBufferVK::setScissorRects(VkRect2D* pScissorRects, uint32_t scissorRectCount)
{
	vkCmdSetScissor(m_CommandBuffer, 0, scissorRectCount, pScissorRects);
}

void CommandBufferVK::setViewports(VkViewport* pViewports, uint32_t viewportCount)
{
	vkCmdSetViewport(m_CommandBuffer, 0, viewportCount, pViewports);
}

void CommandBufferVK::updateBuffer(BufferVK* pDestination, uint64_t destinationOffset, const void* pSource, uint64_t sizeInBytes)
{
	VkDeviceSize offset = m_pStagingBuffer->getCurrentOffset();
	void* pHostMemory	= m_pStagingBuffer->allocate(sizeInBytes);
	memcpy(pHostMemory, pSource, sizeInBytes);

	copyBuffer(m_pStagingBuffer->getBuffer(), offset, pDestination, destinationOffset, sizeInBytes);
}

void CommandBufferVK::copyBuffer(BufferVK* pSource, uint64_t sourceOffset, BufferVK* pDestination, uint64_t destinationOffset, uint64_t sizeInBytes)
{
	VkBufferCopy bufferCopy = {};
	bufferCopy.size			= sizeInBytes;
	bufferCopy.srcOffset	= sourceOffset;
	bufferCopy.dstOffset	= destinationOffset;

	vkCmdCopyBuffer(m_CommandBuffer, pSource->getBuffer(), pDestination->getBuffer(), 1, &bufferCopy);
}

void CommandBufferVK::blitImage2D(ImageVK* pSource, uint32_t sourceMip, VkExtent2D sourceExtent, ImageVK* pDestination, uint32_t destinationMip, VkExtent2D destinationExtent)
{
	VkImageBlit blit = {};
	blit.srcOffsets[0]					= { 0, 0, 0 };
	blit.srcOffsets[1]					= { int32_t(sourceExtent.width), int32_t(sourceExtent.height), int32_t(1) };
	blit.srcSubresource.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.mipLevel		= sourceMip;
	blit.srcSubresource.baseArrayLayer	= 0;
	blit.srcSubresource.layerCount		= 1;
	blit.dstOffsets[0]					= { 0, 0, 0 };
	blit.dstOffsets[1]					= { int32_t(destinationExtent.width), int32_t(destinationExtent.height), int32_t(1) };
	blit.dstSubresource.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.mipLevel		= destinationMip;
	blit.dstSubresource.baseArrayLayer	= 0;
	blit.dstSubresource.layerCount		= 1;

	vkCmdBlitImage(m_CommandBuffer, pSource->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pDestination->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
}

void CommandBufferVK::updateImage(const void* pPixelData, ImageVK* pImage, uint32_t width, uint32_t height, uint32_t pixelStride, uint32_t miplevel, uint32_t layer)
{
	uint32_t sizeInBytes = width * height * pixelStride;
	
	VkDeviceSize offset = m_pStagingTexture->getCurrentOffset();
	void* pHostMemory = m_pStagingTexture->allocate(sizeInBytes);
	memcpy(pHostMemory, pPixelData, sizeInBytes);
	
	copyBufferToImage(m_pStagingTexture->getBuffer(), offset, pImage, width, height, miplevel, layer);
}

void CommandBufferVK::copyBufferToImage(BufferVK* pSource, VkDeviceSize sourceOffset, ImageVK* pImage, uint32_t width, uint32_t height, uint32_t miplevel, uint32_t layer)
{
	VkBufferImageCopy region = {};
	region.bufferImageHeight				= 0;
	region.bufferOffset						= sourceOffset;
	region.bufferRowLength					= 0;
	region.imageSubresource.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.baseArrayLayer	= layer;
	region.imageSubresource.layerCount		= 1;
	region.imageSubresource.mipLevel		= miplevel;
	region.imageExtent.depth				= 1;
	region.imageExtent.height				= height;
	region.imageExtent.width				= width;

	vkCmdCopyBufferToImage(m_CommandBuffer, pSource->getBuffer(), pImage->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}


void CommandBufferVK::transitionImageLayout(ImageVK* pImage, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t baseMiplevel, uint32_t miplevels, uint32_t baseLayer, uint32_t layerCount)
{
	VkImageMemoryBarrier barrier = {};
	barrier.sType							= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex				= VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex				= VK_QUEUE_FAMILY_IGNORED;
	barrier.oldLayout						= oldLayout;
	barrier.newLayout						= newLayout;
	barrier.subresourceRange.aspectMask		= VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel	= baseMiplevel;
	barrier.subresourceRange.levelCount		= miplevels;
	barrier.subresourceRange.baseArrayLayer	= baseLayer;
	barrier.subresourceRange.layerCount		= layerCount;
	barrier.image = pImage->getImage();

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;
	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) 
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage			= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage	= VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		sourceStage			= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		destinationStage	= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage			= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		destinationStage	= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) 
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage			= VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage	= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		sourceStage			= VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage	= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		sourceStage			= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		destinationStage	= VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		sourceStage			= VK_PIPELINE_STAGE_TRANSFER_BIT;
		destinationStage	= VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else 
	{
		LOG("Unsupported layout transition");
	}

	vkCmdPipelineBarrier(m_CommandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBufferVK::drawInstanced(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	vkCmdDraw(m_CommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBufferVK::drawIndexInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, uint32_t vertexOffset, uint32_t firstInstance)
{
	vkCmdDrawIndexed(m_CommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}
