#include "D3DFramework.h"
#include <d3dcompiler.h>

bool D3DFramework::Initialize()
{
	if (!BaseD3DApp::Initialize())
	{
		return false;
	}

	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));

	CreateCBVDescriptorHeap();
	CreateConstantBufferView();
	CreateRootSignature();
	BuildShadersAndInputLayout();
	InitializeGeometry();
	CreatePSO();

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	FlushCommandQueue();
	return true;
}

void D3DFramework::BuildShadersAndInputLayout()
{
	DWORD fileAttr = GetFileAttributes(L"C:/Users/Stepan/Desktop/CG/4/Shaders/shaders.hlsl");

	if (fileAttr == INVALID_FILE_ATTRIBUTES)
	{
		std::wcout << L"ERROR: Shader file not found!" << std::endl;
		MessageBox(NULL, L"Shader file not found!", L"Error", MB_OK);
		return;
	}

	_vsByteCode = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/4/Shaders/shaders.hlsl", nullptr, "VS", "vs_5_0");
	std::cout << "Vertex shader are compiled" << std::endl;

	_psByteCode = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/4/Shaders/shaders.hlsl", nullptr, "PS", "ps_5_0");
	std::cout << "Pixel shader are compiled" << std::endl;

	_inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	std::cout << "Layout is builded" << std::endl;
}

void D3DFramework::InitializeGeometry()
{
	Vertex vertices[] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White) },
		{ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black) },
		{ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red) },
		{ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green) },
		{ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue) },
		{ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow) },
		{ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan) },
		{ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta) }
	};

	std::uint16_t indices[] = {
		0, 1, 2,
		0, 2, 3,

		4, 6, 5,
		4, 7, 6,

		4, 5, 1,
		4, 1, 0,

		3, 2, 6,
		3, 6, 7,

		1, 5, 6,
		1, 6, 2,

		4, 0, 3,
		4, 3, 7
	};

	UINT vbByteSize = 8 * sizeof(Vertex);
	UINT ibByteSize = 36 * sizeof(std::uint16_t);

	_boxGeo = std::make_unique<MeshGeometry>();
	_boxGeo->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &_boxGeo->VertexBufferCPU));
	CopyMemory(_boxGeo->VertexBufferCPU->GetBufferPointer(), vertices, vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &_boxGeo->IndexBufferCPU));
	CopyMemory(_boxGeo->IndexBufferCPU->GetBufferPointer(), indices, ibByteSize);

	_boxGeo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(), vertices, vbByteSize, _boxGeo->VertexBufferUploader);
	_boxGeo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(_d3dDevice.Get(), _cmdList.Get(), indices, ibByteSize, _boxGeo->IndexBufferUploader);

	_boxGeo->VertexByteStride = sizeof(Vertex);
	_boxGeo->VertexBufferByteSize = vbByteSize;
	_boxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
	_boxGeo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = 36;
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	_boxGeo->DrawArgs["box"] = submesh;
}

void D3DFramework::CreateCBVDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;

	ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&_cbvHeap)));

	std::cout << "CBV heap is created" << std::endl;
}

void D3DFramework::CreateConstantBufferView()
{
	_cbUploadBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(_d3dDevice.Get(), 1, true);

	UINT objCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(ObjectConstants));
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = _cbUploadBuffer->Resource()->GetGPUVirtualAddress();
	int boxCBufIndex = 0;
	cbAddress += boxCBufIndex * objCBByteSize;
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = D3DUtil::CalcConstantBufferSize(sizeof(ObjectConstants));

	_d3dDevice->CreateConstantBufferView(&cbvDesc, _cbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void D3DFramework::CreateRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	ThrowIfFailed(hr);
	ThrowIfFailed(_d3dDevice->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)));

	std::cout << "Root Signature is created" << std::endl;
}

void D3DFramework::CreatePSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignature.Get();
	psoDesc.VS = { reinterpret_cast<BYTE*>(_vsByteCode->GetBufferPointer()), _vsByteCode->GetBufferSize() };
	psoDesc.PS = { reinterpret_cast<BYTE*>(_psByteCode->GetBufferPointer()), _psByteCode->GetBufferSize() };
	CD3DX12_RASTERIZER_DESC rastDesc(D3D12_DEFAULT);
	rastDesc.FrontCounterClockwise = true;
	psoDesc.RasterizerState = rastDesc;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = _backBufferFormat;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? _4xMsaaQuality - 1 : 0;
	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
	std::cout << "PSO is created" << std::endl;
}

void D3DFramework::Update(const GameTimer& gt)
{
	float x = _radius * sinf(_phi) * cosf(_theta) + gt.DeltaTime();
	float z = _radius * sinf(_phi) * sinf(_theta);
	float y = _radius * cosf(_phi);

	Vector3 pos(x, y, z);
	Vector3 target(0.0f, 0.0f, 0.0f);
	Vector3 up(0.0f, 1.0f, 0.0f);
	_view = Matrix::CreateLookAt(pos, target, up);

	Matrix worldViewProj = _world * _view * _proj;
	worldViewProj = worldViewProj.Transpose();

	ObjectConstants objConstants;
	objConstants.WorldViewProj = worldViewProj;
	_cbUploadBuffer->CopyData(0, objConstants);
}

void D3DFramework::Draw(const GameTimer& gt)
{
	ThrowIfFailed(_directCmdListAlloc->Reset());
	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), _pso.Get()));

	_cmdList->RSSetViewports(1, &_screenViewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);

	CD3DX12_RESOURCE_BARRIER barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &barrier1);

	_cmdList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	_cmdList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE bb = CurrentBackBufferView();
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencilView();
	_cmdList->OMSetRenderTargets(1, &bb, true, &dsv);

	ID3D12DescriptorHeap* descriptorHeaps[] = { _cbvHeap.Get() };
	_cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	_cmdList->SetGraphicsRootSignature(_rootSignature.Get());

	D3D12_VERTEX_BUFFER_VIEW vb = _boxGeo->VertexBufferView();
	_cmdList->IASetVertexBuffers(0, 1, &vb);

	D3D12_INDEX_BUFFER_VIEW ib = _boxGeo->IndexBufferView();
	_cmdList->IASetIndexBuffer(&ib);

	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_cmdList->SetGraphicsRootDescriptorTable(0, _cbvHeap->GetGPUDescriptorHandleForHeapStart());
	_cmdList->DrawIndexedInstanced(_boxGeo->DrawArgs["box"].IndexCount, 1, 0, 0, 0);

	CD3DX12_RESOURCE_BARRIER barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->ResourceBarrier(1, &barrier2);

	ThrowIfFailed(_cmdList->Close());

	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	ThrowIfFailed(_swapChain->Present(0, 0));
	_currBackBuffer = (_currBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

	FlushCommandQueue();
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
	if ((btnState & MK_LBUTTON) != 0) {
		float dx = XMConvertToRadians(0.25 * static_cast<float>(x - _lastMousePos.x));
		float dy = XMConvertToRadians(0.25 * static_cast<float>(y - _lastMousePos.y));

		_theta += dx;
		_phi += dy;

		_phi = _phi < 0.1f ? 0.1f : (_phi > XM_PI ? XM_PI : _phi);

	}
	else if ((btnState & MK_RBUTTON) != 0) {
		float dx = 0.005f * static_cast<float>(x - _lastMousePos.x);
		float dy = 0.005f * static_cast<float>(x - _lastMousePos.y);

		_radius += dx - dy;

		_radius = _radius < 3.0f ? 3.0f : (_radius > 15.0f ? 15.0f : _radius);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}
