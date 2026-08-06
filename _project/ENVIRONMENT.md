# ENVIRONMENT — sappkit

<!-- UPDATE WHEN: an env var is added, renamed, removed, or its source/owner changes. Also update .env.example in the same edit. -->

SappKit reads **no environment variables** and has no secrets. This file is a
tombstone so nobody hunts for configuration that doesn't exist.

Build-time knobs are CMake options, not env vars:

| CMake option | Default | Purpose |
|---|---|---|
| `SAPPKIT_BUILD_PLUGIN` | ON | JUCE plugin + UiShot targets |
| `SAPPKIT_BUILD_TESTS` | ON | Catch2 suite |
| `SAPPKIT_BUILD_CLI` | ON | `sappkit` agent CLI |
| `SAPPSOUNDS_DIR` | `../sappsounds` | local engine checkout path |
| `FETCHCONTENT_SOURCE_DIR_JUCE` | — | reuse an existing JUCE checkout |
