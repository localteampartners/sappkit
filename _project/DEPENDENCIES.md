# DEPENDENCIES — sappkit

<!-- UPDATE WHEN: an external service/API is added or removed, an account changes hands, billing changes, or credentials rotate -->

External systems this project depends on.

---

## External services

None — no hosted services, no billing, no credentials.

## Sibling repos (build-time)

| Repo | Role | Fallback |
|---|---|---|
| `~/apps/sappsounds` (github.com/localteampartners/sappsounds) | the sampler engine (`Sapp::Sounds`) | FetchContent from GitHub `main` |
| `~/apps/sapptune` | SappLink manifest source of truth (`sapplink/manifests/sappkit.json`) | vendored copy in `tests/data/` keeps CI honest |

## Fetched libraries (build-time, pinned)

- JUCE 8.0.15 (GitHub tag; reuse `~/apps/sappaudio/sappsynth/build/_deps/juce-src`)
- Catch2 v3.7.1

## Sample libraries (runtime, user-fetched, never committed)

Via `~/apps/sappsounds/scripts/fetch-library.sh` into `~/Samples/`:

| Name | License | Notes |
|---|---|---|
| avl-drumkits | CC-BY-SA | Black Pearl / Red Zeppelin kits — verified with the pad mapper |
| sm-drums | royalty-free | SM MegaReaper, deep velocity layers + 4 RR |
| big-rusty-drums | CC0 | Karoryfer character kit |
| vsco2-ce | CC0 | orchestral percussion (raw WAVs — needs generated SFZ) |

## Single points of failure

- Sample-library hosts (bandshed.net zip, GitHub repos) going away would
  break *fetching*, not existing installs. Licenses permit local mirrors.

## Account recovery

- GitHub org `localteampartners` — localteampartners@gmail.com.
