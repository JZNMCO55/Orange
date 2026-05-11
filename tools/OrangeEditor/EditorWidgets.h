#ifndef ORANGE_EDITOR_EDITOR_WIDGETS_H
#define ORANGE_EDITOR_EDITOR_WIDGETS_H

// 编辑器复用 ImGui 控件 —— Inspector 各组件区块用、未来 Settings / 其它
// 面板也可能用。本头不依赖任何引擎类型，只对 ImGui 调用，纯 UI 工具。

// 三色 X/Y/Z 标签 + 3 个 DragFloat 的组合控件，对齐 Unity Transform 的
// 配色（X 红 / Y 绿 / Z 蓝）。比裸 DragFloat3 多视觉占用：每分量前一
// 个有色 Button 当 label —— Button 是装饰，点击吃掉但无副作用（不进
// 入键盘焦点队列）。
//
// 用 PushID(label) 隔离三个内部 DragFloat 的 ImGui ID；外层调用方按需
// 再包 PushID（Inspector 同一 Window 内同名字段不出现，目前不必）。
//
// 返回值：任一分量被改 → true，调用方一般写回 component 字段即可。
bool DragVec3Colored(const char* label, float v[3],
                     float speed = 0.1f,
                     float vMin  = 0.0f,
                     float vMax  = 0.0f,
                     const char* fmt = "%.3f");

#endif  // ORANGE_EDITOR_EDITOR_WIDGETS_H
