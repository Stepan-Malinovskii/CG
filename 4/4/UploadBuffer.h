#ifndef UPLOAD_BUFFER_HPP
#define UPLOAD_BUFFER_HPP

#include <iostream>
#include "ThrowIfFaild.h"
#include "D3DUtil.h"
#include "MyGeometry.h"

using namespace Microsoft::WRL;

template<typename T>
class UploadBuffer
{
public:
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) : _isConstantBuffer(isConstantBuffer)
    {
        _elementByteSize = sizeof(T);

        if (isConstantBuffer)
        {
            _elementByteSize = D3DUtil::CalcConstantBufferSize(sizeof(T));
        }

        CD3DX12_HEAP_PROPERTIES heapPr = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(_elementByteSize * elementCount);

        ThrowIfFailed(device->CreateCommittedResource(&heapPr, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_uploadBuffer)));

        ThrowIfFailed(_uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&_mappedData)));
        std::cout << "Constant buffer is created" << std::endl;
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (_uploadBuffer != nullptr)
        {
            _uploadBuffer->Unmap(0, nullptr);
        }

        _mappedData = nullptr;
    }

    ID3D12Resource* Resource()const { return _uploadBuffer.Get(); }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&_mappedData[elementIndex * _elementByteSize], &data, sizeof(T));
    }

private:
    ComPtr<ID3D12Resource> _uploadBuffer;
    BYTE* _mappedData = nullptr;

    UINT _elementByteSize = 0;
    bool _isConstantBuffer = false;
};

#endif // !UPLOAD_BUFFER_HPP