# 项目设计约束

所有布局、外观和交互改动，先阅读并遵守根目录 [DESIGN.md](DESIGN.md)。

- 以 macOS Human Interface Guidelines 为平台基准，使用 Qt/QML 实现；优先系统控件与系统交互。
- 从用户目标出发删减界面。普通流程不展示模型、路径、内部参数和长篇说明。
- 修改共用组件后，检查实际运行界面的深浅主题、键盘操作、中文/英文及最小窗口；注明未验证的部分。
- 外部设计 skill 的来源和适用边界见 [docs/macos-design-skills.md](docs/macos-design-skills.md)。不能把社区建议或自定尺寸冒充 Apple 硬性规范。
