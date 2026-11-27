#!/bin/sh
# Run once after checking out the claude-audit-findings branch (cloud or fresh clone).
# Creates src/RiptOPL next to this script: the audited commit as a detached git worktree (ignored by git).
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(git -C "$here" rev-parse --show-toplevel)
sha=b00e773bf28c1790b458ab53f25caad69e79f237
if [ ! -e "$here/src/RiptOPL" ]; then
  git -C "$top" cat-file -e "$sha^{commit}" 2>/dev/null || git -C "$top" fetch --quiet origin "$sha" || git -C "$top" fetch --quiet --unshallow origin
  git -C "$top" worktree add --quiet --detach "$here/src/RiptOPL" "$sha"
fi
echo "src/RiptOPL at $(git -C "$here/src/RiptOPL" rev-parse HEAD)"
