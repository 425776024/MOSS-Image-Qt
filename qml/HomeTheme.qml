pragma Singleton
import QtQuick
import "Translations.js" as Strings

QtObject {
    readonly property bool english: workspace.language === "en"
    readonly property bool dark: VideoCutTheme.dark
    readonly property color surface: VideoCutTheme.background
    readonly property color sidebar: VideoCutTheme.sidebar
    readonly property color field: VideoCutTheme.panel
    readonly property color text: VideoCutTheme.text
    readonly property color secondary: VideoCutTheme.secondary
    readonly property color faint: VideoCutTheme.faint
    readonly property color line: VideoCutTheme.line
    readonly property color outline: VideoCutTheme.borderSubtle
    readonly property color accent: VideoCutTheme.accent
    readonly property color accentInk: VideoCutTheme.accentInk
    readonly property color selection: VideoCutTheme.selection
    readonly property color hover: VideoCutTheme.inputHover
    readonly property color preview: VideoCutTheme.elevated
    readonly property color danger: VideoCutTheme.danger
    readonly property color muted: VideoCutTheme.muted
    readonly property color elevated: VideoCutTheme.elevated
    readonly property color chrome: VideoCutTheme.chrome
    // Neutral sheet surfaces; content and the primary action carry the emphasis.
    readonly property color dialogSurface: dark ? "#29292b" : "#f2f2f2"
    readonly property color dialogInset: dark ? "#222224" : "#e5e5e7"
    readonly property color dialogBorder: dark ? "#32ffffff" : "#22000000"
    readonly property var translations: ({
        "嘴形校准缓存":"Mouth calibration cache", "清理缓存":"Clear cache", "缓存已清理，下次生成将重新校准":"Cache cleared. The next generation will recalibrate.", "缓存正在使用或无法清理，请稍后重试":"Cache is in use or could not be cleared. Try again later.", "生成期间无法清理缓存，请稍后重试":"Cache cannot be cleared during generation. Try again when it finishes.",
        "我的作品":"My projects", "我的形象":"My avatars", "我的声音":"My voices", "制作数字人":"Create avatar", "图像创作":"Image studio",
        "使用帮助":"Help", "联系我们":"Contact us", "设置":"Settings", "关于":"About", "创作工作台":"Creative workspace",
        "新建作品":"New project", "导入工程":"Open project", "作品名称":"Project name", "最近修改":"Last modified", "画布":"Canvas", "类型":"Type", "搜索作品":"Search projects",
        "打开":"Open", "复制":"Duplicate", "重命名":"Rename", "移除":"Remove", "恢复上次移除":"Undo removal", "生成记录":"Generations", "创作工程":"Projects",
        "本地作品":"Local project", "未命名作品":"Untitled project", "空白画布":"Blank canvas", "没有找到匹配的作品":"No matching projects", "从第一件作品开始":"Create your first project",
        "新建图像作品，或导入已有工程继续创作。":"Create an image project or open an existing project.", "列表视图":"List view", "缩略图视图":"Grid view", "打开作品目录":"Open project folder",
        "导入形象":"Import avatar", "打开模型目录":"Open model folder", "搜索形象":"Search avatars", "形象名称":"Avatar name", "来源":"Source", "驱动方式":"Animation", "本地导入":"Local import",
        "音频驱动":"Audio driven", "用于创作":"Use in project", "形象详情":"Avatar details", "还没有形象":"No avatars yet", "导入一张人像图片，即可制作说话视频。":"Import a portrait to create a talking video.", "全部":"All", "本地":"Local", "图片数字人":"Portrait avatar",
        "导入音色":"Import voice", "我的音色":"My voices", "内置音色":"Built-in voices", "搜索音色":"Search voices", "音色名称":"Voice name", "参考文本":"Reference text", "操作":"Actions", "试听":"Preview", "停止":"Stop", "编辑":"Edit", "选择":"Select", "已选用":"Selected", "还没有音色":"No voices yet", "导入一段清晰人声，制作自己的参考音色。":"Import a clear recording to create a reference voice.",
        "音色名称与参考文字":"Voice name and transcript", "参考录音":"Reference recording", "选择录音":"Choose audio", "保存音色":"Save voice", "取消":"Cancel", "保存":"Save", "关闭":"Close", "参考录音中的完整原文":"The exact words spoken in the recording", "3–30 秒 WAV 录音":"3–30 second WAV recording",
        "选择素材":"Choose source", "配置声音":"Set voice", "制作视频":"Create video", "选择人像":"Choose portrait", "更换图片":"Change image", "保存到我的形象":"Save to my avatars", "制作数字人视频":"Create talking video", "导入音频":"Import audio", "合成语音":"Text to speech", "配音文案":"Speech script", "输入数字人要说的话…":"What should the avatar say?", "选择配音":"Choose audio", "开始制作":"Create video", "返回形象库":"Avatar library", "数字人将在独立创作窗口中生成，可继续编辑并导出。":"The video opens in the image studio for editing and export.",
        "通用设置":"General", "模型与计算":"Models & compute", "版权声明":"Copyright", "第三方模型与数据":"Third-party notices", "用户协议":"Usage", "更改自动保存":"Changes save automatically",
        "外观":"Appearance", "显示模式":"Display mode", "跟随系统或选择浅色、深色":"Follow the system or choose a light or dark appearance", "跟随系统":"System", "浅色":"Light", "深色":"Dark", "工作台主题":"Workspace theme", "主页配色立即生效":"Applies immediately to the home window", "深空":"Deep space", "石墨":"Graphite", "青绿":"Teal", "蓝色":"Blue", "语言":"Language", "界面语言":"Home language", "更改后立即生效":"Changes apply immediately", "本地存储":"Local storage", "作品目录":"Project folder", "模型目录":"Model folder", "形象目录":"Avatar folder", "打开目录":"Open folder", "选择目录":"Choose folder", "自动保存":"Autosave", "创作内容自动保存在本机":"Your work is automatically saved on this Mac",
        "获取使用帮助，交流产品建议。":"Get help and share product feedback.", "微信":"WeChat", "微信扫一扫":"Scan with WeChat", "扫码添加微信":"Add us on WeChat", "欢迎咨询使用问题或反馈建议。":"Questions and suggestions are welcome.", "B站主页":"Bilibili", "本地 AI 图像与数字人创作":"Local AI image and avatar creation", "版本":"Version", "查看第三方声明":"Third-party notices", "返回主页":"Back to home", "正在制作":"Creating", "查看创作窗口":"Open image studio", "没有匹配的音色":"No matching voices", "没有匹配的形象":"No matching avatars"
    })
    function t(value) {
        if (!english || typeof value !== "string") return value
        if (Object.prototype.hasOwnProperty.call(Strings.english, value)) return Strings.english[value]
        return Object.prototype.hasOwnProperty.call(translations, value) ? translations[value] : value
    }
    function count(value, noun) { return english ? value + " " + ({"作品":"projects","形象":"avatars","音色":"voices"}[noun] || noun) : value + " 个" + noun }
    function taskLabel(task) { return t(({image:"生成图像",edit:"编辑图像",avatar:"让画面说话",voice:"语音合成",sfx:"音效生成",pack:"打包视频"})[task] || "") }
    function projectName(row) { return row.name || (row.document && row.document.prompt ? row.document.prompt.replace(/\n/g, " ").slice(0, 42) : t("未命名作品")) }
}
