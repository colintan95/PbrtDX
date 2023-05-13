#include "Common.h"

struct RayPayload {
    float4 Color;
    float2 LightSample0;
    float2 LightSample1;
};

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

// Global descriptors.

RaytracingAccelerationStructure g_scene : register(t0);

RWTexture2D<float4> g_film : register(u0);

SamplerState g_sampler : register(s0);

StructuredBuffer<SphereLight> g_lights : register(t1);

StructuredBuffer<HaltonEntry> g_haltonEntries : register(t2);
ByteAddressBuffer g_haltonPermutations : register(t3);

static const float ONE_MINUS_EPSILON = 0x1.fffffep-1;

float RadicalInverse(int baseIdx, uint64_t a)
{
    uint base = g_haltonEntries[baseIdx].Prime;

    float invBase = 1.f / (float)base;
    float invBaseM = 1.f;

    uint64_t reversedDigits = 0;

    while (a)
    {
        uint64_t next = a / base;
        uint64_t digit = a - next * base;

        reversedDigits = reversedDigits * base + digit;
        invBaseM *= invBase;
        a = next;
    }

    return min((float)(reversedDigits) * invBaseM, ONE_MINUS_EPSILON);
}

float ScrambledRadicalInverse(int baseIdx, uint64_t a)
{
    uint base = g_haltonEntries[baseIdx].Prime;

    float invBase = 1.f / (float)base;
    float invBaseM = 1.f;

    uint64_t reversedDigits = 0;

    while (a)
    {
        uint64_t next = a / base;
        uint64_t digit = a - next * base;

        int permOffset = g_haltonEntries[baseIdx].PermutationOffset;

        reversedDigits = reversedDigits * base +
            g_haltonPermutations.Load<uint16_t>(permOffset + (int)digit);
        invBaseM *= invBase;
        a = next;
    }

    return min((float)(reversedDigits) * invBaseM, ONE_MINUS_EPSILON);
}

uint64_t InverseRadicalInverse(uint64_t inverse, int base, int numDigits)
{
    uint64_t idx = 0;

    for (int i = 0; i < numDigits; ++i)
    {
        uint64_t digit = inverse % base;
        inverse /= base;
        idx = idx * base + digit;
    }

    return idx;
}

[shader("raygeneration")]
void RayGenShader()
{
    float fov = 26.5f / 180.f * 3.142f;

    float maxScreenY = tan(fov / 2.f);
    float maxScreenX = maxScreenY * (1024.f / 576.f);

    // TODO: Calculate these properly.
    const int baseScale0 = 128;
    const int baseScale1 = 243;
    const int baseExp0 = 7;
    const int baseExp1 = 5;
    const int multInv0 = 59;
    const int multInv1 = 131;
    const int sampleStride = baseScale0 * baseScale1;

    const int MAX_HALTON_RESOLUTION = 128;

    int pixelX = DispatchRaysIndex().x;
    int pixelY = DispatchRaysIndex().y;

    uint64_t haltonIdx = 0;

    uint64_t dimOffset = InverseRadicalInverse(pixelX % MAX_HALTON_RESOLUTION, 2, baseExp0);
    haltonIdx += dimOffset * (sampleStride / baseScale0) * multInv0;

    dimOffset = InverseRadicalInverse(pixelY % MAX_HALTON_RESOLUTION, 3, baseExp1);
    haltonIdx += dimOffset * (sampleStride / baseScale1) * multInv1;

    haltonIdx %= sampleStride;

    float2 filmSample = {RadicalInverse(0, haltonIdx >> baseExp0),
                         RadicalInverse(1, haltonIdx / baseScale1)};

    int samplerDim = 2;

    RayPayload payload;
    payload.Color = float4(0.f, 0.f, 0.f, 0.f);

    payload.LightSample0 = float2(ScrambledRadicalInverse(samplerDim, haltonIdx),
                                  ScrambledRadicalInverse(samplerDim + 1, haltonIdx));
    samplerDim += 2;

    payload.LightSample1 = float2(ScrambledRadicalInverse(samplerDim, haltonIdx),
                                  ScrambledRadicalInverse(samplerDim + 1, haltonIdx));
    samplerDim += 2;

    float2 filmPosNormalized = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    float filmPosX = lerp(-maxScreenX, maxScreenX, filmPosNormalized.x);
    float filmPosY = lerp(maxScreenY, -maxScreenY, filmPosNormalized.y);

    RayDesc ray;
    ray.Origin = float3(0.f, 2.1088f, 13.574f);
    ray.Direction = normalize(float3(filmPosX, filmPosY, -1.f));
    ray.TMin = 0.1f;
    ray.TMax = 1000.f;

    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    g_film[DispatchRaysIndex().xy] = payload.Color;
}

// ClosestHitShader descriptors.

ByteAddressBuffer g_indices: register(t0, space1);

StructuredBuffer<float3> g_normals : register(t1, space1);
StructuredBuffer<float2> g_uvs : register(t2, space1);

Texture2D g_texture : register(t3, space1);

static const float PI = 3.14159265358979323846f;

float3 SphericalDirection(float sinTheta, float cosTheta, float phi, float3 x, float3 y, float3 z)
{
    return sinTheta * cos(phi) * x + sinTheta * sin(phi) * y + cosTheta * z;
}

void CoordinateSystem(float3 v1, out float3 v2, out float3 v3)
{
    if (abs(v1.x) > abs(v1.y))
    {
        v2 = float3(-v1.z, 0.f, v1.x) / sqrt(v1.x * v1.x + v1.z * v1.z);
    }
    else
    {
        v2 = float3(0.f, v1.z, -v1.y) / sqrt(v1.y * v1.y + v1.z * v1.z);
    }

    v3 = cross(v1, v2);
}

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr)
{
    // PrimitiveIndex() gives the index of the triangle in the mesh. Each triangle has 3 indices
    // and each index takes up 4 bytes, so each triangle takes up 12 bytes.
    uint indexByteOffset = PrimitiveIndex() * 12;

    uint indices[] = {g_indices.Load(indexByteOffset), g_indices.Load(indexByteOffset + 4),
                      g_indices.Load(indexByteOffset + 8)};

    float2 uv0 = g_uvs[indices[0]];
    float2 uv1 = g_uvs[indices[1]];
    float2 uv2 = g_uvs[indices[2]];

    float2 uv = uv0 + attr.barycentrics.x * (uv1 - uv0) + attr.barycentrics.y * (uv2 - uv0);

    float3 n0 = g_normals[indices[0]];
    float3 n1 = g_normals[indices[1]];
    float3 n2 = g_normals[indices[2]];

    float3 normal = normalize(n0 + attr.barycentrics.x * (n1 - n0) +
                              attr.barycentrics.y * (n2 - n0));

    float3 hitPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 L = float3(0.f, 0.f, 0.f);

    // TODO: Use the right brdf.
    float3 f = float3(1.f, 1.f, 1.f) / PI;

    for (int i = 0; i < 2; ++i)
    {
        SphereLight light = g_lights[i];

        float dc = distance(hitPos, light.Position);

        float3 wc = normalize(light.Position - hitPos);
        float3 wcX, wcY;
        CoordinateSystem(wc, wcX, wcY);

        float sinThetaMax = light.Radius / dc;
        float invSinThetaMax = 1.f / sinThetaMax;

        float cosThetaMax = sqrt(max(0.f, 1 - sinThetaMax * sinThetaMax));

        float cosTheta = (cosThetaMax - 1.f) * payload.LightSample0.x + 1.f;
        float sinThetaSq = 1 - cosTheta * cosTheta;

        float cosAlpha = sinThetaSq * invSinThetaMax +
            cosTheta * sqrt(max(0.f, 1.f - sinThetaSq * invSinThetaMax * invSinThetaMax));
        float sinAlpha = sqrt(max(0.f, 1.f - cosAlpha * cosAlpha));
        float phi = payload.LightSample0.y * 2.f * PI;

        float3 dir = SphericalDirection(sinAlpha, cosAlpha, phi, -wcX, -wcY, -wc);

        float3 lightSamplePos = light.Position + light.Radius * dir;

        float3 wi = normalize(lightSamplePos - hitPos);

        float3 Li = light.L;
        float pdf = 1.f / (2.f * PI * (1.f - cosThetaMax));

        L += f * Li * abs(dot(wi, normal)) / pdf;
    }

    payload.Color = float4(g_texture.SampleLevel(g_sampler, uv, 0).rgb * L, 1.f);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    payload.Color = float4(0.f, 0.f, 0.f, 1.f);
}

struct SphereIntersectAttributes {
    float Unused;
};

[shader("closesthit")]
void LightClosestHitShader(inout RayPayload payload, SphereIntersectAttributes attr)
{
    payload.Color = float4(1.f, 0.f, 0.f, 1.f);
}

[shader("intersection")]
void SphereIntersectShader()
{
    float3 origin = ObjectRayOrigin();
    float3 dir = ObjectRayDirection();

    float radius = 1.f;

    float a = dot(dir, dir);
    float b = 2 * dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;

    float det = b * b - 4.f * a * c;

    if (det < 0.f)
        return;

    float t = (-b - sqrt(det)) / (2.f * a);

    ReportHit(t, 0, (SphereIntersectAttributes)0);
}
