# The build container, and synthesizing the FPGA in CI

Every toolchain the build system needs — Efinity, GHDL, VSG, the Arm GNU
toolchain, the Pico SDK with TinyUSB, picotool, Ceedling, gcovr, clang-format —
lives pinned in one private image, `ghcr.io/stevenpickles/forgix-build`, built
by `ci/forgix-build/Dockerfile` on Ubuntu 24.04 LTS. Its ENV block sets the
known install paths, and `scripts/env.sh` defers to any variable already set,
so the same scripts run unchanged inside the container and on a Windows host.

The image is private for one reason: the Efinity software is registration-gated
and its license forbids redistribution, and the image contains it. This
document covers that licensing position, the image build, and how the T8F49
bitstream is nonetheless built by GitHub Actions — including how a tag becomes
a release containing that bitstream and the firmware that carries it.

## The licensing position

`license.txt` in the Efinity install, section 2.3(a): *"Licensee shall not use,
distribute, reproduce, modify, create derivative works of, or allow access to the
Licensed Software except as expressly authorized by this Agreement."*

Three consequences drive every decision below.

- The container image holding Efinity is **private, permanently**. It is not
  published to a public registry, not attached to a public package, and not
  reachable by anyone who is not the licensee. Collocating the freely
  installable tools in the same image does not change that: the whole image
  inherits Efinity's restriction.
- **Fork pull requests skip every containerized job.** They receive no registry
  credentials by design; skipping is the correct outcome, not a defect. The
  `fork-verify` job in `ci.yml` still checks a fork's change with the tools
  that are freely installable (the Python checks, VSG, GHDL); the Ceedling
  suite, the clang-format gate, synthesis, and the firmware compile run the
  moment a maintainer pushes the branch, because push events carry secrets.
- The **bitstream is not the Licensed Software**. It is the output of this
  design, so publishing it in a release is unrestricted.

Efinity 2026.1 for Trion needs no license file — the install ships no FlexLM, no
`lmgrd`, no `.lic`, and `bin/setup.sh` sets no license variable. Nothing is
node-locked, so an ephemeral runner is not a licensing problem; the download gate
is the only obstacle.

## One-time setup

1. Download the **Linux** tarball (`efinity-2026.1.<build>-linux-x64.tar.bz2`)
   from <https://www.efinixinc.com/support/efinity.php> with your Efinix
   account. The Windows installer will not do: the image is a Linux one.
   Keep the download outside the repository, or if it does end up in
   `ci/forgix-build/`, note that `.gitignore` is the only thing standing
   between it and a public commit. Every other toolchain is fetched,
   checksum-pinned, by the Dockerfile itself — the Efinity tarball is the only
   input you supply.
2. Build and push the private image:

   ```sh
   echo "$GHCR_PAT" | docker login ghcr.io -u stevenpickles --password-stdin
   ./ci/forgix-build/build_image.sh ~/Downloads/efinity-2026.1.132-linux-x64.tar.bz2
   ```

   The Dockerfile unpacks in a first stage and copies only the unpacked tree
   forward, so the tarball is never a layer of the pushed image. Every
   toolchain layer carries its own build-time gate (the Efinity one
   smoke-tests `efx_run.py`, resolves the shared libraries of `efx_map`,
   `efx_pnr`, `efx_pgm`, and `efx_sta`, and imports the Interface Designer
   module), and the build finishes with `forgix-verify --full`, which compiles
   a throwaway RP2350 project to UF2 — so a broken toolchain fails on your
   machine rather than in a CI run weeks later. `docker run --rm <image>
   forgix-verify` answers "what exactly is in this image" any time.

3. Confirm the `forgix-build` package visibility is **Private**, and leave
   **Manage Actions access** empty — see the next section for why that matters
   here.
4. Create a PAT with `read:packages` only, and store it as the repository secret
   `GHCR_TOKEN`. This is how the workflows authenticate.
5. Set the `FORGIX_BUILD_IMAGE` repository variable to the digest-pinned
   reference the build script prints
   (`ghcr.io/stevenpickles/forgix-build:<tag>@sha256:...`). The workflows fall
   back to a tag if the variable is unset, but the digest is the pin.

Tags are immutable CalVer (`YYYYMMDD`); there is no floating `latest`. To try
new tool versions, edit the Dockerfile ARGs, build a new tag, and repoint
`FORGIX_BUILD_IMAGE` — no workflow edit, and rolling back is repointing the
variable at the previous digest.

## Why a PAT, in a public repository

A private package works fine with a public repository, but there are two ways to
authenticate and only one of them is safe here.

The convenient way is to grant this repository access to the package under
**Manage Actions access**, which lets the built-in `GITHUB_TOKEN` pull it.
GitHub's own documentation warns against exactly that: *"If you grant a public
repository access to private packages, forks of the repository may be able to
access the private packages."*

The reason is a chain of three facts:

- A `pull_request` from a fork runs **the fork's copy of the workflow files**, so
  any guard written here can be deleted by the pull request that runs.
- That run gets a read-only `GITHUB_TOKEN`, and read-only **includes**
  `packages: read`.
- With the repository grant in place, that is sufficient to pull the image — and
  a job that can pull the image can `tar` it to anywhere.

So the fork guard in `ci.yml` is a convenience, not a control: it keeps fork runs
green and readable. The control is that fork pull requests are never issued
secrets, so `GHCR_TOKEN` is empty for them and the pull cannot be authenticated
at all, whatever the workflow file has been rewritten to say.

Two rules follow. **Never add `pull_request_target` to a workflow in this
repository** — it runs with full secrets on unreviewed pull request code, which
hands over the token the fork guard exists to protect. And never re-add the
package grant as a quick fix if a pull fails; a failed pull means the PAT is
missing or expired.

Two smaller consequences of the repository being public: workflow logs are world
readable, so keep tool output out of them beyond what the reports contain, and
artifacts are downloadable by anyone. The bitstream and reports that
`fpga-bitstream.yml` uploads are design output, which is fine to publish — no job
may ever upload any part of the Efinity install as an artifact.

## What runs where

`.github/workflows/fpga-bitstream.yml` is a reusable workflow. It checks out the
repository inside the forgix-build container and runs `scripts/build_fpga.sh`,
the same script used locally: compile, verify the pinout, verify setup and hold
slack, then convert the passive-SPI hex to binary. It uploads the bitstream, both
timing reports, the map report, resource usage, and the Efinity logs.

The rest of `ci.yml` runs in the same image: a `verify` job (static checks,
both format gates, GHDL simulation, the Ceedling suite with its 100%/100%
coverage gate) and a `firmware` job (cross-compile against the bitstream the
run produced). Nothing is installed at CI time — no apt, no pip, no SDK
checkout — so a push pays three pulls of one image and every job sees the
identical toolchain a contributor gets locally with
`docker run ghcr.io/stevenpickles/forgix-build:<tag>`.

Two workflows call it:

- **`ci.yml`** on every push and same-repository pull request, so a routing or
  timing regression is caught with the change that caused it rather than at
  release time. Its firmware job then links against *that run's* bitstream and
  checks the image survives verbatim into the linked binary, so the pair CI
  verifies is the pair that ships. The compile fixture is only embedded when
  synthesis failed; fork pull requests get `fork-verify` instead.
- **`release.yml`** on a `v*` tag, feeding the firmware and publish jobs.

A change touching only `docs/**`, `**.md`, or `LICENSE` skips CI entirely — a
typo fix is not worth a place-and-route. A commit touching both docs and code
still runs in full, and `workflow_dispatch` is never filtered. See the comment
above the trigger in `ci.yml` before extending that list: it explains what
breaks if branch protection is ever added.

`scripts/build_fpga.sh` picks its wrapper by platform: `run_efinity.cmd` under
Git Bash on Windows, `run_efinity.sh` in the Linux container. Both run one
`efx_run.py --prj -f compile` in a child process, because sourcing Efinity's
`setup.sh` rewrites `PATH`, `PYTHONHOME`, and `PYTHONPATH` and must not leak into
the repository's own Python checks.

## Cutting a release

```sh
git tag -a v1.0.0 -m "Forgix hello world 1.0.0"
git push origin v1.0.0
```

That runs three jobs in order:

1. **bitstream** — place and route the T8F49 design, gated on pinout and slack.
2. **firmware** — build the RP2354 image against *that* bitstream (not the
   `tests/fixtures/fpga-test.bin` compile fixture that `ci.yml` uses), enforce the
   2 MB flash budget, and confirm the bitstream appears byte-for-byte in the
   linked binary. Unlike `ci.yml` this build does not set `PICO_NO_PICOTOOL`;
   `picotool_DIR` points the SDK at the picotool 2.3.0 prebuilt in the image,
   so a flashable UF2 is emitted without fetching and compiling picotool.
3. **publish** — assemble `dist/`, write `SHA256SUMS`, and create the GitHub
   release.

Release payload: `forgix-hello-world-<tag>.uf2` and its `.elf`/`.elf.map`, the
USB-free `forgix-led-only-diagnostic-<tag>.uf2`, the `forgix-t8f49-<tag>.bin` and
`.hex` bitstreams, the pinout and timing reports, and `SHA256SUMS`.

`workflow_dispatch` with no tag input rehearses the whole build and uploads the
artifacts without publishing anything.

## Notes

- The Ubuntu base is 24.04, which sits outside one of the two bounds
  `bin/setup.sh` checks at runtime. glibc must be at least 2.28, and 24.04's
  2.39 is fine. But setup.sh also wants the system `libstdc++.so.6` no newer
  than the 6.0.32 Efinity bundles, and 24.04 ships 6.0.33 — for which
  setup.sh's own advice is to delete Efinity's bundled copy. The Dockerfile
  does exactly that in its unpack stage (libgcc_s too, since Efinity's lib dir
  leads `LD_LIBRARY_PATH` after the source), and everything then resolves to
  the system copies, which are ABI-backward-compatible by GCC policy. The
  Efinity build-time gate runs against that arrangement, so a bad deletion
  fails the image build, and a full compile comparing `bin[256:]` against a
  known-good bitstream is the final proof after any base move.
- `setup.sh` exports `EFINITY_USER_DIR_INI=$HOME/.local/share/efinity/...`
  itself, so setting it in the image has no effect — the source overwrites it.
  `run_efinity.sh` pins it to a writable path *after* the source instead, so the
  compile does not depend on whatever `$HOME` the runner gives the container.
- **The bitstream is reproducible, but its SHA-256 is not.** The first **256
  bytes** are a fixed-size text header holding the tool version, a build
  timestamp, and the project path. Everything after that is the configuration
  itself. Three compiles — Windows locally, the container locally, and CI on a
  GitHub runner — produced the same 173124-byte payload, at the same slacks
  (setup 8.701 ns, hold 0.643 ns). So compare `bin[256:]`, never the whole file
  and never the hash the workflow prints.

  Beware of concluding the header is shorter than 256 bytes by diffing two
  builds: the timestamp and path sit near the front, so the differences stop
  early and the boundary looks closer than it is. A runner's project path
  (`/__w/<repo>/<repo>/fpga`) is much longer than a local one, which is what
  makes the padding visible. The header is 256 bytes regardless.
- **A failed Interface Designer import does not fail the build.** `efx_run.py`
  catches it, writes "Skipping Interface Designer step" to a log file, and
  continues. map, pnr, and pgm then all report PASS and the flow exits 0 — but
  without the Interface Designer there is no LPF, and `efx_pgm` says "Missing
  Interface Designer LPF constraint file, no programming file will be generated"
  and produces nothing. The only visible symptom is a green run with no
  bitstream. `build_fpga.sh` catches it because it insists on a nonempty `.hex`,
  and the image build catches it because the gate imports
  `efx_run_pt_unified` explicitly. Keep both: an `ldd` sweep of the binaries
  cannot see it, because the missing library is reached through Python.
- The Qt runtime libraries are not optional even though nothing is displayed.
  The Interface Designer reaches `PyQt6.QtGui` via `qtpy`, so `libGL` and the
  rest are load-bearing; `QT_QPA_PLATFORM=offscreen` is what keeps Qt from
  looking for a display that a runner does not have.
- The built image is roughly 6 GB (Efinity ~2.7 GB, the Arm toolchain ~1.4 GB,
  the Pico SDK ~0.4 GB, plus the base and smaller tools), which each
  containerized job pays as pull time — the price of replacing the old Efinity
  pull, the Ceedling image pull, and every per-run apt/pip/SDK-checkout with
  one pinned image. If pull time becomes the bottleneck, the candidates are
  Efinity's `ipm/` (~451 MB) and `debugger/` (~145 MB), both untouched by a
  compile. `pgm/` is *not*: `bin/efx_pgm` generates the bitstream. Neither is
  `pt/` — that is the Interface Designer, without which the flow silently
  produces nothing. A prune is only proven by a full compile afterwards, for
  exactly the reason described above: this flow fails green.
- Efinity output stays out of git; `fpga/outflow/` is ignored. The artifacts and
  releases are the only published copies.
