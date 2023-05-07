#include "Common.h"

struct RayPayload {
    float4 Color;
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

    float2 lightSample0 = {ScrambledRadicalInverse(samplerDim, haltonIdx),
                           ScrambledRadicalInverse(samplerDim + 1, haltonIdx)};
    samplerDim += 2;

    float2 lightSample1 = {ScrambledRadicalInverse(samplerDim, haltonIdx),
                           ScrambledRadicalInverse(samplerDim + 1, haltonIdx)};
    samplerDim += 2;

    float2 filmPosNormalized = (float2)DispatchRaysIndex() / (float2)DispatchRaysDimensions();

    float filmPosX = lerp(-maxScreenX, maxScreenX, filmPosNormalized.x);
    float filmPosY = lerp(maxScreenY, -maxScreenY, filmPosNormalized.y);

    RayDesc ray;
    ray.Origin = float3(0.f, 2.1088f, 13.574f);
    ray.Direction = normalize(float3(filmPosX, filmPosY, -1.f));
    ray.TMin = 0.1f;
    ray.TMax = 1000.f;

    RayPayload payload;
    payload.Color = float4(0.f, 0.f, 0.f, 0.f);

    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    g_film[DispatchRaysIndex().xy] = payload.Color;
}

// ClosestHitShader descriptors.

ByteAddressBuffer g_indexBuffer : register(t0, space1);
StructuredBuffer<float2> g_uvBuffer : register(t1, space1);

Texture2D g_texture : register(t2, space1);

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr)
{
    // PrimitiveIndex() gives the index of the triangle in the mesh. Each triangle has 3 indices
    // and each index takes up 4 bytes, so each triangle takes up 12 bytes.
    uint indexByteOffset = PrimitiveIndex() * 12;

    float2 uv0 = g_uvBuffer[g_indexBuffer.Load(indexByteOffset)];
    float2 uv1 = g_uvBuffer[g_indexBuffer.Load(indexByteOffset + 4)];
    float2 uv2 = g_uvBuffer[g_indexBuffer.Load(indexByteOffset + 8)];

    float2 uv = uv0 + attr.barycentrics.x * (uv1 - uv0) + attr.barycentrics.y * (uv2 - uv0);

    payload.Color = float4(g_texture.SampleLevel(g_sampler, uv, 0).rgb, 1.f);
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
