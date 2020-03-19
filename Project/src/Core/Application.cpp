#include "Application.h"
#include "Camera.h"
#include "Input.h"
#include "TaskDispatcher.h"
#include "Transform.h"

#include "Common/Profiler.h"
#include "Common/RenderingHandler.hpp"
#include "Common/IImgui.h"
#include "Common/IWindow.h"
#include "Common/IShader.h"
#include "Common/ISampler.h"
#include "Common/IScene.h"
#include "Common/IMesh.h"
#include "Common/IRenderer.h"
#include "Common/IShader.h"
#include "Common/ITexture2D.h"
#include "Common/ITextureCube.h"
#include "Common/IInputHandler.h"
#include "Common/IGraphicsContext.h"

#include <thread>
#include <chrono>

#include <imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>

#include "Vulkan/RenderPassVK.h"
#include "Vulkan/CommandPoolVK.h"
#include "Vulkan/GraphicsContextVK.h"
#include "Vulkan/DescriptorSetLayoutVK.h"

#ifdef max
	#undef max
#endif

#ifdef min
	#undef min
#endif

Application* Application::s_pInstance = nullptr;

constexpr bool FORCE_RAY_TRACING_OFF	= false;
constexpr bool HIGH_RESOLUTION_SPHERE	= false;
constexpr float CAMERA_PAN_LENGTH = 10.0f;

Application::Application()
	: m_pWindow(nullptr),
	m_pContext(nullptr),
	m_pRenderingHandler(nullptr),
	m_pMeshRenderer(nullptr),
	m_pRayTracingRenderer(nullptr),
	m_pImgui(nullptr),
	m_pScene(nullptr),
	m_pMesh(nullptr),
	m_pGunAlbedo(nullptr),
	m_pInputHandler(nullptr),
	m_Camera(),
	m_IsRunning(false),
	m_UpdateCamera(false),
	m_pParticleTexture(nullptr),
	m_pParticleEmitterHandler(nullptr),
	m_CurrentEmitterIdx(0),
	m_CreatingEmitter(false)
{
	ASSERT(s_pInstance == nullptr);
	s_pInstance = this;
}

Application::~Application()
{
	s_pInstance = nullptr;
}

void Application::init()
{
	LOG("Starting application");

	TaskDispatcher::init();

	//Create window
	m_pWindow = IWindow::create("Hello Vulkan", 1440, 900);
	if (m_pWindow)
	{
		m_pWindow->addEventHandler(this);
		m_pWindow->setFullscreenState(false);
	}

	//Create input
	m_pInputHandler = IInputHandler::create();
	Input::setInputHandler(m_pInputHandler);
	m_pWindow->addEventHandler(m_pInputHandler);

	//Create context
	m_pContext = IGraphicsContext::create(m_pWindow, API::VULKAN);

	m_pContext->setRayTracingEnabled(!FORCE_RAY_TRACING_OFF);

	// Create and setup rendering handler
	m_pRenderingHandler = m_pContext->createRenderingHandler();
	m_pRenderingHandler->initialize();
	m_pRenderingHandler->setClearColor(0.0f, 0.0f, 0.0f);
	m_pRenderingHandler->setViewport((float)m_pWindow->getWidth(), (float)m_pWindow->getHeight(), 0.0f, 1.0f, 0.0f, 0.0f);

	// Setup renderers
	m_pMeshRenderer = m_pContext->createMeshRenderer(m_pRenderingHandler);
	m_pMeshRenderer->init();

	m_pParticleRenderer = m_pContext->createParticleRenderer(m_pRenderingHandler);
	m_pParticleRenderer->init();

	if (m_pContext->isRayTracingEnabled()) 
	{
		m_pRayTracingRenderer = m_pContext->createRayTracingRenderer(m_pRenderingHandler);
		m_pRayTracingRenderer->init();
	}

	//Create particlehandler
	m_pParticleEmitterHandler = m_pContext->createParticleEmitterHandler();
	m_pParticleEmitterHandler->initialize(m_pContext, &m_Camera);

	//Setup Imgui
	m_pImgui = m_pContext->createImgui();
	m_pImgui->init();
	m_pWindow->addEventHandler(m_pImgui);

	//Set renderers to renderhandler
	m_pRenderingHandler->setMeshRenderer(m_pMeshRenderer);
	m_pRenderingHandler->setParticleEmitterHandler(m_pParticleEmitterHandler);
	m_pRenderingHandler->setParticleRenderer(m_pParticleRenderer);
	m_pRenderingHandler->setImguiRenderer(m_pImgui);

	if (m_pContext->isRayTracingEnabled())
	{
		m_pRenderingHandler->setRayTracer(m_pRayTracingRenderer);
	}

	m_pRenderingHandler->setClearColor(0.0f, 0.0f, 0.0f);

	//Load resources
	ITexture2D* pPanorama = m_pContext->createTexture2D();
	TaskDispatcher::execute([&]
		{
			pPanorama->initFromFile("assets/textures/arches.hdr", ETextureFormat::FORMAT_R32G32B32A32_FLOAT, false);
			m_pSkybox = m_pRenderingHandler->generateTextureCube(pPanorama, ETextureFormat::FORMAT_R16G16B16A16_FLOAT, 2048, 1);
		});

	m_pMesh = m_pContext->createMesh();
	TaskDispatcher::execute([&]
		{
			m_pMesh->initFromFile("assets/meshes/gun.obj");
		});

	m_pSphere = m_pContext->createMesh();
	TaskDispatcher::execute([&]
		{
			if (HIGH_RESOLUTION_SPHERE)
			{
				m_pSphere->initFromFile("assets/meshes/sphere2.obj");
			}
			else
			{
				m_pSphere->initFromFile("assets/meshes/sphere.obj");
			}
		});

	m_pCube = m_pContext->createMesh();
	TaskDispatcher::execute([&]
		{
			m_pCube->initAsCube();
		});

	m_pGunAlbedo = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pGunAlbedo->initFromFile("assets/textures/gunAlbedo.tga", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pGunNormal = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pGunNormal->initFromFile("assets/textures/gunNormal.tga", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pGunMetallic = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pGunMetallic->initFromFile("assets/textures/gunMetallic.tga", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pGunRoughness = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pGunRoughness->initFromFile("assets/textures/gunRoughness.tga", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pCubeAlbedo = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pCubeAlbedo->initFromFile("assets/textures/cubeAlbedo.jpg", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pCubeNormal = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pCubeNormal->initFromFile("assets/textures/cubeNormal.jpg", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pCubeMetallic = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pCubeMetallic->initFromFile("assets/textures/cubeMetallic.jpg", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	m_pCubeRoughness = m_pContext->createTexture2D();
	TaskDispatcher::execute([this]
		{
			m_pCubeRoughness->initFromFile("assets/textures/cubeRoughness.jpg", ETextureFormat::FORMAT_R8G8B8A8_UNORM);
		});

	// Setup particles
	m_pParticleTexture = m_pContext->createTexture2D();
	if (!m_pParticleTexture->initFromFile("assets/textures/sun.png", ETextureFormat::FORMAT_R8G8B8A8_UNORM, true))
	{
		LOG("Failed to create particle texture");
		SAFEDELETE(m_pParticleTexture);
	}

	ParticleEmitterInfo emitterInfo = {};
	emitterInfo.position = glm::vec3(8.0f, 0.0f, 0.0f);
	emitterInfo.direction = glm::normalize(glm::vec3(0.0f, 0.9f, 0.1f));
	emitterInfo.particleSize = glm::vec2(0.2f, 0.2f);
	emitterInfo.initialSpeed = 5.5f;
	emitterInfo.particleDuration = 3.0f;
	emitterInfo.particlesPerSecond = 200.0f;
	emitterInfo.spread = glm::quarter_pi<float>() / 1.3f;
	emitterInfo.pTexture = m_pParticleTexture;
	m_pParticleEmitterHandler->createEmitter(emitterInfo);

	// Second emitter
	emitterInfo.position.x = -emitterInfo.position.x;
	m_pParticleEmitterHandler->createEmitter(emitterInfo);

	//We can set the pointer to the material even if loading happens on another thread
	m_GunMaterial.setAlbedo(glm::vec4(1.0f));
	m_GunMaterial.setAmbientOcclusion(1.0f);
	m_GunMaterial.setMetallic(1.0f);
	m_GunMaterial.setRoughness(1.0f);
	m_GunMaterial.setAlbedoMap(m_pGunAlbedo);
	m_GunMaterial.setNormalMap(m_pGunNormal);
	m_GunMaterial.setMetallicMap(m_pGunMetallic);
	m_GunMaterial.setRoughnessMap(m_pGunRoughness);
	
	m_PlaneMaterial.setAlbedo(glm::vec4(1.0f));
	m_PlaneMaterial.setAmbientOcclusion(1.0f);
	m_PlaneMaterial.setMetallic(1.0f);
	m_PlaneMaterial.setRoughness(1.0f);
	m_PlaneMaterial.setAlbedoMap(m_pCubeAlbedo);
	m_PlaneMaterial.setNormalMap(m_pCubeNormal);
	m_PlaneMaterial.setMetallicMap(m_pCubeMetallic);
	m_PlaneMaterial.setRoughnessMap(m_pCubeRoughness);

	SamplerParams samplerParams = {};
	samplerParams.MinFilter = VK_FILTER_LINEAR;
	samplerParams.MagFilter = VK_FILTER_LINEAR;
	samplerParams.WrapModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerParams.WrapModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	m_GunMaterial.createSampler(m_pContext, samplerParams);
	m_PlaneMaterial.createSampler(m_pContext, samplerParams);

	for (uint32_t y = 0; y < SPHERE_COUNT_DIMENSION; y++)
	{
		for (uint32_t x = 0; x < SPHERE_COUNT_DIMENSION; x++)
		{
			Material& material = m_SphereMaterials[x + y * SPHERE_COUNT_DIMENSION];
			material.setAmbientOcclusion(1.0f);
			material.setMetallic(float(y) / float(SPHERE_COUNT_DIMENSION));
			material.setRoughness(glm::clamp((float(x) / float(SPHERE_COUNT_DIMENSION)), 0.05f, 1.0f));
			material.createSampler(m_pContext, samplerParams);
		}
	}
	
	//Setup lights
	m_LightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	m_LightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	m_LightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	m_LightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));

	//Setup camera
	m_Camera.setDirection(glm::vec3(0.0f, 0.0f, 1.0f));
	m_Camera.setPosition(glm::vec3(0.0f, 1.0f, -3.0f));
	m_Camera.setProjection(90.0f, (float)m_pWindow->getWidth(), (float)m_pWindow->getHeight(), 0.0001f, 50.0f);
	m_Camera.update();

	//Create Scene
	m_pScene = m_pContext->createScene();
	TaskDispatcher::execute([this]
		{
			m_pScene->initFromFile("assets/sponza/", "sponza.obj");
		});

	TaskDispatcher::waitForTasks();

	m_pRenderingHandler->setSkybox(m_pSkybox);
	m_pWindow->show();

	SAFEDELETE(pPanorama);

	//Add Game Objects to Scene
	//m_GraphicsIndex0 = m_pScene->submitGraphicsObject(m_pMesh, &m_GunMaterial);
	//m_GraphicsIndex1 = m_pScene->submitGraphicsObject(m_pMesh, &m_GunMaterial);
	//m_GraphicsIndex2 = m_pScene->submitGraphicsObject(m_pMesh, &m_GunMaterial);
	//m_GraphicsIndex3 = m_pScene->submitGraphicsObject(m_pCube, &m_PlaneMaterial, glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.5f, 0.0f)), glm::vec3(10.0f, 0.1f, 10.0f)));
	//	
	//constexpr float SPHERE_SCALE = HIGH_RESOLUTION_SPHERE ? 0.25f : 1.15f;
	//
	//for (uint32_t y = 0; y < SPHERE_COUNT_DIMENSION; y++)
	//{
	//	float yCoord = ((float(SPHERE_COUNT_DIMENSION) * 0.5f) / -2.0f) + float(y * 0.5);

	//	for (uint32_t x = 0; x < SPHERE_COUNT_DIMENSION; x++)
	//	{
	//		float xCoord = ((float(SPHERE_COUNT_DIMENSION) * 0.5f) / -2.0f) + float(x * 0.5);

	//		m_SphereIndexes[x + y * SPHERE_COUNT_DIMENSION] = m_pScene->submitGraphicsObject(
	//			m_pSphere, &m_SphereMaterials[x + y * SPHERE_COUNT_DIMENSION], 
	//			glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(xCoord, yCoord, 1.5f)), glm::vec3(SPHERE_SCALE)));
	//	}
	//}
	
	m_pScene->finalize();

	m_pRenderingHandler->onSceneUpdated(m_pScene);

	std::vector<glm::vec3> positionControlPoints
	{
		glm::vec3(-4.0f,  1.0f,  0.45f),
		glm::vec3( 5.0f,  1.0f,  0.0f),

		glm::vec3( 5.0f,  1.0f,  2.0f),
		glm::vec3(-5.0f,  1.0f,  2.0f),

		glm::vec3(-5.0f,  1.5f, -0.75f),

		glm::vec3( 5.0f,  3.0f,  0.55f),

		glm::vec3( 5.0f,  3.0f,  -2.5f),
		glm::vec3(-5.0f,  3.0f,  -2.5f),

		glm::vec3(-5.0f,  3.0f,  0.35f),
		glm::vec3( 3.0f,  5.0f,  0.35f),

		glm::vec3( 3.0f,  5.0f, -0.85f),

		glm::vec3(-4.0f,  1.0f, -0.85f)
	};


	m_CameraPositionSpline = new LoopingUniformCRSpline<glm::vec3, float>(positionControlPoints);

	std::vector<glm::vec3> directionControlPoints
	{
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f),
		glm::vec3(0.0f, -2.0f,  0.0f),
		glm::vec3(0.0f, -2.0f,  0.0f),
		glm::vec3(0.0f, -1.0f,  0.0f),
		glm::vec3(0.0f)
	};

	m_CameraDirectionSpline = new LoopingUniformCRSpline<glm::vec3, float>(directionControlPoints);
	m_CameraSplineTimer = 0.0f;
	m_CameraSplineEnabled = false;
}

void Application::run()
{
	m_IsRunning = true;

	auto currentTime	= std::chrono::high_resolution_clock::now();
	auto lastTime		= currentTime;

	//HACK to get a non-null deltatime
	std::this_thread::sleep_for(std::chrono::milliseconds(16));

	while (m_IsRunning)
	{
		lastTime	= currentTime;
		currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> deltatime = currentTime - lastTime;
		double seconds = deltatime.count() / 1000.0;

		m_pWindow->peekEvents();
		if (m_pWindow->hasFocus())
		{
			update(seconds);
			renderUI(seconds);
			render(seconds);
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	}
}

void Application::release()
{
	m_pWindow->removeEventHandler(m_pInputHandler);
	m_pWindow->removeEventHandler(m_pImgui);
	m_pWindow->removeEventHandler(this);

	m_pContext->sync();

	m_GunMaterial.release();
	m_PlaneMaterial.release();

	for (uint32_t i = 0; i < SPHERE_COUNT_DIMENSION * SPHERE_COUNT_DIMENSION; i++)
	{
		m_SphereMaterials[i].release();
	}

	SAFEDELETE(m_pSkybox);
	SAFEDELETE(m_pSphere);
	SAFEDELETE(m_pGunRoughness);
	SAFEDELETE(m_pGunMetallic);
	SAFEDELETE(m_pGunNormal);
	SAFEDELETE(m_pGunAlbedo);
	SAFEDELETE(m_pCube);
	SAFEDELETE(m_pCubeAlbedo);
	SAFEDELETE(m_pCubeNormal);
	SAFEDELETE(m_pCubeMetallic);
	SAFEDELETE(m_pCubeRoughness);
	SAFEDELETE(m_pMesh);
	SAFEDELETE(m_pRenderingHandler);
	SAFEDELETE(m_pMeshRenderer);
	SAFEDELETE(m_pRayTracingRenderer);
	SAFEDELETE(m_pParticleRenderer);
	SAFEDELETE(m_pParticleTexture);
	SAFEDELETE(m_pParticleEmitterHandler);
	SAFEDELETE(m_pImgui);
	SAFEDELETE(m_pScene);

	SAFEDELETE(m_pContext);

	SAFEDELETE(m_pInputHandler);
	Input::setInputHandler(nullptr);

	SAFEDELETE(m_pWindow);

	TaskDispatcher::release();

	LOG("Exiting Application");
}

void Application::onWindowResize(uint32_t width, uint32_t height)
{
	D_LOG("Resize w=%d h%d", width , height);

	if (width != 0 && height != 0)
	{
		if (m_pRenderingHandler)
		{
			m_pRenderingHandler->setViewport((float)width, (float)height, 0.0f, 1.0f, 0.0f, 0.0f);
			m_pRenderingHandler->onWindowResize(width, height);
		}

		m_Camera.setProjection(90.0f, float(width), float(height), 0.01f, 100.0f);
	}
}

void Application::onMouseMove(uint32_t x, uint32_t y)
{
	if (m_UpdateCamera)
	{
		glm::vec2 middlePos = middlePos = glm::vec2(m_pWindow->getClientWidth() / 2.0f, m_pWindow->getClientHeight() / 2.0f);

		float xoffset = middlePos.x - x;
		float yoffset = y - middlePos.y;

		constexpr float sensitivity = 0.25f;
		xoffset *= sensitivity;
		yoffset *= sensitivity;

		glm::vec3 rotation = m_Camera.getRotation();
		rotation += glm::vec3(yoffset, -xoffset, 0.0f);

		m_Camera.setRotation(rotation);
	}
}

void Application::onKeyPressed(EKey key)
{
	//Exit application by pressing escape
	if (key == EKey::KEY_ESCAPE)
	{
		m_IsRunning = false;
	}
	else if (key == EKey::KEY_1)
	{
		m_pWindow->toggleFullscreenState();
	}
	else if (key == EKey::KEY_2)
	{
		m_UpdateCamera = !m_UpdateCamera;
        if (m_UpdateCamera)
        {
            Input::captureMouse(m_pWindow);
        }
        else
        {
            Input::releaseMouse(m_pWindow);
        }
	}
}

void Application::onWindowClose()
{
	D_LOG("Window Closed");
	m_IsRunning = false;
}

Application* Application::get()
{
	return s_pInstance;
}

static glm::vec4 g_Color = glm::vec4(1.0f);

void Application::update(double dt)
{
	//TODO: Is float enough precision on fast systems?
	Profiler::progressTimer((float)dt);

	constexpr float speed = 0.75f;
	if (Input::isKeyPressed(EKey::KEY_A))
	{
		m_Camera.translate(glm::vec3(-speed * dt, 0.0f, 0.0f));
	}
	else if (Input::isKeyPressed(EKey::KEY_D))
	{
		m_Camera.translate(glm::vec3(speed * dt, 0.0f, 0.0f));
	}

	if (Input::isKeyPressed(EKey::KEY_W))
	{
		m_Camera.translate(glm::vec3(0.0f, 0.0f, speed * dt));
	}
	else if (Input::isKeyPressed(EKey::KEY_S))
	{
		m_Camera.translate(glm::vec3(0.0f, 0.0f, -speed * dt));
	}

	if (Input::isKeyPressed(EKey::KEY_Q))
	{
		m_Camera.translate(glm::vec3(0.0f, speed * dt, 0.0f));
	}
	else if (Input::isKeyPressed(EKey::KEY_E))
	{
		m_Camera.translate(glm::vec3(0.0f, -speed * dt, 0.0f));
	}

	if (Input::isKeyPressed(EKey::KEY_A))
	{
		m_Camera.translate(glm::vec3(-speed * dt, 0.0f, 0.0f));
	}
	else if (Input::isKeyPressed(EKey::KEY_D))
	{
		m_Camera.translate(glm::vec3(speed * dt, 0.0f, 0.0f));
	}

	constexpr float rotationSpeed = 30.0f;
	if (Input::isKeyPressed(EKey::KEY_LEFT))
	{
		m_Camera.rotate(glm::vec3(0.0f, -rotationSpeed * dt, 0.0f));
	}
	else if (Input::isKeyPressed(EKey::KEY_RIGHT))
	{
		m_Camera.rotate(glm::vec3(0.0f, rotationSpeed * dt, 0.0f));
	}

	if (Input::isKeyPressed(EKey::KEY_UP))
	{
		m_Camera.rotate(glm::vec3(-rotationSpeed * dt, 0.0f, 0.0f));
	}
	else if (Input::isKeyPressed(EKey::KEY_DOWN))
	{
		m_Camera.rotate(glm::vec3(rotationSpeed * dt, 0.0f, 0.0f));
	}

	//if (m_CameraTimer > 0.9f)
	//{
	//	constexpr float LOOP_BACK_POINT = 0.03f;
	//	glm::bvec3 backAtStart = glm::epsilonEqual(m_Camera.getPosition(), m_CameraSpline.eval_f(LOOP_BACK_POINT), glm::vec3(0.1f));

	//	if (backAtStart.x && backAtStart.y && backAtStart.z)
	//	{
	//		m_CameraTimer = LOOP_BACK_POINT;
	//	}
	//}

	if (m_CameraSplineEnabled)
	{
		auto& interpolatedPositionPT = m_CameraPositionSpline->getTangent(m_CameraSplineTimer);
		glm::vec3 position = interpolatedPositionPT.position;
		glm::vec3 heading = interpolatedPositionPT.tangent;
		glm::vec3 direction = m_CameraDirectionSpline->getPosition(m_CameraSplineTimer);

		m_CameraSplineTimer += dt / (glm::max(glm::length(heading), 0.0001f));

		m_Camera.setPosition(position);
		m_Camera.setDirection(normalize(glm::normalize(heading) + direction));

		if (m_CameraSplineTimer >= m_CameraPositionSpline->getMaxT())
		{
			m_CameraSplineTimer = 0.0f;
		}
	}

	m_Camera.update();

	if (m_UpdateCamera)
	{
		Input::setMousePosition(m_pWindow, glm::vec2(m_pWindow->getClientWidth() / 2.0f, m_pWindow->getClientHeight() / 2.0f));
	}

	//TODO: Is float enough precision on fast systems?
	//m_pParticleEmitterHandler->update((float)dt);

	//Set sphere color
	//static glm::mat4 rotation = glm::mat4(1.0f);
	//rotation = glm::rotate(rotation, glm::radians(30.0f * float(dt)), glm::vec3(0.0f, 1.0f, 0.0f));
	//m_pScene->updateGraphicsObjectTransform(m_GraphicsIndex0, glm::mat4(1.0f) * rotation);
	//m_pScene->updateGraphicsObjectTransform(m_GraphicsIndex1, glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.0f, 0.0f)));
	//m_pScene->updateGraphicsObjectTransform(m_GraphicsIndex2, glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 0.0f, 0.0f)));

	//for (uint32_t i = 0; i < SPHERE_COUNT_DIMENSION * SPHERE_COUNT_DIMENSION; i++)
	//{
	//	m_SphereMaterials[i].setAlbedo(g_Color);
	//}

	m_pScene->updateCamera(m_Camera);
	m_pScene->updateLightSetup(m_LightSetup);
}

void Application::renderUI(double dt)
{
	m_pImgui->begin(dt);

	// Color picker for mesh
	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Color", NULL, ImGuiWindowFlags_NoResize))
	{
		ImGui::ColorPicker4("##picker", glm::value_ptr(g_Color), ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
	}
	ImGui::End();

	// Particle control panel
	ImGui::SetNextWindowSize(ImVec2(420, 210), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Particles", NULL)) {
		ImGui::Text("Toggle Computation Device");
		const char* btnLabel = m_pParticleEmitterHandler->gpuComputed() ? "GPU" : "CPU";
		if (ImGui::Button(btnLabel, ImVec2(40, 25))) {
			m_pParticleEmitterHandler->toggleComputationDevice();
		}

		std::vector<ParticleEmitter*> particleEmitters = m_pParticleEmitterHandler->getParticleEmitters();

		// Emitter creation
		if (ImGui::Button("New emitter")) {
			m_CreatingEmitter = true;
			ParticleEmitter* pLatestEmitter = particleEmitters.back();

			m_NewEmitterInfo = {};
			m_NewEmitterInfo.direction = pLatestEmitter->getDirection();
			m_NewEmitterInfo.initialSpeed = pLatestEmitter->getInitialSpeed();
			m_NewEmitterInfo.particleSize = pLatestEmitter->getParticleSize();
			m_NewEmitterInfo.spread = pLatestEmitter->getSpread();
			m_NewEmitterInfo.particlesPerSecond = pLatestEmitter->getParticlesPerSecond();
			m_NewEmitterInfo.particleDuration = pLatestEmitter->getParticleDuration();
		}

		// Emitter selection
		m_CurrentEmitterIdx = std::min(m_CurrentEmitterIdx, particleEmitters.size() - 1);
		int emitterIdxInt = (int)m_CurrentEmitterIdx;

		if (ImGui::SliderInt("Emitter selection", &emitterIdxInt, 0, int(particleEmitters.size() - 1))) {
			m_CurrentEmitterIdx = (size_t)emitterIdxInt;
		}

		// Get current emitter data
		ParticleEmitter* pEmitter = particleEmitters[m_CurrentEmitterIdx];
		glm::vec3 emitterPos = pEmitter->getPosition();
		glm::vec2 particleSize = pEmitter->getParticleSize();

		glm::vec3 emitterDirection = pEmitter->getDirection();
		float yaw = getYaw(emitterDirection);
		float oldYaw = yaw;
		float pitch = getPitch(emitterDirection);
		float oldPitch = pitch;

		float emitterSpeed = pEmitter->getInitialSpeed();
		float spread = pEmitter->getSpread();

		if (ImGui::SliderFloat3("Position", glm::value_ptr(emitterPos), -10.0f, 10.0f)) {
			pEmitter->setPosition(emitterPos);
		}
		if (ImGui::SliderFloat("Yaw", &yaw, 0.01f - glm::pi<float>(), glm::pi<float>() - 0.01f)) {
			applyYaw(emitterDirection, yaw - oldYaw);
			pEmitter->setDirection(emitterDirection);
		}
		if (ImGui::SliderFloat("Pitch", &pitch, 0.01f - glm::half_pi<float>(), glm::half_pi<float>() - 0.01f)) {
			applyPitch(emitterDirection, pitch - oldPitch);
			pEmitter->setDirection(emitterDirection);
		}
		if (ImGui::SliderFloat3("Direction", glm::value_ptr(emitterDirection), -1.0f, 1.0f)) {
			pEmitter->setDirection(glm::normalize(emitterDirection));
		}
		if (ImGui::SliderFloat2("Size", glm::value_ptr(particleSize), 0.0f, 1.0f)) {
			pEmitter->setParticleSize(particleSize);
		}
		if (ImGui::SliderFloat("Speed", &emitterSpeed, 0.0f, 20.0f)) {
			pEmitter->setInitialSpeed(emitterSpeed);
		}
		if (ImGui::SliderFloat("Spread", &spread, 0.0f, glm::pi<float>())) {
			pEmitter->setSpread(spread);
		}

		ImGui::Text("Particles: %d", pEmitter->getParticleCount());
	}
	ImGui::End();

	// Emitter creation window
	if (m_CreatingEmitter) {
		// Open a new window
		ImGui::SetNextWindowSize(ImVec2(420, 210), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Emitter Creation", NULL)) {
			float yaw = getYaw(m_NewEmitterInfo.direction);
			float oldYaw = yaw;
			float pitch = getPitch(m_NewEmitterInfo.direction);
			float oldPitch = pitch;

			ImGui::SliderFloat3("Position", glm::value_ptr(m_NewEmitterInfo.position), -10.0f, 10.0f);
			if (ImGui::SliderFloat("Yaw", &yaw, 0.01f - glm::pi<float>(), glm::pi<float>() - 0.01f)) {
				applyYaw(m_NewEmitterInfo.direction, yaw - oldYaw);
			}
			if (ImGui::SliderFloat("Pitch", &pitch, 0.01f - glm::half_pi<float>(), glm::half_pi<float>() - 0.01f)) {
				applyPitch(m_NewEmitterInfo.direction, pitch - oldPitch);
			}
			if (ImGui::SliderFloat3("Direction", glm::value_ptr(m_NewEmitterInfo.direction), -1.0f, 1.0f)) {
				m_NewEmitterInfo.direction = glm::normalize(m_NewEmitterInfo.direction);
			}
			ImGui::SliderFloat2("Particle Size", glm::value_ptr(m_NewEmitterInfo.particleSize), 0.0f, 1.0f);
			ImGui::SliderFloat("Speed", &m_NewEmitterInfo.initialSpeed, 0.0f, 20.0f);
			ImGui::SliderFloat("Spread", &m_NewEmitterInfo.spread, 0.0f, glm::pi<float>());
			ImGui::InputFloat("Particle duration", &m_NewEmitterInfo.particleDuration);
			ImGui::InputFloat("Particles per second", &m_NewEmitterInfo.particlesPerSecond);
			ImGui::Text("Emitted particles: %d", int(m_NewEmitterInfo.particlesPerSecond * m_NewEmitterInfo.particleDuration));

			if (ImGui::Button("Create")) {
				m_CreatingEmitter = false;
				m_NewEmitterInfo.pTexture = m_pParticleTexture;

				m_pParticleEmitterHandler->createEmitter(m_NewEmitterInfo);
			}
			if (ImGui::Button("Cancel")) {
				m_CreatingEmitter = false;
			}
			ImGui::End();
		}
	}

	// Draw profiler UI
	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Profiler", NULL))
	{
		//TODO: If the renderinghandler have all renderers/handlers, why are we calling their draw UI functions should this not be done by the renderinghandler
		m_pParticleEmitterHandler->drawProfilerUI();
		m_pRenderingHandler->drawProfilerUI();
	}
	ImGui::End();

	if (m_pContext->isRayTracingEnabled())
	{
		// Draw Ray Tracing UI
		ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Ray Tracer", NULL))
		{
			m_pRayTracingRenderer->renderUI();
		}
		ImGui::End();
	}

	// Draw Scene UI
	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Scene", NULL))
	{
		m_pScene->renderUI();
	}
	ImGui::End();

	// Draw Application UI
	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Application", NULL))
	{
		ImGui::Checkbox("Camera Spline Enabled", &m_CameraSplineEnabled);
		ImGui::SliderFloat("Camera Timer", &m_CameraSplineTimer, 0.0f, m_CameraPositionSpline->getMaxT());
	}
	ImGui::End();

	m_pImgui->end();
}

void Application::render(double dt)
{
	UNREFERENCED_PARAMETER(dt);

	m_pRenderingHandler->render(m_pScene);
}
