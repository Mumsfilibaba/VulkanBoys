#include "GraphicsContextVK.h"
#include "ShaderVK.h"
#include "RendererVK.h"
#include "SwapChainVK.h"

#include "Core/GLFWWindow.h"

GraphicsContextVK::GraphicsContextVK(IWindow* pWindow)
	: m_pWindow(pWindow),
	m_pSwapChain(nullptr),
	m_Device(),
	m_Instance()
{
}

GraphicsContextVK::~GraphicsContextVK()
{
	SAFEDELETE(m_pSwapChain);
	m_pWindow = nullptr;

	m_Device.release();
	m_Instance.release();
}

void GraphicsContextVK::init()
{
	//Instance Init
#if VALIDATION_LAYERS_ENABLED
	m_Instance.addRequiredExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
	
	GLFWwindow* pNativeWindow = reinterpret_cast<GLFWwindow*>(m_pWindow->getNativeHandle());

	uint32_t count = 0;
	const char** ppExtensions = glfwGetRequiredInstanceExtensions(&count);
	for (uint32_t i = 0; i < count; i++)
	{
		m_Instance.addRequiredExtension(ppExtensions[i]);
	}

	m_Instance.addOptionalExtension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	
	m_Instance.addValidationLayer("VK_LAYER_KHRONOS_validation");
	m_Instance.finalize(VALIDATION_LAYERS_ENABLED);

	//Device Init
	m_Device.addRequiredExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

	m_Device.addOptionalExtension(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	m_Device.addOptionalExtension(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
	m_Device.addOptionalExtension(VK_NV_RAY_TRACING_EXTENSION_NAME);
	
	m_Device.finalize(&m_Instance);

	//SwapChain init
	m_pSwapChain = new SwapChainVK(&m_Instance, &m_Device);
	m_pSwapChain->init(m_pWindow, VK_FORMAT_B8G8R8A8_UNORM, MAX_FRAMES_IN_FLIGHT, true);
}

IRenderer* GraphicsContextVK::createRenderer()
{
	return new RendererVK(this);
}

IShader* GraphicsContextVK::createShader()
{
	return new ShaderVK(&m_Device);
}

IBuffer* GraphicsContextVK::createBuffer()
{
	//Todo: Implement
	return nullptr;
}

IFrameBuffer* GraphicsContextVK::createFrameBuffer()
{
	//Todo: Implement
	return nullptr;
}

IImage* GraphicsContextVK::createImage()
{
	//Todo: Implement
	return nullptr;
}

IImageView* GraphicsContextVK::createImageView()
{
	//Todo: Implement
	return nullptr;
}

ITexture2D* GraphicsContextVK::createTexture2D()
{
	//Todo: Implement
	return nullptr;
}

void GraphicsContextVK::swapBuffers(VkSemaphore renderSemaphore)
{
	m_pSwapChain->present(renderSemaphore);
}
