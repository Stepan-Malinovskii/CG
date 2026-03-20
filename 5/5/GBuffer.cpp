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
    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        bool isDepth = (i == (UINT)GBufferIndex::Depth);

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = _width;
        desc.Height = _height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Format = INFO_FORMATS[i];
        desc.Flags = isDepth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = INFO_FORMATS[i];

        if (isDepth)
        {
            clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
        }
        else
        {
            clearValue.Format = INFO_FORMATS[i];

            if (i == (UINT)GBufferIndex::Albedo)
            {
                clearValue.Color[0] = 0.0f;
                clearValue.Color[1] = 0.0f;
                clearValue.Color[2] = 0.0f;
                clearValue.Color[3] = 1.0f;
            }
            else
            {
                clearValue.Color[0] = 0.5f;
                clearValue.Color[1] = 0.5f;
                clearValue.Color[2] = 1.0f;
                clearValue.Color[3] = 1.0f;
            }
        }

        D3D12_RESOURCE_STATES initialState = isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
        auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            &clearValue,
            IID_PPV_ARGS(&Textures[i].Resource)));

        Textures[i].State = isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
}

void GBuffer::CreateViews(ID3D12Device* device)
{
    const UINT count = (UINT)GBufferIndex::Count;
    const UINT depthIndex = (UINT)GBufferIndex::Depth;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = count - 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&RtvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&DsvHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC srvDescHeap = {};
    srvDescHeap.NumDescriptors = count;
    srvDescHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&srvDescHeap, IID_PPV_ARGS(&SrvHeap)));

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    UINT srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE hRtv(RtvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDsv(DsvHeap->GetCPUDescriptorHandleForHeapStart());
    CD3DX12_CPU_DESCRIPTOR_HANDLE hSrv(SrvHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < count; ++i)
    {
        bool isDepth = (i == depthIndex);
        auto& tex = Textures[i];

        if (isDepth)
        {
            tex.DSV = hDsv;

            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
            dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            dsvDesc.Texture2D.MipSlice = 0;

            device->CreateDepthStencilView(tex.Resource.Get(), &dsvDesc, tex.DSV);
        }
        else
        {
            tex.RTV = hRtv;

            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = INFO_FORMATS[i];
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
            rtvDesc.Texture2D.PlaneSlice = 0;

            device->CreateRenderTargetView(tex.Resource.Get(), &rtvDesc, tex.RTV);

            hRtv.Offset(1, rtvSize);
        }

        tex.SRV = hSrv;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        srvDesc.Format = isDepth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : INFO_FORMATS[i];

        device->CreateShaderResourceView(tex.Resource.Get(), &srvDesc, tex.SRV);

        hSrv.Offset(1, srvSize);
    }
}

void GBuffer::TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        bool isDepth = (i == (UINT)GBufferIndex::Depth);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            Textures[i].Resource.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        bool isDepth = (i == (UINT)GBufferIndex::Depth);

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            Textures[i].Resource.Get(),
            isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::ClearView(ID3D12GraphicsCommandList* cmdList)
{
    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        bool isDepth = (i == (UINT)GBufferIndex::Depth);

        if (isDepth)
        {
            cmdList->ClearDepthStencilView(Textures[i].DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
        else
        {
            cmdList->ClearRenderTargetView(Textures[i].RTV, Colors::Black, 0, nullptr);
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
