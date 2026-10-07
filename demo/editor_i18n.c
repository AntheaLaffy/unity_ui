#include "asmui_editor_i18n.h"
#include <string.h>

typedef struct Message {
    const char *en, *zh;
} Message;
static const Message messages[] = {
    {"File", "文件"},
    {"Edit", "编辑"},
    {"Assets", "资源"},
    {"GameObject", "游戏对象"},
    {"Component", "组件"},
    {"Window", "窗口"},
    {"Help", "帮助"},
    {"Account", "账户"},
    {"Cloud", "云服务"},
    {"Layers", "层"},
    {"Default", "默认"},
    {"Hierarchy", "层级"},
    {"Inspector", "检查器"},
    {"Scene", "场景"},
    {"Game", "游戏"},
    {"Project", "项目"},
    {"Console", "控制台"},
    {"Search", "搜索"},
    {"Shaded", "着色"},
    {"Effects", "效果"},
    {"Gizmos", "辅助图标"},
    {"Perspective", "透视"},
    {"Orthographic", "正交"},
    {"Persp", "透视"},
    {"Display 1", "显示器 1"},
    {"Free Aspect", "自由比例"},
    {"Scale  1x", "缩放  1x"},
    {"Main Camera", "主摄像机"},
    {"Directional Light", "方向光"},
    {"Environment", "环境"},
    {"Plane", "平面"},
    {"Cube", "立方体"},
    {"Sphere", "球体"},
    {"Static", "静态"},
    {"Tag", "标签"},
    {"Untagged", "未标记"},
    {"Layer", "层"},
    {"Transform", "变换"},
    {"Position", "位置"},
    {"Rotation", "旋转"},
    {"Scale", "缩放"},
    {"Cube (Mesh Filter)", "立方体（网格过滤器）"},
    {"Sphere (Mesh Filter)", "球体（网格过滤器）"},
    {"Plane (Mesh Filter)", "平面（网格过滤器）"},
    {"Mesh", "网格"},
    {"Mesh Renderer", "网格渲染器"},
    {"Mesh Filter", "网格过滤器"},
    {"Materials", "材质"},
    {"Element 0", "元素 0"},
    {"Lighting", "光照"},
    {"Cast Shadows", "投射阴影"},
    {"On", "开启"},
    {"Receive Shadows", "接收阴影"},
    {"Contribute GI", "参与全局光照"},
    {"Probes", "探针"},
    {"Additional Settings", "其他设置"},
    {"Box Collider", "盒碰撞体"},
    {"Sphere Collider", "球碰撞体"},
    {"Is Trigger", "是触发器"},
    {"Material", "材质"},
    {"None (Physics Material)", "无（物理材质）"},
    {"Center", "中心"},
    {"Size", "尺寸"},
    {"Add Component", "添加组件"},
    {"Camera", "摄像机"},
    {"Light", "光源"},
    {"Clear Flags", "清除标志"},
    {"Background", "背景"},
    {"Culling Mask", "剔除遮罩"},
    {"Projection", "投影"},
    {"Field of View", "视野"},
    {"Clipping Planes", "裁剪平面"},
    {"Skybox", "天空盒"},
    {"Everything", "全部"},
    {"Near  0.3     Far  1000", "近 0.3     远 1000"},
    {"Type", "类型"},
    {"Color", "颜色"},
    {"Mode", "模式"},
    {"Intensity", "强度"},
    {"Indirect Multiplier", "间接光照倍数"},
    {"Shadow Type", "阴影类型"},
    {"Directional", "方向光"},
    {"Realtime", "实时"},
    {"Soft Shadows", "软阴影"},
    {"Audio Listener", "音频监听器"},
    {"Additional Light Data", "附加光源数据"},
    {"Clear", "清除"},
    {"Collapse", "折叠"},
    {"Clear on Play", "播放时清除"},
    {"Favorites", "收藏"},
    {"All Materials", "所有材质"},
    {"All Models", "所有模型"},
    {"All Prefabs", "所有预制体"},
    {"Prefabs", "预制体"},
    {"Scenes", "场景"},
    {"Scripts", "脚本"},
    {"Textures", "纹理"},
    {"6 items", "6 个项目"},
    {"Folder Asset", "文件夹资源"},
    {"Scene Asset", "场景资源"},
    {"Material Asset", "材质资源"},
    {"Prefab Asset", "预制体资源"},
    {"Script Asset", "脚本资源"},
    {"Texture Asset", "纹理资源"},
    {"Open", "打开"},
    {"Asset Labels", "资源标签"},
    {"Auto Generate Lighting Off", "自动生成光照：关闭"},
    {"SampleScene loaded", "已加载 SampleScene 场景"},
    {"Entered Play Mode", "已进入播放模式"},
    {"Frame", "帧"},
    {"Exited Play Mode", "已退出播放模式"},
    {"New Scene", "新建场景"},
    {"Open Scene...", "打开场景..."},
    {"Save", "保存"},
    {"Save As...", "另存为..."},
    {"Build Profiles...", "构建配置..."},
    {"Exit", "退出"},
    {"Undo", "撤销"},
    {"Redo", "重做"},
    {"Cut", "剪切"},
    {"Copy", "复制"},
    {"Paste", "粘贴"},
    {"Preferences...", "偏好设置..."},
    {"Create", "创建"},
    {"Import New Asset...", "导入新资源..."},
    {"Import Package", "导入资源包"},
    {"Refresh", "刷新"},
    {"Reimport", "重新导入"},
    {"Properties...", "属性..."},
    {"Create Empty", "创建空对象"},
    {"3D Object", "3D 对象"},
    {"2D Object", "2D 对象"},
    {"Audio", "音频"},
    {"Audio Source", "音频源"},
    {"Script", "脚本"},
    {"Layouts", "布局"},
    {"General", "常规"},
    {"Rendering", "渲染"},
    {"Animation", "动画"},
    {"UI Toolkit", "UI 工具包"},
    {"Package Manager", "包管理器"},
    {"Unity Manual", "Unity 手册"},
    {"Scripting Reference", "脚本参考"},
    {"Report a Bug...", "报告问题..."},
    {"About Unity", "关于 Unity"},
    {"Keyboard Shortcuts", "键盘快捷键"},
    {"Close", "关闭"},
    {"Editor visual preview", "编辑器视觉预览"},
    {"SampleScene - unity_ui - Unity Editor", "SampleScene - unity_ui - Unity 编辑器"},
};
const char *editor_localize(EditorLocale locale, const char *message) {
    if (locale == EDITOR_EN_US || !message)
        return message;
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i)
        if (!strcmp(message, messages[i].en))
            return messages[i].zh;
    return message;
}
int editor_locale_parse(const char *s, EditorLocale *out) {
    if (!s || !out)
        return 0;
    if (!strcmp(s, "zh-CN") || !strcmp(s, "zh")) {
        *out = EDITOR_ZH_CN;
        return 1;
    }
    if (!strcmp(s, "en-US") || !strcmp(s, "en")) {
        *out = EDITOR_EN_US;
        return 1;
    }
    return 0;
}
const char *editor_locale_name(EditorLocale l) { return l == EDITOR_EN_US ? "en-US" : "zh-CN"; }
const char *const *editor_language_samples(size_t *count) {
    static const char *samples[2 * sizeof(messages) / sizeof(messages[0]) + 2];
    size_t n = 0;
    samples[n++] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 "
                   "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    samples[n++] = "ff fi fl ffi ffl 简体中文 English □";
    for (size_t i = 0; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        samples[n++] = messages[i].en;
        samples[n++] = messages[i].zh;
    }
    *count = n;
    return samples;
}
