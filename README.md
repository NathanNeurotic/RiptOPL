# RiptOPL test builds

Untested builds kept here so their downloads don't expire like GitHub Actions artifacts do. They are not releases. Use the normal [release channels](https://nathanneurotic.github.io/RiptOPL/releases.html) for everyday play.

| File | What it is | Source | Status |
| --- | --- | --- | --- |
| `RiptOPL-freeze-report-v4-korium-ingamediag-UNTESTED-2026-11-01.zip` | Korium, `ingamediag` configuration (in-game freeze report v4), for the Tales of the Abyss freeze ([#185](https://github.com/NathanNeurotic/RiptOPL/issues/185)) | Test PR [#250](https://github.com/NathanNeurotic/RiptOPL/pull/250), commit `dad4117f` (CI run [41317954534](https://github.com/NathanNeurotic/RiptOPL/actions/runs/41317954534), merged-with-base commit `c1d2f59f` in `BUILD-MANIFEST.json`). Zip SHA-256 `f2e384fd512e6092e2bf47ca6b39cfd2aeba70ad538bdc309c1c10cb9c3bea7e` | **UNTESTED**, waiting on testing |

The zip is a loader build only: `RIPTOPL.ELF` plus its manifest. Keep your normal RiptOPL install for everything else. See [Testing a PR or Actions build](https://github.com/NathanNeurotic/RiptOPL/blob/rebuild/main/docs/TEST-BUILDS.md).
