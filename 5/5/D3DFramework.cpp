#include "D3DFramework.h"
#include <iostream>
#include <fstream> 
#include <d3dcompiler.h>
#include <array>
#include "GeometryGenerator.h"
#include "LunaDDSTextureLoader.h"
#include "D3DUtil.h"
#include "ThrowIfFaild.h"

D3DFramework::D3DFramework(HINSTANCE hInstance) : BaseD3DApp(hInstance) {}

D3DFramework::~D3DFramework() { if (_d3dDevice != nullptr) { FlushCommandQueue(); } }

bool D3DFramework::Initialize()
{
	if (!BaseD3DApp::Initialize()) { return false; }

	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));

	_cbvSrvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();

	//BuildModelGeometry();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
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

void D3DFramework::LoadTextures()
{
	std::unique_ptr<Texture> bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex"; 
	bricksTex->Filename = L"C:/Users/Stepan/Desktop/CG/5/Textures/bricks.dds";
	ThrowIfFailed(CreateDDSTextureFromFile12(_d3dDevice.Get(),
		_cmdList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"C:/Users/Stepan/Desktop/CG/5/Textures/stone.dds";
	ThrowIfFailed(CreateDDSTextureFromFile12(_d3dDevice.Get(),
		_cmdList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"C:/Users/Stepan/Desktop/CG/5/Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(_d3dDevice.Get(),
		_cmdList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	_textures[bricksTex->Name] = std::move(bricksTex);
	_textures[stoneTex->Name] = std::move(stoneTex);
	_textures[tileTex->Name] = std::move(tileTex);
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
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_srvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto bricksTex = _textures["bricksTex"]->Resource;
	auto stoneTex = _textures["stoneTex"]->Resource;
	auto tileTex = _textures["tileTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	_d3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, _cbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	_d3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, _cbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	_d3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);
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

void D3DFramework::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	_geometries[geo->Name] = std::move(geo);
}

void D3DFramework::BuildModelGeometry()
{
	//auto modelData = LunaDX::ModelLoader::LoadModel(
	//	"C:/Users/Stepan/Desktop/CG/5/Models/test.obj");

	//if (modelData.Vertices.empty())
	//{
	//	MessageBox(0, L"Failed to load model", 0, 0);
	//	return;
	//}

	//std::vector<Vertex> vertices(modelData.Vertices.size());
	//for (size_t i = 0; i < modelData.Vertices.size(); ++i)
	//{
	//	vertices[i].Pos = modelData.Vertices[i].Position;
	//	vertices[i].Normal = modelData.Vertices[i].Normal;
	//	vertices[i].TexC = modelData.Vertices[i].TexC;
	//}

	//const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	//const UINT ibByteSize = (UINT)modelData.Indices.size() * sizeof(uint32_t);

	//auto geo = std::make_unique<MeshGeometry>();
	//geo->Name = "loadedModel";

	//ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	//CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	//ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	//CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), modelData.Indices.data(), ibByteSize);

	//geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(),
	//	vertices.data(), vbByteSize, geo->VertexBufferUploader);

	//geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(),
	//	modelData.Indices.data(), ibByteSize, geo->IndexBufferUploader);

	//geo->VertexByteStride = sizeof(Vertex);
	//geo->VertexBufferByteSize = vbByteSize;
	//geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	//geo->IndexBufferByteSize = ibByteSize;

	//for (const auto& sub : modelData.Submeshes)
	//{
	//	SubmeshGeometry subGeo;
	//	subGeo.IndexCount = sub.IndexCount;
	//	subGeo.StartIndexLocation = sub.IndexOffset;
	//	subGeo.BaseVertexLocation = sub.VertexOffset;
	//	geo->DrawArgs[sub.Name] = subGeo;
	//}

	//_geometries[geo->Name] = std::move(geo);
	////_modelSubmeshes = modelData.Submeshes;
}

void D3DFramework::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;

	_materials["bricks0"] = std::move(bricks0);
	_materials["stone0"] = std::move(stone0);
	_materials["tile0"] = std::move(tile0);
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
	gridRitem->Geo = _geometries["shapeGeo"].get();
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
