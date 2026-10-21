# Original synthetic Chinese and English references — 2026-10-16

Two additional presets, `videocut-original-mandarin-01` and
`videocut-original-mandarin-02`, were generated on the isolated RTX 3090 from
original text and generic adult voice descriptions using the fixed official
Qwen VoiceDesign model. No human reference recording was provided.
The user also selected all 20 voices in the later listening collection for
built-in distribution: Mandarin/English, five design age groups, female/male.
Their stable IDs are `videocut-original-{zh,en}-{child,teen,young,middle,senior}-{female,male}`;
the catalog retains the corresponding `preview_id` for the original recordings.
The user then approved the six second-round regional-accent candidates:
Northeast Chinese female A/B, Northeast Chinese male A/B, and Sichuan male A/B.
Their IDs are `videocut-original-dongbei-{female,male}-{a,b}` and
`videocut-original-sichuan-male-{a,b}`. These are new original synthetic voices;
no human reference recording or real-person imitation was used.
The current catalog contains 28 synthetic references (12 Mandarin, 10 English,
6 Chinese regional-accent candidates) and the 21 separately attributed English
donor references below, 49 in total. The regional labels describe the accepted
reference clips; they do not promise dialect preservation by Hojo on new text.

All synthetic WAVs use the existing mono 16 kHz PCM16 Hojo reference contract.
See `assets/licenses/VIDEOCUT-SYNTHETIC-VOICES-NOTICE.txt` for their use terms
and authoring model attribution. Their legacy `consent_confirmed` value records
project authorization for synthetic output; `human_speaker_consent` is
`not_applicable` and no donor evidence URL is fabricated.

Original WAVs, input text and generation logs are retained in
`work/audio-rights-closeout-2026-10-15/evidence/qwen-3090/` and separate release
source materials. The 20-voice collection is retained under
`work/audio-rights-closeout-2026-10-15/voice-preview-20-2026-10-16/`.
The six accepted accent references and user adoption record are retained under
`work/audio-rights-closeout-2026-10-15/accent-retry-6-2026-10-16/`.
The first-round audio and the user's unfavorable feedback remain available;
those first-round accent references were not added to the built-in catalog.
Initial catalog digests are created only when publishing new WAVs for the existing
Hojo/cache identity contract. Existing material was not fingerprint-scanned.
The user listened to the second-round six and approved them for use. No automated
pronunciation/transcript checks or Hojo inference were run; cloned voice quality
and accent retention remain unverified. The names explicitly identify AI synthesis.

# Built-in Kyutai donor voices — 2026-10-15

The 21 donor presets are original WAVs from the verified voice-donations/
subset of kyutai/tts-voices, revision 323332d33f997de8394f24a193e1a76df720e01a.
They replace all 21 former OHF/VCTK presets. Old IDs are retired, never silently
reassigned to another speaker. User-created voices are unchanged.

The publisher identifies this subset as CC0 and says 228 contributions passed
verification: https://huggingface.co/kyutai/tts-voices
The donor terms expressly cover commercial/non-commercial reuse, copying,
transformation, republication, voice replicas and speech the donor did not
write. Contributors declare adulthood, their own voice, and authority to
contribute. The published contribution workflow also requires a spoken
voice-cloning consent sentence. Thus consent_confirmed records this explicit
donation process, not merely a corpus label or a model license.

Terms: https://kyutai.org/next/legal/Terms%20of%20Use%20-%20Unmute%20Voice%20Donation%20Project%20v1.pdf
Workflow: https://github.com/kyutai-labs/unmute/blob/main/unmute/tts/voice_donation.py
Donation closure/published subset: https://unmute.sh/voice-donation

Audio is converted to mono 16kHz PCM16 for the existing Hojo interface.
Reference transcripts are authored from the resulting speech; authoring tools
and ASR weights are not product runtime dependencies. These references are
English; no donor gender or identity is inferred. Chinese display names are
neutral numbered community voices, not new claims about recording language.

CC0 text and original donor terms accompany the catalog. Respect the retained
terms, applicable privacy/personality rights, and the upstream acceptable-use
condition; do not imply donor endorsement or use a preset for deception. The
published terms provide unmute@kyutai.org for donor withdrawal requests.

Only one initial digest per authored WAV is stored because the existing voice
catalog requires it. No recurring audit, extra runtime validation or per-frame
fingerprint check is introduced. This delivery does not claim TTS quality tests.

Previous recordings and provenance remain in
work/audio-rights-closeout-2026-10-15/before/assets/builtin/voices/, outside
production assets and build inputs.
