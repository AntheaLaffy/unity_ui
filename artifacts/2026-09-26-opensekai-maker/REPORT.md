# OpenSekai 谱面编辑迁移证据

此目录保存 Rust / ASM 的执行证据，不是原版 Unity 截图基准。原项目的 6,560 个文件保持 SHA-256 一致，提交为 `0cd2e6212d71671d48373732fbaa3559c16a765a`。

## 当前画面

- `editor-zh.png`：默认中文、LXGW 的 Unity 编辑器界面，Game 视图内运行原 prefab 布局的 OpenSekai。
- `editor-en.png`：同一状态切换英文；应用保留原字体和图集中的文字。
- `maker-zh.png`：1920×1080 应用视图，固定三音符演示谱面。
- `long-notes.png`：原图集的直线、缓入/缓出曲线和引导线，fixture 在 `long-fixture/`。
- `native-smoke.png` / `.json` / `.log`：真实 SDL 窗口缩放为 1280×800，中文标题和编辑的音符保存成功。
- `native-asan.png` / `.json` / `.log`：ASan/UBSan 构建下的同一流程，包含独立音符和长条首尾。

## 已验证

`make opensekai-check` 通过原文件哈希、16 项 Rust 集成测试、C/ASM 四边形检查与真实动态库 C ABI 检查。原 C# 提取器覆盖 Manifest、音乐 ID、BPM、格点量化、轨道宽度；六组小地图案例逐像素匹配原 C# 像素循环，包括拍号变化、隐藏音符、关键音符和缓入/缓出连线。另有六组原 C# 长条网格输出，比较顶点、UV 和三角形对角线。

`cargo clippy --locked --all-targets -- -D warnings`、`make test editor-check` 通过。`make editor-visual-check` 输出并检查 13 张 Vulkan 截图，覆盖 2 种语言、3 种尺寸；LXGW 字形覆盖检查为零缺字。

真实窗口通过 110 帧 SDL 事件从管理页进入谱面编辑器、放置音符及长条、Ctrl+Z/Y/S。验证独立音符 ticks=480、轨道 1..3（含两端）、noteBaseType=1；长条首尾 ticks=480/720、轨道 5..7、noteBaseType=2/1，前后连接 ID 对称。启用 `VK_LAYER_KHRONOS_validation`，未发现 VUID/validation error。应用原图集确实上传，应用动态字形为零缺字。

独立 ASan/UBSan 构建也通过四边形桥接与 110 帧窗口流程，未报告内存访问错误或未定义行为；图形驱动的退出泄漏检查通过 `ASAN_OPTIONS=detect_leaks=0` 排除。此项不代表 Rust 内部或所有应用入口都已覆盖。

## 复现

在仓库根目录执行：

```sh
make opensekai-check
make test editor-check editor-visual-check
make opensekai-window-check OPENSEKAI_WINDOW_ARGS='--validation-root /tmp/unity-ui-vulkan-validation/usr'
python3 application/OpenSekai_Community_rs/tools/capture_minimap.py
python3 application/OpenSekai_Community_rs/tools/capture_long_notes.py
```

验证层路径可替换为本机安装位置；不传参数仍可运行真实窗口检查。谱面截图 fixture 是本目录的 `fixture/`，不使用用户原有谱面：

```sh
./build/editor --project application/OpenSekai_Community_rs \
  --data-dir artifacts/2026-09-26-opensekai-maker/fixture --app-screen maker \
  --headless --size 1920 1080 --output build/opensekai-editor-zh.png
```

## 当前边界

完整连接点插入/变形操作、事件编辑、音频/波形、试游玩、完整 TMP 排版、部分 prefab 动态状态、材质和动画仍未完成。编辑器层级和检查器仍显示内置示例，尚未反映应用对象。未接通入口报告迁移状态。截图中原图集自带日文字样仍存在；原 Unity 整屏像素参照仍为空，不能宣称整款应用视觉完全一致。
