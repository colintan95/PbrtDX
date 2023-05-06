struct RayPayload {
    float4 Color;
};

typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

// Global descriptors.
RaytracingAccelerationStructure g_scene: register(t0);
RWTexture2D<float4> g_film : register(u0);

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

[shader("closesthit")]
void ClosestHitShader(inout RayPayload payload, IntersectAttributes attr)
{
    payload.Color = float4(1.f, 0.f, 0.f, 1.f);
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    payload.Color = float4(0.f, 0.f, 0.f, 1.f);
}
