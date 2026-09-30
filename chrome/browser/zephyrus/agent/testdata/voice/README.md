# Voice test fixtures

Synthesised with the two Windows text-to-speech voices (Microsoft Zira and
Microsoft David, en-US), 16 kHz, 16-bit, mono. Zira is the "owner"; David is
another speaker.

- `*_wake_0..4.wav`: "Hey Zep" at five speaking rates (enrolment recordings).
- `*_cmd_*.wav`: "Hey Zep, <command>" in one breath.
- `*_other_*.wav`, `zira_word_zep.wav`: speech that is not the phrase.

Regenerate with `System.Speech.Synthesis.SpeechSynthesizer` (`SetOutputToWaveFile`
with a 16 kHz mono format). Synthetic voices are much more consistent than
people, so these prove the plumbing and the separation of clearly different
voices, not how the lock behaves on a real microphone.
