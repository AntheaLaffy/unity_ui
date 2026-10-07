# OpenUtau 工程导入

编辑器直接读取 USTX YAML，无需 Python 转换器或 OpenUtau 运行时。「打开」接受 `.ustx`；「保存」另存为 `.dse`，原 USTX 不会被覆盖。命令行也拒绝将当前文档写入 `.ustx` 路径。

```sh
make diffsinger DIFFSINGER_ARGS='--project /home/fuurin/Music/萤琢/fly.ustx'
make diffsinger-ustx-check
make diffsinger-ustx-window-check
```

`ustx-check` 默认只读扫描 `/home/fuurin/Music`。也可用 `USTX_ROOT=/path/to/projects` 指定另一目录。测试生成的数据均在本项目 `build/` 下，所需 Python 依赖为 PyYAML；窗口测试另外需要 Pillow、X11 和 Vulkan。

## 导入内容

| 内容 | 当前行为 |
| --- | --- |
| 版本 / 时间轴 | USTX 0.7、0.8、0.9；480 PPQ；从 tick 0 开始的恒定速度与单一拍号。以 `tempos` 为准，避免旧 `bpm` 字段与实际速度不一致。 |
| 歌声文档 | 轨道、片段、音符位置/时长/音高、UTF-8 歌词、音轨增益/声像/静音/独奏。保留 `+` 等原始歌词文本。 |
| 音高 | 音符控制点的毫秒横坐标、十分之一半音纵坐标和 `snap_first`，合并以音分为单位的 `pitd`；按 5 tick 网格烘焙为 MIDI 绝对音高。支持 io/i/o/线性形状及两点 sp；多点样条会明确拒绝。 |
| 编辑 | 导入音高随选中音符移动和转调；撤销/重做恢复相应曲线。重置音高曲线后使用音符音高。 |
| 声库 | 可自动匹配本机同名且满足当前原生模型契约的打包声库，按音素器选择中/日/英/粤语词典与实际模型语言编号。未适配、未设置或多个不同声库的工程需要手动选择当前全局声库。 |
| 歌词前端 | 假名/罗马字/MeCab 日文汉字、中文拼音、英文词典与方括号音素提示；普通 `+` 音节推进，`+~` / `+*` 元音延音。预检可报告未知音素、重叠和过短音符。详见 [前端规则](FRONTEND.md)。 |
| 完整原文 | 保存导入时的 USTX 文本、来源路径、声库/音素器名称和未适配计数，随 `.dse` 重开保留。包括原始伴奏引用和未知字段。 |
| 伴奏 | 相对路径按原工程目录解析，导入位置、原时长、skip / trim / fadein / fadeout（单位 tick）到可编辑音频片段；可独立试听并与歌声混音，遵循轨道静音/独奏/增益/声像。 |
| 未支持语义 | USTX 表情曲线/逐音素覆盖/颤音/声线混合暂不参与合成，保留原文并显示未应用数量。 |

容量为 8 条音轨、32 个歌声片段、64 个音频片段、2048 个音符、262144 个曲线点，源 USTX 文件不超过 16 MiB。文件类型、字段范围、容量、曲线坐标长度和顺序会验证；加载失败不替换当前文档和来源快照。YAML 标量在类型字段中按原始文本处理，日文音节 `no` 不会被 YAML 1.1 转为 `false`。重复字段、别名和多文档流会被拒绝。

`.dse` 中的原文是导入时的快照，用于保留信息与后续适配，并未将当前编辑结果回写进那份原文。当前没有 USTX 导出/回写。导入支持也不等于全部 OpenUtau 渲染语义兼容：现有前端使用确定性时长或本编辑器的手工时长，全套音素器、USTX 时长覆盖转换、variance / duration / pitch 模型尚未实现。原曲的歌声效果需逐项适配和独立听感验收。

## 本机验收

2026-10-04 对 `/home/fuurin/Music` 中 25 个 USTX 工程进行了原生导入、逐音符位置/时长/音高/歌词对照、原始文本比较、`.dse` 保存/重开和源文件 SHA-256 检查，全部通过。包含 7 条音轨、3 个歌声片段、1384 个音符的工程，及旧顶层 BPM 与实际 `tempos` 不同的工程。

《萤琢》`fly.ustx` 的真实 SDL/Vulkan 窗口验收覆盖音高显示、转调、曲线随动、撤销/重做、中文输入、窗口缩放及来源快照保存。结果截图与报告位于 `docs/evidence`，测试输出位于 `build/ustx-check` 和 `build/ustx-window-check`。这些测试验证导入与编辑，不认证完整歌曲合成的发音或听感。

2026-10-05 再次通过上述 25 份工程的验收，并逐项对照其音频片段元数据。16 个实际 WAV/FLAC 引用通过原生解码和源哈希检查；《萤琢》的伴奏完整缓存、波形、试听时钟、位置/淡入/音量/声像编辑、撤销、最小窗口及原生保存通过真实窗口验收。详见 [验证记录](VALIDATION.md) 与 `make diffsinger-audio-check` / `make diffsinger-audio-window-check`。

## 格式依据

参考 [OpenUtau USTX 格式说明](https://github.com/openutau/openutau/wiki/USTX-file-format) 及上游固定版本 `a06ab28b0ae011bd0b85926f3044572be46a7a63` 的 [UNote](https://github.com/stakira/OpenUtau/blob/a06ab28b0ae011bd0b85926f3044572be46a7a63/OpenUtau.Core/Ustx/UNote.cs)、[UCurve](https://github.com/stakira/OpenUtau/blob/a06ab28b0ae011bd0b85926f3044572be46a7a63/OpenUtau.Core/Ustx/UCurve.cs)、[UPart / UWavePart](https://github.com/stakira/OpenUtau/blob/a06ab28b0ae011bd0b85926f3044572be46a7a63/OpenUtau.Core/Ustx/UPart.cs) 与 [RenderPhrase](https://github.com/stakira/OpenUtau/blob/a06ab28b0ae011bd0b85926f3044572be46a7a63/OpenUtau.Core/Render/RenderPhrase.cs) 字段和单位。实现使用 C/libyaml 与现有 ASM 文档核心；没有接入 OpenUtau 的 C# 插件、旧引擎 ABI 或交换协议。
