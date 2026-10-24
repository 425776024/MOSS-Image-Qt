# Ditto 公开原版

开源客户端使用 `digital-avatar/ditto-talkinghead` 的原始预训练权重，固定 Hugging Face
提交 `e4a2f60328ee7c32af585ac4b3cce299e4c8e254`，不使用本机训练检查点。
下载目录为用户模型根目录下的 `ditto-original/`。

- 音频动作：官方 `ditto_onnx/lmdm_v0.4_hubert.onnx`，本地名 `denoise.onnx`。
- HuBERT：官方 `ditto_onnx/hubert.onnx`；203 关键点：官方 `ditto_pytorch/aux_models/landmark203.onnx`，ModelScope 上同一文件名为 `LivePortrait/liveportrait/landmark.onnx`。
- 检测与初始对齐：OpenCV Zoo YuNet 2023（MIT），替换原 InsightFace 检测器和 106 点模型。
  YuNet 五点初始化 224 像素裁剪，再由相同的 203 点模型确定最终裁剪。
  参数沿用 Xinshen `yunet-203-v1`，最终扩展 2.3、纵向偏移 -0.125。
  固定下载版本及 SHA-256 见 `config/models.json`，许可见
  `vendor/xinshen-ditto/LICENSE.YuNet`。模型清单含新 pipelineId，旧缓存不可混用。
- 外观、动作、拼接、解码器：官方 `ditto_onnx/` 中的原文件。
- Warp：两源下载官方 `ditto_onnx/warp_network_ori.onnx`，原生转换后与 `voxta/ditto-talkinghead-onnx` 提交
  `930ae658ab5247e6209b21c4265cecf2a5b45610` 的 `warp_network_opset20.onnx` 字节完全一致。
  转换仅调整 ONNX opset、IR 与 GridSample 属性以兼容 ONNX Runtime，权重不变。
  上游修改说明：https://huggingface.co/voxta/ditto-talkinghead-onnx/blob/930ae658ab5247e6209b21c4265cecf2a5b45610/NOTICE

原生运行时支持分离的 warp / decoder 图，不要求 Python、PyTorch 或用户自行转换。
这条公开 ONNX 路径与私人 Core ML 优化包的性能不同，不能沿用旧性能数字。
它使用原始模型能力，不是缩小网络或简化渲染器。

`config/ditto-original/config.json` 是固定官方 `v0.4_hubert_cfg_pytorch.pkl`
转换成普通 JSON 的结果，已与原始导出配置逐项核对。
`schedule.json` 是同一官方 LMDM `setup(50)` 导出的 50 步调度常量。
`manifest.json` 绑定全部文件摘要；应用内置这些小型配置，网络下载权重。
完整下载来源、大小和 SHA-256 以 `config/models.json` 为准。

## 人物表情与动作

数字人创作的“生成参数”提供人物表情和可展开的分项动作幅度；轨道编辑器的数字人面板提供相同设置。
这组设置按作品保存，与配音情绪、方言独立，修改后保留配音并使旧说话视频失效。

本地 `moss-ditto generate[-page]` 请求接受 `emo` 和 `use_d_keys`，并传入计划的 `audioDriver`：

- `emo`：`-1` 保留模型包默认条件；`0` 愤怒、`1` 厌恶、`2` 恐惧、`3` 高兴、`4` 中性、`5` 悲伤、`6` 惊讶、`7` 轻蔑。
  显式情绪按上游 softmax 标签规则输入预测网络，在源人物条件准备后生效，不会被源人物的中性默认值覆盖。
- `use_d_keys`：`exp`、`pitch`、`yaw`、`roll`、`t`，分别控制口型表情、点头、转头、歪头和位移。
  权重范围为 0–2，默认全部为 1；提供字典时，未列出的通道保持源图，对应上游的通道选择语义。
  权重只缩放预测变化，不额外生成动作；表情合成仍保留当前引擎的唇部、眼部区域约束。

显式情绪参与静音口型校准的缓存身份；完整情绪和幅度设置参与动作缓存校验，参数不匹配时要求重新预测。
兼容源人物校准的模型使用 `emotion-linked-mouth-calibration-v3`：同一人像按实际情绪条件分别预测
6 秒静音、3 个种子的口型参考；更换情绪后计算或复用该情绪自己的参考，不能套用中性目录中的人工适配项。
非中性情绪同时核验同一人像的中性基准，保留原有源图偏移上限；当前情绪参考另行接受跨种子、跨时间稳定性检查。
稳定的情绪静息偏移不再因偏离中性源图而被拒绝。旧版校准和动作缓存失效，静音音频特征仍可共享。
这项校准对齐口型基准，不构成情绪自然度或唇音同步质量的验收。
结果记录 `motionControls` 和实际采用的 `emotionCondition`。MCP 本轮未增加这些参数。

社区版的私人 `ditto/`、条件缓存、微调检查点和旧 Core ML 导出包均不在公开镜像下载、删除或发布清单中。
原有文件继续保存在开发者忽略目录，开源默认路径不会探测它们。
既有源人物口型适配代码仍可用于显式指定的兼容模型；公开原版保持其官方配置，
没有把微调包的条件或静音校准默认值套用到公开模型。

Ditto、YuNet、LivePortrait、HuBERT 等组件各自保留适用许可；公开可下载不等同于统一商业授权。
旧本地模型目录的 InsightFace 路径仅为兼容及回归对照保留，不在公开下载清单内。
请结合上游 NOTICE 与 THIRD_PARTY.md 审核使用范围。

## App Store 的 focused-05000

`MOSS_APP_STORE=ON` 使用 `config/models-store.json` 覆盖公共清单中的 A2E、配置和 manifest，
并增加 MediaPipe Face Landmarker v1。A2E 为 5000 步微调版，摘要固定为
`bb30d10ccc0ef8746c2623b558cebb8fa6e37dbc1ec0f7bdc04a7243fafa3164`。
`config/ditto-store/config.json` 启用 `source-neutral-v1` 和 `mouthConditionReference`；
YuNet、HuBERT 和渲染组件沿用公共清单。新的 pipelineId 防止复用旧模型的动作缓存。

微调权重和 Face Landmarker 标记为 `appleOnly`，只通过当前 App 的 Apple-hosted asset pack 下载；
用户选择公开镜像也不会让这些文件访问 Hugging Face。不带此编译选项的 GitHub/DMG 仍使用官方 A2E。
权重不嵌入应用或源代码归档；源代码中的摘要和配置不是模型权重。

创建上传清单时显式指定 `scripts/apple_model_assets.py inventory --app-store`。
上传资源包只表示可供测试；客户端必须更新到包含此清单的新构建，并完成 TestFlight 下载验收及 Apple 审核。

## ModelScope 与 Hugging Face 双源

公开版 HuBERT 使用 `ditto_onnx/hubert.onnx`（SHA-256 `327a5b90c0bfb9df251c9af91d0bea5e6284dbb0a4d02814f1d3ace6b4c2b5aa`），两源字节一致。对原流式导出进行了 7 个 6,480-sample 窗口数值对照，最大误差小于 1e-6；并更新两渠道 manifest 与 pipelineId，避免复用旧特征缓存。私人 denoiser 未改动。

关键点 ModelScope 副本位于 `KwaiVGI/LivePortrait/liveportrait/landmark.onnx`，与当前 `landmark203.onnx` 完全一致。Warp 从双源下载原始 `warp_network_ori.onnx`，由下载器原生调整 IR、opset 和 GridSample 模式名称；转换后仍要求与原有 opset-20 文件完整 SHA-256 相符。
