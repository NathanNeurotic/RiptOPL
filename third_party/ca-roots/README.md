# Trust anchors for RiptOPL's HTTPS clients

The only certificates RiptOPL's HTTPS client trusts: the RA lookup and Check for Updates. retroachievements.org currently chains to
**GTS Root R4** (leaf <- Google Trust Services WE1 <- GTS Root R4). The other Google roots cover a
key-type rotation, and the ISRG roots cover a move to Let's Encrypt. Every one is in the current
Mozilla root store.

| File | Subject | SHA-256 (DER) | Expires | Source |
| --- | --- | --- | --- | --- |
| `gtsr1.der` | GTS Root R1 | `d947432abde7b7fa90fc2e6b59101b1280e0e1c7e4e40fa3c6887fff57a7f4cf` | 2036-06-22 | <https://pki.goog/repo/certs/gtsr1.der> |
| `gtsr3.der` | GTS Root R3 | `34d8a73ee208d9bcdb0d956520934b4e40e69482596e8b6f73c8426b010a6f48` | 2036-06-22 | <https://pki.goog/repo/certs/gtsr3.der> |
| `gtsr4.der` | GTS Root R4 | `349dfa4058c5e263123b398ae795573c4e1313c83fe68f93556cd5e8031b3c7d` | 2036-06-22 | <https://pki.goog/repo/certs/gtsr4.der> |
| `isrgrootx1.der` | ISRG Root X1 | `96bcec06264976f37460779acf28c5a7cfe8a3c0aae11a8ffcee05c0bddf08c6` | 2035-06-04 | <https://letsencrypt.org/certs/isrgrootx1.der> |
| `isrg-root-x2.der` | ISRG Root X2 | `69729b8e15a86efc177a57afb7171dfc64add28c2fca8cf1507e34453ccb1470` | 2040-09-17 | <https://letsencrypt.org/certs/isrg-root-x2.der> |
| `sectigo-e46.der` | Sectigo Public Server Authentication Root E46 | `c90f26f0fb1b4018b22227519b5ca2b53e2ca5b3be5cf18efe1bef47380c5383` | 2046-03-21 | Launchstation `res/WLE-UPDATE-CA.PEM` (Sectigo) |
| `sectigo-r46.der` | Sectigo Public Server Authentication Root R46 | `7bb647a62aeeac88bf257aa522d01ffea395e0ab45c73f93f65654ec38f25a06` | 2046-03-21 | Launchstation `res/WLE-UPDATE-CA.PEM` (Sectigo) |

Each file was downloaded from the issuer on 2026-10-13 (copied from the original's vendored set, then
checked again on 2026-10-27): its fingerprint is in the Mozilla root store (certifi 2026.10.18). The
original also trusts **GTS Root R2**, which that store no longer carries (it was still in certifi
2026.02.25), so RiptOPL leaves it out. `.github/scripts/test_mbedtls_vendor.py` pins the same fingerprints
in its own source. `tools/assets.py embed` turns each file into `asm/ratls_root_<name>.c` with
`bin2c`: every root in RA builds; in the other standard builds with an update channel only
GitHub's four (`UPDATER_ROOTS`: both Sectigo roots for github.com, both ISRG roots for the release
download host). The Sectigo roots were taken from Launchstation's updater set on 2026-11-12; their
fingerprints are in certifi 2026.10.23.
