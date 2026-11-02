# Maintaining documentation

Use `rebuild/main` as the code and release baseline. Documentation changes do not establish a
console hardware result: keep the source, host-test and hardware distinctions on experimental
feature pages.

RiptOPL's website, wiki and `docs/` are complete on their own. Do not answer a reader with
"see the legacy site" or a link into the legacy Open-PS2-Loader repository for content this
project should carry: port the text, check it against this tree's code, and link the local copy.
Links to the legacy repository are for history (an Open-PS2-Loader PR, issue or pinned commit),
labelled as such. Before finishing a docs change, search for
`nathanneurotic.github.io/Open-PS2-Loader` and `NathanNeurotic/Open-PS2-Loader/blob` and check
that every hit is history.

## Ownership

| Surface | Edit here |
| --- | --- |
| Repository overview | `README.md`; link to task guides instead of repeating them |
| Detailed source references | `docs/`; start at [the index](README.md) |
| Website task guides and navigation | HTML on `gh-pages` |
| External-tools catalogue in README, website Home and Releases | Templates in `.github/workflows/docs-sync.yml`; hand edits to generated catalogues are overwritten |
| Website RetroAchievements body | `docs/RETROACHIEVEMENTS.md`, rendered by `site_sync.py --ra-md` |
| Website ps2sdk notes, game formats, launch diagnostics, defaults and game library bodies | `docs/PS2SDK-NOTES.md`, `docs/GAME-FORMATS.md`, `docs/LAUNCH-DIAGNOSTICS.md`, `docs/DEFAULTS.md`, `docs/GAME-LIBRARY.md`, rendered by `site_sync.py --md-page` |
| Website search | Page content, then regenerate `data/search-index.json`; curated entry titles/categories are preserved |
| Wiki maps, homepage, aliases and conversion | `.github/scripts/wiki_sync.py`; `.github/workflows/wiki-publish.yml` publishes the generated pages to the separate wiki repository |
| Language install and translation coverage | `docs/LANGUAGES.md`, `tools/audit_translations.py`; copy the user instructions to `gh-pages/languages.html` and regenerate the wiki |

`docs-sync.yml` runs on every push to `rebuild/main` and daily, and pushes the generated pages to
`gh-pages`. A hand edit to a generated page body on `gh-pages` is overwritten within a day; edit
the Markdown here instead.

Keep existing URLs and section anchors when consolidating material. A short forwarding section
is preferable to silently breaking a bookmark. Preserve attribution and verbatim third-party
notices.

## Local checks

Use separate source, Pages and wiki checkouts. The examples assume `source/`, `site/` and
`wiki/` are siblings. In the source checkout, create a Python environment and install:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r .github/requirements-docs.txt
python .github/scripts/site_sync.py --site ../site --ra-md docs/RETROACHIEVEMENTS.md \
    --md-page docs/PS2SDK-NOTES.md=ps2sdk-notes.html \
    --md-page docs/GAME-FORMATS.md=game-formats.html \
    --md-page docs/LAUNCH-DIAGNOSTICS.md=launch-diagnostics.html \
    --md-page docs/DEFAULTS.md=defaults.html \
    --md-page docs/GAME-LIBRARY.md=library.html --check
python .github/scripts/validate_site.py ../site
python .github/scripts/test_site_sync.py
python .github/scripts/test_wiki_sync.py
python tools/audit_translations.py --strict
git diff --check
git -C ../site diff --check
```

`validate_site.py` checks local links, section anchors and duplicate HTML IDs on every site page
without network calls. Inspect changed pages in a browser, including navigation, narrow layouts,
tables, search and download links. An HTTP 403 or timeout is inconclusive; an Actions artifact
may return 404 when viewed without sign-in. Check run/artifact metadata before calling it expired.

## Wiki publication

GitHub wikis have a separate Git history and cannot receive normal repository PRs. Review the
builder changes in the source PR and the guide changes in the Pages PR; the wiki is then rebuilt
from the merged branches, so every generated link names a published revision.

`.github/workflows/wiki-publish.yml` publishes it from GitHub Actions, so no local push is needed.
It runs `wiki_sync.py` against the current `rebuild/main` and `gh-pages` and pushes the result to
the wiki with the workflow's token, fast-forward only, and commits nothing when nothing changed. It
starts when `docs-sync.yml` finishes on `rebuild/main` (so after every push there), daily at 05:17
UTC, and by hand from the Actions tab (**Publish Wiki**, with a dry-run option). A change merged only
to `gh-pages` therefore reaches the wiki with the next `rebuild/main` push or the daily run. Before
building, the workflow deletes the pages the previous run generated (listed in the wiki's
`_tools/manifest.json`), after checking each one's SHA-256; a page edited on the wiki, or a page the
builder does not own, stops the run, so port such an edit into `docs/` or `gh-pages` first or it
would be lost.

To check a build locally, use separate checkouts as above:

```sh
python .github/scripts/wiki_sync.py --source . --site ../site --wiki ../wiki
git -C ../wiki diff --check
git -C ../wiki diff --stat
```

Run generation twice with unchanged inputs and confirm no second diff. The builder validates
internal wiki links/anchors and preserves text, tables and code examples when importing guides.
Overlapping reference pages become small source-section indexes, preserving old wiki URLs and
anchors; unique references remain full pages. A local build is for review; leave publishing to
the workflow.

## Language-guide publication order

Maintain the technical pipeline and audit commands in `docs/LANGUAGES.md`. Update the public
installation steps in the `gh-pages` branch's `languages.html` before regenerating the wiki:
`wiki_sync.py` imports the website Languages page into the wiki's `Languages` entry and keeps
the separate `Language-Maintenance` technical page from the repository. A source-only change does
**not** update the published wiki.

`docs-sync.yml` regenerates the website search index from the published HTML, but it does not
translate strings or update `languages.html` from `docs/LANGUAGES.md`. Run the coverage audit
after new UI labels are appended and before claiming any language is fully translated.
