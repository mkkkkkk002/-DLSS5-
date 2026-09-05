// DepthAnything V2 depth inference via ONNX Runtime + DirectML.

#include "depth_anything.h"

#include <cstring>
#include <cmath>
#include <algorithm>

#include <windows.h>
#undef min
#undef max

// The onnxruntime C API header defines typed opaque structs (OrtEnv*, OrtSession*, ...) and the
// OrtApi function table. DirectML EP is appended via the DLL's exported
// OrtSessionOptionsAppendExecutionProvider_DML symbol rather than the OrtApi table.
#include "depth/onnxruntime_c_api.h"

namespace {

constexpr float MEAN[3] = { 0.485f, 0.456f, 0.406f };
constexpr float STD[3]  = { 0.229f, 0.224f, 0.225f };

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

using OrtStatusPtr = OrtStatus*;
using PFN_AppendDML = OrtStatusPtr(ORT_API_CALL*)(OrtSessionOptions* options, int device_id);

}  // namespace

DepthAnything::~DepthAnything() { destroy(); }

bool DepthAnything::init(const std::string& dllDir, uint32_t fullW, uint32_t fullH,
                         uint32_t interval) {
    // widen UTF-8 dir once for all wide Win32/ORT calls
    if (!dllDir.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, dllDir.c_str(), (int)dllDir.size(), nullptr, 0);
        m_dllDirW.assign((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, dllDir.c_str(), (int)dllDir.size(), &m_dllDirW[0], n);
    }
    m_fullW = fullW;
    m_fullH = fullH;
    m_interval = std::max(1u, interval);
    if (!fullW || !fullH) { m_err = "bad full res"; return false; }

    const uint32_t LONG = 336;
    if (fullW >= fullH) {
        m_inW = LONG;
        m_inH = (int64_t)std::max(1, (int)(fullH * LONG / fullW / 14) * 14);
    } else {
        m_inH = LONG;
        m_inW = (int64_t)std::max(1, (int)(fullW * LONG / fullH / 14) * 14);
    }

    m_inputTensor.assign((size_t)3 * m_inW * m_inH, 0.f);
    m_outputTensor.assign((size_t)m_inW * m_inH, 0.f);
    m_depth.assign((size_t)fullW * fullH, 0.f);

    if (!loadOrt()) return false;
    m_ok = true;
    m_hasDepth = false;
    m_frameCount = 0;
    return true;
}

bool DepthAnything::loadOrt() {
    const std::wstring ortPath = m_dllDirW + L"\\onnxruntime.dll";
    const std::wstring dmlPath = m_dllDirW + L"\\DirectML.dll";
    // Make the depth dir part of the DLL search path so onnxruntime can resolve its sibling
    // providers and DirectML.dll even when the exe runs from another working directory.
    SetDllDirectoryW(m_dllDirW.c_str());
    m_ortModule = (void*)LoadLibraryExW(ortPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_ortModule) {
        char buf[256];
        snprintf(buf, sizeof(buf), "onnxruntime.dll load failed (err=%lu)",
                 (unsigned long)GetLastError());
        m_err = buf;
        return false;
    }
    m_dmlModule = (void*)LoadLibraryExW(dmlPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_dmlModule) {
        char buf[256];
        snprintf(buf, sizeof(buf), "DirectML.dll load failed (err=%lu)",
                 (unsigned long)GetLastError());
        m_err = buf;
        return false;
    }

    auto fnGetApiBase = (const OrtApiBase* (ORT_API_CALL*)())GetProcAddress(
        (HMODULE)m_ortModule, "OrtGetApiBase");
    if (!fnGetApiBase) { m_err = "OrtGetApiBase missing"; return false; }
    const OrtApi* api = fnGetApiBase()->GetApi(ORT_API_VERSION);
    if (!api) { m_err = "ORT api unavailable"; return false; }

    // env
    OrtEnv* env = nullptr;
    OrtStatusPtr st = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "dlss5nr-depth", &env);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    m_env = env;

    // session options
    OrtSessionOptions* so = nullptr;
    st = api->CreateSessionOptions(&so);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    m_sessionOptions = so;
    api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
    api->SetIntraOpNumThreads(so, 1);

    // append DirectML via the exported function
    auto pfnAppend = (PFN_AppendDML)GetProcAddress((HMODULE)m_ortModule,
                                                   "OrtSessionOptionsAppendExecutionProvider_DML");
    if (pfnAppend) {
        OrtStatusPtr dmlSt = pfnAppend(so, 0);
        if (dmlSt) {
            m_err = std::string("DML append failed: ") + api->GetErrorMessage(dmlSt);
            api->ReleaseStatus(dmlSt);
            return false;
        }
    } else {
        // Fall back to CPU EP (slow but functional).
        m_err = "DML append symbol missing; falling back to CPU";
    }

    // session (model path) — m_dllDirW is already a wide UTF-8 directory
    const std::wstring modelPath = m_dllDirW + L"\\model_fp16.onnx";
    OrtSession* sess = nullptr;
    st = api->CreateSession(env, modelPath.c_str(), so, &sess);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    m_session = sess;

    // CPU memory info for input/output tensors
    OrtMemoryInfo* mi = nullptr;
    st = api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mi);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    m_memoryInfo = mi;

    // Discover actual model input/output node names (they differ across exports).
    OrtAllocator* allocator = nullptr;
    api->GetAllocatorWithDefaultOptions(&allocator);
    char* inName = nullptr;
    size_t inNameLen = 0;
    st = api->SessionGetInputName(sess, 0, allocator, &inName);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    char* outName = nullptr;
    size_t outNameLen = 0;
    st = api->SessionGetOutputName(sess, 0, allocator, &outName);
    if (st) { m_err = api->GetErrorMessage(st); api->ReleaseStatus(st); return false; }
    m_inputName = inName ? inName : "image";
    m_outputName = outName ? outName : "depth";
    fprintf(stderr, "[depth] model IO: '%s' -> '%s'\n", m_inputName.c_str(), m_outputName.c_str());
    if (allocator) {
        if (inName) api->AllocatorFree(allocator, inName);
        if (outName) api->AllocatorFree(allocator, outName);
    }

    m_ortInited = true;
    return true;
}

bool DepthAnything::needsUpdate() const {
    return !m_hasDepth || (m_interval > 0 && m_frameCount % m_interval == 0);
}

bool DepthAnything::feed(const uint8_t* rgba, float* outDepth) {
    if (!m_ok || !rgba) return false;
    if (outDepth) {
        if (m_hasDepth) std::memcpy(outDepth, m_depth.data(), m_depth.size() * 4);
        else std::memset(outDepth, 0, m_depth.size() * 4);
    }
    if (!needsUpdate()) { m_frameCount++; return true; }

    // --- preprocess to normalized NCHW at inference resolution ---
    const int64_t W = m_inW, H = m_inH;
    for (int64_t y = 0; y < H; ++y) {
        for (int64_t x = 0; x < W; ++x) {
            float sx = ((float)x + 0.5f) * (float)m_fullW / (float)W - 0.5f;
            float sy = ((float)y + 0.5f) * (float)m_fullH / (float)H - 0.5f;
            int ix = (int)clampf(std::floor(sx), 0.f, (float)m_fullW - 1.f);
            int iy = (int)clampf(std::floor(sy), 0.f, (float)m_fullH - 1.f);
            const uint8_t* p = rgba + ((size_t)iy * m_fullW + ix) * 4;
            float r = p[0] / 255.f, g = p[1] / 255.f, b = p[2] / 255.f;
            size_t o = (size_t)y * W + x;
            m_inputTensor[o]                       = (r - MEAN[0]) / STD[0];
            m_inputTensor[(size_t)W * H + o]       = (g - MEAN[1]) / STD[1];
            m_inputTensor[(size_t)2 * W * H + o]   = (b - MEAN[2]) / STD[2];
        }
    }

    if (!m_ortInited) return false;
    auto fnGetApiBase = (const OrtApiBase* (ORT_API_CALL*)())GetProcAddress(
        (HMODULE)m_ortModule, "OrtGetApiBase");
    const OrtApi* ort = fnGetApiBase()->GetApi(ORT_API_VERSION);
    if (!ort) return false;

    const char* inputName = m_inputName.c_str();
    const char* outputName = m_outputName.c_str();
    int64_t inShape[4] = { 1, 3, H, W };
    OrtValue* inputVal = nullptr;
    OrtStatusPtr st = ort->CreateTensorWithDataAsOrtValue(
        (OrtMemoryInfo*)m_memoryInfo, m_inputTensor.data(),
        m_inputTensor.size() * sizeof(float), inShape, 4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inputVal);
    if (st) { m_err = ort->GetErrorMessage(st); ort->ReleaseStatus(st); return false; }

    OrtValue* outputVal = nullptr;
    st = ort->Run((OrtSession*)m_session, nullptr, &inputName, &inputVal, 1,
                  &outputName, 1, &outputVal);
    if (st) {
        m_err = ort->GetErrorMessage(st);
        ort->ReleaseStatus(st);
        ort->ReleaseValue(inputVal);
        return false;
    }

    // read output into m_outputTensor
    OrtTensorTypeAndShapeInfo* info = nullptr;
    ort->GetTensorTypeAndShape(outputVal, &info);
    size_t dimCount = 0;
    ort->GetDimensionsCount(info, &dimCount);
    std::vector<int64_t> dims(std::max<size_t>(dimCount, 1));
    ort->GetDimensions(info, dims.data(), dimCount);
    size_t n = 1;
    for (size_t i = 0; i < dimCount; ++i) n *= (size_t)dims[i];
    float* outData = nullptr;
    st = ort->GetTensorMutableData(outputVal, (void**)&outData);
    if (!st && outData && n > 0) {
        size_t copyN = std::min(n, m_outputTensor.size());
        std::memcpy(m_outputTensor.data(), outData, copyN * sizeof(float));
    }
    if (info) ort->ReleaseTensorTypeAndShapeInfo(info);
    ort->ReleaseValue(inputVal);
    ort->ReleaseValue(outputVal);
    if (st) { m_err = ort->GetErrorMessage(st); ort->ReleaseStatus(st); return false; }

    // --- upscale depth to full res, normalize to [0,1], near=1 far=0 ---
    float mn = m_outputTensor[0], mx = m_outputTensor[0];
    for (float v : m_outputTensor) { if (v < mn) mn = v; if (v > mx) mx = v; }
    float range = (mx - mn) > 1e-6f ? (mx - mn) : 1.f;
    for (uint32_t y = 0; y < m_fullH; ++y) {
        for (uint32_t x = 0; x < m_fullW; ++x) {
            float u = ((float)x + 0.5f) * (float)W / (float)m_fullW - 0.5f;
            float v = ((float)y + 0.5f) * (float)H / (float)m_fullH - 0.5f;
            int x0 = (int)clampf(std::floor(u), 0.f, (float)W - 1.f);
            int y0 = (int)clampf(std::floor(v), 0.f, (float)H - 1.f);
            int x1 = std::min(x0 + 1, (int)W - 1), y1 = std::min(y0 + 1, (int)H - 1);
            float tx = clampf(u - std::floor(u), 0.f, 1.f);
            float ty = clampf(v - std::floor(v), 0.f, 1.f);
            float d00 = m_outputTensor[(size_t)y0 * W + x0];
            float d10 = m_outputTensor[(size_t)y0 * W + x1];
            float d01 = m_outputTensor[(size_t)y1 * W + x0];
            float d11 = m_outputTensor[(size_t)y1 * W + x1];
            float d = (d00 * (1 - tx) + d10 * tx) * (1 - ty) +
                      (d01 * (1 - tx) + d11 * tx) * ty;
            m_depth[(size_t)y * m_fullW + x] = (d - mn) / range;
        }
    }
    m_hasDepth = true;
    m_frameCount++;
    if (outDepth) std::memcpy(outDepth, m_depth.data(), m_depth.size() * 4);
    return true;
}

void DepthAnything::destroy() {
    const OrtApi* api = nullptr;
    if (m_ortModule) {
        auto fnGetApiBase = (const OrtApiBase* (ORT_API_CALL*)())GetProcAddress(
            (HMODULE)m_ortModule, "OrtGetApiBase");
        if (fnGetApiBase) api = fnGetApiBase()->GetApi(ORT_API_VERSION);
    }
    if (api) {
        if (m_session) api->ReleaseSession((OrtSession*)m_session);
        if (m_sessionOptions) api->ReleaseSessionOptions((OrtSessionOptions*)m_sessionOptions);
        if (m_env) api->ReleaseEnv((OrtEnv*)m_env);
        if (m_memoryInfo) api->ReleaseMemoryInfo((OrtMemoryInfo*)m_memoryInfo);
    }
    if (m_dmlModule) FreeLibrary((HMODULE)m_dmlModule);
    if (m_ortModule) FreeLibrary((HMODULE)m_ortModule);
    m_session = m_sessionOptions = m_env = m_memoryInfo = nullptr;
    m_ortModule = m_dmlModule = nullptr;
    m_ortInited = false;
    m_ok = false;
}
