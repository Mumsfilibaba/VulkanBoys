#pragma once
#include "Common.h"

class DeviceVK;
class CommandBufferVK;

class CommandPoolVK
{
public:
	CommandPoolVK(DeviceVK* pDevice, uint32_t queueFamilyIndex);
	~CommandPoolVK();

	DECL_NO_COPY(CommandPoolVK);

	bool init();

	CommandBufferVK* allocateCommandBuffer();
	void reset();
private:
	DeviceVK* m_pDevice;
	uint32_t m_QueueFamilyIndex;
	VkCommandPool m_CommandPool;
};