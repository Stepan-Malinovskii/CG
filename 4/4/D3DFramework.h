#ifndef D3D_FRAMEWORK_HPP
#define D3D_FRAMEWORK_HPP

#include "BaseD3DApp.h"
#include "UploadBuffer.h"
#include "MyGeometry.h"

#include <d3dcompiler.h>

#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#pragma pop_macro("min")
#pragma pop_macro("max")

class D3DFramework : public BaseD3DApp
{
public:
	D3DFramework(HINSTANCE hInstance) : BaseD3DApp(hInstance) {}
	virtual bool Initialize() override;

protected:
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;
	virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
private:
	void CreateCBVDescriptorHeap();
	void CreateConstantBufferView();
	void CreateRootSignature();

	void BuildShadersAndInputLayout();
	void InitializeGeometry();

	void CreatePSO();

	ComPtr<ID3D12DescriptorHeap> _cbvHeap;
	std::unique_ptr<UploadBuffer<ObjectConstants>> _cbUploadBuffer;
	std::unique_ptr<MeshGeometry> _boxGeo = nullptr;

	ComPtr<ID3D12RootSignature> _rootSignature;

	ComPtr<ID3DBlob> _vsByteCode = nullptr;
	ComPtr<ID3DBlob> _psByteCode = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;
	ComPtr<ID3D12PipelineState> _pso;

	Matrix _world = Matrix::Identity;
	Matrix _view = Matrix::Identity;
	Matrix _proj = Matrix::CreatePerspectiveFieldOfView(XM_PIDIV4, AspectRatio(), 0.1f, 100.0f);

	float _theta = XM_PIDIV4;
	float _phi = XM_PIDIV4;
	float _radius = 5.0f;

	POINT _lastMousePos;
};

#endif // !D3D_FRAMEWORK_HPP
