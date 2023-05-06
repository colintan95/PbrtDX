#include "Mesh.h"

#include <rply.h>

#include <iostream>

static void PlyMessageCallback(p_ply, const char* message)
{
    std::cout << "rply: " << message << std::endl;
}

static int PlyVertexCallback(p_ply_argument argument)
{
    float* buffer = nullptr;

    long index = 0;
    long offset = 0;

    ply_get_argument_user_data(argument, reinterpret_cast<void**>(&buffer), &offset);

    ply_get_argument_element(argument, nullptr, &index);

    static constexpr int stride = 3;

    buffer[index * stride + offset] = static_cast<float>(ply_get_argument_value(argument));

    return 1;
}

struct FaceCallbackContext
{
    int Face[4];
    std::vector<uint32_t> TriIndices;
    std::vector<uint32_t> QuadIndices;
};

static int PlyFaceCallback(p_ply_argument argument)
{
    FaceCallbackContext* context = nullptr;
    long flags = 0;

    ply_get_argument_user_data(argument, reinterpret_cast<void**>(&context), &flags);

    long length = 0;
    long index = 0;

    ply_get_argument_property(argument, nullptr, &length, &index);

    if (length != 3 && length != 4)
        throw std::runtime_error("Only triangles and quads supported.");

    if (index < 0)
        return 1;

    context->Face[index] = static_cast<uint32_t>(ply_get_argument_value(argument));

    if (index == (length - 1))
    {
        if (length == 3)
        {
            context->TriIndices.push_back(context->Face[0]);
            context->TriIndices.push_back(context->Face[1]);
            context->TriIndices.push_back(context->Face[2]);
        }
        else if (length == 4)
        {
            context->QuadIndices.push_back(context->Face[0]);
            context->QuadIndices.push_back(context->Face[1]);
            context->QuadIndices.push_back(context->Face[2]);
            context->QuadIndices.push_back(context->Face[3]);
        }
        else
        {
            throw std::runtime_error("Invalid face length.");
        }
    }

    return 1;
}

void LoadMeshFromPlyFile(std::filesystem::path path, Mesh* mesh)
{
    p_ply ply = ply_open(path.string().c_str(), PlyMessageCallback, 0, nullptr);

    if (!ply)
        throw std::runtime_error("Could not open ply file.");

    if (ply_read_header(ply) == 0)
        throw std::runtime_error("Could not open ply header.");

    p_ply_element element = nullptr;

    size_t vertexCount = 0;
    size_t faceCount = 0;

    while ((element = ply_get_next_element(ply, element)) != nullptr) {
        const char* name = nullptr;
        long numInstances = 0;

        ply_get_element_info(element, &name, &numInstances);

        if (strcmp(name, "vertex") == 0)
            vertexCount = numInstances;
        else if (strcmp(name, "face") == 0)
            faceCount = numInstances;
    }

    if (vertexCount == 0 || faceCount == 0)
        throw std::runtime_error("No face or vertex elements found.");

    mesh->Positions.resize(vertexCount);

    if (ply_set_read_cb(ply, "vertex", "x", PlyVertexCallback, mesh->Positions.data(), 0) == 0 ||
        ply_set_read_cb(ply, "vertex", "y", PlyVertexCallback, mesh->Positions.data(), 1) == 0 ||
        ply_set_read_cb(ply, "vertex", "z", PlyVertexCallback, mesh->Positions.data(), 2) == 0)
    {
        throw std::runtime_error("Could not find vertex data.");
    }

    FaceCallbackContext context{};
    context.TriIndices.reserve(faceCount * 3);
    context.QuadIndices.reserve(faceCount * 4);

    if (ply_set_read_cb(ply, "face", "vertex_indices", PlyFaceCallback, &context, 0) == 0)
        throw std::runtime_error("Could not find vertex indices.");

    std::vector<int> faceIndices;

    if (ply_set_read_cb(ply, "face", "face_indices", nullptr, &faceIndices, 0) != 0)
        throw std::runtime_error("Face indices not supported.");

    if (ply_read(ply) == 0)
        throw std::runtime_error("Could not read ply file.");

    mesh->Indices = std::move(context.TriIndices);

    // Add quad faces as triangles.
    if (!context.QuadIndices.empty())
    {
        mesh->Indices.reserve(mesh->Indices.size() + 3 * context.QuadIndices.size() / 2);

        for (size_t i = 0; i < context.QuadIndices.size(); i += 4)
        {
            mesh->Indices.push_back(context.QuadIndices[i]);
            mesh->Indices.push_back(context.QuadIndices[i + 1]);
            mesh->Indices.push_back(context.QuadIndices[i + 2]);

            mesh->Indices.push_back(context.QuadIndices[i]);
            mesh->Indices.push_back(context.QuadIndices[i + 2]);
            mesh->Indices.push_back(context.QuadIndices[i + 3]);
        }
    }
}
