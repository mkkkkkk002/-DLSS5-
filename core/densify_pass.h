#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

class D3D12Ctx;

// D3D12 dense up-sampler for the sparse NV-OF flow grid.
//
// The NV-OF session (D3D11) only delivers a sparse grid (step 4, ~0.5MB at 1080p). This compute
// pass bilinearly densifies that grid straight into the full-resolution R16G16_FLOAT motion
// texture (texMvec) on the main D3D12 queue, so the 8MB motion field never crosses the CPU:
// previously it was read back to system memory and re-uploaded every frame.
//
// The pass records into the same command list as the NGX evaluate (densify first, evaluate
// second), keeping the grid read -> motion write -> evaluate ordering inside one GPU submission.
class MvecDensify {
public:
    MvecDensify() = default;
    ~MvecDensify();

    MvecDensify(const MvecDensify&) = delete;
    MvecDensify& operator=(const MvecDensify&) = delete;

    // Compiles the shader and builds root signature + PSO. Cheap enough to call lazily once.
    bool init(ID3D12Device* device, const char** errOut);

    // Binds the grid source texture and the full-res motion UAV. Called whenever the working
    // textures are (re)built, i.e. when the resolution or the grid size changes.
    bool setTargets(ID3D12Device* device, ID3D12Resource* texGrid, ID3D12Resource* texMvec,
                    uint32_t width, uint32_t height, uint32_t gridStep);

    // Records the densify dispatch on an open command list. Ends with texGrid in COMMON and
    // texMvec in COMMON (ready for the NGX evaluate that reads it as it always has).
    void record(ID3D12GraphicsCommandList* cmd);

    bool ok() const { return m_ready; }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_heap;   // shader-visible: SRV t0 + UAV u0
    Microsoft::WRL::ComPtr<ID3D12Resource> m_texGrid, m_texMvec;
    uint32_t m_w = 0, m_h = 0, m_gridStep = 4;
    bool m_ready = false;
};
