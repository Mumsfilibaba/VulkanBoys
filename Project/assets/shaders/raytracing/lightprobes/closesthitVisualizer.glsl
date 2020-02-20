#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#define M_PI 3.1415926535897932384626433832795f
//#define M_PHI 1.61803398875f

struct RayPayload
{
	vec3 color;
	uint recursion;
};

struct ShadowRayPayload
{
	float lightIntensity;
};

struct Vertex
{
	vec4 Position;
	vec4 Normal;
	vec4 Tangent;
	vec4 TexCoord;
};

layout(location = 0) rayPayloadInNV RayPayload rayPayload;
layout(location = 1) rayPayloadNV ShadowRayPayload shadowRayPayload;

hitAttributeNV vec3 attribs;

layout(binding = 3, set = 0) buffer Vertices { Vertex v[]; } vertices;
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices;
layout(binding = 5, set = 0) buffer MeshIndices { uint mi[]; } meshIndices;
layout(binding = 6, set = 0) uniform sampler2D albedoMaps[16];
layout(binding = 7, set = 0) uniform sampler2D normalMaps[16];
layout(binding = 8, set = 0) uniform sampler2D roughnessMaps[16];
layout(binding = 9, set = 0) buffer LightProbeValues { vec4 colors[]; } lightProbeValues;

layout (constant_id = 0) const uint WORLD_SIZE_X = 10;
layout (constant_id = 1) const uint WORLD_SIZE_Y = 10;
layout (constant_id = 2) const uint WORLD_SIZE_Z = 10;
layout (constant_id = 3) const uint SAMPLES_PER_PROBE = 64;
layout (constant_id = 4) const uint NUM_PROBES_PER_DIMENSION = 3;

float hash(float n)
{
    return fract(sin(n) * 43758.5453f);
}

float noise(vec3 x)
{
    // The noise function returns a value in the range -1.0f -> 1.0f

    vec3 p = floor(x);
    vec3 f = fract(x);

    f       = f * f * (3.0f - 2.0f * f);
    float n = p.x + p.y * 57.0f + 113.0 * p.z;

    return mix(mix(mix(hash(n + 0.0f), hash(n + 1.0f), f.x),
                   mix(hash(n + 57.0f), hash(n + 58.0f), f.x), f.y),
               mix(mix(hash(n + 113.0f), hash(n + 114.0f), f.x),
                   mix(hash(n + 170.0f), hash(n + 171.0f), f.x), f.y), f.z) * 0.5f + 0.5f;
}

vec3 findNT(vec3 normal)
{
	if (abs(normal.x) > abs(normal.y))
		return normalize(vec3(normal.z, 0.0f, -normal.x));
	
	return normalize(vec3(0.0f, -normal.z, normal.y));
}

vec3 findBestLightProbe(vec3 hitPos)
{
	vec3 worldSize = vec3(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z);
	return round(hitPos * vec3(NUM_PROBES_PER_DIMENSION - 1) / worldSize);
}

float madfrac(float A, float B)
{
	float AtimesB = A * B;
	return AtimesB - floor(AtimesB);
}


void inverseSF(vec3 p, out vec4 closestIndexes, out vec4 weights) 
{
	float M_PHI = (sqrt(5.0f) + 1.0f) / 2.0f;
	float n = float(SAMPLES_PER_PROBE);
	
	float phiMin = atan(p.y, p.x);
	float phi = isnan(phiMin) ? M_PI : min(M_PI, phiMin);
	float cosTheta = p.z;

	float kMax = floor(log(n * M_PI * sqrt(5.0f) * (1.0f - cosTheta * cosTheta)) / log(M_PHI * M_PHI));
	float k = isnan(kMax) ? 2.0f : max(2.0f, kMax);
	float Fk = pow(M_PHI, k) / sqrt(5.0f);
	float F0 = round(Fk);
	float F1 = round(Fk * M_PHI);
	
	mat2 B = mat2(
		2.0f * M_PI * madfrac(F0 + 1.0f, M_PHI - 1.0f) - 2.0f * M_PI * (M_PHI - 1.0f),
		2.0f * M_PI * madfrac(F1 + 1.0f, M_PHI - 1.0f) - 2.0f * M_PI * (M_PHI - 1.0f),
		-2.0f * F0 / n,
		-2.0f * F1 / n);
	mat2 invB = inverse(B);
	vec2 c = floor(vec2(phi, cosTheta - (1.0f - 1.0f / n)) * invB);
	
	float d = 1.0f / 0.0f;
	float j = 0.0f;

	closestIndexes = vec4(d);
	float weightSum = 0.0f;

	for (uint s = 0; s < 4; ++s) 
	{
		cosTheta = dot(B[1], vec2(s % 2, s / 2) + c) + (1.0f - 1.0f / n);
		cosTheta = clamp(cosTheta, -1.0f, 1.0f) * 2.0f - cosTheta;

		float i = floor(n * 0.5f - cosTheta * n * 0.5f);
		phi = 2.0f * M_PI * madfrac(i, M_PHI - 1.0f);
		cosTheta = 1.0f - (2.0f * i + 1.0f) / n;
		float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

		vec3 q = vec3(
			cos(phi) * sinTheta,
			sin(phi) * sinTheta,
			cosTheta);
		
		float distance = length(q - p); 

		closestIndexes[s] = i;
		weights[s] = 1.0f - distance;
		weightSum += weights[s];

		// float squaredDistance = dot(q - p, q - p);
		
		// if (squaredDistance < d) 
		// {
		// 	d = squaredDistance;
		// 	j = i;
		// }
	}

	weights /= weightSum;
}


void main()
{
	uint recursionIndex = rayPayload.recursion;

	uint meshVertexOffset = meshIndices.mi[3 * gl_InstanceCustomIndexNV];
	uint meshIndexOffset = 	meshIndices.mi[3 * gl_InstanceCustomIndexNV + 1];
	uint textureIndex = 	meshIndices.mi[3 * gl_InstanceCustomIndexNV + 2];
	ivec3 index = ivec3(indices.i[meshIndexOffset + 3 * gl_PrimitiveID], indices.i[meshIndexOffset + 3 * gl_PrimitiveID + 1], indices.i[meshIndexOffset + 3 * gl_PrimitiveID + 2]);

	Vertex v0 = vertices.v[meshVertexOffset + index.x];
	Vertex v1 = vertices.v[meshVertexOffset + index.y];
	Vertex v2 = vertices.v[meshVertexOffset + index.z];

	// Interpolate normal
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
	vec2 texCoords = (v0.TexCoord.xy * barycentricCoords.x + v1.TexCoord.xy * barycentricCoords.y + v2.TexCoord.xy * barycentricCoords.z);
	mat4 transform;
	transform[0] = vec4(gl_ObjectToWorldNV[0], 0.0f);
	transform[1] = vec4(gl_ObjectToWorldNV[1], 0.0f);
	transform[2] = vec4(gl_ObjectToWorldNV[2], 0.0f);
	transform[3] = vec4(gl_ObjectToWorldNV[3], 1.0f);

	vec3 T = normalize(v0.Tangent.xyz * barycentricCoords.x + v1.Tangent.xyz * barycentricCoords.y + v2.Tangent.xyz * barycentricCoords.z);
	vec3 N  = normalize(v0.Normal.xyz * barycentricCoords.x + v1.Normal.xyz * barycentricCoords.y + v2.Normal.xyz * barycentricCoords.z);

	T = normalize(vec3(transform * vec4(T, 0.0)));
	N = normalize(vec3(transform * vec4(N, 0.0)));
	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);

	vec3 normal = texture(normalMaps[textureIndex], texCoords).xyz;
	normal = normalize(normal * 2.0f - 1.0f);
	normal = TBN * normal;

	vec4 albedoColor = texture(albedoMaps[textureIndex], texCoords);

	vec3 hitPos = gl_WorldRayOriginNV + normalize(gl_WorldRayDirectionNV) * gl_HitTNV;

	vec3 Nt = findNT(normal);
	vec3 Nb = cross(normal, Nt);

	vec3 worldSize = vec3(WORLD_SIZE_X, WORLD_SIZE_Y, WORLD_SIZE_Z);
	vec3 bestLightProbe = findBestLightProbe(hitPos);
	vec3 bestLightProbeWorldPos = bestLightProbe * worldSize / 2.0f;
	uvec3 bestLightProbe3DIndex = uvec3(bestLightProbe + vec3(NUM_PROBES_PER_DIMENSION - 1) / 2.0f);
	uint baseLightProbeIndex = (bestLightProbe3DIndex.x + NUM_PROBES_PER_DIMENSION * (bestLightProbe3DIndex.y + NUM_PROBES_PER_DIMENSION * bestLightProbe3DIndex.z)) * SAMPLES_PER_PROBE;

	vec3 realNormal = normalize(hitPos - bestLightProbeWorldPos);

	vec4 closestIndexes;
	vec4 weights;
	inverseSF(realNormal, closestIndexes, weights);

	vec3 color0 = weights[0] * lightProbeValues.colors[baseLightProbeIndex + uint(round(closestIndexes[0]))].rgb;
	vec3 color1 = weights[1] * lightProbeValues.colors[baseLightProbeIndex + uint(round(closestIndexes[1]))].rgb;
	vec3 color2 = weights[2] * lightProbeValues.colors[baseLightProbeIndex + uint(round(closestIndexes[2]))].rgb;
	vec3 color3 = weights[3] * lightProbeValues.colors[baseLightProbeIndex + uint(round(closestIndexes[3]))].rgb;

	rayPayload.color = color0 + color1 + color2 + color3; 
	//rayPayload.color = vec3(1.0f, 0.0f, 0.0f);
}
