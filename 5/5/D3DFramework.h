#ifndef D3D_FRAMEWORK_HPP
#define D3D_FRAMEWORK_HPP

#include <memory>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

#include <Windows.h>
#include <Windowsx.h>

#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DDSTextureLoader.h>

#include "BaseD3DApp.h"
#include "FrameResource.h"
#include "UploadBuffer.h"
#include "D3DUtil.h"
#include "MathHelper.h"
#include "ThrowIfFaild.h" 
#include "ModelStruct.h"
#include "ModelParse.h"
#include "GBuffer.h"

using namespace Microsoft::WRL;
using namespace DirectX;

constexpr int NUM_FRAME_RECOURCES = 3;

struct RenderItem
{
    MeshGeometry* Geo;
    Material* Mat;

    std::string SubmeshName;

    std::string UsedPso;

    int NumFramesDirty = NUM_FRAME_RECOURCES;

    UINT IndexCount;
    UINT StartIndexLocation;
    int BaseVertexLocation;

    UINT ObjCBIndex;

    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

class D3DFramework : public BaseD3DApp
{
public:
    D3DFramework(HINSTANCE hInstance);
    D3DFramework(const D3DFramework& rhs) = delete;
    D3DFramework& operator=(const D3DFramework& rhs) = delete;
    ~D3DFramework();

    virtual bool Initialize()override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);

    void AnimateMaterials(const GameTimer& gt);
    void AnimateLight(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateLightSB(const GameTimer& gt);

    void BuildRootSignatureGBuffer();
    void BuildRootSignatureLightPass();

    void BuildDescriptorHeaps();
    void BuildLightSRV();

    void BuildGBufferPSO();
    void BuildLightPassPSO();

    void BuildShadersAndInputLayout();

    void CreateLight();
    void LoadModel(std::string path);
    void CreateSceneObjects();
    void BuildRenderItems();
    void BuildFrameResources();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
    void ParseMesh(const ModelParse::MeshInfo& meshData);
    void LoadTextures(const ModelParse::MeshInfo& meshData);
    void ParseMaterials(const ModelParse::MeshInfo& meshData);

private:

    std::vector<std::unique_ptr<FrameResource>> _frameResources;
    FrameResource* _currFrameResource = nullptr;
    int _currFrameResourceIndex = 0;

    UINT _cbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> _rootSignatureGBuffer = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureLightPass = nullptr;
    ComPtr<ID3D12DescriptorHeap> _srvDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> _geometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> _materials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> _textures;
    std::vector<Light> _lights;

    std::unordered_map<std::string, std::unique_ptr<Model>> _models;
    std::vector<std::unique_ptr<SceneObject>> _sceneObjects;

    std::vector<std::unique_ptr<RenderItem>> _allRitems;
    std::vector<RenderItem*> _opaqueRitems;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> _shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> _psos;

    std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

    PassConstants _mainPassCB;

    std::unique_ptr<GBuffer> _gBuffer;

    XMFLOAT3 _eyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 _view = MathHelper::Identity4x4();
    XMFLOAT4X4 _proj = MathHelper::Identity4x4();

    float _yaw = 0.0f;
    float _pitch = 0.0f;

    XMFLOAT3 _forward = { 0.0f, 0.0f, 1.0f };
    XMFLOAT3 _right = { 1.0f, 0.0f, 0.0f };
    XMFLOAT3 _up = { 0.0f, 1.0f, 0.0f };

    float _moveSpeed = 10.0f;
    float _rotateSpeed = 0.15f;

    POINT _lastMousePos;

private:
    bool _isDebugMode = false;
};

#endif // !D3D_FRAMEWORK_HPP
