#define NOMINMAX

#include "D3DFramework.h"

#include <iostream>
#include <fstream> 
#include <d3dcompiler.h>
#include <array>
#include <algorithm>

#include "LunaDDSTextureLoader.h"
#include "D3DUtil.h"
#include "ThrowIfFaild.h"
#include "WICTextureLoader.h"
#include <ResourceUploadBatch.h>

D3DFramework::D3DFramework(HINSTANCE hInstance) : BaseD3DApp(hInstance) {}

D3DFramework::~D3DFramework() { if (_d3dDevice != nullptr) { FlushCommandQueue(); } }

bool D3DFramework::Initialize()
{
	if (!BaseD3DApp::Initialize()) { return false; }
	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));
	_cbvSrvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	_gBuffer = std::make_unique<GBuffer>(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT);

	LoadModel("C:/Users/Stepan/Desktop/CG/5/Models/Model.obj");
	CreateSceneObjects();
	BuildRenderItems();
	BuildFrameResources();

	BuildRootSignatureGBuffer();
	BuildRootSignatureLightPass();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();

	BuildGBufferPSO();
	BuildLightPassPSO();

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();
	return true;
}

void D3DFramework::OnResize()
{
	BaseD3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&_proj, P);

	if (_gBuffer.get() != nullptr) { _gBuffer->OnResize(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT); }
}

void D3DFramework::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	_currFrameResourceIndex = (_currFrameResourceIndex + 1) % NUM_FRAME_RECOURCES;
	_currFrameResource = _frameResources[_currFrameResourceIndex].get();

	if (_currFrameResource->Fence != 0 && _fence->GetCompletedValue() < _currFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		ThrowIfFailed(_fence->SetEventOnCompletion(_currFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void D3DFramework::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = _currFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(_cmdList->Reset(cmdListAlloc.Get(), nullptr));

	_cmdList->RSSetViewports(1, &_screenViewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);

	// =========================
// 1. GEOMETRY PASS (GBuffer)
// =========================
	_gBuffer->TransitToOpaqueRenderingState(_cmdList.Get());
	_gBuffer->ClearView(_cmdList.Get());

	// GBuffer рендерит ТОЛЬКО в свои текстуры, бэк-буфер не трогаем!

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvs = 
	{
		_gBuffer->Textures[(UINT)GBufferIndex::Albedo].RTV,
		_gBuffer->Textures[(UINT)GBufferIndex::Normal].RTV
	};

	auto dsv = _gBuffer->Textures[(UINT)GBufferIndex::Depth].DSV;
	_cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), TRUE, &dsv);

	_cmdList->SetPipelineState(_psos["gbuffer"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureGBuffer.Get());

	ID3D12DescriptorHeap* dvs_heaps[] = { _srvDescriptorHeap.Get() };
	_cmdList->SetDescriptorHeaps(1, dvs_heaps);

	auto passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress()); // cbPass -> slot 2

	DrawRenderItems(_cmdList.Get(), _opaqueRitems);

	_gBuffer->TransitToLightRenderingState(_cmdList.Get());

	// =========================
	// 2. LIGHTING PASS
	// =========================

	// ✅ Барьер для бэк-буфера ПЕРЕД использованием
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &barrier);

	auto backBuffer = CurrentBackBufferView();
	_cmdList->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr); // DSV не нужен
	_cmdList->ClearRenderTargetView(backBuffer, Colors::Black, 0, nullptr);

	_cmdList->SetPipelineState(_psos["lightPass"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureLightPass.Get());

	ID3D12DescriptorHeap* heaps[] = { _gBuffer->SRVHeap.Get() };
	_cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

	// ✅ GBuffer текстуры -> slot 0
	_cmdList->SetGraphicsRootDescriptorTable(0, _gBuffer->SRVHeap->GetGPUDescriptorHandleForHeapStart());

	// ✅ cbPass -> b1 -> slot 2 (НЕ slot 1!)
	_cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	// ✅ Если используешь cbMaterial в освещении:
	// _cmdList->SetGraphicsRootConstantBufferView(3, matCB->GetGPUVirtualAddress());

	// Fullscreen triangle
	_cmdList->IASetVertexBuffers(0, 0, nullptr);
	_cmdList->IASetIndexBuffer(nullptr);
	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_cmdList->DrawInstanced(3, 1, 0, 0);

	// =========================
	// 3. PRESENT
	// =========================
	auto barrierPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->ResourceBarrier(1, &barrierPresent);

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(_swapChain->Present(0, 0));
	_currBackBuffer = (_currBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

	_currFrameResource->Fence = ++_currFence;
	_cmdQueue->Signal(_fence.Get(), _currFence);
}

void D3DFramework::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void D3DFramework::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void D3DFramework::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(_rotateSpeed * (x - _lastMousePos.x));
		float dy = XMConvertToRadians(_rotateSpeed * (y - _lastMousePos.y));

		_yaw += dx;
		_pitch -= dy;

		_pitch = MathHelper::Clamp(_pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void D3DFramework::OnKeyboardInput(const GameTimer& gt)
{
	float dt = gt.DeltaTime();
	float speed = _moveSpeed * dt;

	XMVECTOR pos = XMLoadFloat3(&_eyePos);
	XMVECTOR forward = XMLoadFloat3(&_forward);
	XMVECTOR right = XMLoadFloat3(&_right);

	if (GetAsyncKeyState('W') & 0x8000)
		pos += speed * forward;

	if (GetAsyncKeyState('S') & 0x8000)
		pos -= speed * forward;

	if (GetAsyncKeyState('A') & 0x8000)
		pos -= speed * right;

	if (GetAsyncKeyState('D') & 0x8000)
		pos += speed * right;

	XMStoreFloat3(&_eyePos, pos);
}

void D3DFramework::UpdateCamera(const GameTimer& gt)
{
	XMVECTOR forward = XMVectorSet(
		cosf(_pitch) * sinf(_yaw),
		sinf(_pitch),
		cosf(_pitch) * cosf(_yaw),
		0.0f);

	forward = XMVector3Normalize(forward);

	XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
	XMVECTOR up = XMVector3Cross(forward, right);

	XMStoreFloat3(&_forward, forward);
	XMStoreFloat3(&_right, right);
	XMStoreFloat3(&_up, up);

	XMVECTOR pos = XMLoadFloat3(&_eyePos);
	XMMATRIX view = XMMatrixLookToLH(pos, forward, up);

	XMStoreFloat4x4(&_view, view);
}

void D3DFramework::AnimateMaterials(const GameTimer& gt)
{
	float t = gt.TotalTime();

	for (auto& kv : _materials)
	{
		Material* mat = kv.second.get();

		float pulse = 0.5f * sinf(2.0f * t) + 0.5f;
		mat->MatTransform._11 = 0.8f + 0.2f * pulse;
		mat->MatTransform._22 = mat->MatTransform._11;

		mat->NumFramesDirty = NUM_FRAME_RECOURCES;
	}
}

void D3DFramework::UpdateObjectCBs(const GameTimer& gt)
{
	UploadBuffer<ObjectConstants>* currObjectCB = _currFrameResource->ObjectCB.get();

	for (auto& e : _allRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void D3DFramework::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = _currFrameResource->MaterialCB.get();
	for (auto& e : _materials)
	{
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			mat->NumFramesDirty--;
		}
	}
}

void D3DFramework::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX proj = XMLoadFloat4x4(&_proj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&_mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&_mainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&_mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&_mainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&_mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&_mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	_mainPassCB.EyePosW = _eyePos;
	_mainPassCB.RenderTargetSize = XMFLOAT2(CLIENT_WIDTH, CLIENT_HEIGHT);
	_mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / CLIENT_WIDTH, 1.0f / CLIENT_HEIGHT);
	_mainPassCB.NearZ = 1.0f;
	_mainPassCB.FarZ = 1000.0f;
	_mainPassCB.TotalTime = gt.TotalTime();
	_mainPassCB.DeltaTime = gt.DeltaTime();

	_mainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	_mainPassCB.Lights[0].Position = { 0.0f, 1.0f, 0.0f };
	_mainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	_mainPassCB.Lights[0].Strength = { 1.0f, 0.0f, 0.0f };

	auto currPassCB = _currFrameResource->PassCB.get();
	currPassCB->CopyData(0, _mainPassCB);
}

void D3DFramework::BuildRootSignatureGBuffer()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, (UINT)staticSamplers.size(),
		staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureGBuffer)
	));
}

void D3DFramework::BuildRootSignatureLightPass()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		4,
		slotRootParameter,
		(UINT)staticSamplers.size(),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureLightPass)
	));
}

void D3DFramework::BuildDescriptorHeaps()
{
	UINT numTextures = (UINT)_textures.size();
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = numTextures > 0 ? numTextures : 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_srvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<Texture*> orderList;
	for (auto& kv : _textures) { orderList.push_back(kv.second.get()); }

	std::sort(orderList.begin(), orderList.end(), [](Texture* a, Texture* b) { return a->SrvHeapIndex < b->SrvHeapIndex; });

	for (auto& tex : orderList)
	{
		ComPtr<ID3D12Resource> res = tex->Resource;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = (res != nullptr) ? (UINT)res->GetDesc().MipLevels : 1;
		srvDesc.Format = (res != nullptr) ? res->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		_d3dDevice->CreateShaderResourceView(res.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, _cbvSrvDescriptorSize);
	}
}

void D3DFramework::BuildGBufferPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignatureGBuffer.Get();

	psoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(_shaders["gbufferVS"]->GetBufferPointer()),
		_shaders["gbufferVS"]->GetBufferSize() 
	};
	psoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(_shaders["gbufferPS"]->GetBufferPointer()),
		_shaders["gbufferPS"]->GetBufferSize() 
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	psoDesc.NumRenderTargets = 2;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;

	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["gbuffer"])));
}

void D3DFramework::BuildLightPassPSO()
{
	CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
	dsDesc.DepthEnable = TRUE;
	dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = _rootSignatureLightPass.Get();
	psoDesc.BlendState = blendDesc;
	psoDesc.DepthStencilState = dsDesc;

	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["lightVS"]->GetBufferPointer()),
		_shaders["lightVS"]->GetBufferSize()
	};

	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders["lightPS"]->GetBufferPointer()),
		_shaders["lightPS"]->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.NumRenderTargets = 1;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	psoDesc.RTVFormats[0] = _backBufferFormat;
	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["lightPass"])));
}

void D3DFramework::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] = { "ALPHA_TEST", "1", NULL, NULL };

	//_shaders["standardVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/Default.hlsl", nullptr, "VS", "vs_5_1");
	//_shaders["opaquePS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/Default.hlsl", nullptr, "PS", "ps_5_1");

	//_shaders["animVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/Anim.hlsl", nullptr, "VS", "vs_5_1");

	_shaders["gbufferVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBuffer.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["gbufferPS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBuffer.hlsl", nullptr, "PS", "ps_5_1");

	_shaders["lightVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/LightPass.hlsl",nullptr, "VS", "vs_5_1");
	_shaders["lightPS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/LightPass.hlsl", nullptr, "PS", "ps_5_1");

	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0,	DXGI_FORMAT_R32G32_FLOAT,	0,	24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,	0 }
	};
}

void D3DFramework::LoadModel(std::string path)
{
	ModelParse geoGen;
	ModelParse::MeshInfo meshData = geoGen.LoadOBJ(path);

	ParseMesh(meshData);
	LoadTextures(meshData);
	ParseMaterials(meshData);
}

void D3DFramework::CreateSceneObjects()
{
	if (_models.count("LoadedModel") == 0) { return; }

	auto sceneObj = std::make_unique<SceneObject>();
	sceneObj->ModelData = _models["LoadedModel"].get();
	XMStoreFloat4x4(&sceneObj->World, XMMatrixIdentity());

	_sceneObjects.push_back(std::move(sceneObj));
}

void D3DFramework::BuildPSO(std::string psoName, std::string vsName, std::string psName)
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	opaquePsoDesc.pRootSignature = _rootSignatureGBuffer.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders[vsName]->GetBufferPointer()),
		_shaders[vsName]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders[psName]->GetBufferPointer()),
		_shaders[psName]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = _backBufferFormat;
	opaquePsoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&_psos[psoName])));
}

void D3DFramework::BuildFrameResources()
{
	for (int i = 0; i < NUM_FRAME_RECOURCES; ++i)
	{
		_frameResources.push_back(std::make_unique<FrameResource>(_d3dDevice.Get(), 1, 
									(UINT)_allRitems.size(), (UINT)_materials.size()));
	}
}

void D3DFramework::BuildRenderItems()
{
	_allRitems.clear();
	_opaqueRitems.clear();

	UINT objIndex = 0;
	for (auto& objPtr : _sceneObjects)
	{
		SceneObject* obj = objPtr.get();
		Model* model = obj->ModelData;

		if (!model || !model->Mesh) { continue; }

		for (const auto& part : model->Parts)
		{
			auto ri = std::make_unique<RenderItem>();

			auto& sub = model->Mesh->DrawArgs[part.SubmeshName];

			ri->Geo = model->Mesh;
			ri->UsedPso = part.SubmeshName == "mesh_0_2" ? "anim" : "opaque";

			if (!part.MaterialName.empty() && _materials.count(part.MaterialName)) { ri->Mat = _materials[part.MaterialName].get(); }
			else if (!_materials.empty()) { ri->Mat = _materials.begin()->second.get(); }
			else { ri->Mat = nullptr; }

			ri->IndexCount = sub.IndexCount;
			ri->StartIndexLocation = sub.StartIndexLocation;
			ri->BaseVertexLocation = sub.BaseVertexLocation;

			ri->World = obj->World;
			ri->ObjCBIndex = objIndex++;

			_allRitems.push_back(std::move(ri));

			RenderItem* rawPtr = _allRitems.back().get();
			_opaqueRitems.push_back(rawPtr);
		}
	}
}

void D3DFramework::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(ObjectConstants));
	UINT matCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(MaterialConstants));

	auto objectCB = _currFrameResource->ObjectCB->Resource();
	auto matCB = _currFrameResource->MaterialCB->Resource();

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		auto vertexBuffer = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);

		auto indexBuffer = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		cmdList->IASetPrimitiveTopology(ri->Geo->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, _cbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> D3DFramework::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void D3DFramework::ParseMesh(const ModelParse::MeshInfo& meshData)
{
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "LoadedModel";

	UINT vbByteSize = (UINT)meshData.Vertices.size() * sizeof(Vertex);
	UINT ibByteSize = (UINT)meshData.Indices32.size() * sizeof(uint32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), meshData.Vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), meshData.Indices32.data(), ibByteSize);

	geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Vertices.data(),
		vbByteSize,
		geo->VertexBufferUploader
	);

	geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Indices32.data(),
		ibByteSize,
		geo->IndexBufferUploader
	);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	for (const auto& sub : meshData.Submeshes)
	{
		SubmeshGeometry subGeo;
		subGeo.IndexCount = sub.IndexCount;
		subGeo.StartIndexLocation = sub.IndexOffset;
		subGeo.BaseVertexLocation = sub.VertexOffset;

		geo->DrawArgs[sub.Name] = subGeo;
	}

	_geometries[geo->Name] = std::move(geo);

	auto model = std::make_unique<Model>();
	model->Mesh = _geometries["LoadedModel"].get();

	for (const auto& sub : meshData.Submeshes)
	{
		Model::Part p;
		p.SubmeshName = sub.Name;
		p.MaterialName = sub.MaterialName;
		model->Parts.push_back(std::move(p));
	}

	_models["LoadedModel"] = std::move(model);
}

void D3DFramework::LoadTextures(const ModelParse::MeshInfo& meshData)
{
	int srvIndex = 0;
	for (auto& kv : meshData.Materials)
	{
		auto& mi = kv.second;
		if (mi.DiffuseTextureName.empty()) { continue; }

		std::string texName = mi.DiffuseTextureName;
		if (_textures.count(texName) != 0) { continue; }

		auto tex = std::make_unique<Texture>();
		tex->Name = texName;
		tex->Filename = L"C:/Users/Stepan/Desktop/CG/5/Textures/" + std::wstring(texName.begin(), texName.end());

		ResourceUploadBatch resourceUpload(_d3dDevice.Get());
		resourceUpload.Begin();

		ThrowIfFailed(DirectX::CreateWICTextureFromFile
		(
			_d3dDevice.Get(),
			resourceUpload,
			tex->Filename.c_str(),
			tex->Resource.ReleaseAndGetAddressOf(),
			true
		));

		auto uploadResourcesFinished = resourceUpload.End(_cmdQueue.Get());
		uploadResourcesFinished.wait();

		tex->SrvHeapIndex = srvIndex++;

		_textures[texName] = std::move(tex);
	}
}

void D3DFramework::ParseMaterials(const ModelParse::MeshInfo& meshData)
{
	int matCBIndex = 0;
	for (auto& kv : meshData.Materials)
	{
		std::string matName = kv.first;
		auto& mi = kv.second;

		if (_materials.count(matName) != 0) { continue; }

		auto mat = std::make_unique<Material>();
		mat->Name = matName;
		mat->MatCBIndex = matCBIndex++;

		mat->DiffuseAlbedo = XMFLOAT4(mi.DiffuseColor.x, mi.DiffuseColor.y, mi.DiffuseColor.z, mi.DiffuseColor.w);

		if (_textures.count(mi.DiffuseTextureName)) { mat->DiffuseSrvHeapIndex = _textures[mi.DiffuseTextureName]->SrvHeapIndex; }
		else { mat->DiffuseSrvHeapIndex = -1; }

		mat->FresnelR0 = { 0.01f, 0.01f, 0.01f };
		mat->Roughness = 0.3f;

		_materials[matName] = std::move(mat);
	}
}