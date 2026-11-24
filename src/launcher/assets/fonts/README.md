# Fonts of the launcher

| Files | Family | Licence |
|---|---|---|
| `Onest-Regular.ttf`, `-Medium`, `-SemiBold`, `-Bold` | Onest, (c) 2021 The Onest Project Authors | SIL Open Font License 1.1, `OFL-Onest.txt` |
| `JetBrainsMono-Medium.ttf`, `-Bold` | JetBrains Mono, (c) 2020 The JetBrains Mono Project Authors | SIL Open Font License 1.1, `OFL-JetBrainsMono.txt` |
| `DelaGothicOne-Regular.ttf` | Dela Gothic One, (c) 2020 The Dela Gothic Project Authors | SIL Open Font License 1.1, `OFL-DelaGothicOne.txt` |

These are **modified versions**, made by `tools/launcher_fonts.py`; none of the families declares a Reserved
Font Name. The fonts are compiled into `meleenet-launcher.exe` as resources.

- Onest and JetBrains Mono: from the files Google Fonts serves (variable fonts, one file per range of
  characters). For each weight the Latin and the Cyrillic file were pinned at that weight and merged into one
  static file; the layout tables (kerning, alternates) were left out.
- Dela Gothic One, the headings' font: the Latin and the Cyrillic part (317 characters, 30 KB) of
  `DelaGothicOne-Regular.ttf` from github.com/google/fonts, `ofl/delagothicone` (9030 characters, 2.5 MB,
  most of them Japanese). Names, metrics and kerning are the original's.

Without `DelaGothicOne-Regular.ttf` in this folder the build still works: the launcher then draws the
headings with Arial Black of Windows.
