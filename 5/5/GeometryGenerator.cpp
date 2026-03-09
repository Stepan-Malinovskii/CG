#define NOMINMAX

#include "GeometryGenerator.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <iostream>
#include <functional>
#include <algorithm>

GeometryGenerator::MeshInfo GeometryGenerator::LoadOBJ(const std::string& path, std::unordered_map<std::string, MaterialInfo*>& materialMap)
{
    MeshInfo meshData;
    Assimp::Importer importer;

    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->mRootNode) {
        std::cerr << "Failed to load: " << path << " Error: " << importer.GetErrorString() << std::endl;
        return meshData;
    }

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubmeshInfo> submeshes;

    std::function<void(aiNode*, const aiScene*)> processNode;
    processNode = [&](aiNode* node, const aiScene* scene)
        {
            for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
                aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

                SubmeshInfo submesh;
                submesh.Name = mesh->mName.length ? mesh->mName.C_Str() : "submesh_" + std::to_string(submeshes.size());
                submesh.VertexOffset = static_cast<UINT>(vertices.size());
                submesh.IndexOffset = static_cast<UINT>(indices.size());

                for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {
                    Vertex vert;
                    vert.Pos = { mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z };
                    vert.Normal = mesh->HasNormals() ? DirectX::XMFLOAT3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z)
                        : DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
                    vert.TexC = mesh->HasTextureCoords(0) ? DirectX::XMFLOAT2(mesh->mTextureCoords[0][v].x, 1.0f - mesh->mTextureCoords[0][v].y)
                        : DirectX::XMFLOAT2(0.0f, 0.0f);
                    vertices.push_back(vert);
                }

                for (uint32_t f = 0; f < mesh->mNumFaces; ++f) {
                    aiFace face = mesh->mFaces[f];
                    for (uint32_t j = 0; j < face.mNumIndices; ++j) {
                        indices.push_back(face.mIndices[j] + submesh.VertexOffset);
                    }
                }

                submesh.VertexCount = static_cast<UINT>(vertices.size()) - submesh.VertexOffset;
                submesh.IndexCount = static_cast<UINT>(indices.size()) - submesh.IndexOffset;

                if (mesh->mMaterialIndex >= 0 && mesh->mMaterialIndex < scene->mNumMaterials) {
                    aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

                    aiColor3D color;
                    MaterialInfo* myMat = new MaterialInfo();
                    myMat->Name = mesh->mName.length ? mesh->mName.C_Str() : "material_" + std::to_string(mesh->mMaterialIndex);

                    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
                        myMat->DiffuseColor = { color.r, color.g, color.b, 1.0f };

                    aiString texPath;
                    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS)
                    {
                        std::filesystem::path p(texPath.C_Str());
                        std::string texName = p.filename().string();
                        std::transform(texName.begin(), texName.end(), texName.begin(), ::tolower);

                        myMat->DiffuseTextureName = texName;
                    }

                    materialMap[myMat->Name] = myMat;
                    submesh.MaterialName = myMat->Name;
                }

                submeshes.push_back(submesh);
            }

            for (uint32_t i = 0; i < node->mNumChildren; ++i)
                processNode(node->mChildren[i], scene);
        };

    processNode(scene->mRootNode, scene);

    meshData.Vertices = std::move(vertices);
    meshData.Indices32 = std::move(indices);
    meshData.Submeshes = std::move(submeshes);

    return meshData;
}