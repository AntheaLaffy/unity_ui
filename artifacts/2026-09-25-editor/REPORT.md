# v0.21 编辑器界面验证记录

本轮目标：用 ASM 继续推进 `unity_ui`，复刻 Unity 编辑器视觉；提供多语言方案，至少简体中文，默认使用用户 LXGW 字体。

## 需求与当前证据

| 需求 | 实际交付与证据 |
| --- | --- |
| Unity 编辑器视觉 | [中文](editor-zh.png)、[英文](editor-en.png)：菜单、工具栏、层级、场景 / 游戏、检查器、项目 / 控制台、深灰面板、蓝色选中、橙色轮廓、网格与坐标辅助图形。人工检查了完整画面、紧凑布局、菜单、预编辑、2D 和窗口缩放回读。 |
| ASM 核心 | `src/asmui.S` 负责布局、裁剪、draw list、文字展开、批合并和顶点；`src/asmui_editor.S` 新增 SSE2 投影与倒序控件命中。编译产物含实际 ASM 符号；投影与标量参考比较通过。 |
| 简体中文与多语言方案 | `zh-CN` 默认、`en-US` 可通过按钮 / 参数 / 环境变量选择；[方案](../../docs/LOCALIZATION.md) 记录文案、字体、输入与新增语言步骤。24 种状态 × 2 语言，共 25,210 个实际排版字形，未发现缺字。 |
| 用户 LXGW 默认字体 | 验证时未设置 `ASMUI_FONT` / `ASMUI_LANG`，实际加载 `/home/fuurin/.local/share/fonts/LXGWWenKai-Regular.ttf`，基础图集 326 字形。字体 SHA-256 见 [环境记录](environment.txt)。 |
| 可运行和交互 | 原生窗口 90 帧，1440×900 → 1280×800 resize，播放 / 面板 / 菜单状态及 SDL 预编辑、动态补字、中文提交通过；[实际窗口回读](editor-live.png)。中文搜索在切到英文后保留同一对象结果。 |

## 验证结果

| 检查 | 结果与日志 |
| --- | --- |
| `make verify` | 全量通过，[日志](verify.log)。包含既有布局、字体、纹理异步上传、stencil、offscreen、blur、glow、音游回归以及编辑器核心 / 图像检查。 |
| `make editor-check editor-visual-check editor-smoke editor-demo` | 最终源码通过，[日志](vulkan-validation.log)。13 张 Vulkan PNG、两种语言、三种尺寸；原生 90 帧与默认中文单帧截图均成功。 |
| Vulkan validation | 使用 `VK_LAYER_KHRONOS_validation`，上述离屏 / 原生检查未出现 validation error 或 VUID。[加载日志](vulkan-layer-loader.log) 确认实例与设备均插入了该层；其中 `VK_INSTANCE_LAYERS` 环境变量提示不是 validation error。层库从发行版官方包临时解压使用，项目运行不依赖该临时目录。 |
| ASan + UBSan | 最终编辑器核心与字体边界通过，[日志](sanitizers.log)。C 边界启用 sanitizer；ASM 本体由数值、布局和边界用例验证。 |
| 人工图像复核 | 核对文字方向与 LXGW 字形、面板比例、菜单覆盖、字段裁剪、中英布局、选中状态；2D 网格的 Y=0 轴与立方体底边对齐。 |

图像脚本校验的是具体区域和状态差异，不用它声称与所有 Unity 版本逐像素一致。视觉参照、字体差异与实现范围见 [说明](../../docs/EDITOR_UI.md)。

## 中文补字卡顿的修正

旧实现为一次新输入重建整套约 320 字形图集，实测约 11–12 秒。`perf` 中 FreeType 占采样周期的 96.62%（Atom）/ 99.27%（Core），见 [采样摘要](font-profile.txt)。轮廓 / 位图 SDF 的三轮比较记录在 [CSV](font-raster-comparison.csv)，位图方案仍需数秒，因此未采用它替换现有字形效果。

最终保留原有轮廓 SDF，按需创建只含新字形的纹理页。验证中的“测试输入”新增 3 个字形，构建加上传分别为 181.825 ms（离屏）和 174.866 ms（原生）。已存在字形继续复用。这是具体输入样例的诊断数据，未把它当作一般帧率或所有字体的性能保证。启动预载完整字体仍是同步的。

## 范围与复现

这是编辑器视觉和基础交互复刻。场景使用内置投影立方体，其他层级对象用于展示检查器；不解析 Unity 工程、不执行 C#、不导入资源或构建游戏。对应菜单置灰，不伪报完成。系统 IME 候选窗口仍由桌面输入法实现，自动化覆盖 SDL 事件与字形渲染，未声称已人工验收全部输入法。

运行 `make editor`；重建所有视觉证据用 `make editor-visual-check`。所有截图使用相同的 Vulkan / ASM 渲染路径。`SHA256SUMS` 记录本目录证据；`SOURCE_SHA256SUMS` 对照本轮 ASMUI 源码与工程文档。源码快照 `asmui-v0.21-source.tar.gz` 包含根构建文件及 ASMUI 的 `src/include/backend/demo/shaders/tests/tools/docs`，不包含编译目录、外部字体或 `application/` 下的独立 Unity 工程。
