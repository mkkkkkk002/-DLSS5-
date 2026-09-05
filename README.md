# DLSS5NR — DLSS 5 Neural Rendering 离线视频/图片处理器

用 NVIDIA DLSS 5 Neural Renderer（DLSS NR）神经网络对**视频与图片做逐帧画质增强**的本地工具。
纯本地运算，浏览器操作界面，无需安装。

> 本仓库只包含**源代码**。模型 dll、NV-OF/NGX 转发器、深度推理运行库、ffmpeg/node
> 等第三方二进制**不在此分发**（体积与许可原因），获取方式见下文「运行依赖」。

---

## 功能

- 整段视频神经渲染（时间窗口、多遍渲染、编码器可选）
- 单帧对比预览 + 图片渲染（拖入 / Ctrl+V 粘贴 / 选文件）
- 硬件光流 NV-OF 运动引导；FP16 / FP8 模型切换（Auto/FP16/FP8）
- 深度推理可选（DeepAnything v2 via ONNX Runtime + DirectML；间隔可调）
- 引擎 16 位浮点输出 + 抖动降位到 8bit，避免去噪后的量化色带
- 残差倍率 / 局部色调 / 结构 / 皮肤细节 / 自动遮罩 / UI 修正等 NR 参数
- 命名参数预设（浏览器本地保存）+ 上次参数自动记忆

## 目录结构

```
core/                   C++ 处理引擎（D3D12 + ffmpeg 管道）
  main.cpp              入口：CLI、任务/参数解析、渲染主循环
  d3d12_ctx.*           渲染上下文、纹理上传/回读
  dlssnr.*              NGX DLSS NR feature 加载与调用
  ngx_params.*          NGX 参数块构造
  nvof_flow.*           NVIDIA 硬件光流(NV-OF, D3D11) → 稀疏网格
  densify_pass.*        D3D12 计算着色器：稀疏网格 → 全分辨率运动场
  blend_pass.*          GPU 残差混合 + Bayer 抖动降位
  depth_anything.*      深度推理（可选，ONNX Runtime + DirectML）
  video_pipe.*          ffmpeg 解码/编码子进程封装
  build.sh              MSVC 构建脚本
  nvof/                 NVIDIA Optical Flow SDK 头文件（宽松许可，见其头文件声明）
  depth/onnxruntime_c_api.h   ONNX Runtime C API 头文件（MIT），仅编译用
web/                    浏览器界面 + 本地服务（node，无第三方依赖）
server_guard.c          启动守护（关窗即清临时缓存），编译见文件头注释
start_ui.bat            开发环境启动脚本
```

## 构建（Windows）

要求：Visual Studio 2022（MSVC `cl.exe`，x64）、Windows 10 SDK、Git-Bash 或类似 bash。

```bash
cd core
bash build.sh          # 产物 core/dlss5nr_engine.exe
```

引擎通过 ffmpeg 子进程做解码/编码，运行期还需要 PATH 里有 `ffmpeg`/`ffprobe` 与 `node`。

## 运行依赖（不随仓库分发）

| 组件 | 用途 | 获取/放置 |
|---|---|---|
| NVIDIA 驱动 | NGX、NV-OF、NVENC | 官网最新驱动（含 nvofapi64.dll / nvngx 运行库） |
| NR 模型 dll | DLSS NR fp16/fp8 权重 | 自行按合法渠道获取，置于 `models/` |
| 转发器 dll | 适配老卡调用（社区构建） | 自行获取，置于 `models/`（`nvngx.dll_dlssnr_*.dll`） |
| ffmpeg / node | 音视频管道 / Web UI | 官网或任意发行渠道，加入 PATH |
| 深度推理（可选） | DeepAnything v2 | `onnxruntime.dll`+`DirectML.dll`+`model_fp16.onnx` 放 `core/depth/`；缺失时引擎自动跳过深度 |

`models/` 期望文件名（引擎启动时探测，可经 `--snippet/--forwarder` 覆盖）：
`nvngx_dlssnr_fp16.dll`、`nvngx_dlssnr_fp8.dll` 与对应 `nvngx.dll_dlssnr_fp16.dll` / `nvngx.dll_dlssnr_fp8.dll`。

## 运行

开发环境：

```bat
start_ui.bat        REM 拉起 server_guard → node web/server.js --open
```

浏览器打开 http://127.0.0.1:8777。命令行直接跑引擎示例：

```bash
core/dlss5nr_engine.exe --input in.mp4 --output out.mp4 \
  --encoder h264_nvenc --residual-mult 1.0 --frame-guidance 3 \
  --end-time 5 --perf        # 整片渲染；--perf 打印分阶段耗时
```

参数一览（`--help` 查看完整列表）：`--snippet --forwarder --encoder --codec-args --pix-fmt
--start-time --end-time --preset --intensity --style --local-tone --local-structure
--skin-structure --auto-mask --ui-correction --residual-mult --frame-guidance
--depth-interval --dump-frame --frame-reset --hw-decode --daemon`。

## 兼容性提示

- 20/30 系显卡用 FP16 模型；50 系用 FP8；40 系两者皆可试。选错只会报错退出，不损坏文件。
- 光流(NV-OF)初始化失败通常是驱动过旧。非 NVIDIA 独显无法运行。
- 性能参考（2080Ti / fp16 / 默认参数）：720P ≈ 20–24 fps，1080P ≈ 10–12 fps；
  帧耗时大头是 NR 模型推理本身（1080P 约 45–48 ms/帧）。

## 许可与致谢

本项目源码以 **GPL-3.0** 发布（见 `LICENSE`），原因：其 DLSSNR 接入思路参考了同为
GPL-3.0 的 [Magpie](https://github.com/SAOG0721/Magpie) 实验分支与
OptiScaler 社区的 DLSSNR 实现。界面、引擎与视频处理流程为本项目独立编写。

- `core/nvof/` 头文件：Copyright (c) 2018-2023 NVIDIA Corporation，宽松许可（见文件头）。
- `core/depth/onnxruntime_c_api.h`：ONNX Runtime 项目头文件，MIT 许可。
- 随附/接入的**模型与转发器二进制属于社区/原作者作品**，版权归原作者所有，未在本仓库
  分发；请自行获取并遵守其许可与 NVIDIA 软件许可条款，勿用于商业用途。

> 再分发或商用前，请自行完成对上述第三方组件的许可与合规核查。
