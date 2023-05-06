#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <vector>

struct Mesh
{
    std::vector<glm::vec3> Positions;
    std::vector<glm::vec2> UVs;

    std::vector<uint32_t> Indices;
};

void LoadMeshFromPlyFile(std::filesystem::path path, Mesh* mesh);
