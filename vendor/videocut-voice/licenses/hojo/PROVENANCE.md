# Hojo-TTS-Light distribution evidence

- Official repository: https://huggingface.co/HojoAI/Hojo-TTS-Light
- Locked revision: `5973d74eaefa2e425ed2c3beb0fefe9871958ca6`.
- The official model card labels the released ONNX model Apache-2.0 and
  explicitly describes secondary development and business integration.
- The six bundled payload byte counts and SHA-256 values are locked in
  `third_party/ai-models/source-lock.json` and match that revision.
- Apache-2.0 is shipped from `licenses/common/Apache-2.0.txt`.

## Chinese text normalization frontend

- Upstream: `wenet-e2e/WeTextProcessing`, Apache-2.0, immutable tag
  `WeTextProcessing` at `327efb8fa8bdd0082a3c128d265dd500f09d6da1`.
- Locked tagger: 3,808,194 bytes,
  SHA-256 `653e4a26ece711d269b95db56d33552099534716cfb341677338557e9ac6bdef`.
- Locked verbalizer: 419,726 bytes,
  SHA-256 `f2c41912a9675eb4cba566eb495bdd9718fdc36f3dfdfd9780e3f46b5064d617`.
- The written-to-spoken frontend is applied before the locked tokenizer so
  dates, money, percentages, measurements, and other written forms are spoken
  naturally. It is directionally separate from ASR inverse normalization.

The model license does not grant rights to a voice-clone reference recording
or to the represented person's voice/personality. Built-in voice cards and
user imports require separate provenance and consent.
