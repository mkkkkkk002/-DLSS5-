#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

class D3D12Ctx;

// GPU finalisation pass that replaces the old CPU finalizeToRgba8().
//
// It blends the 16F neural-render result (texNR) against the original 8-bit frame (texColor)
// using residualMult, then quantises to 8-bit exactly once with a 4x4 Bayer ordered dither -
// all in one pixel-shader pass. The CPU never sees the 16F readback any more: texNR stays on
// the GPU and only the final 8-bit texture is downloaded, which also halves the readback
// traffic. The pass runs inside the same command list as the NGX evaluate so there is no
// extra GPU sync point between them.
class FinalBlender {
public:
    FinalBlender() = default;
    ~FinalBlender();

    FinalBlender(const FinalBlender&) = delete;
    FinalBlender& operator=(const FinalBlender&) = delete;

    // Compiles the shaders and builds root signature + PSO. Cheap enough to call lazily once.
    bool init(ID3D12Device* device, const char** errOut);

    // Binds the three working textures and the render size. Called whenever the texture group
    // is (re)built (i.e. when the working resolution changes).
    bool setTargets(ID3D12Device* device, ID3D12Resource* texColor, ID3D12Resource* texNR,
                    ID3D12Resource* texOut, uint32_t width, uint32_t height);

    // Records the blend + dither draw on an open command list. Resource states are restored so
    // texColor ends in COMMON and texNR/texOut end in UNORDERED_ACCESS (ready for the next
    // NGX evaluate / a following downloadTex respectively).
    void record(ID3D12GraphicsCommandList* cmd, float residualMult);

    bool ok() const { return m_ready; }

private:
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pso;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // shader-visible SRVs t0/t1
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;   // one RTV
    Microsoft::WRL::ComPtr<ID3D12Resource> m_texColor, m_texNR, m_texOut;
    uint32_t m_w = 0, m_h = 0;
    bool m_firstRender = true;   // texOut starts in COMMON, then lives in UNORDERED_ACCESS
    bool m_ready = false;
};
