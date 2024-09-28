#include "core.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "../vendor/tinyobjloader/tiny_obj_loader.h"
#include "../vendor/stb/stb_image.h"
#include "../vendor/imgui/imgui.h"
#include "../vendor/imgui/backends/imgui_impl_win32.h"
#include "../vendor/imgui/backends/imgui_impl_dx11.h"

#include <DirectXMath.h>

bool gAppShouldRun = true;

uint32_t gWindowWidth = 1600;
uint32_t gWindowHeight = 900;
HWND gWindow = nullptr;

IDXGISwapChain* gSwapChain = nullptr;
ID3D11Device* gDevice = nullptr;
ID3D11DeviceContext* gContext = nullptr;

ID3D11RenderTargetView* gBackBufferView = nullptr;

ID3D11DepthStencilView* gDepthBufferView = nullptr;

ID3D11Buffer* gMVPBuffer = nullptr;
ID3D11Buffer* gLightBuffer = nullptr;

constexpr float TO_RADIANS = DirectX::XM_PI / 180.0f;

uint8_t gKeyboard[256];

struct MeshVertex
{
    Vec3 pos; // xyz
    Vec3 normal;
    Vec2 textureCoords; // uv
};

struct SubMeshData
{
    ID3D11ShaderResourceView* texture;
    uint32_t indexCount;
};

struct MVPBuffer
{
    DirectX::XMMATRIX model;
    DirectX::XMMATRIX view;
    DirectX::XMMATRIX proj;

    DirectX::XMMATRIX inverseModel;
};

static Transform sModelTransform
{
    { -0.330f, -0.540f, 2.070f },   // location
    { 0.0f, 150.0f, 0.0f },   // rotation
    { 1.0f, 1.0f, 1.0f }    // scale
};

struct Camera
{
    Vec3 location = { -1.38f, 1.44f, -2.0f };
    Vec3 rotation = { 0.0f, 0.0f, 0.0f };
    float FOV = 60.0f;
    float speed = 0.03f;
};

struct LightSettings
{
    Vec4 camPos;
    Vec4 pos = { 0.9f, 0.0f, 0.6f };
    Vec4 ambientColor = { 0.1f, 0.1f, 0.1f, 1.0f };
    Vec4 lightColor = { 1.0f, 1.0f, 1.0f };
    float ambientStrength = 0.1f;
    float specularStrength = 0.7f;
    float specularPow = 256;
    float dummyPadding0;
};

static Camera sCamera;
static LightSettings sLightSettings;

struct Mesh
{
    std::vector<SubMeshData> submeshes;
    std::string debugName;
    ID3D11Buffer* vertexBuffer;
    ID3D11Buffer* indexBuffer;
};

static std::vector<Mesh> sMeshes;
static uint32_t sMeshIndex = 0;

void LoadMesh(const std::string& meshPath, Mesh& outMesh)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    check(tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, meshPath.c_str()));

    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMeshData> submeshes;

# if 0

    uint32_t vertexCount = attrib.vertices.size() / 3;

    vertices.resize(vertexCount);
    for (uint32_t i = 0; i < vertexCount; i++)
    {
        vertices[i].pos.x = attrib.vertices[i * 3 + 0];
        vertices[i].pos.y = attrib.vertices[i * 3 + 1];
        vertices[i].pos.z = attrib.vertices[i * 3 + 2];
    }

    for (uint32_t i = 0; i < shapes.size(); i++)
    {
        uint32_t indexCount = shapes[i].mesh.indices.size();
        for (uint32_t j = 0; j < indexCount; j++)
        {
            auto index = shapes[i].mesh.indices[j];
            MeshVertex& vertex = vertices[index.vertex_index];

            // texture coordinates
            if (index.texcoord_index != -1)
            {
                vertex.textureCoords.x = attrib.texcoords[index.texcoord_index * 2 + 0];
                vertex.textureCoords.y = -attrib.texcoords[index.texcoord_index * 2 + 1];
            }

            indices.push_back(index.vertex_index);
        }

        SubMeshData& s = submeshes.emplace_back();
        s.indexCount = indexCount;
    }

#else

    for (const auto& shape : shapes)
    {
        for (const auto& boh : shape.mesh.indices)
        {
            MeshVertex& v = vertices.emplace_back();
            v.pos.x = attrib.vertices[3 * boh.vertex_index + 0];
            v.pos.y = attrib.vertices[3 * boh.vertex_index + 1];
            v.pos.z = attrib.vertices[3 * boh.vertex_index + 2];

            if (boh.normal_index != -1)
            {
                v.normal.x = attrib.normals[3 * boh.normal_index + 0];
                v.normal.y = attrib.normals[3 * boh.normal_index + 1];
                v.normal.z = attrib.normals[3 * boh.normal_index + 2];
            }

            if (boh.texcoord_index != -1)
            {
                v.textureCoords.x = attrib.texcoords[2 * boh.texcoord_index + 0];
                v.textureCoords.y = -attrib.texcoords[2 * boh.texcoord_index + 1];
            }

            indices.push_back(indices.size());
        }

        SubMeshData& s = submeshes.emplace_back();
        s.indexCount = shape.mesh.indices.size();
        s.texture = nullptr;
    }

#endif

    ID3D11Buffer* vertexBuffer;
    ID3D11Buffer* indexBuffer;

    D3D11_BUFFER_DESC vBufferDesc = {};
    vBufferDesc.ByteWidth = sizeof(MeshVertex) * vertices.size();
    vBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    vBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vBufferDesc.StructureByteStride = sizeof(MeshVertex);

    D3D11_SUBRESOURCE_DATA vBufferDataPtr = {};
    vBufferDataPtr.pSysMem = (const void*)vertices.data();

    d3dcheck(gDevice->CreateBuffer(&vBufferDesc, &vBufferDataPtr, &vertexBuffer));

    D3D11_BUFFER_DESC iBufferDesc = {};
    iBufferDesc.ByteWidth = sizeof(uint32_t) * indices.size();
    iBufferDesc.Usage = D3D11_USAGE_DEFAULT;
    iBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA iBufferDataPtr = {};
    iBufferDataPtr.pSysMem = (const void*)indices.data();

    d3dcheck(gDevice->CreateBuffer(&iBufferDesc, &iBufferDataPtr, &indexBuffer));

    outMesh.vertexBuffer = vertexBuffer;
    outMesh.indexBuffer = indexBuffer;
    outMesh.submeshes = std::move(submeshes);
    outMesh.debugName = meshPath;
}

void CreateTexture(uint32_t width, uint32_t height, void* data, ID3D11Texture2D*& outTexture, ID3D11ShaderResourceView*& outResource)
{
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.ArraySize = 1;
    texDesc.Width = (UINT)width;
    texDesc.Height = (UINT)height;
    texDesc.MipLevels = 1;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.SampleDesc.Count = 1;

    D3D11_SUBRESOURCE_DATA texData = {};
    texData.pSysMem = data;
    texData.SysMemPitch = width * 4;

    d3dcheck(gDevice->CreateTexture2D(&texDesc, &texData, &outTexture));
    d3dcheck(gDevice->CreateShaderResourceView(outTexture, nullptr, &outResource));
}

void LoadTexture(const std::string& texturePath, ID3D11Texture2D*& outTexture, ID3D11ShaderResourceView*& outResource)
{
    int x, y, channels;
    void* img = stbi_load(texturePath.c_str(), &x, &y, &channels, 4);

    CreateTexture((uint32_t)x, (uint32_t)y, img, outTexture, outResource);

    stbi_image_free(img);
}

void SetMesh(uint32_t meshIndex)
{
    const Mesh& mesh = sMeshes[meshIndex];

    uint32_t strides[] = { sizeof(MeshVertex) };
    uint32_t offsets[] = { 0 };
    gContext->IASetVertexBuffers(0, 1, &mesh.vertexBuffer, strides, offsets);

    gContext->IASetIndexBuffer(mesh.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    sMeshIndex = meshIndex;
}

void Init()
{
    // create swapchain
    DXGI_SWAP_CHAIN_DESC swapchainDesc = {};
    swapchainDesc.BufferCount = 1;
    swapchainDesc.BufferDesc.Width = gWindowWidth;
    swapchainDesc.BufferDesc.Height = gWindowHeight;
    swapchainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swapchainDesc.BufferDesc.RefreshRate.Numerator = 0;
    swapchainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchainDesc.OutputWindow = gWindow;
    swapchainDesc.SampleDesc.Count = 1;
    swapchainDesc.Windowed = true;
    swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    HRESULT swapchainHR = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_DEBUG, 0, 0,
            D3D11_SDK_VERSION, &swapchainDesc, &gSwapChain, &gDevice, 0, &gContext);

    d3dcheck(swapchainHR);

    // back buffer...
    ID3D11Texture2D* backBuffer;
    d3dcheck(gSwapChain->GetBuffer(0 /*first buffer (back buffer)*/, __uuidof(ID3D11Texture2D), (void**)&backBuffer));
    d3dcheck(gDevice->CreateRenderTargetView(backBuffer, nullptr, &gBackBufferView));
    backBuffer->Release();

    // depth buffer
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthDesc.Width = gWindowWidth;
    depthDesc.Height = gWindowHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.SampleDesc.Count = 1;

    ID3D11Texture2D* depthBuffer;
    d3dcheck(gDevice->CreateTexture2D(&depthDesc, nullptr, &depthBuffer));
    d3dcheck(gDevice->CreateDepthStencilView(depthBuffer, nullptr, &gDepthBufferView));
    depthBuffer->Release();

    // shaders
    ID3D11VertexShader* vertexShader;
    ID3D11PixelShader* pixelShader;
    ID3D11InputLayout* inputLayout;

    ID3DBlob* vsBlob;
    d3dcheck(D3DCompileFromFile(L"shaders/vertex.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr));
    d3dcheck(gDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader));

    ID3DBlob* psBlob;
    d3dcheck(D3DCompileFromFile(L"shaders/pixel.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, nullptr));
    d3dcheck(gDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader));
    psBlob->Release();

    D3D11_INPUT_ELEMENT_DESC inputs[3] = {};

    inputs[0].SemanticName = "POS";
    inputs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[0].AlignedByteOffset = 0;

    inputs[1].SemanticName = "NORMAL";
    inputs[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    inputs[1].AlignedByteOffset = 12;

    inputs[2].SemanticName = "TEX_COORDS";
    inputs[2].Format = DXGI_FORMAT_R32G32_FLOAT;
    inputs[2].AlignedByteOffset = 24;

    d3dcheck(gDevice->CreateInputLayout(inputs, sizeof(inputs) / sizeof(D3D11_INPUT_ELEMENT_DESC), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout));
    vsBlob->Release();

    // sampler for textures
    ID3D11SamplerState* sampler;

    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    gDevice->CreateSamplerState(&samplerDesc, &sampler);

    // mvp buffer
    D3D11_BUFFER_DESC mvpBufferDesc = {};
    mvpBufferDesc.ByteWidth = sizeof(MVPBuffer);
    mvpBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    mvpBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    mvpBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    d3dcheck(gDevice->CreateBuffer(&mvpBufferDesc, nullptr, &gMVPBuffer));

    // light settings buffer
    D3D11_BUFFER_DESC lightBufferDesc = {};
    lightBufferDesc.ByteWidth = sizeof(LightSettings);
    lightBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    lightBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lightBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    d3dcheck(gDevice->CreateBuffer(&lightBufferDesc, nullptr, &gLightBuffer));

    // bind everyting
    gContext->OMSetRenderTargets(1, &gBackBufferView, gDepthBufferView);

    gContext->VSSetShader(vertexShader, nullptr, 0);
    gContext->PSSetShader(pixelShader, nullptr, 0);
    gContext->IASetInputLayout(inputLayout);

    gContext->VSSetConstantBuffers(0, 1, &gMVPBuffer);
    gContext->PSSetConstantBuffers(0, 1, &gLightBuffer);

    gContext->PSSetSamplers(0, 1, &sampler);

    D3D11_VIEWPORT viewport = {};
    viewport.Width = gWindowWidth;
    viewport.Height = gWindowHeight;
    viewport.MaxDepth = 1.0f;
    viewport.MinDepth = 0.0f;

    gContext->RSSetViewports(1, &viewport);

    gContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // load meshes
    LoadMesh("meshes/dbd.obj", sMeshes.emplace_back());
    LoadMesh("meshes/cube.obj", sMeshes.emplace_back());
    LoadMesh("meshes/lamp.obj", sMeshes.emplace_back());
    LoadMesh("meshes/negan.obj", sMeshes.emplace_back());

    // load textures
    ID3D11Texture2D* dummy;
    LoadTexture("textures/dbd/0.png", dummy, sMeshes[0].submeshes[0].texture);
    LoadTexture("textures/dbd/1.png", dummy, sMeshes[0].submeshes[1].texture);
    LoadTexture("textures/dbd/2.png", dummy, sMeshes[0].submeshes[2].texture);
    LoadTexture("textures/dbd/3.png", dummy, sMeshes[0].submeshes[3].texture);
    LoadTexture("textures/dbd/4.png", dummy, sMeshes[0].submeshes[4].texture);
    LoadTexture("textures/dbd/5.png", dummy, sMeshes[0].submeshes[5].texture);
    LoadTexture("textures/dbd/6.png", dummy, sMeshes[0].submeshes[6].texture);
    LoadTexture("textures/dbd/7.png", dummy, sMeshes[0].submeshes[7].texture);

    uint32_t whiteTexture = 0xffffffff;
    CreateTexture(1, 1, &whiteTexture, dummy, sMeshes[1].submeshes[0].texture);

    LoadTexture("textures/lamp/0.jpg", dummy, sMeshes[2].submeshes[0].texture);

    LoadTexture("textures/negan/0.png", dummy, sMeshes[3].submeshes[0].texture);

    // set default mesh
    SetMesh(0);

    // imgui
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(gWindow);
    ImGui_ImplDX11_Init(gDevice, gContext);
}

void Update()
{
    // move camera
    constexpr uint8_t KEY_W = 0x57;
    constexpr uint8_t KEY_A = 0x41;
    constexpr uint8_t KEY_S = 0x53;
    constexpr uint8_t KEY_D = 0x44;
    constexpr uint8_t KEY_Q = 0x51;
    constexpr uint8_t KEY_E = 0x45;

    if (gKeyboard[KEY_W])
        sCamera.location.z += sCamera.speed;

    if (gKeyboard[KEY_S])
        sCamera.location.z -= sCamera.speed;

    if (gKeyboard[KEY_D])
        sCamera.location.x += sCamera.speed;

    if (gKeyboard[KEY_A])
        sCamera.location.x -= sCamera.speed;

    if (gKeyboard[KEY_E])
        sCamera.location.y += sCamera.speed;

    if (gKeyboard[KEY_Q])
        sCamera.location.y -= sCamera.speed;

    // mvp
    MVPBuffer mvp;

    DirectX::XMMATRIX model =
        DirectX::XMMatrixScaling(sModelTransform.scale.x, sModelTransform.scale.y, sModelTransform.scale.z)
        *
        DirectX::XMMatrixRotationRollPitchYaw(sModelTransform.rotation.x * TO_RADIANS, sModelTransform.rotation.y * TO_RADIANS, sModelTransform.rotation.z * TO_RADIANS)
        *
        DirectX::XMMatrixTranslation(sModelTransform.location.x, sModelTransform.location.y, sModelTransform.location.z);

    // todo: watch videos about dot, cross, etc.. with vector, goal: calculate forward vector and up vector :)
    DirectX::XMMATRIX view = DirectX::XMMatrixLookToLH({ sCamera.location.x, sCamera.location.y, sCamera.location.z }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f });

    DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(sCamera.FOV * TO_RADIANS, (float)gWindowWidth / (float)gWindowHeight, 0.01f, 1000.0f);

    // shaders use column major, lets transpose
    mvp.model = DirectX::XMMatrixTranspose(model);
    mvp.view = DirectX::XMMatrixTranspose(view);
    mvp.proj = DirectX::XMMatrixTranspose(proj);

    mvp.inverseModel = DirectX::XMMatrixInverse(nullptr, model); // not transposed!

    D3D11_MAPPED_SUBRESOURCE mvpMap;
    d3dcheck(gContext->Map(gMVPBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mvpMap));
    memcpy(mvpMap.pData, &mvp, sizeof(MVPBuffer));
    gContext->Unmap(gMVPBuffer, 0);

    // light settings
    sLightSettings.camPos = { sCamera.location.x, sCamera.location.y, sCamera.location.z };

    D3D11_MAPPED_SUBRESOURCE lightMap;
    d3dcheck(gContext->Map(gLightBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &lightMap));
    memcpy(lightMap.pData, &sLightSettings, sizeof(LightSettings));
    gContext->Unmap(gLightBuffer, 0);
}

void ImguiRender()
{
    ImGui::Begin("Settings");

    ImGui::PushID("model");
    ImGui::Text("Model");
    
    if (ImGui::BeginCombo("Mesh", sMeshes[sMeshIndex].debugName.c_str()))
    {
        for (uint32_t i = 0; i < sMeshes.size(); i++)
            if (ImGui::Selectable(sMeshes[i].debugName.c_str(), i == sMeshIndex))
                SetMesh(i);

        ImGui::EndCombo();
    }
    
    ImGui::DragFloat3("Location", &sModelTransform.location.x, 0.03f);
    ImGui::DragFloat3("Rotation", &sModelTransform.rotation.x, 0.5f);
    ImGui::DragFloat3("Scale", &sModelTransform.scale.x, 0.05f);
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushID("camera");
    ImGui::Text("Camera");
    ImGui::DragFloat3("Location", &sCamera.location.x, 0.03f);
    ImGui::DragFloat("FOV", &sCamera.FOV, 0.05f);
    ImGui::PopID();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushID("light");
    ImGui::Text("Light settings");
    ImGui::DragFloat3("Position", &sLightSettings.pos.x, 0.03f);
    ImGui::ColorEdit3("Ambient color", &sLightSettings.ambientColor.x);
    ImGui::ColorEdit3("Light color", &sLightSettings.lightColor.x);
    ImGui::DragFloat("Ambient intensity", &sLightSettings.ambientStrength);
    ImGui::DragFloat("Specular intensity", &sLightSettings.specularStrength);
    ImGui::DragFloat("Specular power", &sLightSettings.specularPow);
    ImGui::PopID();

    ImGui::End();
}

void Render()
{
    float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    gContext->ClearRenderTargetView(gBackBufferView, clearColor);
    gContext->ClearDepthStencilView(gDepthBufferView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0.0f);

    ImGui_ImplWin32_NewFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();

    ImguiRender();

    uint32_t indexOffset = 0;
    for (SubMeshData s : sMeshes[sMeshIndex].submeshes)
    {
        gContext->PSSetShaderResources(0, 1, &s.texture);
        gContext->DrawIndexed(s.indexCount, indexOffset, 0);
        indexOffset += s.indexCount;
    }

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    gSwapChain->Present(1, 0);
}

LRESULT WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    ImGui_ImplWin32_WndProcHandler(hWnd, Msg, wParam, lParam);

    switch (Msg)
    {
        case WM_KEYDOWN:
            gKeyboard[(uint8_t)wParam] = true;
            break;

        case WM_KEYUP:
            gKeyboard[(uint8_t)wParam] = false;
            break;

        case WM_SIZE:
        {
            gWindowWidth = LOWORD(lParam);
            gWindowHeight = HIWORD(lParam);

            if (gSwapChain)
            {
                // release refs...
                gBackBufferView->Release();
                gDepthBufferView->Release();

                // resize back and front buffers...
                gSwapChain->ResizeBuffers(1, gWindowWidth, gWindowHeight, DXGI_FORMAT_UNKNOWN, 0);

                // get updated ref to back buffer
                ID3D11Texture2D* backBuffer;
                d3dcheck(gSwapChain->GetBuffer(0 /*first buffer (back buffer)*/, __uuidof(ID3D11Texture2D), (void**)&backBuffer));
                d3dcheck(gDevice->CreateRenderTargetView(backBuffer, nullptr, &gBackBufferView));
                backBuffer->Release();

                // create new depth buffer
                D3D11_TEXTURE2D_DESC depthDesc = {};
                depthDesc.Usage = D3D11_USAGE_DEFAULT;
                depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                depthDesc.Width = gWindowWidth;
                depthDesc.Height = gWindowHeight;
                depthDesc.MipLevels = 1;
                depthDesc.ArraySize = 1;
                depthDesc.SampleDesc.Count = 1;

                ID3D11Texture2D* depthBuffer;
                d3dcheck(gDevice->CreateTexture2D(&depthDesc, nullptr, &depthBuffer));
                d3dcheck(gDevice->CreateDepthStencilView(depthBuffer, nullptr, &gDepthBufferView));
                depthBuffer->Release();

                // update viwpoert
                D3D11_VIEWPORT viewport = {};
                viewport.Width = gWindowWidth;
                viewport.Height = gWindowHeight;
                viewport.MaxDepth = 1.0f;
                viewport.MinDepth = 0.0f;

                gContext->RSSetViewports(1, &viewport);

                // set updated render targets
                gContext->OMSetRenderTargets(1, &gBackBufferView, gDepthBufferView);
            }
        }
        break;

        case WM_CLOSE:
            gAppShouldRun = false;
            break;
    }

    return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
    WNDCLASSEXA wndClass = {};
    wndClass.cbSize = sizeof(WNDCLASSEXA);
    wndClass.hInstance = hInst;
    wndClass.lpfnWndProc = WndProc;
    wndClass.lpszClassName = "boh";

    check(RegisterClassExA(&wndClass));

    gWindow = CreateWindowExA(0, "boh", "lighting test", WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SIZEBOX | WS_SYSMENU,
                100, 100, gWindowWidth, gWindowHeight, nullptr, nullptr, hInst, nullptr);

    check(gWindow);

    ShowWindow(gWindow, SW_SHOW);

    Init();

    while (gAppShouldRun)
    {
        MSG msg;
        while (PeekMessageA(&msg, gWindow, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Update();
        Render();
    }

    return 0;
}