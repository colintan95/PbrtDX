struct RayPayload {
    float4 Color;
};

// Global descriptors.
RaytracingAccelerationStructure g_scene: register(t0);
RWTexture2D<float4> g_film : register(u0);

[shader("raygeneration")]
void RayGenShader()
{
    RayDesc ray;
    ray.Origin = float3(0.f, 0.f, 0.f);
    ray.Direction = float3(0.f, 0.f, 1.f);
    ray.TMin = 0.1f;
    ray.TMax = 1000.f;

    RayPayload payload;
    payload.Color = float4(0.f, 0.f, 0.f, 0.f);

    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    g_film[DispatchRaysIndex().xy] = payload.Color;
}

[shader("miss")]
void MissShader(inout RayPayload payload)
{
    payload.Color = float4(1.f, 0.f, 0.f, 1.f);
}
