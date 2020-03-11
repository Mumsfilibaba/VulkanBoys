#include "SceneVK.h"

#include "Core/Material.h"

#include "Vulkan/GraphicsContextVK.h"
#include "Vulkan/DeviceVK.h"
#include "Vulkan/BufferVK.h"
#include "Vulkan/MeshVK.h"
#include "Vulkan/Texture2DVK.h"
#include "Vulkan/SamplerVK.h"

#include "Vulkan/CommandPoolVK.h"
#include "Vulkan/CommandBufferVK.h"

#include <algorithm>

#ifdef max
    #undef max
#endif

SceneVK::SceneVK(IGraphicsContext* pContext) :
	m_pContext(reinterpret_cast<GraphicsContextVK*>(pContext)),
	m_pScratchBuffer(nullptr),
	m_pInstanceBuffer(nullptr),
	m_pGarbageScratchBuffer(nullptr),
	m_TotalNumberOfVertices(0),
	m_TotalNumberOfIndices(0),
	m_pGarbageInstanceBuffer(nullptr),
	m_pCombinedVertexBuffer(nullptr),
	m_pCombinedIndexBuffer(nullptr),
	m_pMeshIndexBuffer(nullptr),
	m_NumBottomLevelAccelerationStructures(0),
	m_pTempCommandPool(nullptr),
	m_pTempCommandBuffer(nullptr),
	m_TopLevelIsDirty(false),
	m_BottomLevelIsDirty(false),
	m_pVeryTempMaterial(nullptr),
	m_pLightProbeMesh(nullptr),
	m_pDefaultTexture(nullptr),
	m_pDefaultNormal(nullptr),
	m_pDefaultSampler(nullptr),
	m_pMaterialParametersBuffer(nullptr)
{
	m_pDevice = reinterpret_cast<DeviceVK*>(m_pContext->getDevice());
}

SceneVK::~SceneVK()
{
	SAFEDELETE(m_pProfiler);

	if (m_pTempCommandBuffer != nullptr)
	{
		m_pTempCommandPool->freeCommandBuffer(&m_pTempCommandBuffer);
		m_pTempCommandBuffer = nullptr;
	}

	SAFEDELETE(m_pTempCommandPool);
	SAFEDELETE(m_pScratchBuffer);
	SAFEDELETE(m_pInstanceBuffer);
	SAFEDELETE(m_pGarbageScratchBuffer);
	SAFEDELETE(m_pGarbageInstanceBuffer);
	SAFEDELETE(m_pCombinedVertexBuffer);
	SAFEDELETE(m_pCombinedIndexBuffer);
	SAFEDELETE(m_pMeshIndexBuffer);
	SAFEDELETE(m_pLightProbeMesh);
	SAFEDELETE(m_pDefaultTexture);
	SAFEDELETE(m_pDefaultNormal);
	SAFEDELETE(m_pDefaultSampler);

	SAFEDELETE(m_pMaterialParametersBuffer);

	for (auto& bottomLevelAccelerationStructurePerMesh : m_NewBottomLevelAccelerationStructures)
	{
		for (auto& bottomLevelAccelerationStructure : bottomLevelAccelerationStructurePerMesh.second)
		{
			vkFreeMemory(m_pDevice->getDevice(), bottomLevelAccelerationStructure.second.Memory, nullptr);
			m_pDevice->vkDestroyAccelerationStructureNV(m_pDevice->getDevice(), bottomLevelAccelerationStructure.second.AccelerationStructure, nullptr);
		}
	}
	m_NewBottomLevelAccelerationStructures.clear();

	for (auto& bottomLevelAccelerationStructurePerMesh : m_FinalizedBottomLevelAccelerationStructures)
	{
		for (auto& bottomLevelAccelerationStructure : bottomLevelAccelerationStructurePerMesh.second)
		{
			vkFreeMemory(m_pDevice->getDevice(), bottomLevelAccelerationStructure.second.Memory, nullptr);
			m_pDevice->vkDestroyAccelerationStructureNV(m_pDevice->getDevice(), bottomLevelAccelerationStructure.second.AccelerationStructure, nullptr);
		}
	}
	m_FinalizedBottomLevelAccelerationStructures.clear();


	if (m_TopLevelAccelerationStructure.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_pDevice->getDevice(), m_TopLevelAccelerationStructure.Memory, nullptr);
		m_TopLevelAccelerationStructure.Memory = VK_NULL_HANDLE;
	}

	if (m_TopLevelAccelerationStructure.AccelerationStructure != VK_NULL_HANDLE)
	{
		m_pDevice->vkDestroyAccelerationStructureNV(m_pDevice->getDevice(), m_TopLevelAccelerationStructure.AccelerationStructure, nullptr);
		m_TopLevelAccelerationStructure.AccelerationStructure = VK_NULL_HANDLE;
	}

	m_TopLevelAccelerationStructure.Handle = VK_NULL_HANDLE;
}

bool SceneVK::finalize()
{
	m_pTempCommandPool = DBG_NEW CommandPoolVK(m_pContext->getDevice(), m_pContext->getDevice()->getQueueFamilyIndices().computeFamily.value());
	m_pTempCommandPool->init();

	m_pTempCommandBuffer = m_pTempCommandPool->allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	
	if (!createTLAS())
	{
		LOG("--- SceneVK: Failed to create TLAS!");
		return false;
	}

	if (!createDefaultTexturesAndSamplers())
	{
		LOG("--- SceneVK: Failed to create Default Textures and/or Samplers!");
		return false;
	}

	initBuildBuffers();

	//Build BLASs
	if (!buildBLASs())
	{
		LOG("--- SceneVK: Failed to initialize BLASs!");
		return false;
	}

	//Build TLASs
	if (!buildTLAS())
	{
		LOG("--- SceneVK: Failed to initialize TLAS!");
		return false;
	}

	if (!createCombinedGraphicsObjectData())
	{
		LOG("--- SceneVK: Failed to create Combined Graphics Object Data!");
		return false;
	}

	updateMaterials();
	
	createProfiler();

	LOG("--- SceneVK: Successfully initialized Acceleration Table!");
	return true;
}

void SceneVK::update()
{
	if (!m_BottomLevelIsDirty)
	{
		updateTLAS();
	}
	else
	{
		buildBLASs();
		updateTLAS();
		createCombinedGraphicsObjectData();
	}
}

void SceneVK::updateMaterials()
{
	for (uint32_t i = 0; i < MAX_NUM_UNIQUE_MATERIALS; i++)
	{
		if (i < m_Materials.size())
		{
			const Material* pMaterial = m_Materials[i];

			const Texture2DVK* pAlbedoMap = reinterpret_cast<const Texture2DVK*>(pMaterial->getAlbedoMap());
			const Texture2DVK* pNormalMap = reinterpret_cast<const Texture2DVK*>(pMaterial->getNormalMap());
			const Texture2DVK* pAOMap = reinterpret_cast<const Texture2DVK*>(pMaterial->getAmbientOcclusionMap());
			const Texture2DVK* pMetallicMap = reinterpret_cast<const Texture2DVK*>(pMaterial->getMetallicMap());
			const Texture2DVK* pRoughnessMap = reinterpret_cast<const Texture2DVK*>(pMaterial->getRoughnessMap());
			const SamplerVK* pSampler = reinterpret_cast<const SamplerVK*>(pMaterial->getSampler());

			m_AlbedoMaps[i] = pAlbedoMap != nullptr ? pAlbedoMap->getImageView() : m_pDefaultTexture->getImageView();
			m_NormalMaps[i] = pNormalMap != nullptr ? pNormalMap->getImageView() : m_pDefaultNormal->getImageView();
			m_AOMaps[i] = pAOMap != nullptr ? pAOMap->getImageView() : m_pDefaultTexture->getImageView();
			m_MetallicMaps[i] = pMetallicMap != nullptr ? pMetallicMap->getImageView() : m_pDefaultTexture->getImageView();
			m_RoughnessMaps[i] = pRoughnessMap != nullptr ? pRoughnessMap->getImageView() : m_pDefaultTexture->getImageView();
			m_Samplers[i] = pSampler != nullptr ? pSampler : m_pDefaultSampler;
			m_MaterialParameters[i] =
			{
				pMaterial->getAlbedo(),
				pMaterial->getMetallic(),
				pMaterial->getRoughness(),
				pMaterial->getAmbientOcclusion(),
				1.0f
			};
		}
		else
		{
			m_AlbedoMaps[i] = m_pDefaultTexture->getImageView();
			m_NormalMaps[i] =m_pDefaultNormal->getImageView();
			m_AOMaps[i] = m_pDefaultTexture->getImageView();
			m_MetallicMaps[i] = m_pDefaultTexture->getImageView();
			m_RoughnessMaps[i] = m_pDefaultTexture->getImageView();
			m_Samplers[i] =m_pDefaultSampler;
			m_MaterialParameters[i] =
			{
				glm::vec4(1.0f),
				1.0f,
				1.0f,
				1.0f,
				1.0f
			};
		}
	}

	constexpr uint32_t SIZE_IN_BYTES = MAX_NUM_UNIQUE_MATERIALS * sizeof(MaterialParameters);

	void* pDest;
	m_pMaterialParametersBuffer->map(&pDest);
	memcpy(pDest, m_MaterialParameters.data(), SIZE_IN_BYTES);
	m_pMaterialParametersBuffer->unmap();
}

void SceneVK::updateCamera(const Camera& camera)
{
	m_Camera = camera;
}

void SceneVK::updateLightSetup(const LightSetup& lightsetup)
{
	m_LightSetup = lightsetup;
}

uint32_t SceneVK::submitGraphicsObject(const IMesh* pMesh, const Material* pMaterial, const glm::mat4& transform, uint8_t customMask)
{
	if (m_pVeryTempMaterial == nullptr)
	{
		m_pVeryTempMaterial = pMaterial;
	}

	const MeshVK* pVulkanMesh = reinterpret_cast<const MeshVK*>(pMesh);

	if (pVulkanMesh == nullptr || pMaterial == nullptr)
	{
		LOG("--- SceneVK: addGraphicsObjectInstance failed, Mesh or Material is nullptr!");
		return false;
	}
	
	m_TopLevelIsDirty = true;

	auto& newBLASPerMesh = m_NewBottomLevelAccelerationStructures.find(pVulkanMesh);
	auto& finalizedBLASPerMesh = m_FinalizedBottomLevelAccelerationStructures.find(pVulkanMesh);

	const BottomLevelAccelerationStructure* pBottomLevelAccelerationStructure = nullptr;

	if (newBLASPerMesh == m_NewBottomLevelAccelerationStructures.end())
	{
		//Not in new BLAS per Mesh

		if (finalizedBLASPerMesh == m_FinalizedBottomLevelAccelerationStructures.end())
		{
			//Not in finalized BLAS per Mesh either, create new BLAS

			m_BottomLevelIsDirty = true;

			pBottomLevelAccelerationStructure = createBLAS(pVulkanMesh, pMaterial);
			m_AllMeshes.push_back(pVulkanMesh);
			m_TotalNumberOfVertices += static_cast<uint32_t>(pVulkanMesh->getVertexBuffer()->getSizeInBytes() / sizeof(Vertex));
			m_TotalNumberOfIndices += pVulkanMesh->getIndexBuffer()->getSizeInBytes() / sizeof(uint32_t);
		}
		else if (finalizedBLASPerMesh->second.find(pMaterial) == finalizedBLASPerMesh->second.end())
		{
			//In finalized BLAS per Mesh but not in finalized BLAS per Material, create copy from finalized, add new BLASs per MESH to new, add BLAS to that

			m_BottomLevelIsDirty = true;
			BottomLevelAccelerationStructure blasCopy = finalizedBLASPerMesh->second.begin()->second;

			blasCopy.Index = m_NumBottomLevelAccelerationStructures;
			m_NumBottomLevelAccelerationStructures++;

			blasCopy.MaterialIndex = m_Materials.size();
			m_Materials.push_back(pMaterial);

			std::unordered_map<const Material*, BottomLevelAccelerationStructure> tempBLASPerMesh;
			tempBLASPerMesh[pMaterial] = blasCopy;
			m_NewBottomLevelAccelerationStructures[pVulkanMesh] = tempBLASPerMesh;

			pBottomLevelAccelerationStructure = &m_NewBottomLevelAccelerationStructures[pVulkanMesh][pMaterial];
		}
		else
		{
			//In finalized BLAS per Mesh and and finalized BLAS per Material
			pBottomLevelAccelerationStructure = &finalizedBLASPerMesh->second[pMaterial];
		}
	}
	else if (newBLASPerMesh->second.find(pMaterial) == newBLASPerMesh->second.end())
	{
		//In new BLAS per Mesh but not in new BLAS per material, create copy from new

		m_BottomLevelIsDirty = true;
		BottomLevelAccelerationStructure blasCopy = newBLASPerMesh->second.begin()->second;

		blasCopy.Index = m_NumBottomLevelAccelerationStructures;
		m_NumBottomLevelAccelerationStructures++;

		blasCopy.MaterialIndex = m_Materials.size();
		m_Materials.push_back(pMaterial);

		newBLASPerMesh->second[pMaterial] = blasCopy;
		pBottomLevelAccelerationStructure = &newBLASPerMesh->second[pMaterial];
	}
	else
	{
		//In new BLAS per Mesh and and new BLAS per Material
		pBottomLevelAccelerationStructure = &newBLASPerMesh->second[pMaterial];
	}

	GeometryInstance geometryInstance = {};
	geometryInstance.Transform = glm::transpose(transform);
	geometryInstance.InstanceId = pBottomLevelAccelerationStructure->Index; //This is not really used anymore, Todo: remove this
	geometryInstance.Mask = customMask;
	geometryInstance.InstanceOffset = 0;
	geometryInstance.Flags = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
	geometryInstance.AccelerationStructureHandle = pBottomLevelAccelerationStructure->Handle;
	m_GeometryInstances.push_back(geometryInstance);

	m_GraphicsObjects.push_back({ pVulkanMesh, pMaterial, transform });
	
	return m_GraphicsObjects.size() - 1;
}

void SceneVK::updateGraphicsObjectTransform(uint32_t index, const glm::mat4& transform)
{
	m_GeometryInstances[index].Transform = glm::transpose(transform);
	m_GraphicsObjects[index].Transform = transform;
}

bool SceneVK::createDefaultTexturesAndSamplers()
{
	uint8_t whitePixels[] = { 255, 255, 255, 255 };
	m_pDefaultTexture = DBG_NEW Texture2DVK(m_pContext->getDevice());
	if (!m_pDefaultTexture->initFromMemory(whitePixels, 1, 1, ETextureFormat::FORMAT_R8G8B8A8_UNORM, 0, false))
	{
		return false;
	}

	uint8_t pixels[] = { 127, 127, 255, 255 };
	m_pDefaultNormal = DBG_NEW Texture2DVK(m_pContext->getDevice());
	if (!m_pDefaultNormal->initFromMemory(pixels, 1, 1, ETextureFormat::FORMAT_R8G8B8A8_UNORM, 0, false))
	{
		return false;
	}

	SamplerParams samplerParams = {};
	samplerParams.MinFilter = VkFilter::VK_FILTER_LINEAR;
	samplerParams.MagFilter = VkFilter::VK_FILTER_LINEAR;
	samplerParams.WrapModeU = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerParams.WrapModeV = samplerParams.WrapModeU;
	samplerParams.WrapModeW = samplerParams.WrapModeU;

	m_pDefaultSampler = new SamplerVK(m_pContext->getDevice());
	if (!m_pDefaultSampler->init(samplerParams))
	{
		return false;
	}

	m_AlbedoMaps.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_NormalMaps.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_AOMaps.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_MetallicMaps.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_RoughnessMaps.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_Samplers.resize(MAX_NUM_UNIQUE_MATERIALS);
	m_MaterialParameters.resize(MAX_NUM_UNIQUE_MATERIALS);

	constexpr uint32_t SIZE_IN_BYTES = MAX_NUM_UNIQUE_MATERIALS * sizeof(MaterialParameters);

	BufferParams materialParametersBufferParams = {};
	materialParametersBufferParams.Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	materialParametersBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	materialParametersBufferParams.SizeInBytes = SIZE_IN_BYTES;

	m_pMaterialParametersBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
	m_pMaterialParametersBuffer->init(materialParametersBufferParams);

	return true;
}

void SceneVK::initBuildBuffers()
{
	// Acceleration structure build requires some scratch space to store temporary information
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;

	VkMemoryRequirements2 memReqTLAS;
	memoryRequirementsInfo.accelerationStructure = m_TopLevelAccelerationStructure.AccelerationStructure;
	m_pDevice->vkGetAccelerationStructureMemoryRequirementsNV(m_pDevice->getDevice(), &memoryRequirementsInfo, &memReqTLAS);

	BufferParams scratchBufferParams = {};
	scratchBufferParams.Usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	scratchBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	scratchBufferParams.SizeInBytes = std::max(findMaxMemReqBLAS(), memReqTLAS.memoryRequirements.size);

	m_pScratchBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
	m_pScratchBuffer->init(scratchBufferParams);

	BufferParams instanceBufferParmas = {};
	instanceBufferParmas.Usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	instanceBufferParmas.MemoryProperty = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	instanceBufferParmas.SizeInBytes = sizeof(GeometryInstance) * m_GeometryInstances.size();

	m_pInstanceBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
	m_pInstanceBuffer->init(instanceBufferParmas);
}

SceneVK::BottomLevelAccelerationStructure* SceneVK::createBLAS(const MeshVK* pMesh, const Material* pMaterial)
{
	BottomLevelAccelerationStructure bottomLevelAccelerationStructure = {};

	bottomLevelAccelerationStructure.Geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
	bottomLevelAccelerationStructure.Geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.vertexData = ((BufferVK*)pMesh->getVertexBuffer())->getBuffer();
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.vertexOffset = 0;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.vertexCount = pMesh->getVertexCount();
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.indexData = ((BufferVK*)pMesh->getIndexBuffer())->getBuffer();
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.indexOffset = 0;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.indexCount = pMesh->getIndexCount();
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.transformData = VK_NULL_HANDLE;
	bottomLevelAccelerationStructure.Geometry.geometry.triangles.transformOffset = 0;
	bottomLevelAccelerationStructure.Geometry.geometry.aabbs = {};
	bottomLevelAccelerationStructure.Geometry.geometry.aabbs.sType = { VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV };
	bottomLevelAccelerationStructure.Geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

	VkAccelerationStructureInfoNV accelerationStructureInfo = {};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
	accelerationStructureInfo.instanceCount = 0;
	accelerationStructureInfo.geometryCount = 1;
	accelerationStructureInfo.pGeometries = &bottomLevelAccelerationStructure.Geometry;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo = {};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	VK_CHECK_RESULT(m_pDevice->vkCreateAccelerationStructureNV(m_pDevice->getDevice(), &accelerationStructureCreateInfo, nullptr, &bottomLevelAccelerationStructure.AccelerationStructure), "--- RayTracingScene: Could not create BLAS!");

	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
	memoryRequirementsInfo.accelerationStructure = bottomLevelAccelerationStructure.AccelerationStructure;

	VkMemoryRequirements2 memoryRequirements2 = {};
	m_pDevice->vkGetAccelerationStructureMemoryRequirementsNV(m_pDevice->getDevice(), &memoryRequirementsInfo, &memoryRequirements2);

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = findMemoryType(m_pDevice->getPhysicalDevice(), memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(m_pDevice->getDevice(), &memoryAllocateInfo, nullptr, &bottomLevelAccelerationStructure.Memory), "--- RayTracingScene: Could not allocate memory for BLAS!");

	VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo = {};
	accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	accelerationStructureMemoryInfo.accelerationStructure = bottomLevelAccelerationStructure.AccelerationStructure;
	accelerationStructureMemoryInfo.memory = bottomLevelAccelerationStructure.Memory;
	VK_CHECK_RESULT(m_pDevice->vkBindAccelerationStructureMemoryNV(m_pDevice->getDevice(), 1, &accelerationStructureMemoryInfo), "--- RayTracingScene: Could not bind memory for BLAS!");

	VK_CHECK_RESULT(m_pDevice->vkGetAccelerationStructureHandleNV(m_pDevice->getDevice(), bottomLevelAccelerationStructure.AccelerationStructure, sizeof(uint64_t), &bottomLevelAccelerationStructure.Handle), "--- RayTracingScene: Could not get handle for BLAS!");

	bottomLevelAccelerationStructure.Index = m_NumBottomLevelAccelerationStructures;
	m_NumBottomLevelAccelerationStructures++;

	bottomLevelAccelerationStructure.MaterialIndex = m_Materials.size();
	m_Materials.push_back(pMaterial);

	std::unordered_map<const Material*, BottomLevelAccelerationStructure> newBLASPerMesh;
	newBLASPerMesh[pMaterial] = bottomLevelAccelerationStructure;
	m_NewBottomLevelAccelerationStructures[pMesh] = newBLASPerMesh;

	return &m_NewBottomLevelAccelerationStructures[pMesh][pMaterial];
}

bool SceneVK::buildBLASs()
{
	cleanGarbage();
	updateScratchBufferForBLAS();

	//Create Memory Barrier
	VkMemoryBarrier memoryBarrier = {};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

	m_pTempCommandBuffer->reset(true);
	m_pTempCommandBuffer->begin(nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	for (auto& bottomLevelAccelerationStructurePerMesh : m_NewBottomLevelAccelerationStructures)
	{
		const MeshVK* pMesh = bottomLevelAccelerationStructurePerMesh.first;

		std::unordered_map<const Material*, BottomLevelAccelerationStructure>* finalizedBLASperMaterial;
		auto finalizedBLASperMaterialIt = m_FinalizedBottomLevelAccelerationStructures.find(pMesh);

		//Check if this map exists in finalized maps
		if (finalizedBLASperMaterialIt == m_FinalizedBottomLevelAccelerationStructures.end())
		{
			m_FinalizedBottomLevelAccelerationStructures[pMesh] = std::unordered_map<const Material*, BottomLevelAccelerationStructure>();
			finalizedBLASperMaterial = &m_FinalizedBottomLevelAccelerationStructures[pMesh];
		}
		else
		{
			finalizedBLASperMaterial = &finalizedBLASperMaterialIt->second;
		}

		for (auto& bottomLevelAccelerationStructure : bottomLevelAccelerationStructurePerMesh.second)
		{
			/*
				Build bottom level acceleration structure
			*/
			VkAccelerationStructureInfoNV buildInfo = {};
			buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
			buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
			buildInfo.geometryCount = 1;
			buildInfo.pGeometries = &bottomLevelAccelerationStructure.second.Geometry;

			//Todo: Make this better?
			m_pDevice->vkCmdBuildAccelerationStructureNV(
				m_pTempCommandBuffer->getCommandBuffer(),
				&buildInfo,
				VK_NULL_HANDLE,
				0,
				VK_FALSE,
				bottomLevelAccelerationStructure.second.AccelerationStructure,
				VK_NULL_HANDLE,
				m_pScratchBuffer->getBuffer(),
				0);

			finalizedBLASperMaterial->insert(std::make_pair(bottomLevelAccelerationStructure.first, bottomLevelAccelerationStructure.second));
			vkCmdPipelineBarrier(m_pTempCommandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);
		}

		bottomLevelAccelerationStructurePerMesh.second.clear();
	}

	m_BottomLevelIsDirty = false;
	m_NewBottomLevelAccelerationStructures.clear();

	m_pTempCommandBuffer->end();
	m_pContext->getDevice()->executeCommandBuffer(m_pContext->getDevice()->getComputeQueue(), m_pTempCommandBuffer, nullptr, nullptr, 0, nullptr, 0);
	m_pContext->getDevice()->wait();

	return true;
}

bool SceneVK::createTLAS()
{
	VkAccelerationStructureInfoNV accelerationStructureInfo = {};
	accelerationStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	accelerationStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
	accelerationStructureInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
	accelerationStructureInfo.instanceCount = m_GeometryInstances.size();
	accelerationStructureInfo.geometryCount = 0;

	VkAccelerationStructureCreateInfoNV accelerationStructureCreateInfo = {};
	accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
	accelerationStructureCreateInfo.info = accelerationStructureInfo;
	VK_CHECK_RESULT_RETURN_FALSE(m_pDevice->vkCreateAccelerationStructureNV(m_pDevice->getDevice(), &accelerationStructureCreateInfo, nullptr, &m_TopLevelAccelerationStructure.AccelerationStructure), "--- RayTracingScene: Could not create TLAS!");

	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
	memoryRequirementsInfo.accelerationStructure = m_TopLevelAccelerationStructure.AccelerationStructure;

	VkMemoryRequirements2 memoryRequirements2 = {};
	m_pDevice->vkGetAccelerationStructureMemoryRequirementsNV(m_pDevice->getDevice(), &memoryRequirementsInfo, &memoryRequirements2);

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.allocationSize = memoryRequirements2.memoryRequirements.size;
	memoryAllocateInfo.memoryTypeIndex = findMemoryType(m_pDevice->getPhysicalDevice(), memoryRequirements2.memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT_RETURN_FALSE(vkAllocateMemory(m_pDevice->getDevice(), &memoryAllocateInfo, nullptr, &m_TopLevelAccelerationStructure.Memory), "--- RayTracingScene: Could not allocate memory for TLAS!");

	VkBindAccelerationStructureMemoryInfoNV accelerationStructureMemoryInfo = {};
	accelerationStructureMemoryInfo.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
	accelerationStructureMemoryInfo.accelerationStructure = m_TopLevelAccelerationStructure.AccelerationStructure;
	accelerationStructureMemoryInfo.memory = m_TopLevelAccelerationStructure.Memory;
	VK_CHECK_RESULT_RETURN_FALSE(m_pDevice->vkBindAccelerationStructureMemoryNV(m_pDevice->getDevice(), 1, &accelerationStructureMemoryInfo), "--- RayTracingScene: Could not allocate bind memory for TLAS!");

	VK_CHECK_RESULT_RETURN_FALSE(m_pDevice->vkGetAccelerationStructureHandleNV(m_pDevice->getDevice(), m_TopLevelAccelerationStructure.AccelerationStructure, sizeof(uint64_t), &m_TopLevelAccelerationStructure.Handle), "--- RayTracingScene: Could not get handle for TLAS!");
	return true;
}

bool SceneVK::buildTLAS()
{
	void* pData;
	m_pInstanceBuffer->map(&pData);
	memcpy(pData, m_GeometryInstances.data(), sizeof(GeometryInstance) * m_GeometryInstances.size());
	m_pInstanceBuffer->unmap();

	VkAccelerationStructureInfoNV buildInfo = {};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
	buildInfo.pGeometries = 0;
	buildInfo.geometryCount = 0;
	buildInfo.instanceCount = m_GeometryInstances.size();

	//Create Memory Barrier
	VkMemoryBarrier memoryBarrier = {};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
	memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

	m_pTempCommandBuffer->reset(true);
	m_pTempCommandBuffer->begin(nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	m_pDevice->vkCmdBuildAccelerationStructureNV(
		m_pTempCommandBuffer->getCommandBuffer(),
		&buildInfo,
		m_pInstanceBuffer->getBuffer(),
		0,
		VK_FALSE,
		m_TopLevelAccelerationStructure.AccelerationStructure,
		VK_NULL_HANDLE,
		m_pScratchBuffer->getBuffer(),
		0);

	vkCmdPipelineBarrier(m_pTempCommandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

	m_pTempCommandBuffer->end();
	m_pContext->getDevice()->executeCommandBuffer(m_pContext->getDevice()->getComputeQueue(), m_pTempCommandBuffer, nullptr, nullptr, 0, nullptr, 0);
	m_pContext->getDevice()->wait();

	return true;
}

bool SceneVK::updateTLAS()
{
	if (m_TopLevelIsDirty)
	{
		//Instance count changed, recreate TLAS
		m_TopLevelIsDirty = false;
		m_OldTopLevelAccelerationStructure = m_TopLevelAccelerationStructure;

		if (!createTLAS())
		{
			LOG("--- SceneVK: Could not create TLAS!");
			return false;
		}

		cleanGarbage();
		updateScratchBufferForTLAS();
		updateInstanceBuffer();

		void* pData;
		m_pInstanceBuffer->map(&pData);
		memcpy(pData, m_GeometryInstances.data(), sizeof(GeometryInstance) * m_GeometryInstances.size());
		m_pInstanceBuffer->unmap();

		VkAccelerationStructureInfoNV buildInfo = {};
		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
		buildInfo.pGeometries = 0;
		buildInfo.geometryCount = 0;
		buildInfo.instanceCount = m_GeometryInstances.size();

		//Create Memory Barrier
		VkMemoryBarrier memoryBarrier = {};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
		memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

		m_pTempCommandBuffer->reset(true);
		m_pTempCommandBuffer->begin(nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		m_pProfiler->reset(0, m_pTempCommandBuffer);
		m_pProfiler->beginFrame(m_pTempCommandBuffer);
		m_pProfiler->beginTimestamp(&m_TimestampBuildAccelStruct);

		m_pDevice->vkCmdBuildAccelerationStructureNV(
			m_pTempCommandBuffer->getCommandBuffer(),
			&buildInfo,
			m_pInstanceBuffer->getBuffer(),
			0,
			VK_FALSE,
			m_TopLevelAccelerationStructure.AccelerationStructure,
			VK_NULL_HANDLE,
			m_pScratchBuffer->getBuffer(),
			0);

		m_pProfiler->endTimestamp(&m_TimestampBuildAccelStruct);
		vkCmdPipelineBarrier(m_pTempCommandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

		m_pProfiler->endFrame();
		m_pTempCommandBuffer->end();
		m_pContext->getDevice()->executeCommandBuffer(m_pContext->getDevice()->getComputeQueue(), m_pTempCommandBuffer, nullptr, nullptr, 0, nullptr, 0);
		m_pContext->getDevice()->wait();
	}
	else
	{
		//Instance count has not changed, update old TLAS

		cleanGarbage();
		updateScratchBufferForTLAS();
		updateInstanceBuffer();

		VkAccelerationStructureInfoNV buildInfo = {};
		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
		buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV;
		buildInfo.pGeometries = 0;
		buildInfo.geometryCount = 0;
		buildInfo.instanceCount = m_GeometryInstances.size();

		//Create Memory Barrier
		VkMemoryBarrier memoryBarrier = {};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;
		memoryBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV;

		m_pTempCommandBuffer->reset(true);
		m_pTempCommandBuffer->begin(nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		m_pProfiler->reset(0, m_pTempCommandBuffer);
		m_pProfiler->beginFrame(m_pTempCommandBuffer);
		m_pProfiler->beginTimestamp(&m_TimestampBuildAccelStruct);

		m_pDevice->vkCmdBuildAccelerationStructureNV(
			m_pTempCommandBuffer->getCommandBuffer(),
			&buildInfo,
			m_pInstanceBuffer->getBuffer(),
			0,
			VK_TRUE,
			m_TopLevelAccelerationStructure.AccelerationStructure,
			m_TopLevelAccelerationStructure.AccelerationStructure,
			m_pScratchBuffer->getBuffer(),
			0);

		m_pProfiler->endTimestamp(&m_TimestampBuildAccelStruct);
		vkCmdPipelineBarrier(m_pTempCommandBuffer->getCommandBuffer(), VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, &memoryBarrier, 0, 0, 0, 0);

		m_pProfiler->endFrame();
		m_pTempCommandBuffer->end();
		m_pContext->getDevice()->executeCommandBuffer(m_pContext->getDevice()->getComputeQueue(), m_pTempCommandBuffer, nullptr, nullptr, 0, nullptr, 0);
		m_pContext->getDevice()->wait();
	}

	return true;
}

void SceneVK::createProfiler()
{
	m_pProfiler = DBG_NEW ProfilerVK("Scene Update", m_pDevice);
	m_pProfiler->initTimestamp(&m_TimestampBuildAccelStruct, "Build top-level acceleration structure");
}

void SceneVK::cleanGarbage()
{
	SAFEDELETE(m_pGarbageScratchBuffer);
	SAFEDELETE(m_pGarbageInstanceBuffer)

	if (m_OldTopLevelAccelerationStructure.Memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(m_pDevice->getDevice(), m_OldTopLevelAccelerationStructure.Memory, nullptr);
		m_OldTopLevelAccelerationStructure.Memory = VK_NULL_HANDLE;
	}

	if (m_OldTopLevelAccelerationStructure.AccelerationStructure != VK_NULL_HANDLE)
	{
		m_pDevice->vkDestroyAccelerationStructureNV(m_pDevice->getDevice(), m_OldTopLevelAccelerationStructure.AccelerationStructure, nullptr);
		m_OldTopLevelAccelerationStructure.AccelerationStructure = VK_NULL_HANDLE;
	}

	m_OldTopLevelAccelerationStructure.Handle = VK_NULL_HANDLE;
}

void SceneVK::updateScratchBufferForBLAS()
{
	VkDeviceSize requiredSize = findMaxMemReqBLAS();

	if (m_pScratchBuffer->getSizeInBytes() < requiredSize)
	{
		m_pGarbageScratchBuffer = m_pScratchBuffer;

		BufferParams scratchBufferParams = {};
		scratchBufferParams.Usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
		scratchBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		scratchBufferParams.SizeInBytes = requiredSize;

		m_pScratchBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
		m_pScratchBuffer->init(scratchBufferParams);
	}
}

void SceneVK::updateScratchBufferForTLAS()
{
	VkDeviceSize requiredSize = findMaxMemReqTLAS();

	if (m_pScratchBuffer->getSizeInBytes() < requiredSize)
	{
		m_pGarbageScratchBuffer = m_pScratchBuffer;

		BufferParams scratchBufferParams = {};
		scratchBufferParams.Usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
		scratchBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		scratchBufferParams.SizeInBytes = requiredSize;

		m_pScratchBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
		m_pScratchBuffer->init(scratchBufferParams);
	}
}

void SceneVK::updateInstanceBuffer()
{
	if (m_pInstanceBuffer->getSizeInBytes() < sizeof(GeometryInstance) * m_GeometryInstances.size())
	{
		m_pGarbageInstanceBuffer = m_pInstanceBuffer;

		BufferParams instanceBufferParmas = {};
		instanceBufferParmas.Usage = VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
		instanceBufferParmas.MemoryProperty = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		instanceBufferParmas.SizeInBytes = sizeof(GeometryInstance) * m_GeometryInstances.size();

		m_pInstanceBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
		m_pInstanceBuffer->init(instanceBufferParmas);
	}

	void* pData;
	m_pInstanceBuffer->map(&pData);
	memcpy(pData, m_GeometryInstances.data(), sizeof(GeometryInstance) * m_GeometryInstances.size());
	m_pInstanceBuffer->unmap();
}

bool SceneVK::createCombinedGraphicsObjectData()
{
	if (m_NewBottomLevelAccelerationStructures.size() > 0)
	{
		LOG("--- SceneVK: Can't create Combined Graphics Object Data if BLASs aren't finalized!");
		return false;
	}

	SAFEDELETE(m_pCombinedVertexBuffer);
	SAFEDELETE(m_pCombinedIndexBuffer);
	SAFEDELETE(m_pMeshIndexBuffer);

	m_pCombinedVertexBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
	m_pCombinedIndexBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());
	m_pMeshIndexBuffer = reinterpret_cast<BufferVK*>(m_pContext->createBuffer());

	BufferParams vertexBufferParams = {};
	vertexBufferParams.Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vertexBufferParams.SizeInBytes = sizeof(Vertex) * m_TotalNumberOfVertices;
	vertexBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	BufferParams indexBufferParams = {};
	indexBufferParams.Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	indexBufferParams.SizeInBytes = sizeof(uint32_t) * m_TotalNumberOfIndices;
	indexBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	BufferParams meshIndexBufferParams = {};
	meshIndexBufferParams.Usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	meshIndexBufferParams.SizeInBytes = sizeof(uint32_t) * 3 * m_NumBottomLevelAccelerationStructures;
	meshIndexBufferParams.MemoryProperty = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	m_pCombinedVertexBuffer->init(vertexBufferParams);
	m_pCombinedIndexBuffer->init(indexBufferParams);
	m_pMeshIndexBuffer->init(meshIndexBufferParams);

	m_pTempCommandBuffer->reset(true);
	m_pTempCommandBuffer->begin(nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	uint32_t vertexBufferOffset = 0;
	uint32_t indexBufferOffset = 0;
	uint64_t meshIndexBufferOffset = 0;
	uint32_t currentCustomInstanceIndexNV = 0;

	void* pMeshIndexBufferMapped;
	m_pMeshIndexBuffer->map(&pMeshIndexBufferMapped);

	for (auto& pMesh : m_AllMeshes)
	{
		uint32_t numVertices = pMesh->getVertexCount();
		uint32_t numIndices = pMesh->getIndexCount();

		m_pTempCommandBuffer->copyBuffer(reinterpret_cast<BufferVK*>(pMesh->getVertexBuffer()), 0, m_pCombinedVertexBuffer, vertexBufferOffset * sizeof(Vertex), numVertices * sizeof(Vertex));
		m_pTempCommandBuffer->copyBuffer(reinterpret_cast<BufferVK*>(pMesh->getIndexBuffer()), 0, m_pCombinedIndexBuffer, indexBufferOffset * sizeof(uint32_t), numIndices * sizeof(uint32_t));

		for (auto& bottomLevelAccelerationStructure : m_FinalizedBottomLevelAccelerationStructures[pMesh])
		{
			for (auto& geometryInstance : m_GeometryInstances)
			{
				if (geometryInstance.AccelerationStructureHandle == bottomLevelAccelerationStructure.second.Handle)
				{
					geometryInstance.InstanceId = currentCustomInstanceIndexNV;
				}
			}

			memcpy((void*)((size_t)pMeshIndexBufferMapped +  meshIndexBufferOffset		* sizeof(uint32_t)), &vertexBufferOffset, sizeof(uint32_t));
			memcpy((void*)((size_t)pMeshIndexBufferMapped + (meshIndexBufferOffset + 1) * sizeof(uint32_t)), &indexBufferOffset, sizeof(uint32_t));
			memcpy((void*)((size_t)pMeshIndexBufferMapped + (meshIndexBufferOffset + 2) * sizeof(uint32_t)), &bottomLevelAccelerationStructure.second.MaterialIndex, sizeof(uint32_t));
			meshIndexBufferOffset += 3;
			currentCustomInstanceIndexNV++;
		}

		vertexBufferOffset += numVertices;
		indexBufferOffset += numIndices;
	}

	uint32_t test[8];
	memcpy(test, pMeshIndexBufferMapped, sizeof(uint32_t) * 6);
	m_pMeshIndexBuffer->unmap();

	m_pTempCommandBuffer->end();
	m_pDevice->executeCommandBuffer(m_pDevice->getComputeQueue(), m_pTempCommandBuffer, nullptr, nullptr, 0, nullptr, 0);
	m_pDevice->wait(); //Todo: Remove?

	return true;
}

VkDeviceSize SceneVK::findMaxMemReqBLAS()
{
	VkDeviceSize maxSize = 0;
	
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
	
	//Todo: Do we need to loop through finalized BLASs here as well?
	for (auto& bottomLevelAccelerationStructurePerMesh : m_NewBottomLevelAccelerationStructures)
	{
		for (auto& bottomLevelAccelerationStructure : bottomLevelAccelerationStructurePerMesh.second)
		{
			VkMemoryRequirements2 memReqBLAS = {};
			memoryRequirementsInfo.accelerationStructure = bottomLevelAccelerationStructure.second.AccelerationStructure;
			m_pDevice->vkGetAccelerationStructureMemoryRequirementsNV(m_pDevice->getDevice(), &memoryRequirementsInfo, &memReqBLAS);

			if (memReqBLAS.memoryRequirements.size > maxSize)
				maxSize = memReqBLAS.memoryRequirements.size;
		}
	}

	return maxSize;
}

VkDeviceSize SceneVK::findMaxMemReqTLAS()
{
	VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
	memoryRequirementsInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
	memoryRequirementsInfo.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV;
	memoryRequirementsInfo.accelerationStructure = m_TopLevelAccelerationStructure.AccelerationStructure;

	VkMemoryRequirements2 memReqTLAS = {};
	m_pDevice->vkGetAccelerationStructureMemoryRequirementsNV(m_pDevice->getDevice(), &memoryRequirementsInfo, &memReqTLAS);

	return memReqTLAS.memoryRequirements.size;
}

void SceneVK::generateLightProbeGeometry(float probeStepX, float probeStepY, float probeStepZ, uint32_t samplesPerProbe, uint32_t numProbesPerDimension)
{
	glm::vec3 worldSize = glm::vec3(probeStepX, probeStepY, probeStepZ) * (float)(numProbesPerDimension - 1);

	m_pLightProbeMesh = new MeshVK(m_pDevice);
	m_pLightProbeMesh->initAsSphere(3);

	for (uint32_t x = 0; x < numProbesPerDimension; x++)
	{
		for (uint32_t y = 0; y < numProbesPerDimension; y++)
		{
			for (uint32_t z = 0; z < numProbesPerDimension; z++)
			{
				float xPosition = (float(x) / float(numProbesPerDimension - 1)) * worldSize.x - worldSize.x / 2.0f;
				float yPosition = (float(y) / float(numProbesPerDimension - 1)) * worldSize.y - worldSize.y / 2.0f;
				float zPosition = (float(z) / float(numProbesPerDimension - 1)) * worldSize.z - worldSize.z / 2.0f;

				glm::mat4 transform(1.0f);
				float diameter = 0.2f;
				glm::vec3 finalPosition = glm::vec3(xPosition, yPosition, zPosition);
				transform = glm::translate(transform, finalPosition);
				transform = glm::scale(transform, glm::vec3(diameter));
				transform = glm::transpose(transform);

				submitGraphicsObject(m_pLightProbeMesh, m_pVeryTempMaterial, transform, 0x40);
			}
		}
	}
}