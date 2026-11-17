# ript-core-08 notes (src/app/opl.cpp, structure lens)

Done: src/app/opl.cpp lines 1-2134. Findings file written and validated OK (5 findings).

Dropped after checks: deinitAllSupport (also used by lwnbdctl.cpp:173), setErrorMessage* family (all reached via host.cpp wrappers), other globals (all referenced in 2+ other files), gPopstarterDevice/gNeutrinoDevice (deliberate downgrade/migration keys, settingsio domain).
