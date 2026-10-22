# Native MOSS FlowEdit

This adapter implements source/target velocity-difference editing using the
existing MOSS diffusion model, Qwen text encoder and VAE. It is derived from
[FlowEdit](https://github.com/fallenshock/FlowEdit) (Kulikov et al., ICCV 2025),
under the included MIT license. The algorithm reference was
`FlowEdit_utils.py` (SHA-256
`9115a12b872d8dc46e099fac7d9fa946f31e65a170a5829662c9c99554004ac3`);
its Python runtime is not bundled. No weights, attention processors or model
architecture are changed; no extra model is loaded.

## Build and use

`scripts/build.sh` applies `engine.patch` to the pinned stable-diffusion.cpp
revision `b13a48219d5cdf408edcb93da6cd69b612ed03d3`, copies the native header,
and builds the engine. `scripts/prepare_flowedit.py` is idempotent and refuses
incompatible source changes. Python is a build tool, not an inference runtime.
The resulting CLI answers `--moss-flowedit-version` with `MOSS_FLOWEDIT_V1`.
The app checks this capability before starting; an old engine cannot silently
fall back to ordinary image-to-image.

In Edit Image → Whole Image → Image to Image, select **Preserve appearance**.
Describe the source image accurately, then describe the full desired image in
the composer. Keep unchanged details consistent between the descriptions.
Source description, method and strength persist with the work and job recipe.
Free redraw and selected-area editing retain the existing pipeline.

The public application runs this path locally. The CUDA experiment used the
same adapter on an independently built engine; private server configuration is
not part of this repository.

## Sampling contract

Let `z` be the encoded source and `edited` initially equal `z`. At each selected
sigma, draw shared noise and construct:

```text
x_source = (1 - sigma) * z + sigma * noise
x_target = x_source + (edited - z)
edited += (next_sigma - sigma) *
          (v(x_target, target_description) - v(x_source, source_description))
```

Both velocities use the same existing MOSS CFG behavior, including the negative
branch's native skip-layer 22. The app uses Euler, the discrete schedule with
flow shift 5, one noise sample per step, and no final target-only steps.
Strength maps to `round(total_steps * strength)`, clamped to 1…total_steps.
The default 20/28 fraction gives 20 steps at a 28-step schedule. It is separate
from free redraw's denoising strength. The latter CLI parameter is set to 1
for FlowEdit so it does not shorten the schedule a second time.

The app clears inherited `MOSS_FLOWEDIT_*` environment variables for every
local job. Only a FlowEdit edit receives `MODE=flow`, `SOURCE_PROMPT` (a UTF-8
file), `NMAX`, `NMIN=0`, `NAVG=1`, and `SOURCE_CFG` equal to target CFG. The
adapter also retains `MODE=vae` and `MODE=euler` for reconstruction/parity
controls. These diagnostics are not user-facing editing modes.

FlowEdit rejects masks, reference-image tokens, LoRA, control inputs, hires and
multi-image batches. The app provides the original image, two descriptions and
sampling settings. The decoded result is only cropped to remove inference
padding; no face or background pixels are pasted back. A saved local brush
selection is retained for later use but is neither passed to FlowEdit nor used
to composite its output.

## Evidence and limitations

The README comparison is two independent edits of the same 768×1024 portrait:
28 steps, Q4_K, CFG 3, seed 42, NMAX 20, NMIN 0, NAVG 1. The CUDA experiments
completed in about 63 seconds each on an RTX 3090. The subject's facial shape
and composition stayed visually closer than with ordinary image-to-image;
skin tone, fine details and jewelry still differed from the input or request.
This is one demonstrated case, not an identity or quality guarantee.

Control experiments showed that equal source/target captions returned the
same pixels as VAE reconstruction, and the Euler control matched the original
pipeline at the tested settings. Even VAE reconstruction changes pixels.
Higher strengths can increase skin, clothing and facial drift. Repeated edits
can accumulate reconstruction and editing drift.

MOSS also accepts structured [Layout prompts](https://github.com/OpenMOSS/MOSS-Image/blob/main/USERGUIDE.md#layout-prompts-experimental).
Both descriptions can contain model-facing Layout JSON (`summary`, `items`,
centered pixel boxes in `yxyx` order). Keep source/target boxes and unaffected
captions identical and change the desired item's caption. This is semantic
spatial conditioning, not a hard mask or an exact outside-pixel lock. The
existing layout canvas is not yet connected to paired FlowEdit layouts;
manual structured descriptions are an experimental path, not a guarantee of
precise local editing.

A follow-up CUDA comparison used the edited portrait as input and changed only
the necklace from gold to silver. Both plain paired descriptions and paired
Layout JSON completed the edit at the same sampling settings (about 64 and
67 seconds respectively). They looked similar in this example; this does not
establish a Layout advantage or exact preservation outside the necklace box.
The integrated engine also reproduced the original experimental jewelry edit
byte-for-byte, including the PNG file.

The main Qt conversation path was also exercised on Apple Metal at 384×512,
28 steps / 20 FlowEdit steps (about 228 seconds). The saved result matched the
raw decoded pixels, with no inference mask or composite. The 768×1024 Mac
attempt was stopped after 425 seconds due to slow sampling; full-resolution
Mac completion is not claimed. Chinese/English, light/dark and the minimum
window were checked, including Cocoa window renders and Qt keyboard events.
Edit-state, conversation persistence and public-release tests passed. These
checks do not constitute a new signed/notarized installer release.
