"""Check release configuration isolation and compile the production texture table."""
from pathlib import Path
import re
import shlex
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
        first = editions.definition("riptopl")
        for name in editions.EDITIONS:
            config = editions.definition(name)
            targets = {t["name"]: t for t in config["targets"]}
            loader, core = targets["riptopl"], targets["eecore"]
            ra = editions.EDITIONS[name]["ra"]
            for target in (loader, core):
                self.assertEqual("RETROACHIEVEMENTS" in target["defines"], ra)
            self.assertNotIn("BUILTIN_KORIUM", loader["defines"])
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
        self.assertEqual(first, editions.definition("riptopl"))

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
                flags = shlex.split(" ".join(f"-D{define}" for define in defines))
                subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags, str(path / "textures.cpp"), "-o", str(executable)], check=True)
                subprocess.run([str(executable)], check=True)

    def test_lite_updater_preserves_published_asset_names(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("g++ unavailable; required in Linux CI")
        updater = (ROOT / "src/ui/selfupdate.cpp").read_text(encoding="utf-8")
        select_asset = updater[updater.index("static bool suSelectAsset("):].split("\n}\n", 1)[0] + "\n}\n"
        driver = """
#include <cassert>
#include <cstring>

#define SU_STRINGIFY_VALUE(value) #value
#define SU_STRINGIFY(value) SU_STRINGIFY_VALUE(value)

enum { SU_ELF_PACKED, SU_ELF_UNPACKED };
struct SuTarget {};
static const char *suAsset;
static int suFormat;
static int bootFormat;
static bool suResolveTarget(SuTarget &, bool) { return true; }
static int suReadBootFormat(const SuTarget &) { return bootFormat; }
""" + select_asset + """
int main(int argc, char **argv)
{
    assert(argc == 3);
    bootFormat = SU_ELF_PACKED;
    assert(suSelectAsset() && std::strcmp(suAsset, argv[1]) == 0);
    bootFormat = SU_ELF_UNPACKED;
    assert(suSelectAsset() && std::strcmp(suAsset, argv[2]) == 0);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "su_asset.cpp"
            executable = Path(directory) / "su_asset.exe"
            source.write_text(driver, encoding="utf-8")
            for edition in editions.LITES:
                for device in editions.LITE_DEVICES:
                    with self.subTest(edition=edition, device=device):
                        targets = editions.definition(edition, device=device)["targets"]
                        defines = next(target["defines"] for target in targets if target["name"] == "riptopl")
                        flags = shlex.split(" ".join(f"-D{define}" for define in defines))
                        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", *flags,
                                        str(source), "-o", str(executable)], check=True)
                        subprocess.run([str(executable), editions.update_asset(edition, device=device),
                                        editions.update_asset(edition, device=device, unpacked=True)], check=True)


if __name__ == "__main__":
    unittest.main()
