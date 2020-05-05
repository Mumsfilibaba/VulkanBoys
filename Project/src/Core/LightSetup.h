#pragma once
#include "PointLight.h"

#include <optional>
#include <vector>

class LightSetup
{
public:
	LightSetup();
	~LightSetup() = default;

	void release();

	void addPointLight(const PointLight& pointlight);

	uint32_t getPointLightCount() const { return uint32_t(m_PointLights.size()); }
	const PointLight* getPointLights() const	{ return m_PointLights.data(); }

private:
	std::vector<PointLight> m_PointLights;
};
