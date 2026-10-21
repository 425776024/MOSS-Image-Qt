# Native audio integration

Imported on 2026-10-20 at the user's request.

- `src/` and `include/`: selected native Hojo TTS backend, tensor/ORT helpers,
  text frontend and normalization code from the user's
  `VideoCut-Qt` checkout.
- `worker.cpp` and `src/MossModelAdapter.cpp`: Moss adapters. The worker reads
  a request file, normalizes/chunks text, uses a 3–30 second reference recording,
  and writes 24 kHz PCM WAV. This integration currently selects CPU inference.
- `assets/voices/catalog.json` and its WAV files: VideoCut-Qt's 28 selected
  synthetic voice presets. Preserve the catalog's individual source, license
  and synthetic-reference fields. Other copied reference assets retain their
  existing provenance and are not added to the active voice selector.
- Hojo ONNX weights and WeTextProcessing FST files are downloaded on demand
  from the pinned public catalog. Historical upstream evidence is under `licenses/hojo`;
  the current FST download revision and hashes are in `config/models.json`.
- Text-to-SFX uses `sa3.cpp` from the user's **VideoCut** checkout at
  `VideoCut/.local/ai-models/runtime/src/sa3.cpp`,
  revision `10d0373ec4f3e195cfee9bc73a39feefa933ecab`, with its bundled ggml
  sources. The MIT engine notice is `licenses/sa3-MIT.txt`.
  The five Small SFX GGUF payloads come from that checkout's
  `.local/ai-models/eval/stable-audio-3-small-sfx-gguf` directory.
  Engine licensing does not replace the separate model license.

`scripts/prepare_audio.sh` copies dependencies to this project's owned
directories and builds the Metal SFX executable. Neither reference checkout
is modified. Application execution is native; shell/CMake scripts are build
tools and are not needed by a packaged user.

This is an authorized local port, not a claim of independently authored
backend code. Public distribution still needs the exact model/dependency
notices and the release checks described in `THIRD_PARTY.md`.
