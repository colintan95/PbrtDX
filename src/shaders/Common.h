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

#endif // SHADERS_COMMON_H
