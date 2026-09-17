# Wiki migration

The upstream Open PS2 Loader wiki is a separate Git repository, not part of the
source checkout. Its source is:

```text
https://github.com/ps2homebrew/Open-PS2-Loader.wiki.git
```

As of 2026-09-17, `NathanNeurotic/RiptOPL` reports `has_wiki: false` and its
`RiptOPL.wiki.git` endpoint does not exist. This checkout therefore cannot
publish wiki pages without first enabling the target repository's Wiki feature
with repository administration access. No wiki content is fabricated or copied
into the source tree.

After enabling the Wiki feature, migrate the upstream pages explicitly:

```sh
git clone https://github.com/ps2homebrew/Open-PS2-Loader.wiki.git upstream-opl-wiki
git clone https://github.com/NathanNeurotic/RiptOPL.wiki.git riptopl-wiki
cd riptopl-wiki
git remote add upstream https://github.com/ps2homebrew/Open-PS2-Loader.wiki.git
git fetch upstream
git checkout -B master upstream/master
git remote set-url origin https://github.com/NathanNeurotic/RiptOPL.wiki.git
git push -u origin master
```

Review links and fork-specific wording after the mirror, then make any
RiptOPL-specific edits in the wiki repository. The upstream attribution and
history remain available through the upstream remote.
