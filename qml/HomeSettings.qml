import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: root
    color: HomeTheme.sidebar
    property int section: 0
    property string cacheNotice: ""
    signal openEditor()
    FileDialog {
        id: diagnosticExport
        objectName: "diagnosticsSaveDialog"
        title: HomeTheme.t("导出诊断日志")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "log"
        nameFilters: [HomeTheme.t("诊断日志 (*.log)")]
        onAccepted: studio.exportDiagnostics(selectedFile)
    }
    HomeNoticeDialog { id: diagnosticNotice; objectName: "diagnosticsExportNotice" }
    Connections {
        target: studio
        function onDiagnosticsExportFinished(success, detail) {
            diagnosticNotice.message=HomeTheme.t(success ? "文件已保存" : "导出失败")
            diagnosticNotice.severity=success ? "success" : "error"
            diagnosticNotice.filePath=success ? detail : ""
            diagnosticNotice.detail=success ? "" : detail
            diagnosticNotice.open()
        }
    }
    onVisibleChanged: if (visible) { cacheNotice=""; studio.refreshMouthCache() }
    component LegalPage: HomeSection {
        id: legal; property string bodyText: ""
        Text { Layout.fillWidth: true; text: legal.bodyText; color: HomeTheme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap; lineHeight: 1.5; textFormat: Text.PlainText }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 22; spacing: 20
        HomeHeader { Layout.fillWidth: true; title: HomeTheme.t("设置") }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 20
            ColumnLayout {
                Layout.preferredWidth: HomeTheme.english ? 182 : 144; Layout.minimumWidth: Layout.preferredWidth; Layout.maximumWidth: Layout.preferredWidth; Layout.fillHeight: true; spacing: 4
                Repeater {
                    model: [{title:HomeTheme.t("通用设置"),icon:"settings",section:0},{title:HomeTheme.t("模型与计算"),icon:"model",section:1},{title:HomeTheme.t("关于"),icon:"info",section:5},{title:HomeTheme.t("联系我们"),icon:"contact",section:6},{title:HomeTheme.t("版权声明"),icon:"document",section:2},{title:HomeTheme.t("第三方模型与数据"),icon:"layers",section:3},{title:HomeTheme.t("用户协议"),icon:"document",section:4}]
                    AbstractButton {
                        id: nav; required property var modelData
                        objectName: "settingsSection-"+modelData.section
                        focusPolicy: Qt.StrongFocus
                        activeFocusOnTab: true
                        Layout.fillWidth: true; implicitHeight: 34; padding: 9; hoverEnabled: true
                        Layout.topMargin: modelData.section===2 ? 14 : 0
                        Accessible.name: HomeTheme.t(modelData.title); Accessible.selected: root.section===modelData.section
                        background: Rectangle { radius: 7; color: root.section===nav.modelData.section ? HomeTheme.field : nav.hovered ? HomeTheme.hover : "transparent"; border.color: nav.visualFocus ? HomeTheme.accent : root.section===nav.modelData.section ? HomeTheme.outline : "transparent" }
                        contentItem: RowLayout { spacing: 8
                            StudioIcon { name: nav.modelData.icon; ink: HomeTheme.secondary; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                            Text { Layout.fillWidth: true; text: HomeTheme.t(nav.modelData.title); color: HomeTheme.text; font.pixelSize: 12; elide: Text.ElideRight }
                        }
                        onClicked: root.section=modelData.section
                    }
                }
                Item { Layout.fillHeight: true }
            }
            StackLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 640; Layout.fillHeight: true; currentIndex: root.section
                ScrollView {
                    id: general; contentWidth: availableWidth; clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: Math.min(840,general.availableWidth); x: (general.availableWidth-width)/2; spacing: 14
                        HomeSection {
                            Layout.fillWidth: true; title: HomeTheme.t("外观"); iconName: "monitor"
                            RowLayout { Layout.fillWidth: true; spacing: 10
                                Repeater { model: [{key:"system",title:HomeTheme.t("跟随系统")},{key:"light",title:HomeTheme.t("浅色")},{key:"dark",title:HomeTheme.t("深色")}]
                                    HomeChoiceCard { required property var modelData; Layout.fillWidth: true; Layout.minimumWidth: 0; title: HomeTheme.t(modelData.title); appearance: modelData.key; selected: workspace.appearanceMode===modelData.key; onClicked: workspace.appearanceMode=modelData.key }
                                }
                            }
                            RowLayout { Layout.fillWidth: true; spacing: 8
                                Repeater { model: [{key:"blue",name:HomeTheme.t("蓝色"),color:"#007aff"},{key:"graphite",name:HomeTheme.t("石墨"),color:"#7b7d83"},{key:"teal",name:HomeTheme.t("青绿"),color:"#2ba88b"},{key:"dark",name:HomeTheme.t("深空"),color:"#38b5aa"}]
                                    AbstractButton {
                                        id: tone; required property var modelData; Layout.fillWidth: true; implicitHeight: 34; hoverEnabled: true
                                        Accessible.name: HomeTheme.t(modelData.name); Accessible.selected: workspace.themePreset===modelData.key
                                        onClicked: workspace.themePreset=modelData.key
                                        background: Rectangle { radius: 6; color: tone.hovered ? HomeTheme.hover : "transparent"; border.color: tone.visualFocus ? HomeTheme.accent : "transparent" }
                                        contentItem: RowLayout { spacing: 6
                                            Item { Layout.fillWidth: true }
                                            Rectangle { width: 18; height: 18; radius: 9; color: tone.modelData.color
                                                StudioIcon { visible: workspace.themePreset===tone.modelData.key; anchors.centerIn: parent; width: 12; height: 12; name: "check"; ink: "white" }
                                            }
                                            Text { text: HomeTheme.t(tone.modelData.name); color: HomeTheme.secondary; font.pixelSize: 11 }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }
                                }
                            }
                        }
                        HomeSection {
                            Layout.fillWidth: true; title: HomeTheme.t("界面语言"); iconName: "text"
                            RowLayout { Layout.fillWidth: true; spacing: 10
                                HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("跟随系统"); selected: workspace.languagePreference==="system"; onClicked: workspace.language="system" }
                                HomeChoiceCard { Layout.fillWidth: true; title: HomeTheme.t("简体中文"); selected: workspace.languagePreference==="zh"; onClicked: workspace.language="zh" }
                                HomeChoiceCard { Layout.fillWidth: true; title: "English"; selected: workspace.languagePreference==="en"; onClicked: workspace.language="en" }
                            }
                        }
                        HomeSection {
                            Layout.fillWidth: true; title: HomeTheme.t("本地存储"); iconName: "folder"
                            Repeater { model: [{name:HomeTheme.t("作品目录"),path:workspace.root.replace(/\/library$/,"")},{name:HomeTheme.t("模型目录"),path:studio.modelRoot},{name:HomeTheme.t("形象目录"),path:workspace.root+"/avatars"}]
                                RowLayout {
                                    id: storageRow; required property var modelData; Layout.fillWidth: true; spacing: 12
                                    Text { text: HomeTheme.t(storageRow.modelData.name); color: HomeTheme.text; font.pixelSize: 12; Layout.preferredWidth: HomeTheme.english ? 96 : 65 }
                                    Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: storageRow.modelData.path; elide: Text.ElideMiddle; color: HomeTheme.muted; font.pixelSize: 11 }
                                    IconButton { glyph: "folder"; hint: HomeTheme.t("打开目录"); onClicked: workspace.openDirectory(storageRow.modelData.path) }
                                }
                            }
                        }
                        HomeSection {
                            Layout.fillWidth: true; title: HomeTheme.t("诊断日志"); iconName: "document"
                            RowLayout {
                                Layout.fillWidth: true; spacing: 12
                                Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: HomeTheme.t("记录性能、计算设备和错误，最多保留 16 MiB。"); color: HomeTheme.secondary; font.pixelSize: 12; wrapMode: Text.Wrap }
                                HomeButton {
                                    objectName: "exportDiagnostics"
                                    text: HomeTheme.t("导出日志")
                                    focusPolicy: Qt.StrongFocus
                                    activeFocusOnTab: true
                                    onClicked: {
                                        diagnosticExport.selectedFile=studio.defaultDiagnosticsExportUrl()
                                        diagnosticExport.open()
                                    }
                                }
                            }
                        }
                        HomeSection {
                            Layout.fillWidth: true; title: HomeTheme.t("嘴形校准缓存"); iconName: "person"
                            RowLayout { Layout.fillWidth: true
                                Text { Layout.fillWidth: true; text: (studio.mouthCacheBytes/1048576).toFixed(1)+" / "+studio.mouthCacheLimitMiB+" MiB"; color: HomeTheme.secondary; font.pixelSize: 12 }
                                HomeButton { objectName: "clearMouthCache"; text: HomeTheme.t("清理缓存"); enabled: !studio.busy && studio.mouthCacheBytes>0; onClicked: root.cacheNotice=studio.clearMouthCache() }
                            }
                            Text { visible: !!root.cacheNotice; Layout.fillWidth: true; text: HomeTheme.t(root.cacheNotice); wrapMode: Text.Wrap; color: HomeTheme.secondary; font.pixelSize: 11 }
                        }
                        Item { height: 4 }
                    }
                }
                HomeModels { onOpenEditor: root.openEditor() }
                ScrollView {
                    id: copyrightView; contentWidth: availableWidth; clip: true
                    LegalPage { width: Math.min(840,copyrightView.availableWidth); x: (copyrightView.availableWidth-width)/2; title: HomeTheme.t("版权声明"); bodyText: HomeTheme.english
                        ? "Imported portraits, images, recordings and text remain the property of their respective rights holders. Obtain the necessary permissions before importing, cloning or distributing content.\n\nOpen-source components, models and built-in voice recordings retain their own notices and licenses. See Third-party notices for the local component inventory."
                        : HomeTheme.t("用户自行导入的人像、图片、声音、文字及由此制作的作品，权利仍归原权利人。导入、克隆、生成、导出及传播前，请取得所需的著作权、肖像权、声音及个人信息处理授权。\n\n应用使用的开源组件、模型与内置参考音色分别遵循其原有声明和许可。具体组件来源见「第三方模型与数据」。") }
                }
                ScrollView {
                    id: noticeView; contentWidth: availableWidth; clip: true
                    HomeSection { width: Math.min(840,noticeView.availableWidth); x: (noticeView.availableWidth-width)/2
                        Text { Layout.fillWidth: true; text: workspace.noticesHtml(HomeTheme.accent); wrapMode: Text.Wrap; textFormat: Text.RichText; color: HomeTheme.text; linkColor: HomeTheme.accent; onLinkActivated: link=>Qt.openUrlExternally(link); font.pixelSize: 12 }
                    }
                }
                ScrollView {
                    id: agreementView; contentWidth: availableWidth; clip: true
                    LegalPage { width: Math.min(840,agreementView.availableWidth); x: (agreementView.availableWidth-width)/2; title: HomeTheme.t("用户协议"); bodyText: (HomeTheme.english
                        ? "Use content you own or have permission to use. Do not impersonate others, clone voices without consent or distribute misleading content. Review generated results before publishing.\n\nLocal generation stores material on this Mac. The public edition supports local processing only.\n\nModel preparation may require downloads. Generated output quality depends on the models and input material."
                        : HomeTheme.t("请使用拥有合法权利或已获授权的素材。不得冒用他人身份、未经同意克隆他人声音或传播误导性内容。发布前请核查作品的真实性、准确性、权利来源及所需标识。\n\n本地生成时，素材和作品保存在本机。公开版仅支持本地运行。\n\n首次准备模型可能需要下载。生成质量取决于模型、输入素材与参数，请在导出和发布前检查结果。")) + "\n\n" + studio.models.modelTerms(HomeTheme.english) }
                }
                ScrollView {
                    id: aboutView; contentWidth: availableWidth; clip: true
                    HomeSection { width: Math.min(640,aboutView.availableWidth); x: (aboutView.availableWidth-width)/2; padding: 24
                        RowLayout { Layout.fillWidth: true; spacing: 16
                            Rectangle { width: 64; height: 64; radius: 16; color: HomeTheme.accent
                                StudioIcon { anchors.centerIn: parent; width: 36; height: 36; name: "spark"; ink: HomeTheme.accentInk }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 6
                                Text { text: HomeTheme.t("新身数字人"); color: HomeTheme.text; font.pixelSize: 23; font.weight: Font.DemiBold }
                                Text { text: HomeTheme.t("版本")+" "+workspace.version; color: HomeTheme.secondary; font.pixelSize: 12 }
                            }
                        }
                        Text { Layout.fillWidth: true; text: HomeTheme.t("本地 AI 图像与数字人创作"); color: HomeTheme.secondary; font.pixelSize: 13; wrapMode: Text.Wrap }
                        HomeButton {
                            objectName: "aboutWebsite"
                            text: HomeTheme.english ? "Website: https://ffclip.com" : "官网：https://ffclip.com"
                            quiet: true
                            onClicked: Qt.openUrlExternally("https://ffclip.com")
                        }
                        Text { Layout.fillWidth: true; text: "Powered by Stability AI"; color: HomeTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                        Rectangle { Layout.fillWidth: true; height: 1; color: HomeTheme.line }
                        HomeButton { text: HomeTheme.t("查看第三方声明"); iconName: "document"; quiet: true; onClicked: root.section=3 }
                    }
                }
                HomeContact { embedded: true }
            }
        }
    }
}
