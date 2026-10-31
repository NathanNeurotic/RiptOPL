# Languages / translations — how they work and how to maintain them

RiptOPL's on-screen text is built from three sources that `tools/lang_compiler.py` merges at
build time. This doc explains the pipeline and the exact steps to add or update strings.

## The pieces

| Piece | What it is | Do you edit it? |
|---|---|---|
| `lng/tmpl/_base.yml` | The fork's **master list** of every string + its **English** text. Source of truth. | **Yes** — add strings here. |
| `lng/fork/<Language>.yml` | The fork's **own translations** of fork strings (and overrides). | **Yes** — add translations here. |
| `lng_src/` (git-ignored) | The **upstream** community translations, cloned from [`ps2homebrew/Open-PS2-Loader-lang`](https://github.com/ps2homebrew/Open-PS2-Loader-lang) by `tools/download_lng.sh`. | **No** — upstream owns these. |
| `tools/lang_compiler.py` | The tool that assembles everything into the binary `.lng` files + the C sources. | No (it's the machinery). |

### `_base.yml` entry format
```yaml
- label: MMCE_PREFIX
  string: MMCE Prefix Path
```

### `lng/fork/<Language>.yml` entry format
```yaml
translations:
  MMCE_PREFIX: Chemin du préfixe MMCE
```

## The build flow (`make languages`, all via `tools/lang_compiler.py`)

1. **`--make_header` / `--make_source`** → generate `include/lang_autogen.h` (the `_STR_*`
   enum every `.c` file uses) and `src/lang_internal.c` (the built-in **English fallback**)
   straight from `_base.yml`. So English is always complete and automatic — you never
   translate English, and a missing translation always falls back to English, never blank.
2. **`--update_translation_yml`** → sync each upstream `lng_src/<Lang>.yml` against
   `_base.yml`; any label it lacks is marked `untranslated`.
3. **`--overlay_translation_yml --overlay lng/fork/<Lang>.yml`** → pour the fork's own
   translations on top. If upstream later translates a label, the overlay quietly steps
   aside (upstream wins). Idempotent — safe to re-run.
4. **`--make_lng`** → compile the merged `.yml` into the binary `lng/lang_<Lang>.lng`.

`lang_autogen.h` and `lang_internal.c` are generated (git-ignored) — never hand-edit them.

## How to ADD a new string

1. Add it to the **END** of `lng/tmpl/_base.yml`, below the APPEND-ONLY banner (~line 923):

   ```yaml
   - label: MY_NEW_SETTING
     string: My New Setting
   ```

   External `lang_*.lng` files are consumed by **line position** (`lang.c`), so inserting a
   label mid-list shifts every later string ID and makes stale user language packs show the
   **wrong text** — never an English fallback.
2. Use `_STR_MY_NEW_SETTING` in code (the enum id is auto-generated).
3. That's enough to ship — it shows English everywhere until translated.

## How to TRANSLATE a string (per language)

Add the key under `translations:` in the target `lng/fork/<Language>.yml`:
```yaml
translations:
  MY_NEW_SETTING: Mon nouveau réglage
```
Repeat per language you want to cover. Untranslated languages fall back to English.

## Important: there is **no auto-translator** in the build

`tools/lang_compiler.py` only *merges* translations that already exist in `lng/fork/*.yml`. The
initial bulk fill of the 31 fork languages was a **one-off external step** (a machine
translation pass), not part of this repo's build. So to keep translations current you must
supply the translated text for `lng/fork/*.yml` yourself (by hand, an external MT tool, or
an assistant) — the build will not invent it.

## Which languages are built, and adding one

The `TRANSLATIONS` list in the `Makefile` decides which `lang_<Language>.lng` files get built. A
language exists upstream as `lng_src/<Language>.yml`; to ship it, add its name to that list.

- **Fonts.** A language that needs glyphs the built-in font lacks has a `font_<Language>.ttf` (or
  `.otf`) in the upstream repo's `thirdparty/` folder. `tools/lng_pack.sh` copies it beside the
  `.lng` in the `RIPTOPL-LANGS` pack, and the loader picks it up from the same folder.
- **Serbian** has no font upstream, and the built-in font has no `č`, `ć` or `đ`. Serbian Latin uses
  exactly Croatian's letters, so `lng_pack.sh` ships `font_Croatian.ttf` as `font_Serbian.ttf`.
  Check a new language's characters against the built-in font before assuming it needs no font.
- **Fork strings.** A language with no `lng/fork/<Language>.yml` shows
  the fork's own strings in English until someone adds that file.

## Building just the languages

```sh
make languages
```
Requires `python3` + `PyYAML` (`py3-yaml`) and network access for `tools/download_lng.sh` (first
run clones the upstream lang repo into `lng_src/`). CI does this automatically.

## Where the fork strings live

Fork-added strings are the labels in `lng/tmpl/_base.yml` that upstream doesn't have. To
see which are still English-only in a given language, run `make languages` and look for
`untranslated` entries in `lng_src/<Language>.yml` after the overlay step.

## Where language files are found

Language registration grows as needed (no 15-file limit). Precedence, highest first:

1. **Beside the launched ELF (CWD)**, e.g. `mass0:/APPS/APP_RIPTOPL/lang_French.lng`.
2. **CWD/LNG/**, e.g. `mass0:/APPS/APP_RIPTOPL/LNG/lang_French.lng`.
3. **Other locations**: `mc0:/OPL/` and each enabled device's `LNG/` folder.

Only files named `lang_<Name>.lng` (any case) count. A name appears once in the menu; a
higher-priority copy wins, including one whose folder only becomes readable after the storage
drivers mount. A matching `font_<Name>.ttf`/`.otf` beside the file is loaded with it.

## Right-to-left and CJK text

Ported from [RIPPS2](https://github.com/akilluminati47/RIPPS2) build 104 by [akilluminati47](https://github.com/akilluminati47) (`src/gfx/textshape.cpp`). NOT TESTED on hardware.

- **Arabic and Persian** letters are joined into their presentation forms (lam-alef as one
  letter), and each line is laid out right to left. Latin words and numbers keep their own order,
  and brackets are turned round. Centred Arabic stays centred, and zero-width marks draw nothing.
  The forms table is RIPPS2's, generated from Unicode 16.0.0.
- **CJK, Thai and Lao** lines may break between letters, but never before closing punctuation or
  before a vowel that belongs to the letter in front of it.
- The settings hint box and message boxes wrap with these rules, break an over-wide word anywhere,
  and never cut a character in half.

A translation needs no markup for any of this: write the text in logical order, as typed. The
language still needs a font that has the glyphs.

## Translations that misread their arguments

Every string in a loaded `.lng` is checked against its English string's printf conversions. A
translation that would read different arguments (upstream French "Auto start" has `%s` where
English has `%i`, which crashed the countdown) is replaced by the English string. The same
conversions, or fewer of them in the same order, are kept.

`python3 tools/audit_translations.py` reports, per language, how many strings are translated and how
many fall back to English. With `--strict` (run by the host tests) it fails on an overlay label the
base does not have, or an overlay string whose conversions differ from English. Community strings
that differ are listed as warnings, since the loader already shows English for them.

The fork overlays were backfilled from the original project's `lng_fork/` translations, only for
labels whose English text is identical here and only where an overlay lacked the label. Galician
and Serbian now have overlays too. These are machine-assisted and still need speaker review.

For **UDPFS Files**, language files on the PC belong directly in the served root's `LNG/` folder
(any case), e.g. `LNG/lang_Portuguese.lng`, with its font beside it. RiptOPL registers that folder
once the server answers, including while a remembered PS1 page is shown. Copy the files out of the
release pack's language subfolder rather than placing the whole pack directory under `LNG/`. This
does not apply to UDPFS IMG or UDPBD disk-image roots.
