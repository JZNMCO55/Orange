# Golden image 视觉回归

把引擎渲染输出纳入自动化回归网的「像素 golden」基建（断言层三级分工的第 ②
层；① 是 `tests/support/EngineTestAssert.h` 的白盒状态断言）。

## 文件

- `GoldenImage.{h,cpp}` / `GoldenTestHarness.h` —— CPU 侧 PPM(P6) codec + 双阈值
  容差比较（per-channel 绝对阈值 + 失败像素百分比）+ diff heatmap + harness。
  逐字搬自 `OrangeRender/tests/golden/`，零 Vulkan 依赖、自包含、不改一行。
- `data/*.ppm` —— 提交进仓的参考图（`.gitattributes` 标 `binary` 保 byte-stable）。

## 用例

- `visual_golden_pbr_test`（`../render/VisualGoldenPbrTest.cpp`）：程序化构造一个
  确定的 PBR 场景（相机 + 方向光 + 亮色 quad），经 `Pipeline::RenderToTexture`
  渲到 256×256 外部 RT，逐像素与 `data/pbr_quad.ppm` 比对。

## 生成 / 更新参考图

参考图首次缺失时测试会 **FAIL**（不静默生成，避免坏图被默认接受）。本机需有
Vulkan。直接跑 exe（便于传 `--update-golden`，ctest 默认只传 goldenDir）：

```
build/bin/Debug/visual_golden_pbr_test.exe tests/golden/data --update-golden
```

人眼确认生成的 `pbr_quad.ppm` 合理（非全黑 / 非乱色）后再 `git add`。

## 跨 GPU / 驱动限制

PPM 像素与本机 GPU / 驱动绑定（浮点非结合 + 驱动 reorder/fuse）。本基建首版
**不做确定性渲染开关**，走 `RenderToTexture`（PBR 直出、无后处理）已剔除
TAA / bloom / dither 等 temporal/随机源，但跨 GPU 仍可能超容差。故定位为
「本机 / 同 GPU 视觉回归网 + 引擎自检黑屏防护」。换机器 / 换卡后用上面的
`--update-golden` 重新生成。容差可经 `--tolerance N` / `--max-failed-percent F`
临时放宽。跨 GPU 收敛（FLIP / pixelmatch 感知度量、确定性渲染开关）留作后续。
