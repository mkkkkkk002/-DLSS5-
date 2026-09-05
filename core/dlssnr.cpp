#include "dlssnr.h"

#include <cstdio>

DlssNr::~DlssNr() {
    release();
    if (m_mod) {
        FreeLibrary(m_mod);
        m_mod = nullptr;
    }
}

void DlssNr::setError(const char* msg) {
    m_lastError = msg;
    printf("[dlssnr] error: %s\n", msg);
}

bool DlssNr::load(const wchar_t* forwarderPath) {
    m_mod = LoadLibraryExW(forwarderPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_mod) {
        setError("LoadLibrary failed for the forwarder");
        return false;
    }

    m_create = reinterpret_cast<PFN_create>(GetProcAddress(m_mod, "dlssnr_call_create"));
    m_evaluate = reinterpret_cast<PFN_evaluate>(GetProcAddress(m_mod, "dlssnr_call_evaluate"));
    m_release = reinterpret_cast<PFN_release>(GetProcAddress(m_mod, "dlssnr_call_release"));
    m_setSlot = reinterpret_cast<PFN_set_slot>(GetProcAddress(m_mod, "dlssnr_call_set_float_slot"));
    m_probeFloat =
        reinterpret_cast<PFN_probe_float>(GetProcAddress(m_mod, "dlssnr_call_probe_float"));

    if (m_create) m_resolved |= 1;
    if (m_evaluate) m_resolved |= 2;
    if (m_release) m_resolved |= 4;
    if (m_setSlot) m_resolved |= 8;

    if (!m_create || !m_evaluate) {
        setError("forwarder is missing create or evaluate");
        return false;
    }

    // Our own block keeps floats at slot 1, which is where the shim already defaults. Setting it
    // explicitly rather than relying on that default, so the two cannot drift apart silently.
    if (m_setSlot) m_setSlot(1);

    return true;
}

bool DlssNr::create(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, void* params,
                    const wchar_t* snippetPath, uint32_t width, uint32_t height,
                    const DlssNrSettings& s) {
    if (!m_create) {
        setError("create was not resolved");
        return false;
    }

    m_feature = m_create(snippetPath, L".", device, cmd, params, width, height, s.preset,
                         s.intensity, s.style, s.localStructure, s.localTone, s.skinStructure,
                         s.useAutoMask, s.uiCorrection);

    // The shim publishes the raw NGX results as exported variables rather than through the
    // return value, so the reason a feature failed to appear is visible.
    int* pInit = reinterpret_cast<int*>(GetProcAddress(m_mod, "dlssnr_call_last_init"));
    int* pCreate = reinterpret_cast<int*>(GetProcAddress(m_mod, "dlssnr_call_last_create"));
    if (pInit) m_lastInit = *pInit;
    if (pCreate) m_lastCreate = *pCreate;

    if (!m_feature) {
        printf("[dlssnr] create failed: init=0x%08X create=0x%08X\n", (unsigned)m_lastInit,
               (unsigned)m_lastCreate);
        setError("feature creation returned null");
        return false;
    }

    return true;
}

int DlssNr::evaluate(ID3D12GraphicsCommandList* cmd, void* params, ID3D12Resource* color,
                     ID3D12Resource* depth, ID3D12Resource* motion, ID3D12Resource* output,
                     uint32_t width, uint32_t height, uint32_t guideWidth, uint32_t guideHeight,
                     const DlssNrSettings& s, bool reset) {
    if (!m_evaluate || !m_feature) return 0;

    // depthInverted 0, motion scales 1.0. Under Force Zero the motion texture is all zeros, so
    // the scale is inert; it is passed because the shim writes it unconditionally and a missing
    // value would be read as whatever was in the block before.
    return m_evaluate(cmd, m_feature, params, color, depth, motion, output, width, height,
                      guideWidth, guideHeight, 0, reset ? 1 : 0, s.intensity, s.style,
                      s.localStructure, s.localTone, s.skinStructure, s.useAutoMask, 1.0f, 1.0f);
}

void DlssNr::release() {
    if (m_release && m_feature) {
        m_release(m_feature);
    }
    m_feature = nullptr;
}
