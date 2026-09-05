#include "densify_pass.h"
#include "d3d12_ctx.h"

#include <cstdio>
#include <cstring>

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace {

// Same bilinear formula as the CPU densifier and the old D3D11 compute version (floor, bilinear
// with the same clamp rules, S10.5 /32 px scale) so the motion values stay numerically close.
const char* kDensifyCS = R"(
Texture2D<int2> gFlow : register(t0);
RWTexture2D<float2> gOut : register(u0);
cbuffer Dense : register(b0) { uint4 P; }   // xy = out size, z = grid step, w = unused

[numthreads(8, 8, 1)]
void CS(uint3 dt : SV_DispatchThreadID) {
    if (dt.x >= P.x || dt.y >= P.y) return;
    const int ds = (int)P.z;
    const int gw = (int)((P.x + P.z - 1u) / P.z);
    const int gh = (int)((P.y + P.z - 1u) / P.z);
    const float gx = (float)dt.x / (float)ds - 0.5f;
    const float gy = (float)dt.y / (float)ds - 0.5f;
    int x0 = (int)floor(gx), y0 = (int)floor(gy);
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = clamp(x0, 0, gw - 1); x1 = clamp(x1, 0, gw - 1);
    y0 = clamp(y0, 0, gh - 1); y1 = clamp(y1, 0, gh - 1);
    const float tx = gx - floor(gx), ty = gy - floor(gy);
    const int2 A = int2(x0, y0), B = int2(x1, y0), C = int2(x0, y1), D = int2(x1, y1);
    const int2 va = gFlow.Load(int3(A, 0));
    const int2 vb = gFlow.Load(int3(B, 0));
    const int2 vc = gFlow.Load(int3(C, 0));
    const int2 vd = gFlow.Load(int3(D, 0));
    const float fx0 = (float)va.x + ((float)vb.x - (float)va.x) * tx;
    const float fx1 = (float)vc.x + ((float)vd.x - (float)vc.x) * tx;
    const float fy0 = (float)va.y + ((float)vb.y - (float)va.y) * tx;
    const float fy1 = (float)vc.y + ((float)vd.y - (float)vc.y) * tx;
    float2 mv;
    mv.x = (fx0 + (fx1 - fx0) * ty) / 32.0f;
    mv.y = (fy0 + (fy1 - fy0) * ty) / 32.0f;
    gOut[dt.xy] = mv;   // R16G16_FLOAT target: the hardware quantises to half for us
}
)";

}  // namespace

MvecDensify::~MvecDensify() = default;

bool MvecDensify::init(ID3D12Device* device, const char** errOut) {
    if (errOut) *errOut = "";
    auto fail = [&](const char* m) {
        if (errOut) *errOut = m;
        printf("[densify] error: %s\n", m);
        return false;
    };

    ComPtr<ID3DBlob> cs, errBlob;
    HRESULT hr = D3DCompile(kDensifyCS, strlen(kDensifyCS), nullptr, nullptr, nullptr, "CS",
                            "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &cs, &errBlob);
    if (FAILED(hr)) {
        return fail(errBlob ? (const char*)errBlob->GetBufferPointer()
                            : "CS compile failed");
    }

    // Root signature: root constant (b0, 4 uint32s) + descriptor table (SRV t0, UAV u0).
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = 4;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
    rsDesc.NumParameters = 2;
    rsDesc.pParameters = params;
    rsDesc.NumStaticSamplers = 0;
    rsDesc.pStaticSamplers = nullptr;
    rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &rsBlob, &rsErr);
    if (FAILED(hr)) {
        return fail(rsErr ? (const char*)rsErr->GetBufferPointer()
                          : "root signature serialize failed");
    }
    hr = device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(),
                                     IID_PPV_ARGS(&m_rootSig));
    if (FAILED(hr)) return fail("CreateRootSignature failed");

    D3D12_COMPUTE_PIPELINE_STATE_DESC psDesc = {};
    psDesc.pRootSignature = m_rootSig.Get();
    psDesc.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};

    hr = device->CreateComputePipelineState(&psDesc, IID_PPV_ARGS(&m_pso));
    if (FAILED(hr)) return fail("CreateComputePipelineState failed");
    return true;
}

bool MvecDensify::setTargets(ID3D12Device* device, ID3D12Resource* texGrid,
                             ID3D12Resource* texMvec, uint32_t width, uint32_t height,
                             uint32_t gridStep) {
    m_ready = false;
    m_w = width;
    m_h = height;
    m_gridStep = gridStep;
    m_texGrid = texGrid;
    m_texMvec = texMvec;

    if (!m_rootSig || !texGrid || !texMvec) return false;

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 2;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&m_heap)))) return false;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_heap->GetCPUDescriptorHandleForHeapStart();
    UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R16G16_SINT;
    device->CreateShaderResourceView(texGrid, &srv, cpu);
    cpu.ptr += inc;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format = DXGI_FORMAT_R16G16_FLOAT;
    device->CreateUnorderedAccessView(texMvec, nullptr, &uav, cpu);

    m_ready = true;
    return true;
}

void MvecDensify::record(ID3D12GraphicsCommandList* cmd) {
    if (!m_ready) return;
    using S = D3D12_RESOURCE_STATES;
    D3D12Ctx::barrierTo(cmd, m_texGrid.Get(), S::D3D12_RESOURCE_STATE_COMMON,
                        S::D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    D3D12Ctx::barrierTo(cmd, m_texMvec.Get(), S::D3D12_RESOURCE_STATE_COMMON,
                        S::D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmd->SetPipelineState(m_pso.Get());
    cmd->SetComputeRootSignature(m_rootSig.Get());
    unsigned p[4] = {m_w, m_h, m_gridStep, 0};
    cmd->SetComputeRoot32BitConstants(0, 4, p, 0);

    ID3D12DescriptorHeap* heaps[] = {m_heap.Get()};
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetComputeRootDescriptorTable(
        1, m_heap->GetGPUDescriptorHandleForHeapStart());

    cmd->Dispatch((m_w + 7u) / 8u, (m_h + 7u) / 8u, 1);

    D3D12Ctx::barrierTo(cmd, m_texGrid.Get(), S::D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                        S::D3D12_RESOURCE_STATE_COMMON);
    D3D12Ctx::barrierTo(cmd, m_texMvec.Get(), S::D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                        S::D3D12_RESOURCE_STATE_COMMON);
}
