# Native audio integration

Imported on 2026-10-20 at the user's request.

- `worker.cpp`: Moss adapter using the audio.cpp C ABI, Ming-3.0 Q8 CPU
  inference, built-in tag-aware chunking and native 44.1 kHz PCM WAV output.
  The same worker is compiled for public and App Store editions.
- Runtime: https://github.com/0xShug0/audio.cpp at
  `db433f3e6dfe0159828532958698d56529bb3c5c` (Apache-2.0).
  `scripts/prepare_ming.py` builds a Ming-only deployment library, excludes
  eSpeak and download tools, hides ggml symbols and replaces debugger
  subprocess backtraces with native stack symbols.
- Weights: https://huggingface.co/audio-cpp/Ming-3.0-TTS-0.5B-GGUF at
  `2eb8d37c7f10672e69c8aa33091baa04c461b66c` (upstream inclusionAI MIT).
  The Q8 file is 1,742,280,896 bytes; hash is pinned in `config/models.json`.
- `assets/voices/catalog.json` and its WAV files retain the existing selected
  synthetic voice references, individual licenses and consent metadata.
  Custom voices retain their reference audio and transcript. Changing engines
  does not establish similarity or accent retention for each reference.
- The retired Hojo sources, headers and model notices have been removed.
  Hash-matched local Hojo weights and obsolete asset archives were removed
  from this checkout; existing voice recordings and generated audio are retained.
- Text-to-SFX uses `sa3.cpp` from the user's **VideoCut** checkout at
  `VideoCut/.local/ai-models/runtime/src/sa3.cpp`,
  revision `10d0373ec4f3e195cfee9bc73a39feefa933ecab`, with its bundled ggml
  sources. The MIT engine notice is `licenses/sa3-MIT.txt`.
  The default sound model is Stable Audio 3 Medium F16, with SAME-L and
  the shared T5Gemma encoder/tokenizer. The five pinned files are available
  from `LAVSSOFTWARE/Stable-Audio-3-Medium-GGUF` on ModelScope;
  upstream GGUF conversion is by `thepatch` on Hugging Face.
  File identities and provider-specific URLs are pinned in `config/models.json`.
  Engine licensing does not replace the separate model license.

`scripts/prepare_audio.sh` copies dependencies to this project's owned
directories and builds the Metal SFX executable. Neither reference checkout
is modified. Application execution is native; shell/CMake scripts are build
tools and are not needed by a packaged user.

This is an authorized local port, not a claim of independently authored
backend code. Public distribution still needs the exact model/dependency
notices and the release checks described in `THIRD_PARTY.md`.
