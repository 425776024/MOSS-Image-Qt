# macOS 设计 skill 参考

查阅日期：2026-10-22。框架：Qt 6 / QML / C++，macOS 桌面创作应用。

权威来源是 [Apple HIG](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos)。下面是本次实际查阅的社区 skill，供后续设计与审查按需使用；没有将第三方仓库或安装脚本引入项目依赖。

| Skill | 适用内容 | 本项目采用方式 |
| --- | --- | --- |
| [macos-design-guidelines](https://github.com/leny/macos-design-guidelines-skill/blob/develop/SKILL.md) | macOS 布局、工具栏、侧栏、控件对齐与间距 | 参考分组、基线对齐和按钮顺序；其中尺寸也来自社区实践，不能标成 Apple 强制规则。 |
| [macos-design](https://github.com/ceorkm/macos-design-skill/blob/main/SKILL.md) | 原生观感、深浅主题、视觉层级、渐进展示、键盘交互 | 采用内容优先和按需展示；不照搬网页模拟标题栏、交通灯、CSS 或移动端尺寸。 |
| [apple-design](https://github.com/dickwu/apple-design-skill/blob/main/SKILL.md) | 依据 HIG 审查平台习惯、可读性、反馈、模态流程 | 用作后续审查流程的候选；具体规则回查 Apple 原文，并转成 Qt/QML 实现。 |

本次额外阅读了前两套 skill 的布局、视觉、交互参考文件，以及 Apple 的 macOS、Sheets、Alerts、Color 和 Typography 页面。第三套已查阅入口与范围，尚未按其完整流程对全应用作审计。

## 使用边界

- 优先级：用户需求 → 本项目 `DESIGN.md` 与当前 Apple HIG → 社区建议。
- 只采用适合 macOS 的章节，不把 iOS/visionOS 的按钮、手势、字号套到桌面界面。
- 不占用 ⌘Space、⌘Tab 等系统快捷键。部分社区示例包含这类组合，不在本项目采用。
- 系统窗口按钮由 macOS 提供，不手绘红黄绿；系统字体通过平台调用，不复制或打包字体文件。
- SwiftUI 示例仅用于理解平台行为，不能据此宣称 QML 使用了 AppKit 原生控件或原生材质。
- 避免为了“苹果感”增加毛玻璃、卡片、说明文案或动画。符合平台习惯、任务清楚、信息克制优先。
- 引用具体规则时附来源，尺寸和效果要在当前系统实际运行检查。截图通过不等于全应用无障碍或平台适配完成。
