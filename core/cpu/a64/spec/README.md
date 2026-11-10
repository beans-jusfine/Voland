# Pinned A64 decoder input

Voland's initial shared A64 decoder is generated from Arm's A-profile Machine Readable Architecture Specification (AARCHMRS) release `2026-09_rel`, schema `2.9.6`, dated 2026-09-21.

- Official upstream archive: <https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-OS-Machine-Readable-Data/AARCHMRS_BSD/AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09.tar.gz>
- Upstream mirror commit: `1346b16d47619400448f3cb6d4f22d62d3b76848`
- Archive SHA-256: `d5bf3f538663fb18be081ba8e9f08b188ed06f81dd19d4aa2da5b2795103a4ec`
- Vendored subset: `AARCHMRS-2026-09-A64-supported.json.gz`
- Vendored subset SHA-256: `a57997df7def8e9f1ef9d7c6d5bb5f10e15bb017103ef5dfa233fcd3a1947454`

The compressed JSON contains only the 76 concrete A64 encoding forms selected for the interpreter's initial instruction coverage. It preserves each form's upstream operation ID, name, condition, and encoding fields. It excludes A32/T32, aliases, prose, and unrelated A64 forms; it is not a claim of full A64 ISA coverage. The selection and generated C are reproducible with `python tools/a64/generate_decoder.py --output-dir build/a64-generated`.

The exact release's upstream `docs/notice.html` is retained as `NOTICE.html`. It identifies the material as non-confidential and licensed under the BSD 3-Clause license. Arm's copyright and all three redistribution conditions and disclaimer are preserved there.
