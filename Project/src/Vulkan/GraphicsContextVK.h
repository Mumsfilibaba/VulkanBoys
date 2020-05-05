#pragma once

#include "Common/IGraphicsContext.h"
#include "VulkanCommon.h"

#include "InstanceVK.h"
#include "DeviceVK.h"

class IWindow;
class SwapChainVK;

class GraphicsContextVK : public IGraphicsContext
{
public:
	DECL_NO_COPY(GraphicsContextVK);

	GraphicsContextVK(IWindow* pWindow);
	~GraphicsContextVK();

	void init();

	virtual RenderingHandler* createRenderingHandler() override;
	virtual IRenderer* createMeshRenderer(RenderingHandler* pRenderingHandler) override;
	virtual IRenderer* createRayTracingRenderer(RenderingHandler* pRenderingHandler) override;
	virtual IImgui* createImgui() override;

	virtual IScene* createScene(const RenderingHandler* pRenderingHandler) override;

    virtual IMesh* createMesh() override;

	virtual IShader* createShader() override;

	virtual IBuffer* createBuffer() override;
	virtual void updateBuffer(IBuffer* pDestination, uint64_t destinationOffset, const void* pSource, uint64_t sizeInBytes) override;
	virtual IFrameBuffer* createFrameBuffer() override;

	virtual IImage* createImage() override;
	virtual IImageView* createImageView() override;
	virtual ITexture2D* createTexture2D() override;
	virtual ISampler* createSampler() override;

	virtual void sync() override;

	void swapBuffers(VkSemaphore renderSemaphore);

	bool setRayTracingEnabled(bool enabled) override;
	bool isRayTracingEnabled() const override { return m_RayTracingEnabled; }

	DeviceVK*		getDevice()				{ return &m_Device; } //Const function?
	InstanceVK*		getInstance()			{ return &m_Instance; } //Const function?
	SwapChainVK*	getSwapChain() const	{ return m_pSwapChain; }

private:
	InstanceVK m_Instance;
	DeviceVK m_Device;
	IWindow* m_pWindow;
	SwapChainVK* m_pSwapChain;

	bool m_RayTracingEnabled;
};
