# QTree-EJMDK

> Trilingual README for both the **theoretical algorithm design** and the **current working Windows Vulkan CLI prototype**.
>
> 本文档同时说明两层内容：
> 1. **算法/论文层**：QTree-EJMDK 的完整理论设计；
> 2. **工程/仓库层**：当前仓库中已经能运行的 C++ / Vulkan / CLI 原型。
>
> This document covers two layers at once:
> 1. the **research design** of QTree-EJMDK, and
> 2. the **practical C++ / Vulkan / CLI prototype** implemented in this repository.
>
> このドキュメントは、
> 1. **QTree-EJMDK の理論設計**と、
> 2. このリポジトリで実際に動作する **C++ / Vulkan / CLI プロトタイプ**
> の両方を説明します。

## Navigation

- [中文版](#中文版)
- [English Version](#english-version)
- [日本語版](#日本語版)
- [Shared Shader Skeleton / 共享着色器骨架 / 共通シェーダースケルトン](#shared-shader-skeleton--共享着色器骨架--共通シェーダースケルトン)

---

## 中文版

### 1. 项目概览

**QTree-EJMDK：基于四叉树注意力匹配与高效联合运动-细节核的一体化实时帧生成与超分辨率框架**（最新优化版 2026.04）

QTree-EJMDK 试图解决一个核心问题：

> 能否把**帧生成（Frame Generation, FG）**与**超分辨率（Super-Resolution, SR）**从传统的“两阶段串联处理”改造成“一个统一的、内容自适应的、GPU 友好的单一 pipeline”？

为此，理论设计将以下几类思想合并起来：

- **QDM (Quadtree Diffusion Model, 2025)**：提供四叉树驱动的自适应区域划分；
- **Mob-FGSR (SIGGRAPH 2024)**：提供移动端友好的 FG + SR 轻量级 warping / blending 思路；
- **Attention Matching (2026 arXiv)**：提供闭式注意力匹配权重；
- **PolarQuant / TurboQuant (2026 系列)**：提供在线旋转量化和残差修正；
- **EJMDK (Efficient Joint Motion-Detail Kernel)**：将运动补偿、帧融合与细节增强合并到单次核采样中。

本仓库同时包含两层东西：

1. **完整算法蓝图**：README 中详细解释理论版 QTree-EJMDK；
2. **可运行工程原型**：仓库中已经实现了一个可以在 Windows 下直接命令行处理视频文件的 `ejmdk.exe`。

### 2. 当前仓库已经实现了什么

当前仓库提供了一个**Windows 下可直接命令行处理视频文件的单可执行工具**：`ejmdk.exe`。

它不是外部脚本拼接的 filter chain，而是一个原生 C++ / Vulkan 管线：

- **Media Foundation**：负责 MP4 / H.264 解码与重新编码；
- **Vulkan Compute**：负责 SR + FG 核心计算；
- **CPU 侧四叉树分析**：根据空间方差与前后帧差异构造 `NodeMap`；
- **构建期嵌入 SPIR-V**：shader 编译后直接嵌入 exe，运行时不依赖额外 `.spv` 文件；
- **CLI 入口**：支持直接在命令行中传入输入文件路径处理视频。

> 这里的“单一二进制”是工程意义上的：目标是交付一个**主程序 `ejmdk.exe`** 作为运行入口，不需要 Python、单独 shader 文件或外部 hook 脚本参与运行。系统级依赖仍然包括 Windows 自带的 Media Foundation 与显卡驱动提供的 Vulkan Runtime。

### 3. 重要说明：理论版与仓库版不是一回事

为了避免“README 写得像完整论文实现，但仓库里其实只是原型”的误会，这里明确区分：

| 模块 | 理论版 QTree-EJMDK | 当前仓库实现状态 |
| --- | --- | --- |
| 四叉树自适应分区 | 完整设计 | **已实现工程近似版** |
| 注意力匹配闭式权重 | 理论核心模块 | **未完整实现为独立模块** |
| PolarQuant / TurboQuant | 理论设计中重要部分 | **未完整实现为独立模块** |
| EJMDK 联合运动-细节核 | 理论核心模块 | **已实现工程近似版** |
| 独立 Fusion + Residual Pass | 理论完整链路的一部分 | **当前实现合并进主 compute 逻辑** |
| 时序复用缓存 | 理论设计包含 | **尚未完整实现** |
| 单 exe 命令行完整视频处理 | 工程目标 | **已实现** |

换句话说：

- **README 中的算法部分**描述的是完整的、偏论文级的 QTree-EJMDK 设计；
- **仓库中的代码**实现的是一个以工程可落地为优先的 **C++ / Vulkan CLI 原型**，它保留了 QTree + 联合 FG/SR 的主干思想，但尚未复刻全部论文设想模块。

### 4. 算法详细说明（理论版）

#### 4.1 摘要

本文提出一种新型的实时帧生成（Frame Generation, FG）与超分辨率（Super-Resolution, SR）一体化 Pipeline——QTree-EJMDK。该框架将 QDM（Quadtree Diffusion Model, 2025）的四叉树自适应稀疏处理、Mob-FGSR（SIGGRAPH 2024）的移动端轻量 FG warping 与 blending、Attention Matching（2026 arXiv）的闭式注意力匹配，以及 PolarQuant / TurboQuant（2026 系列）的在线旋转量化技术无缝融合，并创新性地引入 **Efficient Joint Motion-Detail Kernel（EJMDK）** 算子，实现 FG 与 SR 在 Per-Node 阶段的单次加权采样融合。

针对原始 5×5 暴力循环带来的高寄存器压力、重复采样与 occupancy 下降，理论设计进一步给出**生产级优化实现**：采用 LDS（groupshared memory）分块加载 + 直方图重心量化 + 即算即用动态权重生成，将采样次数从 50 次/像素降至可控范围，同时通过对称性与 separable 分解进一步降低指令流。

理论分析表明，优化后 pipeline 在现代 GPU 上的目标延迟可稳定控制在 0.8–2.2 ms / frame（1024p，理想优化条件），较传统两阶段方法在计算量上可降低 65% 以上，同时尽可能保留高时序稳定性和细节保真度。

#### 4.2 关键词

- 实时超分辨率
- 帧生成
- 四叉树自适应
- 注意力匹配
- 联合运动-细节核
- LDS 分块优化
- 直方图量化
- GPU compute shader
- 寄存器压力控制

#### 4.3 引言与问题背景

在实时渲染领域（如游戏、VR / AR、移动掌机），帧生成（FG）与超分辨率（SR）是突破计算瓶颈的关键技术。传统分离式 pipeline（如先做插帧再做超分，或先做超分再做时序融合）通常存在：

- 中间结果读写开销大；
- 带宽冗余明显；
- 时序伪影与 halo 更难统一处理；
- 高复杂区域和低复杂区域无法按内容自适应分配算力。

QTree-EJMDK 的核心思路是：

1. 用四叉树先找出“值得花算力”的区域；
2. 对 leaf node 提取运动-细节联合特征；
3. 在单个动态核里同时完成运动补偿、帧融合、细节增强与超分采样；
4. 再通过注意力加权融合与残差修正提高稳定性。

#### 4.4 记号定义

| 记号 | 含义 |
| --- | --- |
| $I_{t-1}$ | 前一帧图像 |
| $I_t$ | 当前帧图像 |
| $\mathbf{p}$ | 当前输出像素位置 |
| $\mathbf{f}$ | 局部特征向量（运动、细节、纹理、时域统计等） |
| $\hat{\mathbf{f}}$ | 量化或压缩后的局部特征 |
| $\mathbf{m}(\hat{\mathbf{f}})$ | 由特征估计得到的局部运动偏移 |
| $\beta(\hat{\mathbf{f}})$ | previous / current frame 混合系数 |
| $K_{ij}(\hat{\mathbf{f}})$ | 由特征驱动生成的动态采样核权重 |
| $r$ | 核半径，例如 $r = 2$ 对应 $5\times5$ |

#### 4.5 整体 Pipeline

理论版 QTree-EJMDK 可分为 4 个 compute shader pass：

1. **Tree Build Pass**：构建四叉树并确定 leaf node；
2. **Per-Node EJMDK Pass**：对每个 leaf node 做联合 FG + SR；
3. **Fusion + Residual Pass**：注意力加权融合与 TurboQuant 残差修正；
4. **Temporal Reuse**：在跨帧相似时跳过部分重建计算。

实际工程实现时，也可以把其中若干 pass 合并，以减少中间 buffer 与调度成本。

#### 4.6 Tree Build Pass：四叉树构建与自适应细分

Tree Build Pass 的目标是：

- 在平坦区域保持大块 leaf，减少不必要计算；
- 在高频纹理或高运动区域细分到更小块，提高局部建模精度。

一个典型的空间方差定义为：

$$
\sigma_R^2 = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left(I_t(\mathbf{p}) - \mu_R\right)^2
$$

其中 $R$ 是一个候选区域，$\mu_R$ 为区域均值。

一个简单的时域差异项可以定义为：

$$
d_R = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left|I_t(\mathbf{p}) - I_{t-1}(\mathbf{p})\right|
$$

理论版还可引入注意力匹配相似度 $s_{attn}(R)$，于是细分规则可写为：

$$
\text{split}(R) = \left(\sigma_R^2 > \tau_{var}\right) \lor \left(s_{attn}(R) < \tau_{match}\right)
$$

在当前仓库的工程实现中，采用的是：

- **空间方差** + **时域差异**
- 递归 CPU 四叉树构建
- 输出逐像素 `NodeMap`

`NodeMap` 当前包含 4 个通道：

- `detailStrength`
- `temporalConfidence`
- `searchRadius`
- `leafRefinement`

它们不是最终论文版中的全部特征，但足够驱动一个工程可运行的自适应联合 SR + FG pipeline。

#### 4.7 PolarQuant / TurboQuant 与局部特征压缩

理论版 QTree-EJMDK 中，局部特征 $\mathbf{f}$ 会经过量化以降低内存带宽和寄存器压力。设量化后特征为：

$$
\hat{\mathbf{f}} = Q(\mathbf{f})
$$

其中 $Q(\cdot)$ 可来自：

- PolarQuant：旋转后量化，尽量减少信息损失；
- Histogram Centroid Quantization：用直方图重心近似替代在线聚类；
- TurboQuant：为后续残差修正提供更稳定的压缩表示。

在当前仓库实现中，这部分尚未作为独立模块完整实现；取而代之的是：

- 局部统计特征
- leaf refinement 级别
- motion search 半径
- temporal confidence

这些特征被打包进 `NodeMap`，供 Vulkan compute shader 在运行时直接使用。

#### 4.8 Per-Node Pass：优化版 EJMDK 核心算子

EJMDK（Efficient Joint Motion-Detail Kernel）是整个框架最关键的统一算子。它将以下操作合并为一次特征驱动的局部采样：

- motion compensation
- previous / current frame blending
- detail-aware enhancement
- super-resolution upsampling

其核心形式为：

$$
O(\mathbf{p}) = \sum_{i,j=-r}^{r} K_{ij}(\hat{\mathbf{f}}) \cdot \Bigl[
\beta(\hat{\mathbf{f}}) \cdot I_{t-1}\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
+
(1 - \beta(\hat{\mathbf{f}})) \cdot I_t\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
\Bigr]
$$

其中：

- $K_{ij}(\hat{\mathbf{f}})$：动态采样核，由特征生成；
- $\mathbf{m}(\hat{\mathbf{f}})$：局部运动偏移；
- $\beta(\hat{\mathbf{f}})$：前后帧融合比例。

在生产级优化中，重点不是把公式写得漂亮，而是把它写成 GPU 真能高 occupancy 跑得动的样子。因此引入以下工程策略：

1. **LDS / groupshared tile loading**
   - 将局部 patch 加载到共享内存，避免重复采样；
   - 典型 tile 带 `2-pixel halo`，适配 $5\times5$ 邻域。

2. **Histogram centroid quantization**
   - 用确定性更强、分支更少的重心量化代替在线 k-means；
   - 降低 shader 中不必要的动态控制流。

3. **即算即用动态权重**
   - 避免 `float4 kernel[5][5]` 这类寄存器杀手；
   - 用 `ComputeDynamicWeight(qFeat, dx, dy)` 形式现场生成权重。

4. **Separable decomposition（进一步优化方向）**
   - 若核近似可分离，则 $5\times5$ 可拆成两次 1D；
   - 理论上可进一步降低指令流和寄存器压力。

#### 4.9 Fusion Pass：Attention-weighted Fusion + TurboQuant Residual

理论版中，Per-Node EJMDK 的输出还会进一步进入独立的融合阶段。假设有两个候选 node 输出 $A$ 与 $B$，则注意力加权融合可表示为：

$$
F = \alpha A + (1 - \alpha) B
$$

其中 $\alpha$ 由注意力匹配得到。

随后再加入残差修正：

$$
Y = F + \Delta_q(F)
$$

其中 $\Delta_q(\cdot)$ 表示由 TurboQuant 或类似压缩残差模块估计出的细节补偿。

当前仓库实现中，为了优先保证**单 exe + 可跑通整段视频**，这部分并没有拆成一个完全独立的 pass，而是将融合逻辑与主 shader 中的核采样、detail enhance 近似合并。

#### 4.10 时序复用（Temporal Reuse）

理论设计中的 Temporal Reuse 负责减少跨帧重复计算。基本思路是：

- 如果当前帧与上一帧局部结构足够接近；
- 且四叉树划分、量化结果、统计特征变化不大；
- 那么就直接复用上一帧的 tree mask 或局部表。

一种理想化条件写法为：

$$
\text{reuse if } \operatorname{sim}(T_t, T_{t-1}) > 0.95
$$

其中 $T_t$ 表示当前帧相关结构描述（tree、量化表或其他统计描述）。

当前仓库实现尚未建立完整的 temporal reuse cache，但 README 保留该设计，是为了明确完整算法路线。

#### 4.11 GPU 复杂度与工程优势

与传统“先插帧再超分”的两阶段流程相比，QTree-EJMDK 的理论优势在于：

- **一次统一采样完成多个目标**：减少冗余访存；
- **内容自适应分区**：把高成本计算集中在真正复杂区域；
- **共享内存友好**：LDS tile 使局部访问更具缓存效率；
- **寄存器压力可控**：动态权重现场生成避免大核数组常驻寄存器。

理论上：

- Tree Build 复杂度可近似看作 $O(N \log D)$；
- 单 pass EJMDK 近似为 $O(k^2 \times L)$；
- 若使用 separable 近似与 reuse，则实际运行成本还能进一步下降。

README 中原始论文设想给出的目标延迟是：

- **1024p 下约 0.8–2.2 ms / frame**（现代 GPU，理想优化条件下）

这属于理论目标，不应被误读为当前仓库原型在所有硬件上的实测结果。

### 5. 当前仓库中的工程实现映射

#### 5.1 仓库结构

- `src/main.cpp`：命令行入口，负责整段视频 pipeline 调度；
- `src/media_foundation_video.cpp`：Media Foundation 解码与 H.264 / MP4 输出；
- `src/quadtree_analyzer.cpp`：CPU 侧四叉树分析与 `NodeMap` 生成；
- `src/vulkan_ejmdk.cpp`：Vulkan 设备、资源、descriptor、pipeline、dispatch；
- `src/shaders/ejmdk.comp`：核心 compute shader；
- `compile_ejmdk.ps1`：一键构建脚本；
- `tools/embed_binary.cmake`：将 SPIR-V 嵌入头文件。

#### 5.2 当前实现采用的工程近似

当前实现为了优先满足：

- Windows 原生可跑；
- 直接命令行指定文件即可处理；
- 尽量收敛为单 exe；

因此采用了如下工程折中：

- 用 **Media Foundation** 替代外部 FFmpeg 依赖；
- 用 **CPU 四叉树统计** 替代理论版更复杂的注意力驱动 tree build；
- 用 **局部运动搜索 + 动态核** 实现可运行的近似 EJMDK；
- 用 **构建期 shader 嵌入** 保证运行期无需额外 shader 文件；
- 把若干理论上可拆开的 pass 合并，先保证 pipeline 完整跑通。

#### 5.3 当前仓库尚未完全覆盖的部分

以下内容在 README 中解释了理论意义，但**代码里尚未完整实现**：

- Attention Matching 的闭式最优融合权重；
- PolarQuant / TurboQuant 的完整在线旋转量化链路；
- 独立 residual correction pass；
- 完整的时序复用缓存系统；
- 端到端训练或学习型参数估计；
- 完整音频透传或音视频复合输出。

### 6. 构建要求

- Windows 10 / 11
- Visual Studio 2022 Build Tools（含 MSVC C++ 工具链）
- Vulkan SDK（至少需要 `glslc` 和 `vulkan-1.lib`）
- 支持 Vulkan compute 的 GPU

### 7. 构建方法

在仓库根目录运行：

```powershell
.\compile_ejmdk.ps1
```

成功后会生成：

- `build\Release\ejmdk.exe`

### 8. 命令行用法

最短路径就是：**直接把视频文件路径丢给 exe**。

```powershell
.\build\Release\ejmdk.exe sample-5.mp4
```

也支持显式指定输出文件和放大倍率：

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out.mp4 --scale 2
```

如果只想做超分，不做插帧：

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out_sr.mp4 --scale 2 --no-fg
```

支持的参数：

- `ejmdk.exe <input.mp4> [output.mp4] [--scale 2]`
- `-i, --input <path>`：输入视频路径
- `-o, --output <path>`：输出视频路径
- `--scale <n>`：整数放大倍率，默认 `2`
- `--no-fg`：关闭插帧，只做超分

默认行为：

- 默认输出文件名：`<输入名>_ejmdk_fg_x<scale>.mp4`
- 默认启用 FG，因此输出帧率通常为输入的 $2\times$

### 9. 当前原型的运行特性与限制

这部分很重要，避免把“能跑通”误解成“所有生产细节都已补齐”：

1. **当前实现主要覆盖视频流**
   - 当前代码路径主要处理视频解码、Vulkan 处理与视频重新编码；
   - 尚未实现完整的音频透传或音视频复合输出策略。

2. **当前实现偏原型而非极限性能版本**
   - 每帧存在 CPU / GPU 数据上传与回读；
   - 没有做零拷贝优化；
   - 没有完成完整 temporal reuse cache。

3. **当前实现优先保证正确性与可用性**
   - 目标是先满足“一个 exe 直接吃视频文件”；
   - 而不是立即追求论文中最理想的 GPU 延迟数字。

### 10. 已完成的实际验证

本次会话中已在当前机器上实际完成以下验证：

- 输入：`sample-5.mp4`
- 输入属性：`640×360 / H.264 / 25fps / 752 帧`
- 运行命令：`build\Release\ejmdk.exe sample-5.mp4`
- 输出文件：`sample-5_ejmdk_fg_x2.mp4`
- 输出属性：`1280×720 / H.264 / 50fps / 1503 帧`
- 实测耗时：`47.43 s`
- 实测 GPU：`AMD Radeon RX 550`

这说明当前版本已经满足：

- **C++ + Vulkan 直接实现**
- **单 exe 命令行处理完整视频**
- **直接指定输入文件即可运行**

### 11. 后续最值得继续增强的方向

如果要继续把这个项目往“论文完整版 + 更强工程版”推进，优先级最高的是：

1. 将 CPU 四叉树分析逐步迁移到 GPU；
2. 引入真正的 Attention Matching 权重模块；
3. 补齐 PolarQuant / TurboQuant 的完整压缩与残差修正链路；
4. 引入跨帧 temporal reuse cache；
5. 将当前 5×5 核进一步做 separable 近似；
6. 增加 PSNR / SSIM / OSLTT 自动评测；
7. 对音频透传、容器封装、错误恢复做更完整支持；
8. 针对移动端 Vulkan / QNN / ncnn Vulkan 路线做专门裁剪。

---

## English Version

### 1. Overview

**QTree-EJMDK: A Unified Real-Time Frame Generation and Super-Resolution Framework Based on Quadtree Attention Matching and an Efficient Joint Motion-Detail Kernel** (Optimized Revision, 2026.04)

QTree-EJMDK is built around a single question:

> Can **frame generation (FG)** and **super-resolution (SR)** be merged into one unified, content-adaptive, GPU-friendly pipeline instead of being executed as two separate stages?

The full design combines ideas from:

- **QDM (Quadtree Diffusion Model, 2025)** for adaptive quadtree partitioning;
- **Mob-FGSR (SIGGRAPH 2024)** for lightweight FG/SR warping and blending on constrained hardware;
- **Attention Matching (2026 arXiv)** for closed-form fusion weights;
- **PolarQuant / TurboQuant (2026 series)** for online rotation-aware quantization and residual correction;
- **EJMDK (Efficient Joint Motion-Detail Kernel)** for unified motion-compensated sampling, blending, and detail enhancement.

This repository contains two things at once:

1. the **full algorithmic blueprint** described in this README; and
2. a **working Windows-native C++/Vulkan CLI prototype** that can already process an MP4 file end-to-end.

### 2. What is already implemented in this repository

The repository currently provides a **single executable command-line tool for Windows**: `ejmdk.exe`.

It is not a script-assembled filter pipeline. It is a native C++ / Vulkan application composed of:

- **Media Foundation** for MP4 / H.264 decoding and re-encoding;
- **Vulkan Compute** for the core SR + FG processing stage;
- **CPU-side quadtree analysis** to produce a per-pixel `NodeMap`;
- **build-time SPIR-V embedding** so the executable does not depend on a separate shader file at runtime;
- **a CLI entry point** that accepts the input video path directly from the command line.

> In engineering terms, “single binary” means that the runtime entry point is a single program, `ejmdk.exe`, without requiring Python, external shader files, or hook scripts. System-level dependencies such as Windows Media Foundation and the Vulkan runtime provided by the GPU driver still apply.

### 3. Important distinction: research design vs repository implementation

To avoid confusion, the README explicitly distinguishes between the **full research design** and the **current codebase status**.

| Component | Full QTree-EJMDK design | Current repository status |
| --- | --- | --- |
| Adaptive quadtree partitioning | fully part of the design | **implemented as an engineering approximation** |
| Closed-form attention matching | core theoretical module | **not yet implemented as a complete standalone module** |
| PolarQuant / TurboQuant | important theoretical component | **not yet fully implemented as a standalone module** |
| EJMDK unified motion-detail kernel | core theoretical module | **implemented as an engineering approximation** |
| Separate Fusion + Residual pass | part of the full design | **currently folded into the main compute path** |
| Temporal reuse cache | included in the full design | **not fully implemented yet** |
| Single-exe full-video CLI processing | engineering objective | **implemented** |

So, in short:

- the **algorithm sections** of this README describe the complete QTree-EJMDK concept;
- the **code in this repository** implements a practical C++ / Vulkan prototype that preserves the main ideas of quadtree-guided unified FG + SR, but does not yet reproduce every theoretical module.

### 4. Detailed algorithm explanation (theoretical design)

#### 4.1 Abstract

This work proposes a unified pipeline for real-time frame generation (FG) and super-resolution (SR), named **QTree-EJMDK**. The framework combines quadtree-adaptive sparse processing from QDM, lightweight mobile-oriented FG/SR warping and blending ideas from Mob-FGSR, closed-form attention matching, and online rotation-aware quantization concepts from PolarQuant / TurboQuant. Its key innovation is the **Efficient Joint Motion-Detail Kernel (EJMDK)**, which merges motion-aware sampling, frame blending, and detail enhancement into a single per-node sampling stage.

To address the high register pressure and repeated sampling caused by naïve 5×5 brute-force loops, the design further adopts a production-oriented optimization strategy: LDS/shared-memory tile loading, histogram-centroid quantization, and on-the-fly dynamic weight generation. Under the full theoretical optimization path, the total latency target is approximately 0.8–2.2 ms per frame at 1024p on modern GPUs, while substantially reducing redundant computation compared with conventional two-stage pipelines.

#### 4.2 Keywords

- real-time super-resolution
- frame generation
- quadtree adaptivity
- attention matching
- joint motion-detail kernel
- LDS tiling optimization
- histogram quantization
- GPU compute shader
- register-pressure control

#### 4.3 Motivation and problem setting

In real-time rendering workloads such as games, VR/AR, and handheld devices, FG and SR are both essential techniques for improving perceptual quality under limited compute budgets. However, a traditional pipeline that treats them as separate stages often suffers from:

- redundant intermediate memory traffic,
- duplicated sampling cost,
- temporal instability,
- and poor content adaptivity.

QTree-EJMDK aims to solve this by:

1. using a quadtree to decide where high-cost processing is actually necessary,
2. extracting local motion/detail descriptors per leaf node,
3. applying a unified EJMDK operator that jointly performs motion compensation, frame blending, detail enhancement, and upsampling,
4. and optionally refining the result with attention-weighted fusion and residual correction.

#### 4.4 Notation

| Symbol | Meaning |
| --- | --- |
| $I_{t-1}$ | previous frame |
| $I_t$ | current frame |
| $\mathbf{p}$ | output pixel position |
| $\mathbf{f}$ | local feature vector |
| $\hat{\mathbf{f}}$ | quantized / compressed local feature |
| $\mathbf{m}(\hat{\mathbf{f}})$ | feature-driven motion offset |
| $\beta(\hat{\mathbf{f}})$ | previous/current blending coefficient |
| $K_{ij}(\hat{\mathbf{f}})$ | dynamic kernel weight |
| $r$ | kernel radius |

#### 4.5 Overall pipeline

The full theoretical pipeline is composed of four compute stages:

1. **Tree Build Pass**
2. **Per-Node EJMDK Pass**
3. **Fusion + Residual Pass**
4. **Temporal Reuse**

An optimized production implementation may merge some of these passes to reduce intermediate buffers and dispatch overhead.

#### 4.6 Tree Build Pass

The purpose of the tree build stage is to keep flat regions coarse and subdivide only regions that are visually complex or temporally unstable.

A typical spatial variance term is:

$$
\sigma_R^2 = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left(I_t(\mathbf{p}) - \mu_R\right)^2
$$

and a simple temporal difference term is:

$$
d_R = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left|I_t(\mathbf{p}) - I_{t-1}(\mathbf{p})\right|
$$

Theoretical subdivision can be expressed as:

$$
\text{split}(R) = \left(\sigma_R^2 > \tau_{var}\right) \lor \left(s_{attn}(R) < \tau_{match}\right)
$$

where $s_{attn}(R)$ is an attention similarity measure.

In the current repository, this is approximated by CPU-side quadtree recursion driven by:

- spatial variance,
- temporal difference,
- adaptive leaf-size heuristics.

The output is a 4-channel per-pixel `NodeMap` containing:

- `detailStrength`
- `temporalConfidence`
- `searchRadius`
- `leafRefinement`

#### 4.7 PolarQuant / TurboQuant and feature compression

In the full design, the local feature vector is compressed before being consumed by the dynamic kernel:

$$
\hat{\mathbf{f}} = Q(\mathbf{f})
$$

where $Q(\cdot)$ may be implemented using:

- PolarQuant,
- histogram centroid quantization,
- TurboQuant residual-aware compression.

The current repository does **not** yet implement the full PolarQuant / TurboQuant chain. Instead, it passes a compact set of heuristically derived local descriptors through `NodeMap`.

#### 4.8 Optimized EJMDK operator

EJMDK is the core unified operator. It merges:

- motion compensation,
- frame blending,
- detail enhancement,
- and super-resolution sampling.

Its core mathematical form is:

$$
O(\mathbf{p}) = \sum_{i,j=-r}^{r} K_{ij}(\hat{\mathbf{f}}) \cdot \Bigl[
\beta(\hat{\mathbf{f}}) \cdot I_{t-1}\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
+
(1 - \beta(\hat{\mathbf{f}})) \cdot I_t\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
\Bigr]
$$

The production-oriented optimization strategy includes:

1. **LDS / shared-memory tile loading**
2. **histogram centroid quantization** instead of online k-means
3. **on-the-fly dynamic weight generation** instead of storing a large kernel array in registers
4. **optional separable decomposition** for additional cost reduction

#### 4.9 Fusion and residual correction

In the full design, per-node outputs are further fused with attention-derived weights:

$$
F = \alpha A + (1 - \alpha) B
$$

followed by residual correction:

$$
Y = F + \Delta_q(F)
$$

In the current prototype, this logic is simplified and partially folded into the main compute path rather than being implemented as a separate production-grade fusion/residual stage.

#### 4.10 Temporal reuse

The theoretical design includes temporal reuse to skip redundant work when the quadtree structure and local features remain sufficiently similar across adjacent frames.

A simple idealized criterion is:

$$
\text{reuse if } \operatorname{sim}(T_t, T_{t-1}) > 0.95
$$

where $T_t$ represents the current frame’s reusable structural state.

This mechanism is not yet fully implemented in the current repository.

#### 4.11 Complexity and expected benefits

Compared with a conventional two-stage “interpolate first, upscale later” pipeline, QTree-EJMDK aims to reduce redundant memory traffic and concentrate expensive processing only where needed.

The theoretical benefits are:

- unified sampling for multiple objectives;
- adaptive partitioning;
- better shared-memory locality;
- lower register pressure through procedural kernel generation.

Under the full optimized design, the README’s target numbers are roughly:

- Tree Build: $O(N \log D)$
- EJMDK pass: $O(k^2 \times L)$
- ideal 1024p latency target: **0.8–2.2 ms/frame** on modern GPUs under highly optimized conditions

These numbers should be treated as design goals rather than guaranteed measurements of the current prototype.

### 5. Engineering implementation in this repository

#### 5.1 Repository layout

- `src/main.cpp` — CLI entry point and pipeline orchestration
- `src/media_foundation_video.cpp` — decode / encode path based on Media Foundation
- `src/quadtree_analyzer.cpp` — CPU-side quadtree analysis and `NodeMap` generation
- `src/vulkan_ejmdk.cpp` — Vulkan setup, resources, descriptors, pipeline, and dispatch
- `src/shaders/ejmdk.comp` — core compute shader
- `compile_ejmdk.ps1` — build script
- `tools/embed_binary.cmake` — SPIR-V embedding helper

#### 5.2 Why the current implementation looks different from the full paper-style design

The current code prioritizes:

- Windows-native execution,
- direct CLI usability,
- a single executable runtime entry point,
- minimal external runtime dependencies.

That is why the implementation currently uses:

- **Media Foundation** instead of an FFmpeg-based runtime path;
- **CPU-side quadtree statistics** instead of a complete attention-driven tree builder;
- **local motion search + dynamic kernel approximation** instead of the full theoretical EJMDK stack;
- **embedded shaders** for single-exe deployment simplicity.

#### 5.3 What is not yet fully covered by the codebase

The following features are still described mainly at the design level:

- full closed-form attention matching,
- full PolarQuant / TurboQuant feature compression,
- a dedicated residual correction stage,
- a complete temporal reuse cache,
- end-to-end learned parameter estimation,
- full audio passthrough / multiplexing.

### 6. Build requirements

- Windows 10 / 11
- Visual Studio 2022 Build Tools with MSVC
- Vulkan SDK (`glslc` and `vulkan-1.lib` are required)
- a GPU with Vulkan compute support

### 7. Build instructions

Run this in the repository root:

```powershell
.\compile_ejmdk.ps1
```

Expected output:

- `build\Release\ejmdk.exe`

### 8. Command-line usage

The shortest path is exactly what the user asked for: pass a video file path directly to the executable.

```powershell
.\build\Release\ejmdk.exe sample-5.mp4
```

You can also specify an explicit output path and scale factor:

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out.mp4 --scale 2
```

To disable frame generation and run SR only:

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out_sr.mp4 --scale 2 --no-fg
```

Supported arguments:

- `ejmdk.exe <input.mp4> [output.mp4] [--scale 2]`
- `-i, --input <path>`
- `-o, --output <path>`
- `--scale <n>`
- `--no-fg`

Default behavior:

- default output naming: `<input>_ejmdk_fg_x<scale>.mp4`
- FG enabled by default, so the output frame rate is typically doubled

### 9. Runtime characteristics and current limitations

The current prototype should be understood as a working engineering baseline, not a fully optimized final system.

1. **Video-first implementation**
   - the current path primarily handles video decode, Vulkan processing, and video encode;
   - full audio passthrough / multiplexing is not yet implemented.

2. **Prototype-oriented data flow**
   - frames are uploaded to the GPU and read back each iteration;
   - there is no zero-copy optimization yet;
   - full temporal reuse caching is not yet in place.

3. **Correctness and usability over peak performance**
   - the immediate goal is a working single-exe CLI pipeline;
   - not yet the absolute lowest-latency realization of the full research design.

### 10. Verified run in this session

The following run has already been validated on the current machine:

- input: `sample-5.mp4`
- input properties: `640×360 / H.264 / 25 fps / 752 frames`
- command: `build\Release\ejmdk.exe sample-5.mp4`
- output: `sample-5_ejmdk_fg_x2.mp4`
- output properties: `1280×720 / H.264 / 50 fps / 1503 frames`
- measured runtime: `47.43 s`
- measured GPU: `AMD Radeon RX 550`

This confirms that the current version already provides:

- a direct **C++ + Vulkan** implementation,
- end-to-end **single-exe command-line video processing**,
- and direct processing by passing the input file path on the command line.

### 11. Best next steps

The highest-value future improvements are:

1. move more of the quadtree analysis onto the GPU,
2. add true attention matching,
3. implement the full PolarQuant / TurboQuant chain,
4. add a real temporal reuse cache,
5. factor or approximate the current $5\times5$ kernel into separable passes,
6. add PSNR / SSIM / OSLTT evaluation,
7. improve audio/container support,
8. build dedicated mobile-oriented Vulkan / QNN / ncnn variants.

---

## 日本語版

### 1. 概要

**QTree-EJMDK：四分木注意マッチングと Efficient Joint Motion-Detail Kernel に基づく、リアルタイム補間生成・超解像の統合フレームワーク**（最適化版 2026.04）

QTree-EJMDK が目指している中心課題は次の通りです。

> 従来のように **フレーム生成 (FG)** と **超解像 (SR)** を二段階で処理するのではなく、両者を 1 本の内容適応型・GPU フレンドリーな統合パイプラインとして実現できるか。

理論設計では、以下の流れを統合します。

- **QDM (Quadtree Diffusion Model, 2025)**：四分木ベースの適応分割
- **Mob-FGSR (SIGGRAPH 2024)**：軽量な warping / blending 発想
- **Attention Matching (2026 arXiv)**：閉形式の融合重み
- **PolarQuant / TurboQuant (2026 シリーズ)**：回転量子化と残差補正
- **EJMDK**：運動補償・融合・細部強調・アップサンプリングの統合カーネル

このリポジトリには二つの層があります。

1. README に記述された **完全なアルゴリズム設計**
2. 実際に動作する **Windows ネイティブの C++ / Vulkan CLI プロトタイプ**

### 2. このリポジトリで実装済みの内容

現在のリポジトリは、Windows 上で直接動画ファイルを処理できる単一実行ファイル **`ejmdk.exe`** を提供します。

これは外部スクリプトをつなぎ合わせたフィルタチェーンではなく、ネイティブな C++ / Vulkan アプリケーションです。

- **Media Foundation**：MP4 / H.264 のデコードと再エンコード
- **Vulkan Compute**：SR + FG の中核処理
- **CPU 側四分木解析**：各フレーム対に対して `NodeMap` を生成
- **ビルド時 SPIR-V 埋め込み**：実行時に外部 shader ファイルを不要化
- **CLI エントリポイント**：コマンドラインから入力動画パスを直接指定可能

> ここでいう「単一バイナリ」とは、実行時の主役が `ejmdk.exe` 1 本であることを意味します。Python や外部 hook / shader ファイルは不要です。ただし、Windows の Media Foundation と GPU ドライバが提供する Vulkan Runtime などのシステム依存は前提です。

### 3. 理論設計と現在の実装の違い

README では、**理論上の完全版** と **現在の実装状況** を明確に区別しています。

| コンポーネント | 理論上の QTree-EJMDK | 現在の実装状況 |
| --- | --- | --- |
| 適応的四分木分割 | 設計に含まれる | **工学的近似として実装済み** |
| 閉形式 attention matching | 理論コア | **独立モジュールとしては未完成** |
| PolarQuant / TurboQuant | 理論上の重要要素 | **独立実装は未完成** |
| EJMDK 統合カーネル | 理論コア | **工学的近似版を実装済み** |
| 独立 Fusion + Residual pass | 完全設計に含まれる | **現在は主 compute に統合** |
| Temporal reuse cache | 完全設計に含まれる | **未完成** |
| 単一 exe の CLI 動画処理 | 工学目標 | **実装済み** |

つまり、README のアルゴリズム記述は**完全設計**を説明しており、コードはその主思想を保った**実用プロトタイプ**です。

### 4. アルゴリズム詳細（理論設計）

#### 4.1 要約

本設計は、リアルタイムのフレーム生成（FG）と超解像（SR）を統合したパイプライン **QTree-EJMDK** を提案します。QDM による四分木適応分割、Mob-FGSR の軽量 warping / blending、Attention Matching の閉形式重み、PolarQuant / TurboQuant の量子化・残差補正の考え方を統合し、さらに **Efficient Joint Motion-Detail Kernel (EJMDK)** によって、運動補償・フレーム融合・細部強調・アップサンプリングを単一の核演算で処理しようとします。

5×5 の単純な総当たりサンプリングで生じるレジスタ圧迫や重複サンプリングに対し、LDS タイル読み込み、ヒストグラム重心量子化、動的重みの即時計算などの生産指向最適化を導入します。完全最適化版では、1024p クラスで 0.8–2.2 ms/frame 程度を理論目標としています。

#### 4.2 キーワード

- リアルタイム超解像
- フレーム生成
- 四分木適応処理
- 注意マッチング
- joint motion-detail kernel
- LDS タイリング最適化
- ヒストグラム量子化
- GPU compute shader
- レジスタ圧力制御

#### 4.3 背景と問題設定

ゲームや VR/AR、携帯機器のようなリアルタイムレンダリング環境では、FG と SR はどちらも性能限界を突破するための重要技術です。しかし両者を別々の段階で処理すると、中間結果のメモリ帯域、重複サンプリング、時系列アーティファクト、内容適応性の不足といった問題が起きやすくなります。

QTree-EJMDK は、

1. 四分木で計算コストをかけるべき領域を選び、
2. leaf node ごとに局所的な運動・細部特徴を取り出し、
3. 統合カーネルで FG と SR を同時に処理し、
4. 必要に応じて attention 融合と残差補正を追加する

という流れを取ります。

#### 4.4 記号

| 記号 | 意味 |
| --- | --- |
| $I_{t-1}$ | 前フレーム |
| $I_t$ | 現フレーム |
| $\mathbf{p}$ | 出力画素位置 |
| $\mathbf{f}$ | 局所特徴ベクトル |
| $\hat{\mathbf{f}}$ | 量子化後の特徴 |
| $\mathbf{m}(\hat{\mathbf{f}})$ | 局所運動オフセット |
| $\beta(\hat{\mathbf{f}})$ | 前後フレームの混合係数 |
| $K_{ij}(\hat{\mathbf{f}})$ | 動的カーネル重み |
| $r$ | カーネル半径 |

#### 4.5 全体パイプライン

理論版では次の 4 段構成です。

1. **Tree Build Pass**
2. **Per-Node EJMDK Pass**
3. **Fusion + Residual Pass**
4. **Temporal Reuse**

実運用では、dispatch 数や中間バッファを減らすために一部の pass を統合しても構いません。

#### 4.6 Tree Build Pass

空間分散の一例は：

$$
\sigma_R^2 = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left(I_t(\mathbf{p}) - \mu_R\right)^2
$$

時系列差分の一例は：

$$
d_R = \frac{1}{|R|}\sum_{\mathbf{p}\in R}\left|I_t(\mathbf{p}) - I_{t-1}(\mathbf{p})\right|
$$

理論上の分割条件は例えば：

$$
\text{split}(R) = \left(\sigma_R^2 > \tau_{var}\right) \lor \left(s_{attn}(R) < \tau_{match}\right)
$$

現在の実装では、attention 類似度そのものではなく、CPU 側の空間分散 + 時系列差分による近似四分木を使っています。

#### 4.7 PolarQuant / TurboQuant

理論版では局所特徴 $\mathbf{f}$ を圧縮し、

$$
\hat{\mathbf{f}} = Q(\mathbf{f})
$$

として後段の EJMDK に渡します。

現状のコードでは、この完全な量子化系列はまだ未実装であり、代わりに `NodeMap` に格納した軽量な局所記述子を使います。

#### 4.8 EJMDK 統合カーネル

理論上の中心式は次の通りです。

$$
O(\mathbf{p}) = \sum_{i,j=-r}^{r} K_{ij}(\hat{\mathbf{f}}) \cdot \Bigl[
\beta(\hat{\mathbf{f}}) \cdot I_{t-1}\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
+
(1 - \beta(\hat{\mathbf{f}})) \cdot I_t\bigl(\mathbf{p} + \mathbf{m}(\hat{\mathbf{f}}) + (i,j)\bigr)
\Bigr]
$$

ここで EJMDK は、

- 運動補償
- 前後フレーム融合
- 細部強調
- アップサンプリング

を単一の動的カーネルで扱います。

さらに実装最適化として：

- LDS / groupshared memory による tile 読み込み
- histogram centroid quantization
- レジスタ常駐配列を避ける即時計算型カーネル重み
- separable 分解可能性

が重要になります。

#### 4.9 Fusion + Residual

理論版では、さらに

$$
F = \alpha A + (1 - \alpha) B
$$

で融合し、

$$
Y = F + \Delta_q(F)
$$

で残差補正を加えます。現在のコードでは、これらは完全独立 pass としてはまだ分離されていません。

#### 4.10 Temporal Reuse

理論設計では、構造類似度が十分に高いときに前フレームの結果を再利用します。理想化すれば：

$$
\text{reuse if } \operatorname{sim}(T_t, T_{t-1}) > 0.95
$$

という形になります。現在の実装にはこの完全なキャッシュ機構はまだありません。

### 5. リポジトリ実装の要点

- `src/main.cpp`：CLI と全体制御
- `src/media_foundation_video.cpp`：デコード / エンコード
- `src/quadtree_analyzer.cpp`：CPU 側四分木解析
- `src/vulkan_ejmdk.cpp`：Vulkan compute 実行
- `src/shaders/ejmdk.comp`：中心シェーダ

現状の実装は、以下を優先しています。

- Windows ネイティブで動くこと
- コマンドラインから直接動画を渡せること
- 単一 exe を中心に扱えること
- 外部ランタイム依存を減らすこと

### 6. ビルド要件

- Windows 10 / 11
- Visual Studio 2022 Build Tools（MSVC 含む）
- Vulkan SDK（`glslc` と `vulkan-1.lib` が必要）
- Vulkan compute 対応 GPU

### 7. ビルド方法

```powershell
.\compile_ejmdk.ps1
```

生成物：

- `build\Release\ejmdk.exe`

### 8. コマンドライン使用例

最短の使い方：

```powershell
.\build\Release\ejmdk.exe sample-5.mp4
```

出力先と倍率を明示する例：

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out.mp4 --scale 2
```

SR のみを行う例：

```powershell
.\build\Release\ejmdk.exe --input sample-5.mp4 --output out_sr.mp4 --scale 2 --no-fg
```

### 9. 現在の制限

1. 主に**動画ストリーム中心**であり、完全な音声透過対応は未実装
2. CPU/GPU 間コピーが各フレーム発生する
3. Temporal reuse cache は未完成
4. 最大性能追求版ではなく、まずは動作可能な原型

### 10. このセッションで確認済みの実行結果

- 入力：`sample-5.mp4`
- 入力属性：`640×360 / H.264 / 25fps / 752 フレーム`
- コマンド：`build\Release\ejmdk.exe sample-5.mp4`
- 出力：`sample-5_ejmdk_fg_x2.mp4`
- 出力属性：`1280×720 / H.264 / 50fps / 1503 フレーム`
- 実測時間：`47.43 s`
- GPU：`AMD Radeon RX 550`

### 11. 今後の強化候補

1. 四分木解析の GPU 化
2. 真の Attention Matching 実装
3. PolarQuant / TurboQuant 完全実装
4. Temporal reuse cache 実装
5. 5×5 カーネルの separable 近似
6. PSNR / SSIM / OSLTT 評価追加
7. 音声・コンテナ対応の強化
8. モバイル向け Vulkan / QNN / ncnn 最適化

---

## Shared Shader Skeleton / 共享着色器骨架 / 共通シェーダースケルトン

The following code block is a **shared reference skeleton** for the theoretical production-style pipeline described above.  
下面这段代码是上文理论版生产实现的**共享参考骨架**。  
以下のコードブロックは、上で説明した理論上のプロダクション向けパイプラインの**共通参照スケルトン**です。

```hlsl
// ====================== Pass 1: Tree Build (reference skeleton) ======================
[numthreads(8,8,1)]
void TreeBuild(uint3 id : SV_DispatchThreadID) {
    float4 region = LoadRegion(id);
    float var = ComputeVariance(region);
    float attnSim = SimpleAttentionMatch(I_curr, I_prev, region);
    if (var > tau_var || attnSim < tau_match) {
        SubdivideNode(id);
    } else {
        MarkAsLeaf(id);
    }
    if (ReusePrevTree(prevMask, id)) return;  // Temporal Reuse
}

// ====================== Pass 2: Optimized Per-Node EJMDK ======================
groupshared float4 TilePrev[32+4][32+4];
groupshared float4 TileCurr[32+4][32+4];

[numthreads(32, 32, 1)]
void PerNodeEJMDK_Optimized(uint3 gid : SV_GroupID, uint3 tid : SV_GroupThreadID, uint3 id : SV_DispatchThreadID)
{
    if (!IsLeaf(id)) return;

    // Step 1: cooperative LDS loading
    LoadPatchToShared(I_prev, TilePrev, gid, tid);
    LoadPatchToShared(I_curr, TileCurr, gid, tid);
    GroupMemoryBarrierWithGroupSync();

    // Step 2: local feature extraction + fast quantization
    float4 feat = ExtractLocalFeature(TileCurr, TilePrev, tid);
    float4 qFeat = HistogramCentroidQuantize(feat);

    // Step 3: dynamic EJMDK sampling
    float2 motion = mul(M, qFeat);
    float  beta   = saturate(dot(v, qFeat));

    float4 result = 0.0;
    float  weightSum = 0.0;

    for (int dy = -2; dy <= 2; dy += 1) {
        for (int dx = -2; dx <= 2; dx += 1) {
            float2 samplePos = float2(tid.x + dx, tid.y + dy) + motion;

            float4 sPrev = SampleShared(TilePrev, samplePos);
            float4 sCurr = SampleShared(TileCurr, samplePos);

            float w = ComputeDynamicWeight(qFeat, dx, dy);

            result += w * (beta * sPrev + (1.0 - beta) * sCurr);
            weightSum += w;
        }
    }

    result /= weightSum;
    StoreNodeResult(id, result);
}

// ====================== Pass 3: Fusion + Residual ======================
[numthreads(8,8,1)]
void Fusion(uint3 id : SV_DispatchThreadID) {
    float4 nodeA = LoadAdjacent(id,0), nodeB = LoadAdjacent(id,1);
    float weight = AttentionWeight(q, nodeA, nodeB, beta);
    float4 fused = weight * nodeA + (1-weight) * nodeB;
    float4 residual = ComputeResidual(fused);
    float4 corrected = fused + QJLResidual(residual);
    Output[id] = BilateralFilter(corrected, id);
}
```