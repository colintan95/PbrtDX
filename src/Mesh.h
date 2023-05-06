#pragma once

#include <filesystem>
#include <vector>

struct Point3
{
    float x = 0;
    float y = 0;
    float z = 0;
};

struct Mesh
{
    std::vector<Point3> Positions;

    std::vector<uint32_t> Indices;
};

void LoadMeshFromPlyFile(std::filesystem::path path, Mesh* mesh);
