"""Regression: PSBBN left/centre/right positions and custom #Name title colours."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
elems = (root / "src/gfx/themeelems.cpp").read_text()
themes = (root / "src/gfx/themes.cpp").read_text()

start = elems.index("static int thmMenuStyleStartX(")
end = elems.index("\n}\n", start) + 2
helper = elems[start:end]
assert elems.count("thmMenuStyleStartX(elem->posX,") == 3, "All three PSBBN menu styles must use horizontal alignment"
name_start = elems.index("void drawNameAttributeText(")
name_end = elems.index("\n}\n", name_start)
name = elems[name_start:name_end]
assert name.index('snprintf(formatted, sizeof(formatted), "%s%s", mutableText->alias, disp);') < name.index("fntFitString(elem->font, wrapped, elem->width);"), "Alias must be measured before wrapping"
assert "isName ? theme->titleColor : theme->textColor" in themes, "#Name initial colour must follow game-title picker"
assert themes.count("elem->drawElem == &drawNameAttributeText") == 2, "#Name must follow live title-colour updates in main and secondary views"

code = r'''
#include <assert.h>
#define ALIGN_RIGHT (1 << 2)
#define ALIGN_HCENTER (2 << 2)
''' + helper + r'''
int main()
{
    // Positions checked against the actual code, including when the width is not even.
    assert(thmMenuStyleStartX(600, 180, ALIGN_RIGHT) == 420);
    assert(thmMenuStyleStartX(600, 180, ALIGN_HCENTER) == 510);
    assert(thmMenuStyleStartX(600, 180, 0) == 600);
    assert(thmMenuStyleStartX(600, 181, ALIGN_RIGHT) == 419);
    assert(thmMenuStyleStartX(600, 181, ALIGN_HCENTER) == 510);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="psbbn-alignment-") as directory:
    source = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    source.write_text(code)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: PSBBN alignments and #Name layout/color wiring")
