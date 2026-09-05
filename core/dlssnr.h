#pragma once

#include <windows.h>
#include <d3d12.h>

#include <cstdint>

// The tuning the model latches when the feature is built. Everything here is read once, at
// create; values written only at evaluate are ignored. That is why changing any of these
// means rebuilding the feature rather than just passing them to the next evaluate.
struct DlssNrSettings {
    int preset = 0;           // DLSSNR.Hint.Render.Preset    0..3
    float intensity = 1.0f;   // DLSSNR.Intensity             0..2
    int style = 0;            // DLSSNR.Style                  0 default, 1 natural, 2 cinematic
    float localStructure = 1.0f;  // DLSSNR.LocalStructureStrength  0..2
    float localTone = 1.0f;       // DLSSNR.LocalToneStrength       0..2
    float skinStructure = 0.5f;   // DLSSNR.SkinStructureStrength  -1..2
    int useAutoMask = 1;          // DLSSNR.UseAutoMask
    int uiCorrection = 0;         // DLSSNR.UICorrection
};

// Post-process control: residual reconstruction multiplier (Magpie's DLSSNR does this on GPU).
// final = input + (modelOutput - input) * residualMult. 1.0 = pure model output; values >1
// amplify the model's detail contribution so the result stays closer to the source (like Magpie,
// which clamps this to 1.0..2.0 and defaults to ~1.1).
struct ResidualSettings {
    float multiplier = 1.0f;  // 1.0..2.0; 1.0 disables the blend (pure model output)
};

// Drives nvngx_dlssnr.dll through the caller-gate shim.
//
// The snippet refuses any caller whose module path does not contain "nvngx.dll", so it cannot be
// called from this executable directly. The shim (nvngx.dll_dlssnr.dll) exists only to satisfy
// that check; every NGX call originates from it.
class DlssNr {
public:
    ~DlssNr();

    // Loads the shim. The shim loads the snippet itself when the path is first passed to create.
    bool load(const wchar_t* forwarderPath);

    // Which exports resolved, as a bit field: create 1, evaluate 2, release 4, set_slot 8.
    int resolved() const { return m_resolved; }

    bool create(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, void* params,
                const wchar_t* snippetPath, uint32_t width, uint32_t height,
                const DlssNrSettings& s);

    int evaluate(ID3D12GraphicsCommandList* cmd, void* params, ID3D12Resource* color,
                 ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
                 uint32_t width, uint32_t height, uint32_t guideWidth, uint32_t guideHeight,
                 const DlssNrSettings& s, bool reset);

    void release();

    void* feature() const { return m_feature; }
    int lastInit() const { return m_lastInit; }
    int lastCreate() const { return m_lastCreate; }
    const char* lastError() const { return m_lastError; }

private:
    void setError(const char* msg);

    HMODULE m_mod = nullptr;
    void* m_feature = nullptr;
    int m_resolved = 0;
    int m_lastInit = 0;
    int m_lastCreate = 0;
    const char* m_lastError = "";

    // Signatures taken from the forwarder source in OptiScaler_DLSSNR.
    typedef void*(__cdecl* PFN_create)(const wchar_t*, const wchar_t*, ID3D12Device*,
                                       ID3D12GraphicsCommandList*, void*, unsigned int,
                                       unsigned int, int, float, int, float, float, float, int,
                                       int);
    typedef int(__cdecl* PFN_evaluate)(ID3D12GraphicsCommandList*, void*, void*, ID3D12Resource*,
                                       ID3D12Resource*, ID3D12Resource*, ID3D12Resource*,
                                       unsigned int, unsigned int, unsigned int, unsigned int, int,
                                       int, float, int, float, float, float, int, float, float);
    typedef void(__cdecl* PFN_release)(void*);
    typedef void(__cdecl* PFN_set_slot)(int);
    typedef void(__cdecl* PFN_probe_float)(void*, const char*, float, int);

    PFN_create m_create = nullptr;
    PFN_evaluate m_evaluate = nullptr;
    PFN_release m_release = nullptr;
    PFN_set_slot m_setSlot = nullptr;
    PFN_probe_float m_probeFloat = nullptr;
};
