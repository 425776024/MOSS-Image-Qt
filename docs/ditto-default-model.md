# Ditto 公开原版

开源客户端使用 `digital-avatar/ditto-talkinghead` 的原始预训练权重，固定 Hugging Face
提交 `e4a2f60328ee7c32af585ac4b3cce299e4c8e254`，不使用本机训练检查点。
下载目录为用户模型根目录下的 `ditto-original/`。

- 音频动作：官方 `ditto_onnx/lmdm_v0.4_hubert.onnx`，本地名 `denoise.onnx`。
- HuBERT、检测器、106/203 关键点：官方 `ditto_pytorch/aux_models/` 中的原文件。
- 外观、动作、拼接、解码器：官方 `ditto_onnx/` 中的原文件。
- Warp：`voxta/ditto-talkinghead-onnx` 提交
  `930ae658ab5247e6209b21c4265cecf2a5b45610` 的 `warp_network_opset20.onnx`。
  此文件仅调整 ONNX opset、IR 与 GridSample 属性以兼容 ONNX Runtime，权重不变。
  上游修改说明：https://huggingface.co/voxta/ditto-talkinghead-onnx/blob/930ae658ab5247e6209b21c4265cecf2a5b45610/NOTICE

原生运行时支持分离的 warp / decoder 图，不要求 Python、PyTorch 或用户自行转换。
这条公开 ONNX 路径与私人 Core ML 优化包的性能不同，不能沿用旧性能数字。
它使用原始模型能力，不是缩小网络或简化渲染器。

`config/ditto-original/config.json` 是固定官方 `v0.4_hubert_cfg_pytorch.pkl`
转换成普通 JSON 的结果，已与原始导出配置逐项核对。
`schedule.json` 是同一官方 LMDM `setup(50)` 导出的 50 步调度常量。
`manifest.json` 绑定全部文件摘要；应用内置这些小型配置，网络下载权重。
完整下载来源、大小和 SHA-256 以 `config/models.json` 为准。

私人 `ditto/`、条件缓存、微调检查点和旧 Core ML 导出包均不在下载、删除或发布清单中。
原有文件继续保存在开发者忽略目录，开源默认路径不会探测它们。
既有源人物口型适配代码仍可用于显式指定的兼容模型；公开原版保持其官方配置，
没有把微调包的条件或静音校准默认值套用到公开模型。

Ditto、InsightFace、HuBERT 等组件各自保留适用许可；公开可下载不等同于统一商业授权。
请结合上游 NOTICE 与 THIRD_PARTY.md 审核使用范围。
