#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3d11.lib")
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpfnWndProc=proc; cls.lpszClassName=L"PK_FpsTestRenderer";
    RegisterClassW(&cls);
    HWND window=CreateWindowW(cls.lpszClassName,L"FPS verification · D3D11 test renderer",WS_OVERLAPPEDWINDOW,80,80,480,320,nullptr,nullptr,instance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=2; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=window; desc.SampleDesc.Count=1;
    desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ID3D11Device* device=nullptr; ID3D11DeviceContext* context=nullptr; IDXGISwapChain* swap=nullptr;
    HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context);
    if (FAILED(result)) return 1;
    ID3D11Texture2D* texture=nullptr; ID3D11RenderTargetView* view=nullptr;
    result=swap->GetBuffer(0,__uuidof(ID3D11Texture2D),(void**)&texture);
    if (SUCCEEDED(result)) result=device->CreateRenderTargetView(texture,nullptr,&view);
    if(texture)texture->Release(); if(FAILED(result)){swap->Release();context->Release();device->Release();return 2;}
    ShowWindow(window,SW_SHOW); ULONGLONG start=GetTickCount64(); MSG message{}; int status=0;
    while(GetTickCount64()-start<30000){
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)goto done;TranslateMessage(&message);DispatchMessageW(&message);}
        float color[4]={0.06f,0.2f,0.36f,1};context->ClearRenderTargetView(view,color);
        if(FAILED(swap->Present(1,0))){status=3;break;}
    }
done:
    view->Release();swap->Release();context->Release();device->Release();if(IsWindow(window))DestroyWindow(window);return status;
}
