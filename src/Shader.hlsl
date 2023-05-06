struct RayPayload {
    float4 Color;
};

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

// Global descriptors.

RaytracingAccelerationStructure g_scene: register(t0);

RWTexture2D<float4> g_film : register(u0);

SamplerState  g_sampler : register(s0);

[shader("raygeneration")]
void RayGenShader()
{
    float fov = 26.5f / 180.f * 3.142f;

    float maxScreenY = tan(fov / 2.f);
    float maxScreenX = maxScreenY * (1024.f / 576.f);

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
