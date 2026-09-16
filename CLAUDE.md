# Tempo — plugin notes

Serve's fork of [Tempo](https://github.com/tempo-sim/Tempo): Unreal plugins that expose the sim to
external clients over gRPC. This is a **git submodule** (`.gitmodules` pins the branch — read it,
don't assume `main`). Changes here need a commit in the submodule *and* a pin bump in machine-city,
in the same PR.

## Where to look

| For | Read |
|---|---|
| Architecture, plugin map, the RPC service pattern, codegen | [`AGENTS.md`](AGENTS.md) — **start here** |
| The Python/Rust client API and how it's generated | [`TempoCore/README.md`](TempoCore/README.md) |
| Publishing the wheels to the `acp` registry | `genesis/scripts/publish_machine_city_wheels.sh` |

## ⚠️ Proto/gRPC changes REQUIRE a version bump

**Any change to a `.proto` file or a gRPC service definition must bump the `tempo-sim` version in
the same commit** — the `.postN` suffix described below. Do this automatically — do not wait to be
asked.

One file holds the version:

| File | Package |
|---|---|
| [`TempoCore/Content/Python/API/pyproject.toml`](TempoCore/Content/Python/API/pyproject.toml) | `tempo-sim` (Python) |

`machine-city`'s own [`Content/Python/API/pyproject.toml`](../../Content/Python/API/pyproject.toml)
is **generated** — codegen single-sources the version above into its `tempo-sim==` pin, so don't
hand-edit that pin; it is rewritten on every build.

The Rust crate ([`TempoCore/Content/Rust/API/Cargo.toml`](TempoCore/Content/Rust/API/Cargo.toml))
is **not used here** — leave its version alone. It is Cargo/SemVer, which cannot express the
`.postN` scheme below anyway, and `Scripts/Package.sh` skips Rust packaging when `cargo` is absent.

`.proto` files live under `Tempo*/Source/Tempo*/Public/*.proto`. They are the **source of truth** —
a prebuild step generates the C++ stubs and the Python/Rust clients from them, so a proto edit
always changes the published client.

### Why this is mandatory, not hygiene

`Scripts/Package.sh` does **not** bump versions. So a proto change plus a repackage produces a
wheel with the *same version* and *different wire format*. Nothing errors:

- Artifact Registry rejects re-uploading an existing version, so the new wheel silently never
  publishes — consumers keep resolving the old one against a new server.
- `uv.lock` pins one sha256 per version, so a consumer that already locked keeps the stale wheel.
- A flat index keys on `(name, version)` with no hash, so a cached copy can win over a newer file.

This has already happened: `tempo-sim 0.1.1` shipped, then commits `a0e5132` and `414d5ed` retyped
`ActorState.instance_bounds` field 7 from `repeated Box` to `repeated InstanceBounds` and added
`SetSplinePoints` — with no bump. Two incompatible wire formats under one version. Field 7 keeps its
number but changes type, so a mismatched client misparses rather than failing loudly.

**Reusing a field number with a new type is wire-breaking. Prefer a new field number.**

### Bumping: `.postN` on upstream's version

**Serve's fork uses a `.postN` suffix on the upstream version it is based on:** `0.1.1` → `0.1.1.post1`
→ `0.1.1.post2`, and so on.

This exists because **`tempo-sim` is also published on public PyPI** by upstream Tempo — a
*different* fork of the same package name. Public releases already include `0.1.0, 0.1.1, 0.2.0 …
0.3.0`, so a bare Serve version can collide with an upstream one (`0.1.1` did). A `.postN` suffix
is outside the range upstream uses, so the name/version pair stays unambiguous, and it sorts *after*
the upstream release it builds on.

Do **not** use plain `0.2.x` / `0.3.x` for this fork — those are upstream's.

Two consequences to remember:

- **`.postN` does not satisfy a bare `==` pin.** `tempo-sim==0.1.1` will *not* match `0.1.1.post1`.
  Consumers must pin the full version (`==0.1.1.post1`) or a wildcard (`==0.1.1.*`).
- `post` is a reserved PEP 440 keyword. There is no arbitrary-word suffix — `.serve1` is invalid,
  and `.rev`/`.r`/`-N` are just other spellings of `.post`. The only free-form slot is a local
  label (`+serve1`), which sorts *before* `.post1` and which public indexes refuse to host.

Prefer a `.devN` version for a pre-release share — it sorts *below* the release and won't satisfy a
`>=` pin.

Full flow: bump the version file → `Scripts/Build.sh` → `Scripts/Package.sh` →
`genesis/scripts/publish_machine_city_wheels.sh` → bump the pins in `genesis/sim/pyproject.toml` →
`uv lock`.

## Wheels are reproducible

`Scripts/Package.sh` sets `SOURCE_DATE_EPOCH` from the submodule's HEAD commit time before building
the Python packages, so the same commit yields byte-identical wheels. Without it every zip entry
carried its source mtime, and codegen rewrites `tempo_sim/*.py` on every prebuild — a no-op rebuild
changed the sha256.

This is what makes genesis' `build.sh` "same version, different content" warning meaningful: a hash
difference now means a real content difference. Two caveats: a wheel built from a **dirty** tree
hashes as its committed HEAD, and the wheel records its setuptools version, so a toolchain upgrade
shifts the hash without a source change.
