#ifndef SHADERS_COMMON_H
#define SHADERS_COMMON_H

#ifndef HLSL
#include "CppTypes.h"
#endif // #ifndef HLSL

struct SphereLight
{
    float3 Position;
    float Radius;
    float3 L;
};

struct HaltonEntry
{
    uint32_t PermutationOffset;
    uint16_t Prime;
};

struct HitGroupShaderConstants
{
    uint32_t VisibilityHitGroupBaseIndex;
};

struct HitGroupGeometryConstants
{
    uint32_t IsTextured;
};

#endif // SHADERS_COMMON_H
