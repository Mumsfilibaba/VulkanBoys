#include "LightSetup.h"

LightSetup::LightSetup()
	: m_PointLights()
{
}

void LightSetup::release()
{
	m_PointLights.clear();
}

void LightSetup::addPointLight(const PointLight& pointlight)
{
	m_PointLights.emplace_back(pointlight);
}
