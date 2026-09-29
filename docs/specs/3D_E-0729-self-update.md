# 3D_E-0729 — Self-update from GitHub Releases, with what changed

**Status:** accepted (2026-09-29).
**Kind:** feature.
**Source:** ROADMAP 3D_E-0729 (user request 2026-09-29, modelled on finbreak's updater).
**Pairs with:** 3D_E-S0461 (the larger updater: backups, migrations, crash rollback), which stays planned and builds on this.

Layman: the editor tells you when a newer version is out, shows you everything that changed since yours, and updates itself with one click after checking the download is genuine.

## 1. Goal

A user running a release download of the Vestige editor learns that a newer
stable release exists, reads the CHANGELOG text of every release between theirs
and the new one, and with one click gets the new version installed and
relaunched — the AppImage and the Windows zip in place, the Linux tarball by a
link to the download page. Nothing is installed unless its Ed25519 signature,
made in `release.yml` and bound to the release's version, verifies against a
public key compiled into the running build.

## 2. Problem

1. A user has no way to know a newer version exists; the last public release
   (v0.1.70, 2026-08-19) is what a new user downloads, and nothing in the editor
   points past it.
2. There is no network or cryptography code in the engine to build on:
   `external/CMakeLists.txt` fetches no HTTP or crypto library, and nothing in
   `engine/` or `app/` opens a socket.
3. The GitHub release body is GitHub's generated commit list:
   `release.yml` sets `generate_release_notes: true` on both
   `softprops/action-gh-release` steps and passes no `body`. The CHANGELOG, which
   is written for users, reaches them only if they browse the repository, so
   the release page cannot tell a user what changed.
4. Releases carry no signature or checksum, so nothing lets the app tell a
   genuine download from a tampered one.

## 3. Scope decisions (agreed with the user)

All made by the user on 2026-09-29, each the recommended option:

- **finbreak-shaped first version**: check, accumulated notes, download,
  verify, swap, restart; *Later / Skip this version / Update now*. Project
  backup, migrations and crash rollback stay in 3D_E-S0461.
- **Signing is automatic in CI**: the private key is a GitHub Actions secret and
  `release.yml` signs every asset it uploads. Nothing is signed by hand.
- **Ask on first run** whether to check automatically; *Help → Check for
  Updates…* and a Settings checkbox exist either way.
- **In place for the AppImage and the Windows zip**; the tarball shows the
  notes and opens the download page.

Following from those, decided here: stable releases only (`/releases/latest`
never returns a pre-release, so an RC tester is never offered a stable build
older than theirs, see §4.2); the editor only — the `--play` walkthrough and
shipped games never check.

## 4. Design

### 4.1 Pieces and where they live

```
engine/update/update_version.h/.cpp   parseVersion, isNewer            (pure)
engine/update/update_release.h/.cpp   ReleaseInfo, parseLatest, selectAsset,
                                      notesSince                       (pure)
engine/update/update_signature.h/.cpp signedMessage, verifySignature   (pure)
engine/update/update_key.h            kUpdatePublicKey[32]
engine/update/http_transport.h/.cpp   IHttpTransport, CurlTransport
engine/update/update_service.h/.cpp   UpdateService: check, download, verify
engine/update/update_installer.h/.cpp detectInstall, AppImageInstaller,
                                      WindowsZipInstaller
engine/update/notes_markup.h/.cpp     parseNotes -> std::vector<NotesLine> (pure)
engine/editor/panels/update_dialog.h/.cpp  UpdateDialog (ImGui modal)
engine/core/settings.*                UpdateSettings, schema v6
external/monocypher/                  Monocypher 4.0.3, vendored
tools/sign_release.py                 CI signer
```

Every decision a test needs to reach is a pure function, following
`applyFirstRunIntent` in `engine/editor/panels/first_run_wizard.h`.

### 4.2 Versions

The running version is `VESTIGE_ENGINE_VERSION`, which `release.yml` sets from
the tag through `VESTIGE_VERSION_STAMP` (so `0.1.76` or `0.1.76-rc.1`). A build
that did not come from `release.yml` has no install kind (§4.6) and never
installs.

```cpp
struct Version { std::array<int,3> core; std::string pre; }; // "0.1.76-rc.1" -> {0,1,76},"rc.1"
std::optional<Version> parseVersion(std::string_view s);    // optional leading 'v'
bool isNewer(const Version& offered, const Version& installed);
```

Ordering is SemVer §11 for the two shapes this project tags: core compared
numerically; at equal core, a version with a pre-release is lower than one
without. So `0.1.75` is not newer than `0.1.76-rc.1`, and `0.1.76` is newer
than `0.1.76-rc.1`. Anything else fails to parse and nothing is offered.

### 4.3 Checking and the notes

One GET to `api.github.com/repos/milnet01/Vestige/releases/latest`, with
`User-Agent: Vestige/<version>` and `Accept: application/vnd.github+json`. GitHub
returns the newest published release that is neither a draft nor a pre-release.

- If its `tag_name` is not newer than the installed version, an automatic check
  ends quietly and a Help-menu check reports "you have the latest version".
- If it equals `updates.skippedVersion`, an automatic check ends quietly and a
  Help-menu check offers it anyway, saying it was skipped.

**The notes come from the CHANGELOG itself, not from release bodies.** Two more
GETs fetch `CHANGELOG.md` as it stood at each tag:
`https://raw.githubusercontent.com/milnet01/Vestige/v<installed>/CHANGELOG.md`
and the same at `v<offered>`. `notesSince(installedText, offeredText)` returns
the lines of the offered file that the installed file does not contain (counted
as a multiset, so a repeated line is shown as often as it was added), in the
offered file's order. Every entry added since the installed version is shown
exactly once, whatever releases were published or left as drafts in between.
If either fetch fails, the dialog offers the update with "Release notes could
not be loaded" and a link to the release page. The release page's own body is
unchanged (GitHub's generated notes).

`parseNotes` turns those lines into lines the dialog draws: `#`…`###`
headings, `- ` / `* ` bullets with their indent, and paragraphs; `**`, `` ` ``
and link syntax are stripped to their text. Links are not clickable. It is a
display subset, not a markdown renderer.

### 4.4 Signing

One signature per uploaded asset, published as `<asset>.sig` (64 raw bytes). It
signs a message that binds the bytes to the release:

```
vestige-update-v1\n
version=<tag without v>\n
asset=<asset file name>\n
blake2b=<lowercase hex of BLAKE2b-512 of the asset bytes>\n
```

The app rebuilds this message from the offered `tag_name`, the asset name it
chose and the hash of what it downloaded, and checks it with Monocypher's
`crypto_ed25519_check` against `kUpdatePublicKey`. Binding the version closes
finbreak's known gap, where an old signed build republished under a higher tag
would verify (recorded as open in finbreak's `ROADMAP.md`; finbreak signs the
asset bytes alone in `src/finbreak/services/update.py`).

`tools/sign_release.py` builds the same message with Python's
`hashlib.blake2b` (64-byte digest, the same as Monocypher's `crypto_blake2b`
default) and signs with the `cryptography` package's `Ed25519PrivateKey`. The
private key is the Actions secret `VESTIGE_UPDATE_SIGNING_KEY` (base64 of the
32-byte seed). `release.yml` then verifies every `.sig` against the public key
read from `engine/update/update_key.h` and fails the job if any does not verify
— a key mismatch cannot publish.

Source: https://github.com/LoupVaillant/Monocypher/releases (4.0.3, 2026-06-15).

### 4.5 Downloading

`IHttpTransport` is the one seam that touches the network:

```cpp
struct HttpResult { int status; std::string body; std::string error; };
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResult get(const std::string& url, std::size_t maxBytes,
                           const std::function<bool(std::size_t got, std::size_t total)>& progress) = 0;
};
```

`CurlTransport` uses libcurl: HTTPS only (`CURLOPT_PROTOCOLS_STR "https"`, and
the same for redirects), TLS 1.2 or later, 15 s connect timeout, abort below
1 KB/s for 30 s, and `maxBytes` enforced while streaming. A `false` from
`progress` cancels. On Linux it sets `CURLOPT_CAINFO` to the first existing file
of `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`,
`/etc/ssl/ca-bundle.pem`, `/etc/ssl/cert.pem`, because an AppImage built on
Ubuntu runs on distros that keep the bundle elsewhere; on Windows libcurl is
built with Schannel and uses the system store.

Caps: 256 KB for the API call, 4 MB for each CHANGELOG (681 439 bytes today,
`ls -l CHANGELOG.md`), 200 MB for an asset (the largest today is the
36 MB tarball, `gh release view v0.1.75-rc.1 --json assets`).

The check and the download run on a dedicated `std::thread` owned by
`UpdateDialog`, and hand results back with `JobSystem::runOnMainThread`,
drained once a frame. Not a job-system worker: a download blocks for seconds,
and the workers are shared with the renderer and audio. The dialog reads
progress from an atomic.

### 4.6 Installing

```cpp
enum class InstallKind { None, AppImage, WindowsZip, Tarball };
InstallKind detectInstall(std::string_view packageStamp, const char* appimageEnv,
                          const char* appdirEnv, std::string_view selfExePath); // pure
InstallKind detectInstall();  // reads VESTIGE_PACKAGE, the environment, /proc/self/exe
```

`release.yml` stamps `VESTIGE_PACKAGE`: `linux` in the Linux job, whose one
build feeds both the tarball and the AppImage, and `windows-zip` in the Windows
job. A build without it is `None`. A `linux` build is `AppImage` when `APPIMAGE` and `APPDIR` are set
(the AppImage runtime sets both) **and** the running executable lies under
`$APPDIR`; otherwise it is `Tarball`. The path test matters because child
processes inherit both variables: a tarball Vestige started from inside another
AppImage sees that app's `APPIMAGE`, and must not rename over it. The
asset suffix for each kind is `-x86_64.AppImage`, `-windows-x86_64.zip` and
`-linux-x86_64.tar.gz`; `selectAsset` requires exactly one match and exactly one
`.sig` beside it, or nothing is offered.

**Before any install**, *Update now* runs the editor's existing unsaved-changes
flow (`FileMenu::isDirty` and its modal); cancelling there cancels the update.

**AppImage.** Download to a temporary file created in the directory of
`$APPIMAGE` (same filesystem), verify, `chmod 0755`, `rename()` over `$APPIMAGE`.
Then remove `APPDIR`, `APPIMAGE`, `ARGV0` and `OWD` from the environment and
`execv()` the new file with the user's own arguments: `packaging/AppRun` puts
`--assets <old mount>/usr/share/vestige/assets` in front of them, and that pair
is dropped, since the new AppRun adds its own and the old mount's path would
point the new build at the old build's assets. The old AppImage runtime,
our parent, unmounts when we exit; no helper process is needed. Any failure
before the rename deletes the temporary file and leaves the install untouched.

**Windows zip.** Needs the install folder's parent to be writable; if it is not,
the dialog says so and offers the download page. Download next to the install
folder, verify, extract with miniz (already built as tinyexr's `miniz` target)
into `<install>.update-<version>`. The zip holds one top-level folder,
`vestige-<ver>-windows-x86_64/` (`Compress-Archive -Path $STAGE` in
`release.yml`); that inner folder must hold `vestige.exe`, and it is what gets
renamed into place. Write a
PowerShell helper to the temp directory and start it detached with the absolute
`%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe`; it waits until
no process runs the old `vestige.exe` (by path, at most 60 s), renames the
install folder to `<install>.old`, renames the staging folder into its place,
starts the new `vestige.exe`, and waits 20 s. **The new build failed** when
any rename fails, the process cannot be created, or it exits within those 20 s
with a non-zero code; then the helper puts `.old` back, deletes the staging
folder and starts the old `vestige.exe`. The
editor exits once the helper has started. The next successful launch deletes
`<install>.old`.

**Tarball and None.** No install. The dialog shows the notes and an *Open
download page* button (`https://github.com/milnet01/Vestige/releases/latest`,
opened with `xdg-open` / `ShellExecuteW`).

### 4.7 Settings and when it checks

```cpp
struct UpdateSettings {
    enum class Mode { Ask, On, Off } mode = Mode::Ask;
    std::string skippedVersion;   // empty = none
};
```

Settings schema 5 → 6: `migrate_v5_to_v6` inserts `"updates": {"mode": "ask",
"skippedVersion": ""}`, following `migrate_v2_to_v3`'s shape.

- `Ask`: on the first editor launch after the first-run wizard closes, a small
  modal asks *Check for updates automatically when the editor starts?* with
  *Yes* / *No*; the answer is saved as `On` / `Off`.
- `On`: one check per editor launch, on a worker, a few seconds after the first
  frame. An offer opens the update dialog once no other modal is open.
- `Off`: no automatic check.
- *Help → Check for Updates…* checks whatever the mode, and reports every
  outcome, including "you have the latest version" and failures.
- The Settings editor shows the mode as a checkbox (*Check for updates on
  startup*); unticking from `Ask` stores `Off`.

### 4.8 The dialog

`UpdateDialog`, an ImGui modal after `TemplateDialog`'s open/close pattern:
title *Vestige <new> is available* (you have <installed>); a scrollable region
with `parseNotes` output; buttons *Later* (saves nothing), *Skip this version*
(saves `skippedVersion`), and *Update now* or *Open download page* per §4.6.
After *Update now* it shows a progress bar and a *Cancel* button until it
relaunches or shows the error.

## 5. Invariants

- **INV-1** — `isNewer` orders versions as §4.2 states.
  *Test:* `tests/test_update_core.cpp` — table of pairs including
  `0.1.75` vs `0.1.76-rc.1` (not newer), `0.1.76` vs `0.1.76-rc.1` (newer),
  `0.1.10` vs `0.1.9` (newer), and unparseable inputs (`1.2`, `v`, `1.2.3.4`).
  *Breaks when:* versions are compared as strings (`0.1.10` < `0.1.9`), or a
  pre-release is treated as higher than its release.

- **INV-2** — Nothing is installed unless the signature over §4.4's message,
  rebuilt from the offered tag, the chosen asset name and the downloaded bytes,
  verifies against `kUpdatePublicKey`.
  *Test:* `tests/test_update_core.cpp` — a throwaway key signs a fixture;
  the check passes, and fails for one flipped byte, for the same bytes under a
  different version, and for a signature from another key.
  *Breaks when:* the signature covers the bytes alone (a re-tagged old build
  verifies), or verification runs on a different buffer from the one installed.

- **INV-3** — The key the app ships is the key CI signs with.
  *Test:* `release.yml` verifies every `.sig` against the key parsed from
  `engine/update/update_key.h` before upload and fails the job otherwise.
  *Breaks when:* the secret is rotated without updating `update_key.h`, which
  would publish releases no installed build can accept.

- **INV-4** — Network access is HTTPS only, capped in size, and only through
  `IHttpTransport`.
  *Test:* `tests/test_update_service.cpp` — a fake release lists an
  `http://` asset URL (refused before any request), and an asset body one byte over
  the cap (refused); plus a source scan that `curl/curl.h` is included only by
  `engine/update/http_transport.cpp`.
  *Breaks when:* a second code path calls libcurl directly, or an `http://`
  `browser_download_url` is followed.

- **INV-5** — A failed update leaves the installed version runnable.
  *Test:* `tests/test_update_installer.cpp` — AppImage path in a temp dir with a
  fake `$APPIMAGE`: a verification failure leaves the original file byte-identical
  and no temp file behind. The Windows helper's rename-back is a manual recipe
  on the `wintest` box: stage a build whose `vestige.exe` exits with code 1 at
  once, run the update, check the old folder is back and running.
  *Breaks when:* the rename happens before verification, or the helper deletes
  `<install>.old` before the new build has started.

- **INV-6** — The notes shown are every CHANGELOG line added between the
  installed tag and the offered one, each once, in the offered file's order.
  *Test:* `tests/test_update_core.cpp` — `notesSince` on fixture texts where
  the offered file adds entries under two version headings, repeats one bullet
  already present once, and reorders nothing.
  *Breaks when:* notes are assembled from per-release bodies (overlapping drafts
  repeat lines, unpublished ones vanish), or lines present in both files are
  shown.

- **INV-7** — A build without a release install kind never installs, and a
  `Tarball` build never writes to its own folder.
  *Test:* `tests/test_update_installer.cpp` — `installerFor(InstallKind::None)`
  and `(Tarball)` return no installer; the pure `detectInstall` returns
  `Tarball` for a `linux` stamp with `APPIMAGE` unset, and for `APPIMAGE` and
  `APPDIR` set while the executable path lies outside `$APPDIR`.
  *Breaks when:* a developer build overwrites the build tree it is running
  from.

- **INV-8** — A settings file at schema 5 loads as schema 6 with
  `updates.mode = Ask`, and a user who chose *No* is never checked
  automatically.
  *Test:* `tests/test_settings.cpp` — migration of a v5 fixture;
  `shouldAutoCheckForUpdates` is true only for `On`, and
  `shouldAskAboutUpdateChecks` only for `Ask` once the wizard is done.
  *Breaks when:* the migration is skipped or defaults the mode to `On`.

## 6. Failure modes

| Assumption | When it breaks | Result |
|---|---|---|
| GitHub reachable | offline, DNS, TLS failure | Automatic check: silent. Help menu: "Could not reach GitHub" and the error. |
| Rate limit (60/h unauthenticated per IP) | many launches, shared IP | 403/429 treated as a failed check, as above. |
| Release has assets for this kind | a release built without the AppImage | nothing offered (`selectAsset`). |
| CHANGELOG reachable at both tags | raw.githubusercontent.com down, tag missing | update offered without notes, with a link to the release page. |
| Download completes | dropped connection, cap exceeded | error in the dialog; temp file deleted. |
| Signature verifies | tampered or re-tagged asset, key rotated | "The download could not be verified", nothing installed. |
| Install folder writable | AppImage in a root-owned dir, zip in Program Files | dialog says so and offers the download page. |
| Old process exits (Windows) | the editor hangs on exit | helper gives up after 60 s, deletes staging, leaves the install as it was. |
| New build starts | missing driver, crash | Linux: the user runs the old version from its download page (no rollback, 3D_E-S0461). Windows: within 20 s the helper restores `.old` (§4.6); a later crash leaves `.old` on disk, deleted only by a launch that reaches its first frame. |

## 7. Tests

`tests/test_update_core.cpp` (INV-1, INV-2, INV-6, asset choice, install
detection), `test_update_service.cpp` (INV-2, INV-4, INV-6 through the service),
`test_update_installer.cpp` (INV-5, INV-7), additions to `test_settings.cpp`
(INV-8),
all listed in `tests/CMakeLists.txt`'s `vestige_tests` sources. No test touches
the network: the service takes an `IHttpTransport&`, and the tests pass a fake.
The `release.yml` verification step is INV-3's test. Each test is run red first
against a stub before its code lands.

## 8. Alternatives considered (and rejected)

- **The April design's `release-manifest.json` and SHA-256 list** — a second
  file to publish and keep in step with the assets, and a checksum proves
  nothing against a tampered release. Rejected for the signature-per-asset
  scheme finbreak already runs.
- **Signing on the maintainer's PC** — rejected by the user (§3): every release
  would need a manual step.
- **AppImageUpdate / zsync** — the AppImage already embeds zsync info, but it
  covers one package, needs an external tool, and cannot show release notes.
- **cpp-httplib + OpenSSL** — pulls OpenSSL into the Windows build; libcurl
  with Schannel uses the system TLS there.
- **Release bodies built from CHANGELOG diffs, accumulated in the app** (this
  spec's first draft) — finals are created as drafts and several can wait for
  their Publish click, so bodies cut at build time overlap or cover versions
  never published, and the app repeats or loses lines. Diffing the CHANGELOG at
  the two tags the user actually moves between has neither problem.
- **Close CHANGELOG sections at each release cut** — the train would have to
  commit to `main`, and the app would still have to join sections.
- **A helper .exe instead of PowerShell** — another build target to sign and
  ship; PowerShell is on every supported Windows.

## 9. Out of scope

- Project backup, migrations, auto-rollback, post-update welcome panel —
  tracked by 3D_E-S0461.
- An update channel for release candidates — deferred; not yet queued.
- Updating shipped games built with Vestige — deferred; not yet queued.
- macOS — no macOS build exists.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/test_update_core.cpp` |
| INV-2 | `tests/test_update_core.cpp` |
| INV-3 | `release.yml` signature-verification step |
| INV-4 | `tests/test_update_service.cpp` |
| INV-5 | Partial: `tests/test_update_installer.cpp` covers the AppImage path; the Windows helper is a manual recipe on `wintest` |
| INV-6 | `tests/test_update_core.cpp` |
| INV-7 | `tests/test_update_installer.cpp` |
| INV-8 | `tests/test_settings.cpp` |
| The private key never enters the repo | **nothing** — gitleaks in CI catches common key shapes, not a raw base64 seed |

## 11. Cross-doc impact

`THIRD_PARTY_NOTICES.md` (libcurl, Monocypher), `DEPENDENCY_STANDARDS.md`'s
registry if either is pinned below latest, `SECURITY.md` (the signing key and
what it protects), `RELEASING.md` (the signing secret, and that the
CHANGELOG is what users see as update notes), `TESTING.md` (updating from the editor), `CHANGELOG.md`.

## 12. Cold-eyes loop log

Rows live in `../reviews/3D_E-0729-self-update-loop-log.md`.

## 13. Resource cost

Two new dependencies: libcurl (latest `curl-8_22_0`; system package on Linux
and bundled into the AppImage by linuxdeploy; built from source with Schannel
on Windows) and Monocypher 4.0.3 (two vendored C files). miniz is already
built. At run time: one worker job per check, the download buffered to disk,
the 64-byte signature and the notes text in memory.

## 14. Migration / compatibility

Settings schema 5 → 6 (§4.7). Releases published before this ships have no
`.sig`, so no build can install them, and builds before this ship cannot
update themselves; the first self-updating release is the first one a user
downloads by hand after it ships.

## 15. Open questions

- Should a running RC be offered the next RC? Stable-only for now (§3).
