#version 460
#extension GL_NV_ray_tracing : require

struct ShadowRayPayload 
{
	float Distance;
};

layout(location = 1) rayPayloadInNV ShadowRayPayload shadowRayPayload;

void main()
{
	shadowRayPayload.Distance = gl_RayTmaxNV;
}