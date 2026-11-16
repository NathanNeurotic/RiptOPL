"""Check release configuration isolation and compile the production texture table."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build_edition as editions


class EditionsTests(unittest.TestCase):
    def test_definitions_are_isolated_and_only_ra_contains_runtime(self):
        first = editions.definition("incebtion")
        for name in editions.EDITIONS:
            config = editions.definition(name)
            targets = {t["name"]: t for t in config["targets"]}
            loader, core = targets["riptopl"], targets["eecore"]
            ra = editions.EDITIONS[name]["ra"]
            for target in (loader, core):
                self.assertEqual("RETROACHIEVEMENTS" in target["defines"], ra)
            self.assertEqual("BUILTIN_KORIUM" in loader["defines"], editions.EDITIONS[name]["theme"] == "korium")
            self.assertEqual("raudp" in loader["embed_irx"], ra)
            self.assertNotIn("ps2sdk_socket_compat", loader["libs"])
            self.assertNotIn("ps2sdk_socket_compat", targets)
            self.assertEqual("ee_core/src/ra.c" in core["sources"], ra)
            self.assertEqual("INGAME_UDP" in targets["SMSTCPIP"]["defines"], ra)
            for target in config["targets"]:
                for source in target.get("sources", []):
                    # lwNBD is fetched at its declared revision by download_lwNBD.sh;
                    # host-only CI deliberately validates a plain source checkout.
                    if "*" not in source and source != "src/core/lang_internal.c" and not source.startswith("modules/network/lwNBD/"):
                        self.assertTrue((ROOT / source).is_file(), source)
        self.assertEqual(first, editions.definition("incebtion"))

    def test_production_texture_table_matches_enum_for_all_editions(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ unavailable; required in Linux CI")
        header = (ROOT / "include/textures.h").read_text()
        enums = header[header.index("enum INTERNAL_TEXTURE"):header.index("constexpr int ERR_BAD_FILE")]
        source = (ROOT / "src/gfx/textures.cpp").read_text()
        start = source.index("static texture_t internalDefault[TEXTURES_COUNT]")
        end = source.index("\n};", start) + len("\n};")
        table = source[start:end]
        symbols = sorted(set(re.findall(r"&([A-Za-z_][A-Za-z_0-9]*)", table)))
        code = '#include <cassert>\n#include <cstring>\n' + enums
        code += '\nstruct texture_t { int id; const char *name; void *data; };\n'
        code += '\n'.join(f'void *{symbol};' for symbol in symbols) + '\n' + table
        code += '\nint main() { for(int i=0;i<TEXTURES_COUNT;++i) { assert(internalDefault[i].id==i); assert(internalDefault[i].name); for(int j=0;j<i;++j) assert(strcmp(internalDefault[i].name,internalDefault[j].name)); } }\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "textures.cpp").write_text(code)
            for edition in editions.EDITIONS:
                defines = next(t["defines"] for t in editions.definition(edition)["targets"] if t["name"] == "riptopl")
                executable = path / edition
                subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *["-D" + d for d in defines], str(path / "textures.cpp"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
