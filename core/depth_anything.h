#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "d3d12_ctx.h"

// DepthAnything V2 (fp16) depth inference via ONNX Runtime with the DirectML execution
// provider. Runs on the same machine using the bundled onnxruntime.dll / DirectML.dll /
// model_fp16.onnx (same model Magpie uses for DLSSNR Frame Guidance depth).
//
// The model wants a small normalized RGB input (long side 336 in Magpie); we downscale the
// current frame on the CPU into the ONNX tensor, run inference, and receive a depth map back
// on the CPU which the caller uploads as the DLSSNR depth texture. Inference is only scheduled
// every `interval` frames (Magpie default 4) because DLSSNR uses depth as low-frequency
// guidance and NV-OF already keeps the temporal continuity.
class DepthAnything {
public:
    DepthAnything() = default;
    ~DepthAnything();

    DepthAnything(const DepthAnything&) = delete;
    DepthAnything& operator=(const DepthAnything&) = delete;

    // Loads onnxruntime + DirectML and the model. dllDir is the UTF-8 directory holding the
    // DLLs and model_fp16.onnx (it is widened internally for the wide Win32/ORT APIs).
    // fullW/fullH is the working-resolution frame size; interval = inference every N frames.
    bool init(const std::string& dllDir, uint32_t fullW, uint32_t fullH, uint32_t interval);

    bool ok() const { return m_ok; }
    const char* lastError() const { return m_err.c_str(); }

    // Feed the current RGBA8 frame (fullW*fullH*4). Writes a depth buffer into outDepth
    // (fullW*fullH floats; 0 far .. 1 near, matching DLSSNR DepthInverted=1 convention).
    // Returns false on hard failure.
    bool feed(const uint8_t* rgba, float* outDepth);

    bool needsUpdate() const;  // true on the first frame and every `interval` frames

private:
    bool loadOrt();
    void destroy();

    bool m_ok = false;
    std::string m_err;
    std::wstring m_dllDirW;

    void* m_ortModule = nullptr;
    void* m_dmlModule = nullptr;

    // ORT handles (typed pointers from the C API header).
    void* m_env = nullptr;
    void* m_sessionOptions = nullptr;
    void* m_session = nullptr;
    void* m_memoryInfo = nullptr;

    int64_t m_inW = 0, m_inH = 0;      // inference input resolution
    std::string m_inputName = "image";
    std::string m_outputName = "depth";
    std::vector<float> m_inputTensor;   // NCHW float, normalized
    std::vector<float> m_outputTensor;  // depth map at m_inW*m_inH

    std::vector<float> m_depth;
    uint32_t m_fullW = 0, m_fullH = 0;
    uint32_t m_interval = 4;
    uint32_t m_frameCount = 0;
    bool m_hasDepth = false;
    bool m_ortInited = false;
};
