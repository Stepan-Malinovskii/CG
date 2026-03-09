#ifndef FRAME_RESOURCE_HPP
#define FRAME_RESOURCE_HPP

#include <memory>

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>

#include "UploadBuffer.h"
#include "MathHelper.h"
#include "D3DUtil.h"

using namespace Microsoft::WRL;

struct ObjectConstants
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct PassConstants
{
    XMFLOAT4X4 View = MathHelper::Identity4x4();
    XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    Light Lights[MaxLights];
};

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC;
};

struct FrameResource
{
public:

    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() {}

    ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    UINT64 Fence = 0;
};

#endif // !FRAME_RESOURCE_HPP

