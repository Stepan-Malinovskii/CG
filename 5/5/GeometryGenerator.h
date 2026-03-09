#ifndef GEOMETRY_GENERATOR_HPP
#define GEOMETRY_GENERATOR_HPP

#include <Windows.h>
#include <cstdint>
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <string>

#include "MathHelper.h"
#include "ModelStruct.h"

using namespace DirectX;

class GeometryGenerator
{
public:

	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct SubmeshInfo
	{
		std::string Name;
		std::string MaterialName;
		UINT VertexOffset = 0;
		UINT IndexOffset = 0;
		UINT VertexCount = 0;
		UINT IndexCount = 0;
	};

	struct MeshInfo
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32> Indices32;
		std::vector<SubmeshInfo> Submeshes;

		std::vector<uint16>& GetIndices16()
		{
			if (mIndices16.empty())
			{
				mIndices16.resize(Indices32.size());
				for (size_t i = 0; i < Indices32.size(); ++i)
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
			}
			return mIndices16;
		}

	private:
		std::vector<uint16> mIndices16;
	};

	struct MaterialInfo
	{
		std::string Name;
		std::string DiffuseTextureName;
		XMFLOAT4 DiffuseColor = { 1,1,1,1 };
		XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
	};

	MeshInfo LoadOBJ(const std::string& path, std::unordered_map<std::string, MaterialInfo*>& materialMap);
};

#endif // !GEOMETRY_GENERATOR_HPP
