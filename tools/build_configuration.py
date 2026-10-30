"""Configuration overlays shared by all three editions; never additional release channels.

Adapted from the preserved 2026-10-12 transition snapshot, checked against the
legacy Makefile/EE-core/IOP flags. Edition-specific RA and assets stay in build_edition.
"""

CONFIGURATIONS = (
    "standard", "pademu0", "ds5", "extras", "extras_ds5", "dtl", "diag",
    "debug", "debug_uart", "ingame_debug", "iopcore_debug", "eesio_debug",
    "ingame_debug_uart", "iopcore_debug_uart", "ingamediag",
)


def add(target, field, *values):
    target.setdefault(field, []).extend(v for v in values if v not in target.get(field, []))


def apply_configuration(config, configuration):
    if configuration not in CONFIGURATIONS:
        raise ValueError(f"unknown configuration: {configuration}")
    targets = {t["name"]: t for t in config["targets"]}
    main, core = targets["riptopl"], targets["eecore"]
    if configuration == "pademu0":
        for target in config["targets"]:
            target["defines"] = [v for v in target.get("defines", []) if v != "PADEMU"]
        main["libs"] = [v for v in main["libs"] if v not in ("ds34usb_ee", "ds34bt_ee")]
        main["embed_irx"] = [v for v in main["embed_irx"] if v not in ("usb_pademu", "bt_pademu", "ds34usb", "ds34bt")]
    if configuration in ("ds5", "extras_ds5"):
        add(targets["ds34usb"], "defines", "DS5_ENABLE")
    if configuration in ("extras", "extras_ds5", "ingamediag", "eesio_debug"):
        # ee_core's ram84 holds the in-game menu or IGS / the freeze report / the EE SIO debug output, not both
        # (IGS overflows it; the report would leave the stack about 4 KB; eesio_debug with LLVM 1 KB): these keep
        # the plain IGR combo and no In-Game Menu row.
        for target in (main, core):
            target["defines"] = [v for v in target.get("defines", []) if v not in ("IGR_MENU", "IGR_OVERLAY")]
        main["embed_irx"] = [v for v in main["embed_irx"] if v not in ("igrmbox", "igrovl")]
    if configuration in ("extras", "extras_ds5"):
        add(main, "defines", "__RTL", "IGS")
        add(core, "defines", "IGS")
        add(core, "sources", "ee_core/src/igs_api.c")
    if configuration == "dtl":
        for target in (main, core):
            add(target, "defines", "_DTL_T10000")
        main["embed_irx"] = ["udnl-t300:udnl" if v == "udnl" else v for v in main["embed_irx"]]
    if configuration == "diag":
        add(main, "defines", "__OPLDIAG")
    if configuration == "ingamediag":
        # In-game freeze report: the IGR combo after a freeze writes mc0:/RIPTFRZ.TXT (see
        # ee_core/src/freeze_diag.c). RA runs IGR from VBLANK, so the two cannot be combined.
        if "RETROACHIEVEMENTS" in core.get("defines", []):
            raise ValueError("ingamediag is not supported with RetroAchievements")
        for target in config["targets"]:
            if target["name"] in ("riptopl", "eecore", "cdvdfsv") or target["name"].endswith("_cdvdman"):
                add(target, "defines", "INGAMEDIAG")
        add(core, "sources", "ee_core/src/freeze_diag.c")
        # The formatter is ee_core's, shared so the host test covers what runs on the IOP.
        config["targets"].append({
            "name": "freezediag", "type": "iop",
            "sources": ["modules/iopcore/freezediag/freezediag.c", "ee_core/src/freeze_diag_format.c"],
            "include_dirs": ["modules/iopcore/freezediag", "ee_core/include", "asm"],
            "defines": ["FREEZEDIAG_BUILD=OPL_VERSION"], "cflags": ["-include", "opl_version.h"],
            "headers": list(targets["resetspu"]["headers"]),
            "imports_lst": "modules/iopcore/freezediag/imports.lst",
        })
        add(main, "embed_irx", "freezediag")
    if "debug" in configuration:
        uart = configuration.endswith("_uart")
        add(main, "defines", "__DEBUG", "__OPLDIAG", "TTY_PPC_UART" if uart else "TTY_UDP")
        add(main, "sources", "src/platform/debug.cpp")
        add(main, "embed_irx", "ioptrap", "ppctty" if uart else "udptty")
        # UART resolves the native ppctty driver from sdk-world v2026.10.12.2+.
        if not uart:
            config["targets"].append({
                "name": "ps2link", "type": "iop",
                "sources": [f"modules/debug/ps2link/{name}.c" for name in
                            ("net_fsys", "net_fio", "ps2link", "cmdHandler", "nprintf", "excepHandler")],
                "include_dirs": ["modules/debug/ps2link"], "defines": ["DEBUG"],
                "headers": list(targets["isofs"]["headers"]),
                "imports_lst": "modules/debug/ps2link/imports.lst",
            })
            add(main, "embed_irx", "ps2link")
        if configuration.startswith(("ingame_", "iopcore_")):
            add(main, "defines", "__INGAME_DEBUG")
            add(core, "defines", "__LOAD_DEBUG_MODULES", "TTY_PPC" if uart else "TTY_UDP")
            # Preserve RA's INGAME_UDP; only drop the release driver's reduced API.
            targets["SMSTCPIP"]["defines"] = [v for v in targets["SMSTCPIP"]["defines"] if v != "INGAME_DRIVER"]
            if not uart:
                add(main, "embed_irx", "udptty_ingame")
            for target in config["targets"]:
                if target["name"].endswith("_cdvdman"):
                    add(target, "defines", "__IOPCORE_DEBUG")
                    # PCCF exports its own dev9 table and cannot share the real driver.
                    if target["name"] != "pccf_cdvdman":
                        add(target, "defines", "__USE_DEV9")
                        add(target, "sources", "modules/iopcore/cdvdman/dev9.c")
                if configuration.startswith("iopcore_") and target["name"].endswith("_mcemu"):
                    add(target, "defines", "DEBUG")
            add(targets["bdm_ata_cdvdman"], "defines", "DEV9_DEBUG")
        if configuration == "eesio_debug":
            add(main, "defines", "__EESIO_DEBUG")
            add(main, "libs", "siocookie")
            add(main, "headers", "siocookie")
            add(core, "defines", "__EESIO_DEBUG")
    return config
