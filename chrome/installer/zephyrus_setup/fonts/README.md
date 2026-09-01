# Fonts embedded in the Zephyrus installer

`Inter-Regular.ttf`, `Inter-SemiBold.ttf` — https://rsms.me/inter/

## Licence

Inter is licensed under the **SIL Open Font License 1.1**, which permits
embedding and redistribution and **requires that the licence text accompany
the font**.

`OFL.txt` is the verbatim licence, fetched from the Inter project
(`rsms/inter`, `LICENSE.txt`). It is not transcribed or summarised — a licence
has to be the exact text.

The obligation is met by *embedding* it, not merely by keeping it here:
`IDR_LICENSE_INTER_OFL` in `setup_resources.rc` puts it inside
`zephyrus_setup.exe`, so it travels with the fonts even though the installer
ships as a single downloaded file. A file left only in the source tree would
not accompany anything a user receives.

Stage 2 additionally writes it into the install directory, so the licence sits
beside the installed browser rather than only inside the installer.

## Why the fonts are embedded at all

Inter is not a Windows system font. Requesting it by name works on a machine
that happens to have it installed and silently falls back elsewhere — so the
installer would look correct to whoever designed it and wrong to everyone else.
