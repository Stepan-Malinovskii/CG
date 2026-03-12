#define NOMINMAX

#include "D3DFramework.h"

#include <iostream>
#include <fstream> 
#include <d3dcompiler.h>
#include <array>
#include <algorithm>

#include "GeometryGenerator.h"
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

	BuildModelGeometry();

	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();

	//BuildRenderItems();
	BuildFrameResources();

	BuildPSOs();

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
	ThrowIfFailed(_cmdList->Reset(cmdListAlloc.Get(), _psos["opaque"].Get()));

	_cmdList->RSSetViewports(1, &_screenViewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);

	auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &barrier1);

	_cmdList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_cmdList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto backBuffer = CurrentBackBufferView();
	auto depthBuffer = DepthStencilView();
	_cmdList->OMSetRenderTargets(1, &backBuffer, true, &depthBuffer);

	ID3D12DescriptorHeap* descriptorHeaps[] = { _srvDescriptorHeap.Get() };
	_cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	_cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	auto passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	DrawRenderItems(_cmdList.Get(), _opaqueRitems);

	auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->ResourceBarrier(1, &barrier2);

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
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - _lastMousePos.y));

		_theta += dx;
		_phi += dy;

		_phi = MathHelper::Clamp(_phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.05f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - _lastMousePos.y);

		_radius += dx - dy;

		_radius = MathHelper::Clamp(_radius, 5.0f, 150.0f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void D3DFramework::OnKeyboardInput(const GameTimer& gt) {}

void D3DFramework::UpdateCamera(const GameTimer& gt)
{
	_eyePos.x = _radius * sinf(_phi) * cosf(_theta);
	_eyePos.z = _radius * sinf(_phi) * sinf(_theta);
	_eyePos.y = _radius * cosf(_phi);

	XMVECTOR pos = XMVectorSet(_eyePos.x, _eyePos.y, _eyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&_view, view);
}

void D3DFramework::AnimateMaterials(const GameTimer& gt) {}

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
	_mainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	_mainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	_mainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	_mainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	_mainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	_mainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currPassCB = _currFrameResource->PassCB.get();
	currPassCB->CopyData(0, _mainPassCB);
}

void D3DFramework::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr) { OutputDebugStringA((char*)errorBlob->GetBufferPointer()); }
	ThrowIfFailed(hr);

	ThrowIfFailed(_d3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(), 
		IID_PPV_ARGS(_rootSignature.GetAddressOf())));
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
	for (auto& kv : _textures)
		orderList.push_back(kv.second.get());

	std::sort(orderList.begin(), orderList.end(),
		[](Texture* a, Texture* b) { return a->SrvHeapIndex < b->SrvHeapIndex; });

	UINT descriptorIndex = 0;
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

		tex->SrvHeapIndex = descriptorIndex++;

		hDescriptor.Offset(1, _cbvSrvDescriptorSize);
	}
}

void D3DFramework::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] = { "ALPHA_TEST", "1", NULL, NULL };

	_shaders["standardVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/Default.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["opaquePS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/Default.hlsl", nullptr, "PS", "ps_5_1");

	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0,	DXGI_FORMAT_R32G32_FLOAT,	0,	24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,	0 }
	};
}

void D3DFramework::BuildModelGeometry()
{
	GeometryGenerator geoGen;

	std::unordered_map<std::string, GeometryGenerator::MaterialInfo*> materialMap;
	GeometryGenerator::MeshInfo meshData =
		geoGen.LoadOBJ("C:/Users/Stepan/Desktop/CG/5/Models/Model.obj", materialMap);

	//
	// Create MeshGeometry
	//
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "loadedModel";

	UINT vbByteSize = (UINT)meshData.Vertices.size() * sizeof(Vertex);
	UINT ibByteSize = (UINT)meshData.Indices32.size() * sizeof(uint32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(),
		meshData.Vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(),
		meshData.Indices32.data(), ibByteSize);

	geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Vertices.data(),
		vbByteSize,
		geo->VertexBufferUploader);

	geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Indices32.data(),
		ibByteSize,
		geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	//
	// Submeshes
	//
	for (const auto& sub : meshData.Submeshes)
	{
		SubmeshGeometry subGeo;

		subGeo.IndexCount = sub.IndexCount;
		subGeo.StartIndexLocation = sub.IndexOffset;

		// VertexOffset используется как BaseVertexLocation
		subGeo.BaseVertexLocation = sub.VertexOffset;

		geo->DrawArgs[sub.Name] = subGeo;
	}

	_geometries[geo->Name] = std::move(geo);

	//
	// Create textures
	//
	int srvIndex = 0;

	for (auto& kv : materialMap)
	{
		auto* mi = kv.second;

		if (!mi->DiffuseTextureName.empty())
		{
			std::string texName = mi->DiffuseTextureName;

			if (_textures.count(texName) == 0)
			{
				auto tex = std::make_unique<Texture>();

				tex->Name = texName;

				std::wstring texPath =
					L"C:/Users/Stepan/Desktop/CG/5/Textures/" +
					std::wstring(texName.begin(), texName.end());

				tex->Filename = texPath;

				ResourceUploadBatch resourceUpload(_d3dDevice.Get());
				resourceUpload.Begin();

				ThrowIfFailed(DirectX::CreateWICTextureFromFile(
					_d3dDevice.Get(),
					resourceUpload,
					texPath.c_str(),
					tex->Resource.ReleaseAndGetAddressOf(),
					true
				));

				auto uploadResourcesFinished = resourceUpload.End(_cmdQueue.Get());
				uploadResourcesFinished.wait();

				tex->SrvHeapIndex = srvIndex++;

				_textures[texName] = std::move(tex);
			}
		}
	}

	//
	// Create materials
	//
	int matCBIndex = 0;

	for (auto& kv : materialMap)
	{
		std::string matName = kv.first;
		auto* mi = kv.second;

		auto mat = std::make_unique<Material>();

		mat->Name = matName;
		mat->MatCBIndex = matCBIndex++;

		mat->DiffuseAlbedo =
		{
			mi->DiffuseColor.x,
			mi->DiffuseColor.y,
			mi->DiffuseColor.z,
			mi->DiffuseColor.w
		};

		if (_textures.count(mi->DiffuseTextureName))
			mat->DiffuseSrvHeapIndex = _textures[mi->DiffuseTextureName]->SrvHeapIndex;
		else
			mat->DiffuseSrvHeapIndex = -1;

		mat->FresnelR0 = { 0.01f,0.01f,0.01f };
		mat->Roughness = 0.3f;

		_materials[matName] = std::move(mat);
	}

	//
	// RenderItems
	//
	UINT objCBIndex = 0;

	for (const auto& sub : meshData.Submeshes)
	{
		auto ritem = std::make_unique<RenderItem>();

		XMStoreFloat4x4(&ritem->World, XMMatrixIdentity());
		XMStoreFloat4x4(&ritem->TexTransform, XMMatrixIdentity());

		ritem->ObjCBIndex = objCBIndex++;

		ritem->Geo = _geometries["loadedModel"].get();

		auto& draw = ritem->Geo->DrawArgs[sub.Name];

		ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

		ritem->IndexCount = draw.IndexCount;
		ritem->StartIndexLocation = draw.StartIndexLocation;
		ritem->BaseVertexLocation = draw.BaseVertexLocation;

		//
		// assign material
		//
		if (!sub.MaterialName.empty() && _materials.count(sub.MaterialName))
			ritem->Mat = _materials[sub.MaterialName].get();
		else
			ritem->Mat = _materials.begin()->second.get();

		_allRitems.push_back(std::move(ritem));
	}

	//
	// opaque items
	//
	_opaqueRitems.clear();

	for (auto& e : _allRitems)
		_opaqueRitems.push_back(e.get());
}

void D3DFramework::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	opaquePsoDesc.pRootSignature = _rootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["standardVS"]->GetBufferPointer()),
		_shaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders["opaquePS"]->GetBufferPointer()),
		_shaders["opaquePS"]->GetBufferSize()
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

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&_psos["opaque"])));
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
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));

	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = _materials["stone0"].get();
	boxRitem->Geo = _geometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	_allRitems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = _materials["tile0"].get();
	gridRitem->Geo = _geometries[""].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	_allRitems.push_back(std::move(gridRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 2;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = _materials["bricks0"].get();
		leftCylRitem->Geo = _geometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = _materials["bricks0"].get();
		rightCylRitem->Geo = _geometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = _materials["stone0"].get();
		leftSphereRitem->Geo = _geometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = _materials["stone0"].get();
		rightSphereRitem->Geo = _geometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		_allRitems.push_back(std::move(leftCylRitem));
		_allRitems.push_back(std::move(rightCylRitem));
		_allRitems.push_back(std::move(leftSphereRitem));
		_allRitems.push_back(std::move(rightSphereRitem));
	}

	for (auto& e : _allRitems) { _opaqueRitems.push_back(e.get()); }
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

		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

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
