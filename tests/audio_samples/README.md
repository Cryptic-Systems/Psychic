# Real-programme audio evaluation samples

These recordings are reference inputs for offline evaluation of Psychic's adaptive track-state and section-transition analysis.

They are not CI fixtures and are not copied into the production package.

## Samples

- **Quiet Passages, Loud Sections & Noticeable Transitions** — intended to exercise dynamic range and section-transition tracking.
- **Heavily Compressed, Consistently Loud Commercial Mastering** — intended to exercise steady-state behaviour and false transition resistance.
- **Strong Kick Drums, Bass & Transient-Heavy Percussion** — intended to exercise transient rejection, bass prominence and pumping resistance.

## Evaluation notes

The files are MP3 sources. Measurements describe the decoded MP3 signal; codec artefacts and lossy encoding mean they cannot establish whether the original master clipped.

Keep the source recordings unchanged. Record codec, sample rate, channel count and duration in evaluation output. Do not infer musical section labels from signal measurements alone; verify detected transitions against listening or supplied annotations.

These files are for manual/offline evaluation only. Do not add them to automated CI or redistribute them unless the repository maintainer has the necessary rights.
