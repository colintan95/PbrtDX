typedef BuiltInTriangleIntersectionAttributes IntersectAttributes;

RaytracingAccelerationStructure g_scene : register(t0);

RWTexture2D<float4> g_film : register(u0);

struct RayPayload {
    float3 Value;
    int Depth;
};

[shader("raygeneration")]
void TestRayGenShader()
{
    RayDesc ray;
    ray.Origin = float3(0.f, 2.1088f, 13.574f);
    ray.Direction =
        normalize(float3(
            lerp(-0.422f, 0.422f, (float)DispatchRaysIndex().x / (float)DispatchRaysDimensions().x),
            lerp(0.237f, -0.237f, (float)DispatchRaysIndex().y / (float)DispatchRaysDimensions().y),
            -1.f));
    ray.TMin = 0.1f;
    ray.TMax = 1000.f;

    RayPayload payload;
    payload.Value = float3(0.f, 0.f, 0.f);
    payload.Depth = 1;

    TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

    g_film[DispatchRaysIndex().xy].rgb = payload.Value;
}

[shader("closesthit")]
void TestClosestHitShader(inout RayPayload payload, IntersectAttributes attr)
{
    payload.Value += float3(0.5f, 0.f, 0.f);

    if (payload.Depth == 1)
    {
        RayDesc ray;
        ray.Origin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
        ray.Direction = float3(0.f, -1.f, 0.f);
        ray.TMin = 0.1f;
        ray.TMax = 1000.f;

        payload.Depth += 1;

        TraceRay(g_scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);
    }
}

[shader("miss")]
void TestMissShader(inout RayPayload payload)
{
}
