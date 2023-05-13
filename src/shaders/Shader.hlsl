#include "Common.h"

struct RayPayload {
    float3 L;
    uint Depth;

    float2 Samples[5];
};

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

struct VisibilityPayload {
    float T;
};

// Global descriptors.

struct DrawConstants
{
    uint32_t SampleIndex;
};

RaytracingAccelerationStructure g_scene : register(t0);

RWTexture2D<float4> g_film : register(u0);

ConstantBuffer<DrawConstants> g_drawConstants : register(b0);

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

    uint2 pixel = DispatchRaysIndex().xy;
    uint sampleIdx = g_drawConstants.SampleIndex;

    uint64_t haltonIdx = 0;

    uint64_t dimOffset = InverseRadicalInverse(pixel.x % MAX_HALTON_RESOLUTION, 2, baseExp0);
    haltonIdx += dimOffset * (sampleStride / baseScale0) * multInv0;

    dimOffset = InverseRadicalInverse(pixel.y % MAX_HALTON_RESOLUTION, 3, baseExp1);
    haltonIdx += dimOffset * (sampleStride / baseScale1) * multInv1;

    haltonIdx %= sampleStride;

    haltonIdx += sampleIdx * sampleStride;

    float2 filmOffset = float2(RadicalInverse(0, haltonIdx >> baseExp0),
                               RadicalInverse(1, haltonIdx / baseScale1));

    int samplerDim = 2;

    RayPayload payload;
    payload.L = float3(0.f, 0.f, 0.f);
    payload.Depth = 1;

    for (int i = 0; i < 5; ++i)
    {
        payload.Samples[i] = float2(ScrambledRadicalInverse(samplerDim, haltonIdx),
                                    ScrambledRadicalInverse(samplerDim + 1, haltonIdx));
        samplerDim += 2;
    }

    float2 filmPos = (float2)pixel + filmOffset;

    float3 rayDir =
        normalize(float3(
            lerp(-maxScreenX, maxScreenX, filmPos.x / (float)DispatchRaysDimensions().x),
            lerp(maxScreenY, -maxScreenY, filmPos.y / (float)DispatchRaysDimensions().y),
            -1.f));

    float3 L = float3(0.f, 0.f, 0.f);

    RayDesc ray;
    ray.Origin = float3(0.f, 2.1088f, 13.574f);
    ray.Direction = rayDir;
    ray.TMin = 0.1f;
    ray.TMax = 1000.f;

    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    L += payload.L;

    float3 prevFilmVal = g_film[pixel].rgb;

    float N = (float)(sampleIdx + 1);

    g_film[pixel].rgb = ((N - 1.f) / N) * prevFilmVal + (1.f / N) * L;
    g_film[pixel].a = 1.f;
}

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

float2 ConcentricSampleDisk(float2 u)
{
    float2 offset = 2.f * u - float2(1.f, 1.f);

    if (offset.x == 0 && offset.y == 0)
        return float2(0.f, 0.f);

    float theta = 0.f;
    float r = 0.f;

    if (abs(offset.x) > abs(offset.y))
    {
        r = offset.x;
        theta = PI / 4.f * (offset.y / offset.x);
    }
    else
    {
        r = offset.y;
        theta = PI / 2.f * PI / 4.f * (offset.x / offset.y);
    }

    return r * float2(cos(theta), sin(theta));
}

float3 CosineSampleHemisphere(float2 u)
{
    float2 d = ConcentricSampleDisk(u);
    float z = sqrt(max(0.f, 1.f - d.x * d.x - d.y * d.y));

    return float3(d.x, d.y, z);
}

void Lambertian_Sample_f(float3 wo, float2 u, float3 n, out float3 wi, out float pdf)
{
    float3 nx, ny;
    CoordinateSystem(n, nx, ny);

    wo = float3(dot(wo, nx), dot(wo, ny), dot(wo, n));

    wi = CosineSampleHemisphere(u);
    if (wo.z > 0.f)
    {
        wi.z *= -1.f;
    }

    pdf = (wo.z * wi.z > 0) ? abs(wi.z) / PI : 0.f;

    wi = wi.x * nx + wi.y * ny + wi.z * n;
}

// Hit group descriptors.

ConstantBuffer<HitGroupShaderConstants> g_hitGroupConstants : register(b0, space1);

ByteAddressBuffer g_indices: register(t0, space1);

StructuredBuffer<float3> g_normals : register(t1, space1);
StructuredBuffer<float2> g_uvs : register(t2, space1);

StructuredBuffer<HitGroupGeometryConstants> g_hitGroupGeomConstants : register(t3, space1);

Texture2D g_texture : register(t4, space1);

struct DiffuseSphereLight
{
    SphereLight m_data;

    float3 Sample_Li(float3 p, float2 u, out float3 wi, out float pdf, out bool visible)
    {
        float dc = distance(p, m_data.Position);

        float3 wc = normalize(m_data.Position - p);
        float3 wcX, wcY;
        CoordinateSystem(wc, wcX, wcY);

        float sinThetaMax = m_data.Radius / dc;
        float invSinThetaMax = 1.f / sinThetaMax;

        float cosThetaMax = sqrt(max(0.f, 1 - sinThetaMax * sinThetaMax));

        float cosTheta = (cosThetaMax - 1.f) * u.x + 1.f;
        float sinThetaSq = 1 - cosTheta * cosTheta;

        float cosAlpha = sinThetaSq * invSinThetaMax +
            cosTheta * sqrt(max(0.f, 1.f - sinThetaSq * invSinThetaMax * invSinThetaMax));
        float sinAlpha = sqrt(max(0.f, 1.f - cosAlpha * cosAlpha));
        float phi = u.y * 2.f * PI;

        float3 dir = SphericalDirection(sinAlpha, cosAlpha, phi, -wcX, -wcY, -wc);

        float3 lightSamplePos = m_data.Position + m_data.Radius * dir;

        wi = normalize(lightSamplePos - p);
        pdf = 1.f / (2.f * PI * (1.f - cosThetaMax));

        RayDesc ray;
        ray.Origin = p;
        ray.Direction = wi;
        ray.TMin = 0.1f;
        ray.TMax = 10000.f;

        float lightDist = distance(p, lightSamplePos);

        VisibilityPayload payload;
        payload.T = 1000000.f;

        TraceRay(g_scene, RAY_FLAG_NONE, ~0, g_hitGroupConstants.VisibilityHitGroupBaseIndex, 1,
                 1, ray, payload);

        visible = (payload.T >= lightDist);

        return m_data.L;
    }
};

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

    float3 position = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();

    float3 L = float3(0.f, 0.f, 0.f);

    // TODO: Use the right brdf.
    float3 f = float3(1.f, 1.f, 1.f) / PI;

    for (int i = 0; i < 2; ++i)
    {
        DiffuseSphereLight light;
        light.m_data = g_lights[i];

        float3 wi = float3(0.f, 0.f, 0.f);
        float pdf = 0.f;
        bool visible = false;

        float3 Li = light.Sample_Li(position, payload.Samples[i], wi, pdf, visible);

        if (visible)
        {
            L += f * Li * abs(dot(wi, normal)) / pdf;
        }
    }

    if (payload.Depth < 2)
    {
        RayPayload reflectPayload;
        reflectPayload.L = float3(0.f, 0.f, 0.f);
        reflectPayload.Depth = payload.Depth + 1;
        reflectPayload.Samples[0] = payload.Samples[3];
        reflectPayload.Samples[1] = payload.Samples[4];

        float3 wo = normalize(-WorldRayDirection());

        float3 wi = float3(0.f, 0.f, 0.f);
        float pdf = 0.f;
        Lambertian_Sample_f(wo, payload.Samples[2], normal, wi, pdf);

        RayDesc reflectRay;
        reflectRay.Origin = position;
        reflectRay.Direction = wi;
        reflectRay.TMin = 0.1f;
        reflectRay.TMax = 1000.f;

        TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, reflectRay,
                 reflectPayload);

        L += reflectPayload.L;
    }

    if (g_hitGroupGeomConstants[GeometryIndex()].IsTextured)
    {
        L *= g_texture.SampleLevel(g_sampler, uv, 0).rgb;
    }

    payload.L = L;
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
}

[shader("closesthit")]
void VisibilityClosestHitShader(inout VisibilityPayload payload, IntersectAttributes attr)
{
    payload.T = RayTCurrent();
}

[shader("miss")]
void VisibilityMissShader(inout VisibilityPayload payload)
{
}

struct SphereIntersectAttributes {
    float Unused;
};

[shader("closesthit")]
void LightClosestHitShader(inout RayPayload payload, SphereIntersectAttributes attr)
{
    payload.L = float3(0.f, 0.f, 0.f);
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
