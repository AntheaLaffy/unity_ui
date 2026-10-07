# Unity 编辑器视觉实现

目标界面是 Unity **编辑器**。视觉参照为 [Unity 编辑器布局](https://docs.unity3d.com/cn/2023.2/Manual/UsingTheEditor.html) 的层级、场景、检查器、项目和工具栏结构。字体按用户要求使用 LXGW，因此不是某一版本 Unity 默认字体的逐像素截图复制。

## 视觉约束

默认 1440×900：左侧层级 236 px、右侧检查器 326 px、底部项目区域 246 px。深灰背景以 `#282828`、`#303030`、`#383838` 区分面板；1 px 分隔线、紧凑标签、蓝色选中行和橙色模型轮廓构成主要层次。控件和图标均由几何绘制，不贴一张编辑器截图。

场景使用可旋转视角的透视网格、灰色立方体、阴影、橙色选择轮廓和坐标辅助线。2D 切换到正交网格；游戏标签隐藏网格线和编辑器辅助图形。检查器的立方体变换会改变投影；欧拉角使用 [Unity 的 ZXY 顺序](https://docs.unity3d.com/ScriptReference/Quaternion.Euler.html)。

窗口支持 1024×680 起的布局和拖动面板边界；内容超出时裁剪、滚动，资源缩略图换行。菜单盖在所有面板之上，命中顺序与绘制顺序一致。

## 实现边界

| 部分 | 实现 |
| --- | --- |
| 布局、层级变换、裁剪、draw list、字形展开、相邻批合并、顶点打包 | `src/asmui.S`，x86-64 ASM |
| 3D 点投影、倒序控件命中 | `src/asmui_editor.S`，SSE2 / ASM |
| 编辑器控件状态、面板、几何与文案 | `demo/editor_scene.c`、`demo/editor_i18n.c` |
| 窗口、事件、Vulkan 生命周期、PNG 回读 | `demo/editor_main.c` |
| 字形排版 / 字体图集 | HarfBuzz / FreeType 边界适配器 |
| SDF、小线条抗锯齿、透视 / 正交网格 | `shaders/ui.frag` |

核心节点继续使用 +Y 向上的 authored 坐标。v0.21 在**根布局矩阵**中统一转换为屏幕 +Y 向下；子节点、文字、裁剪与 Vulkan / SDL 后端沿用这个已解析结果。原来的双重 / 缺失翻转会导致整个画面上下颠倒，不能在不同后端分别补丁修正。

`ASMUI_GRADIENT_Y` 使用现有 material3 位保存顶端颜色，ABI 大小不变；只用于普通 / 圆角矩形。新增 line 与 grid 材质使用现有四个材质参数。

## 验证边界

`make editor-check` 检查 ASM 投影数值、命中优先级、嵌套坐标 / 裁剪、渐变方向、控件状态、三轴变换、Unicode 边界及两种语言的实际排版字形覆盖。

`make editor-visual-check` 运行真实 Vulkan 渲染，输出中文、英文、紧凑窗口、菜单、检查器、控制台、2D、游戏、变换和输入截图，并检查面板颜色、选中行、模型轮廓和区域差异。图像检查是回归信号；整体视觉仍通过人工查看 PNG 核对。

`make editor-smoke` 创建实际窗口，经历 resize / swapchain 重建、面板切换、播放状态和 SDL 中文输入事件。`make verify` 同时运行既有布局、文字、遮罩、stencil、offscreen、blur、glow、纹理上传回归。

这版验证不代表 Unity 工程兼容性：文件菜单中的读写与构建、资源导入、账户与云服务没有接入；场景不是 Unity 渲染引擎。已有接口支持后续继续接数据，但不伪报保存或构建成功。
