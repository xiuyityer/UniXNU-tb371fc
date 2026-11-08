# Notices and licensing

This is an independent TB371FC device port assembled from UniXNU and community implementations.
It is not an official Lenovo or Apple project.

The root LICENSE is the UniXNU BSD-2-Clause-Patent license. It applies to port integration code
unless a file or component specifies another license. Existing copyright and SPDX notices
are preserved. Imported components keep their own licenses:

- Adreno650, AdrenoMetal, ArmSmmu4K, QcomGeniSpi, ShengHidInput and the imported Novatek protocol:
  use the LICENSE inside the corresponding directory and each file's SPDX notice.
- TB371FC GPU/display glue and common Adreno PM4/HFI source: BSD-2-Clause-Patent where marked.
- PlatformPower helpers: MIT where marked.
- RootLog common code: BSD-2-Clause where marked; license text is in LICENSES/.

The V129 allocation policy is adapted from UniXNU/Adreno740; the file remains subject to
its original MIT license. The Metal frontend and its Pipa translator patches retain their
upstream licensing. The source lock and UPSTREAM.md identify the corresponding projects.

Apple operating-system files, original Apple binary components, device firmware blobs,
private device dumps and credentials are not included. External inputs must be obtained
separately and are not relicensed by this repository.
