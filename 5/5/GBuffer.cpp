#include "GBuffer.h"
#include "ThrowIfFaild.h"
#include <d3dx12.h>

GBuffer::GBuffer(ID3D12Device* device, int width, int height)
{
    _width = width;
    _height = height;

    Textures.resize((UINT)GBufferIndex::Count);

    CreateResources(device);
    CreateViews(device);
}

void GBuffer::CreateResources(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,
        _width, _height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

    D3D12_CLEAR_VALUE clearValue = {};

    clearValue.Color[0] = 0.0f; clearValue.Color[1] = 0.0f; clearValue.Color[2] = 0.0f; clearValue.Color[3] = 1.0f;

    auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        desc.Format = INFO_FORMATS[i];

        clearValue.Format = INFO_FORMATS[i];

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&Textures[i].Resource)));

        Textures[i].State = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
}

void GBuffer::CreateViews(ID3D12Device* device)
{
    const UINT count = (UINT)GBufferIndex::Count;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = count;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&RtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvDescHeap = {};
    srvDescHeap.NumDescriptors = count;
    srvDescHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDescHeap, IID_PPV_ARGS(&SrvHeap)));

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE hRtv(RtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE hSrv(SrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    for (UINT i = 0; i < count; ++i)
    {
        auto& tex = Textures[i];

        tex.RTV = hRtv;
        rtvDesc.Format = INFO_FORMATS[i];

        device->CreateRenderTargetView(tex.Resource.Get(), &rtvDesc, tex.RTV);
        hRtv.Offset(1, rtvSize);

        tex.SRV = hSrv;
        srvDesc.Format = INFO_FORMATS[i];

        device->CreateShaderResourceView(tex.Resource.Get(), &srvDesc, tex.SRV);
        hSrv.Offset(1, srvSize);
    }
}

void GBuffer::TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            Textures[i].Resource.Get(),
            D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            Textures[i].Resource.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::ClearView(ID3D12GraphicsCommandList* cmdList)
{
    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        if (i == (UINT)GBufferIndex::Depth)
        {
            float clearDepth[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            cmdList->ClearRenderTargetView(Textures[i].RTV, clearDepth, 0, nullptr);
        }
        else
        {
            float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
            cmdList->ClearRenderTargetView(Textures[i].RTV, clearColor, 0, nullptr);
        }
    }
}

void GBuffer::OnResize(ID3D12Device* device, int width, int height)
{
    if (_width == width && _height == height) { return; }

    _width = width;
    _height = height;

    for (auto& t : Textures) { t.Resource = nullptr; }

    CreateResources(device);
    CreateViews(device);
}
