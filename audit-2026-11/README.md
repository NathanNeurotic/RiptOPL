# Five-repo audit — both finals + comparison (reports only)

This folder contains the complete deliverables of two independent LESS-NOT-MORE audits of **NathanNeurotic/RiptOPL** at commit **2e347d5ac885** (branch `rebuild/main`), plus the mechanism-level comparison of the two finals. It is documentation only: **no code in this repository is changed by these files.** The accompanying pull request is a draft, opened at the owner's direction as a delivery vehicle; whether anything here is ever merged or implemented is the owner's decision.

## The two runs

- **Original audit** (`old/`) — nine rounds over 2026-11-13/14 plus two adversarial verification phases. Final: 1,200 numbered items across five repos, 1,182 actionable, 18 withdrawn. Start with `old/Sable-SUMMARY.md` and `old/Sable-MASTER-INDEX.md` (the index's §4 ledger overrides original finding text where they conflict). Implementation guide: `old/Sable-HANDOFF-PROMPT.md`.
- **Blind redo** (`redo/`) — a second team re-audited from the code alone, forbidden from reading the original's deliverables, then went through its own adversarial verification and reconciliation. Final: 885 numbered items, 879 actionable, 6 withdrawn. Start with `redo/Redo-SUMMARY.md` and `redo/Redo-MASTER-INDEX.md`.
- **Comparison** (`redo/Comparison-FINAL.md` and `redo/Compare-*.md`) — every finding in both finals matched mechanism-by-mechanism; non-matching Category 6 items were adjudicated against the code (REAL / NOT-REAL / COVERAGE). Union of the two finals: ≈1,557 distinct mechanisms. Neither final contains the other.

## This repo

Old final: 504 numbered findings, 489 actionable. Redo final: 268 numbered, 265 actionable. Independent overlap: 125 full mechanism matches + 71 partial.

**Most consequential redo-only find:** RE-RIPTOPL-020 — a hardcoded parental-lock master password `#define` in the public header `include/opl.h:56` (value deliberately not reproduced in the reports' fix text beyond location): a universal backdoor present in every build, missed by the original audit.
**Most consequential old-only finds:** RIPTOPL-281 (GSHandler REGIMM mask into a 32-entry jump table — uncontrolled jump in the exception handler), RIPTOPL-432 (SMB2 semaphore held over an unbounded server-declared drain), RIPTOPL-433 (SMB2 reply matching / FID confusion).

## Strike list — do not implement as filed

- OLD **RIPTOPL-500** ("orphaned korium test") — not real: `ps2build.yml` runs `tools/run_tests.sh -k`, whose glob covers the test. Do not act on it.
- REDO **RE-RIPTOPL-089** — the filed aliasing defect does not hold (distinct bounded arrays); survives only as a readability note.
- Cross strikes in `redo/Compare-Cross.md` also apply repo-wide: old X-32 item D5's "functionally equivalent" ruling is overturned (see RE-X-08), and old X-38's attr limb does not exist.

(Cross-repo strikes that affect every repo: old X-32 item D5's equivalence ruling is overturned by RE-X-08; old X-38's attr limb does not exist. Both finals' affirmative "this area is clean" lists should be treated as unverified — each run produced several wrong ones.)

## Layout

- `old/` — original audit: per-repo reports and round files, verification reports (`Sable-VERIFY-*`, `Sable-VERIFY2-*`), cross-repo reports, master index, summary, pins, handoff prompt, and section-level evidence.
- `redo/` — redo audit: per-repo reports (`Redo-*`), its verification reports (`Redo-VERIFY-*`), new findings from verification (`Redo-NEW-FINDINGS.md`), master index, summary, and the comparison reports (`Compare-*.md`, `Comparison-FINAL.md`) with their evidence sections.
- Shared/global files (indexes, summaries, cross-repo reports) are included in every repo's package because the cross findings and indexes span all five repositories.

## Archives (download links expire 2026-11-17 ~10:55 UTC; anyone with a link can download)

- Both finals + comparison, all five repos (699 files, 6,078,940 B, SHA-256 `52824c1ac885f9047003d2ada77d8d3c47ca5b61819fb1d7b7474033c3cbb862`): https://muse.ai/files/570121466194596/2866327167094404/ckbdv71m8bppkfs4su9uz67z/Sable-audit-BOTH-FINALS-2026-11-15.zip
- This repo's package: https://muse.ai/files/570121466194596/1487985939869694/xff2v19618fcrutohhbvzvxt/Sable-audit-RiptOPL-both-finals-2026-11-15.zip
- Original final only (564 files, SHA-256 `2773df7c03bce277449a0acd293c63a50fe1177824ddcacc02f7885662d7388d`): https://muse.ai/files/570121466194596/1668795564645485/t1kdjzqeri3kpji8hi2aw4b5/Sable-audit-complete-2026-11-14.zip

## Limits

Nothing in either audit is PS2-build- or hardware-verified (no PS2SDK in the audit environment); performance findings are structural; live UDP interop was sandbox-blocked. Pins: RiptOPL `2e347d5ac885`, Launchstation `9fb0598a79f0`, POPSLoader `29272cddbf01`, Simple-Media-System `e65ced39a27e`, PS2-Servers `c5f021c4d386`.
