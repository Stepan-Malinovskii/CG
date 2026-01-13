#include "BaseD3DApp.h"

BaseD3DApp* BaseD3DApp::_app = nullptr;

BaseD3DApp::BaseD3DApp(HINSTANCE hInstance) : _hInstance(hInstance)
{
	_app = this;
}

BaseD3DApp::~BaseD3DApp()
{
	if (_d3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

void BaseD3DApp::Set4xMsaaState(bool value)
{
    if (_4xMsaaState == value)
    {
        return;
    }

    _4xMsaaState = value;

    if (_4xMsaaState)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
        msQualityLevels.Format = _backBufferFormat;
        msQualityLevels.SampleCount = 4;
        msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        msQualityLevels.NumQualityLevels = 0;

        HRESULT hr = _d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels));

        if (FAILED(hr) || msQualityLevels.NumQualityLevels == 0)
        {
            _4xMsaaState = false;
            std::cout << "WARNING: MSAA 4x is NOT supported. Disabling." << std::endl;
        }
        else
        {
            _4xMsaaQuality = msQualityLevels.NumQualityLevels - 1;
            std::cout << "MSAA 4x is enabled (quality level: " << _4xMsaaQuality << ")" << std::endl;
        }
    }
    else
    {
        std::cout << "MSAA 4x is disabled" << std::endl;
    }

    if (_hMainWnd)
    {
        OnResize();
    }
}

bool BaseD3DApp::Initialize()
{
    return InitMainWindow() && InitDirect3D();
}

bool BaseD3DApp::InitMainWindow()
{
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.hInstance = _hInstance;
    wc.lpszClassName = L"MainWndClass";
    wc.lpfnWndProc = BaseD3DApp::MsgProc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName = nullptr;
    wc.hIconSm = wc.hIcon;

    if (!RegisterClassEx(&wc))
    {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK);
        return false;
    }

    _hMainWnd = CreateWindowExW(0, L"MainWndClass", _mainWndCaption.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CLIENT_WIDTH, CLIENT_HEIGHT, NULL, NULL, _hInstance, reinterpret_cast<LPVOID>(this));

    if (!_hMainWnd)
    {
        MessageBoxW(nullptr, L"Failed to create main window", L"Error", MB_OK);
        return false;
    }

    ShowWindow(_hMainWnd, SW_SHOW);
    UpdateWindow(_hMainWnd);

    return true;
}

bool BaseD3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
    debugController->EnableDebugLayer();
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_d3dDevice)));
    ThrowIfFailed(_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));

    _rtvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    _dsvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();

    _screenViewport = { 0.0f, 0.0f, (float)CLIENT_WIDTH, (float)CLIENT_HEIGHT, 0.0f, 1.0f };
    _scissorRect = { 0, 0, (LONG)CLIENT_WIDTH, (LONG)CLIENT_HEIGHT };

    return true;
}

void BaseD3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_cmdQueue)));
	std::cout << "Command queue is created" << std::endl;

	ThrowIfFailed(_d3dDevice->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(_directCmdListAlloc.GetAddressOf())));
	std::cout << "Command allocatoris is created" << std::endl;

	ThrowIfFailed(_d3dDevice->CreateCommandList(0, queueDesc.Type, _directCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
	std::cout << "Command list is created" << std::endl;
	ThrowIfFailed(_cmdList->Close());
}

void BaseD3DApp::CreateSwapChain()
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = CLIENT_WIDTH;
    sd.BufferDesc.Height = CLIENT_HEIGHT;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = _backBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    sd.OutputWindow = _hMainWnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(_dxgiFactory->CreateSwapChain(_cmdQueue.Get(), &sd, &_swapChain));

    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i)
    {
        ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_swapChainBuffer[i])));
    }

	std::cout << "Swap chain is created" << std::endl;
}

void BaseD3DApp::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;

    ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)));
    std::cout << "RTV heap is created" << std::endl;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_swapChainBuffer[i])));
        _d3dDevice->CreateRenderTargetView(_swapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, _rtvDescriptorSize);
    }
    std::cout << "RTV descriptors are created" << std::endl;

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;

    ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap)));
    std::cout << "DSV heap is created" << std::endl;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Alignment = 0;
    depthDesc.Width = CLIENT_WIDTH;
    depthDesc.Height = CLIENT_HEIGHT;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = _depthStencilFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = _depthStencilFormat;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(_d3dDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&_depthStencilBuffer)));
    std::cout << "DSV resource is created" << std::endl;

    _d3dDevice->CreateDepthStencilView(_depthStencilBuffer.Get(), nullptr, DepthStencilView());
    std::cout << "DSV descriptor is created" << std::endl;
}

void BaseD3DApp::OnResize() {}

int BaseD3DApp::Run()
{
    ShowWindow(_hMainWnd, SW_SHOW);
    UpdateWindow(_hMainWnd);
    _timer.Reset();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            _timer.Tick();

            if (!_appPaused)
            {
                CalculateFrameStats();
                Update(_timer);
                Draw(_timer);
            }
            else
            {
                Sleep(100);
            }
        }
    }

    return static_cast<int>(msg.wParam);
}

void BaseD3DApp::FlushCommandQueue()
{
    _currentFence++;
    ThrowIfFailed(_cmdQueue->Signal(_fence.Get(), _currentFence));

    if (_fence->GetCompletedValue() < _currentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(_fence->SetEventOnCompletion(_currentFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void BaseD3DApp::CalculateFrameStats()
{
    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;

    if ((_timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = (float)frameCnt;
        float mspf = 1000.0f / fps;

        std::wstring windowText = _mainWndCaption +
            L"    FPS: " + std::to_wstring((int)fps) +
            L"    MS per Frame: " + std::to_wstring(mspf);

        SetWindowTextW(_hMainWnd, windowText.c_str());

        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

LRESULT BaseD3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BaseD3DApp* app = BaseD3DApp::GetApp();

    switch (msg)
    {
        case WM_CREATE:
        {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            if (app)
            {
                app->OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            if (app)
            {
                app->OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (app)
            {
                app->OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
        {
            if (app && app->_d3dDevice)
            {
                app->_resizing = true;
                if (wParam == SIZE_MINIMIZED)
                {
                    app->_minimized = true;
                }
                else if (wParam == SIZE_MAXIMIZED)
                {
                    app->_maximized = true;
                }
                else if (wParam == SIZE_RESTORED)
                {
                    if (app->_minimized)
                    {
                        app->_minimized = false;
                    }
                    else if (app->_maximized)
                    {
                        app->_maximized = false;
                    }
                    else if (app->_resizing)
                    {
                        app->_resizing = false;
                    }

                    app->OnResize();
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                app->_appPaused = true;
                app->_timer.Stop();
            }
            else
            {
                app->_appPaused = false;
                app->_timer.Start();
            }

            return 0;
        }

        case WM_ENTERSIZEMOVE:
        {
            app->_appPaused = true;
            app->_resizing = true;
            app->_timer.Stop();

            return 0;
        }

        case WM_EXITSIZEMOVE:
        {
            app->_appPaused = false;
            app->_resizing = false;
            app->_timer.Start();

            app->OnResize();

            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

        default:
        {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
}