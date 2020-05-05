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
#include <fstream>

#include <imgui/imgui.h>

#include <glm/gtc/type_ptr.hpp>

#include "LightSetup.h"
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

constexpr bool	FORCE_RAY_TRACING_OFF	= true;
constexpr bool	HIGH_RESOLUTION_SPHERE	= false;
constexpr float CAMERA_PAN_LENGTH		= 10.0f;

Application::Application()
	: m_pWindow(nullptr),
	m_pContext(nullptr),
	m_pRenderingHandler(nullptr),
	m_pMeshRenderer(nullptr),
	m_pRayTracingRenderer(nullptr),
	m_pImgui(nullptr),
	m_pScene(nullptr),
	m_pGunMesh(nullptr),
	m_pGunAlbedo(nullptr),
	m_pInputHandler(nullptr),
	m_Camera(),
	m_IsRunning(false),
	m_UpdateCamera(false),
	m_pCameraPositionSpline(nullptr),
	m_pCameraDirectionSpline(nullptr),
	m_CameraSplineTimer(0.0f),
	m_CameraSplineEnabled(false),
	m_KeyInputEnabled(false)
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

	//Create Scene
	m_pScene = m_pContext->createScene(m_pRenderingHandler);
	m_pScene->init();

	m_pRenderingHandler->setScene(m_pScene);

	TaskDispatcher::execute([this]
		{
			m_pScene->loadFromFile("assets/sponza/", "sponza.obj");
		});

	//Setup lights
	LightSetup& lightSetup = m_pScene->getLightSetup();
	lightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	lightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	lightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));
	lightSetup.addPointLight(PointLight(glm::vec3( 0.0f, 4.0f, 0.0f), glm::vec4(100.0f)));

	// Setup renderers
	m_pImgui = m_pContext->createImgui();
	m_pImgui->init();
	m_pWindow->addEventHandler(m_pImgui);

	m_pMeshRenderer = m_pContext->createMeshRenderer(m_pRenderingHandler);
	m_pMeshRenderer->init();

	if (m_pContext->isRayTracingEnabled())
	{
		m_pRayTracingRenderer = m_pContext->createRayTracingRenderer(m_pRenderingHandler);
		m_pRayTracingRenderer->init();
	}

	//Set renderers to renderhandler
	m_pRenderingHandler->setMeshRenderer(m_pMeshRenderer);
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

	m_pGunMesh = m_pContext->createMesh();
	TaskDispatcher::execute([&]
		{
			m_pGunMesh->initFromFile("assets/meshes/gun.obj");
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
	
	//We can set the pointer to the material even if loading happens on another thread
	m_GunMaterial.setAlbedo(glm::vec4(1.0f));
	m_GunMaterial.setAmbientOcclusion(1.0f);
	m_GunMaterial.setMetallic(1.0f);
	m_GunMaterial.setRoughness(1.0f);
	m_GunMaterial.setAlbedoMap(m_pGunAlbedo);
	m_GunMaterial.setNormalMap(m_pGunNormal);
	m_GunMaterial.setMetallicMap(m_pGunMetallic);
	m_GunMaterial.setRoughnessMap(m_pGunRoughness);

	SamplerParams samplerParams = {};
	samplerParams.MinFilter = VK_FILTER_LINEAR;
	samplerParams.MagFilter = VK_FILTER_LINEAR;
	samplerParams.WrapModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerParams.WrapModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	m_GunMaterial.createSampler(m_pContext, samplerParams);

	//Setup camera
	m_Camera.setDirection(glm::vec3(0.0f, 0.0f, 1.0f));
	m_Camera.setPosition(glm::vec3(0.0f, 1.0f, -3.0f));
	m_Camera.setProjection(90.0f, (float)m_pWindow->getWidth(), (float)m_pWindow->getHeight(), 0.0001f, 50.0f);
	m_Camera.update();

	TaskDispatcher::waitForTasks();

	glm::mat4 scale = glm::scale(glm::vec3(0.75f));
	m_GraphicsIndex0 = m_pScene->submitGraphicsObject(m_pGunMesh, &m_GunMaterial, glm::translate(glm::mat4(1.0f), glm::vec3( 0.0f, 1.0f, 0.1f)) * scale);
	m_GraphicsIndex1 = m_pScene->submitGraphicsObject(m_pGunMesh, &m_GunMaterial, glm::translate(glm::mat4(1.0f), glm::vec3( 1.5f, 1.0f, 0.1f)) * scale);
	m_GraphicsIndex2 = m_pScene->submitGraphicsObject(m_pGunMesh, &m_GunMaterial, glm::translate(glm::mat4(1.0f), glm::vec3(-1.5f, 1.0f, 0.1f)) * scale);

	m_pRenderingHandler->setSkybox(m_pSkybox);
	m_pWindow->show();

	SAFEDELETE(pPanorama);

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


	m_pCameraPositionSpline = DBG_NEW LoopingUniformCRSpline<glm::vec3, float>(positionControlPoints);

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

	m_pCameraDirectionSpline = DBG_NEW LoopingUniformCRSpline<glm::vec3, float>(directionControlPoints);
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

	SAFEDELETE(m_pSkybox);
	SAFEDELETE(m_pGunRoughness);
	SAFEDELETE(m_pGunMetallic);
	SAFEDELETE(m_pGunNormal);
	SAFEDELETE(m_pGunAlbedo);
	SAFEDELETE(m_pGunMesh);
	SAFEDELETE(m_pRenderingHandler);
	SAFEDELETE(m_pMeshRenderer);
	SAFEDELETE(m_pRayTracingRenderer);
	SAFEDELETE(m_pImgui);
	SAFEDELETE(m_pScene);

	SAFEDELETE(m_pContext);

	SAFEDELETE(m_pInputHandler);
	Input::setInputHandler(nullptr);

	SAFEDELETE(m_pWindow);

	SAFEDELETE(m_pCameraDirectionSpline);
	SAFEDELETE(m_pCameraPositionSpline);

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

	if (m_KeyInputEnabled)
	{
		if (key == EKey::KEY_1)
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
	Profiler::progressTimer((float)dt);

	if (!m_TestParameters.Running)
	{
		if (m_KeyInputEnabled)
		{
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
		}
	}
	else
	{
		float deltaTimeMS = (float)dt * 1000.0f;
		m_TestParameters.FrameTimeSum += deltaTimeMS;
		m_TestParameters.FrameCount += 1.0f;
		m_TestParameters.AverageFrametime = m_TestParameters.FrameTimeSum / m_TestParameters.FrameCount;
		m_TestParameters.WorstFrametime = deltaTimeMS > m_TestParameters.WorstFrametime ? deltaTimeMS : m_TestParameters.WorstFrametime;
		m_TestParameters.BestFrametime = deltaTimeMS < m_TestParameters.BestFrametime ? deltaTimeMS : m_TestParameters.BestFrametime;
		m_TestParameters.Frametimes.push_back(deltaTimeMS);

		auto& interpolatedPositionPT = m_pCameraPositionSpline->getTangent(m_CameraSplineTimer);
		glm::vec3 position = interpolatedPositionPT.position;
		glm::vec3 heading = interpolatedPositionPT.tangent;
		glm::vec3 direction = m_pCameraDirectionSpline->getPosition(m_CameraSplineTimer);

		m_CameraSplineTimer += (float)dt / (glm::max(glm::length(heading), 0.0001f));

		m_Camera.setPosition(position);
		m_Camera.setDirection(normalize(glm::normalize(heading) + direction));

		if (m_CameraSplineTimer >= m_pCameraPositionSpline->getMaxT())
		{
			m_TestParameters.CurrentRound++;
			m_CameraSplineTimer = 0.0f;

			if (m_TestParameters.CurrentRound >= m_TestParameters.NumRounds)
			{
				testFinished();
			}
		}
	}

	m_Camera.update();

	if (m_UpdateCamera)
	{
		Input::setMousePosition(m_pWindow, glm::vec2(m_pWindow->getClientWidth() / 2.0f, m_pWindow->getClientHeight() / 2.0f));
	}

	static glm::mat4 rotation = glm::mat4(1.0f);
	rotation = glm::rotate(rotation, glm::radians(30.0f * float(dt)), glm::vec3(0.0f, 1.0f, 0.0f));

	const glm::mat4 scale = glm::scale(glm::vec3(0.75f));
	m_pScene->updateGraphicsObjectTransform(m_GraphicsIndex0, glm::translate(glm::mat4(1.0f), glm::vec3( 0.0f, 1.0f, 0.1f)) * rotation * scale);

	m_pScene->updateCamera(m_Camera);
	m_pScene->updateDebugParameters();

	m_pScene->updateMeshesAndGraphicsObjects();

	m_pRenderingHandler->onSceneUpdated(m_pScene);
}

void Application::renderUI(double dt)
{
	m_pImgui->begin(dt);

	if (m_ApplicationParameters.IsDirty)
	{
		m_pRenderingHandler->setRayTracingResolutionDenominator(m_ApplicationParameters.RayTracingResolutionDenominator);
		m_ApplicationParameters.IsDirty = false;
	}

	if (!m_TestParameters.Running)
	{
		// Color picker for mesh
		ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Color", NULL, ImGuiWindowFlags_NoResize))
		{
			ImGui::ColorPicker4("##picker", glm::value_ptr(g_Color), ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview);
		}
		ImGui::End();

		// Draw profiler UI
		ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Profiler", NULL))
		{
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
		m_pScene->renderUI();
	}

	// Draw Application UI
	ImGui::SetNextWindowSize(ImVec2(430, 450), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Application", NULL))
	{
		if (!m_TestParameters.Running)
		{
			//ImGui::Checkbox("Camera Spline Enabled", &m_CameraSplineEnabled);
			//ImGui::SliderFloat("Camera Timer", &m_CameraSplineTimer, 0.0f, m_CameraPositionSpline->getMaxT());

			//Input Parameters
			if (ImGui::Button("Toggle Key Input"))
			{
				m_KeyInputEnabled = !m_KeyInputEnabled;
			}
			ImGui::SameLine();
			if (m_KeyInputEnabled)
				ImGui::Text("Key Input Enabled");
			else
				ImGui::Text("Key Input Disabled");

			ImGui::NewLine();

			//Graphical Parameters

			m_ApplicationParameters.IsDirty = m_ApplicationParameters.IsDirty || ImGui::SliderInt("Ray Tracing Res. Denom.", &m_ApplicationParameters.RayTracingResolutionDenominator, 1, 8);

			ImGui::NewLine();

			//Test Parameters
			ImGui::InputText("Test Name", m_TestParameters.TestName, 256);
			ImGui::SliderInt("Number of Test Rounds", &m_TestParameters.NumRounds, 1, 5);

			if (ImGui::Button("Start Test"))
			{
				m_CameraSplineTimer = 0.0f;

				m_TestParameters.Running = true;
				m_TestParameters.FrameTimeSum = 0.0f;
				m_TestParameters.FrameCount = 0.0f;
				m_TestParameters.AverageFrametime = 0.0f;
				m_TestParameters.WorstFrametime = std::numeric_limits<float>::min();
				m_TestParameters.BestFrametime = std::numeric_limits<float>::max();
				m_TestParameters.CurrentRound = 0;

				constexpr const uint32_t FRAME_TIMES_RESERVED = 500 * 60 * 2 * 5;

				m_TestParameters.Frametimes.clear();
				m_TestParameters.Frametimes.reserve(FRAME_TIMES_RESERVED);
			}

			ImGui::Text("Previous Test: %s : %d ", m_TestParameters.TestName, m_TestParameters.NumRounds);
			ImGui::Text("Average Frametime: %f", m_TestParameters.AverageFrametime);
			ImGui::Text("Worst Frametime: %f", m_TestParameters.WorstFrametime);
			ImGui::Text("Best Frametime: %f", m_TestParameters.BestFrametime);
			ImGui::Text("Frame count: %f", m_TestParameters.FrameCount);
		}
		else
		{
			if (ImGui::Button("Stop Test"))
			{
				m_TestParameters.Running = false;
			}

			ImGui::Text("Round: %d / %d", m_TestParameters.CurrentRound, m_TestParameters.NumRounds);
			ImGui::Text("Average Frametime: %f", m_TestParameters.AverageFrametime);
			ImGui::Text("Worst Frametime: %f", m_TestParameters.WorstFrametime);
			ImGui::Text("Best Frametime: %f", m_TestParameters.BestFrametime);
			ImGui::Text("Frame Count: %f", m_TestParameters.FrameCount);
		}

	}
	ImGui::End();

	m_pImgui->end();
}

void Application::render(double dt)
{
	UNREFERENCED_PARAMETER(dt);
	m_pRenderingHandler->render(m_pScene);
}

void Application::testFinished()
{
	m_TestParameters.Running = false;

	sanitizeString(m_TestParameters.TestName, 256);

	std::ofstream fileStream;
	fileStream.open("Results/test_" + std::string(m_TestParameters.TestName) + ".txt");

	if (fileStream.is_open())
	{
		/*fileStream << "Avg. FT\tWorst FT\tBest FT\tFrame Count" << std::endl;
		fileStream << m_TestParameters.AverageFrametime << "\t";
		fileStream << m_TestParameters.WorstFrametime << "\t";
		fileStream << m_TestParameters.BestFrametime << "\t";
		fileStream << (uint32_t)m_TestParameters.FrameCount;*/

		fileStream << "Frame Times" << std::endl;

		for (uint32_t i = 0; i < m_TestParameters.Frametimes.size(); i++)
		{
			fileStream << m_TestParameters.Frametimes[i] << std::endl;
		}
	}

	fileStream.close();
}

void Application::sanitizeString(char string[], uint32_t numCharacters)
{
	static std::string illegalChars = "\\/:?\"<>|";
	for (uint32_t i = 0; i < numCharacters; i++)
	{
		if (illegalChars.find(string[i]) != std::string::npos)
		{
			string[i] = '_';
		}
	}
}
