# MeshLab / Rotatrix OpenAxis preview

Work branch `rotatrix/work/MeshLab-2025.07` (based directly on upstream tag `MeshLab-2025.07`) integrates the OpenAxis C++ SDK release `cpp/v1.0.0-rc.1`
(commit `acc4da095cde6747556245b4b6c110c16b968b6b`). The design follows
`rotatrix/PrusaSlicer`'s `backport-2.9.6` integration: the SDK owns WebSocket
transport, connection retries, gesture coordination and reconciliation; a host
adapter supplies camera and scene data on the GUI thread.

Start Rotatrix 1.6 or newer and select a profile matching `app.meshlab`.
The client reports its PID, the `workspace.modeling` tag and navigation capability.
The SDK connects to the local service on port 6607. Open a mesh and activate its
viewport. Use **Ctrl+Shift+O** with viewport focus to open the modeless
**OpenAxis Diagnostics** window. It shows connection/focus/gesture status, the
connection error and automatic retry countdown. **Reconnect** restarts the connection
immediately; **Copy diagnostics** copies status, SDK evidence and the last 64 KiB
of the SDK log. On Windows, logs are in `%LOCALAPPDATA%/Rotatrix/logs/meshlab-*.log`.
A green marker shows the active pivot.
Opening the window automatically draws the SDK presentation:
semantic-colored text rows, labeled cursor/center crosshairs, candidate bounds and
point crosses, and any supplied world-orientation geometry. Closing the window
removes the diagnostic overlay; the normal pivot remains independent.
There is no event-log viewer or diagnostics checkbox. Coincident query labels stack beside one
crosshair. Geometry follows native camera motion, clips to the viewport/frustum,
and clears with its captured context or SDK expiry. Diagnostics never enter depth
picking, bounds, selection, undo or saved content. No pick rays or camera axes are added.
Text is rasterized before compositing to avoid the blank OpenGL text overlay.

The renderer follows the [OpenAxis rendering contract](https://openaxis.rotatrix.com/reference/diagnostic-rendering/)
and the PrusaSlicer OpenAxis overlay example. The separate pivot follows the
[pivot appearance guidance](https://openaxis.rotatrix.com/experience/pivots-diagnostics/):
a four-logical-pixel lime disc with a black rim, with opaque visible fragments
and 23% opacity behind geometry. It never writes scene depth.

Perspective and orthographic poses map to the native trackball, including its
scale and nonzero center. Native mouse navigation remains available. Only the
current, visible viewport in the active window accepts input; modal dialogs,
popups, raster mode, mesh editors, snapshots and busy documents suspend navigation.
Scene changes and viewport changes invalidate gestures. One shared connection
and navigation session serve all document windows and split panes in a MeshLab
process. The active main window's current document and pane select the target;
document-local current-pane flags cannot independently claim focus. Switching
targets cancels the old gesture and changes its context generation, so delayed
requests cannot act on a previous or closed viewport. Losing application focus
reports unfocused while the transport and retry scheduler continue running.
The last viewport closing tears down the connection. Separate MeshLab processes
have separate connections and process IDs.

Model bounds use visible transformed meshes. Selection bounds currently mean the
active mesh layer. Cursor and viewport-center picks use MeshLab's upstream
depth-buffer picker (`vcg::Pick`) during a current-camera repaint, before the
trackball overlay is drawn. This works with both rendered meshes and point clouds,
without a first-gesture spatial-index build. Selection-only picks render the active
layer into an offscreen buffer without changing visible depth or selection, preserving
the viewport projection so rendered point sizes remain pickable.
Background pixels use Rotatrix's configured fallback.
World orientation is right-handed, Y-up, matching MeshLab's default view.

## Build locally on Windows

Requires Visual Studio 2022 C++, CMake 3.24+, Git, and Qt 5.15.2 MSVC x64.
Qt 5.15.2's MSVC 2019 libraries are ABI compatible with the VS 2022 build.

```powershell
git submodule update --init --recursive
cmake -S . -B build-local/app -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/path/to/Qt/5.15.2/msvc2019_64 `
  -DMESHLAB_OPENAXIS=ON -DVCG_ALLOW_SYSTEM_EIGEN=OFF `
  -DMESHLAB_BUILD_MINI=ON `
  "-DMESHLAB_PLUGINS=meshlabplugins/io_base;meshlabplugins/filter_create"
cmake --build build-local/app --config Release --parallel 6
cmake --build build-local/app --config Release --target io_base filter_create --parallel 6
cmake --install build-local/app --config Release --prefix build-local/install
C:/path/to/Qt/5.15.2/msvc2019_64/bin/windeployqt.exe --release build-local/install/meshlab.exe
```

This quick local build has common mesh import/export and primitive creation.
CI uses the upstream build scripts and their normal dependency/plugin selection.
For a full upstream plugin build, omit `MESHLAB_BUILD_MINI` and `MESHLAB_PLUGINS`,
and leave `MESHLAB_ALLOW_OPTIONAL_EXTERNAL_LIBRARIES` enabled.

OpenAxis defaults on in this branch; use `-DMESHLAB_OPENAXIS=OFF` to disable it.
For SDK development, `-DOPENAXIS_SOURCE_DIR=C:/path/to/openaxis` overrides the
pinned dependency. Normal builds fetch the released SDK automatically.

```powershell
cmake -S tests/openaxis -B build-local/tests -DOPENAXIS_SOURCE_DIR=C:/absolute/path/to/build-local/app/_deps/openaxis-src -DCMAKE_PREFIX_PATH=C:/path/to/Qt/5.15.2/msvc2019_64
cmake --build build-local/tests --config Release
$env:PATH = "C:/path/to/Qt/5.15.2/msvc2019_64/bin;" + $env:PATH
ctest --test-dir build-local/tests -C Release --output-on-failure
```

## CI and releases

See [the fork workflow spec](RotatrixForkWorkflow.md). This integration is still
work in progress: `rotatrix/work/MeshLab-2025.07`. No maintained branch or final
release tag has been created. The earlier remote `rotatrix/main` branch has been retired. Its existing draft
preview releases remain legacy test snapshots; new development happens on the work branch.

`BuildMeshLab.yml` reuses upstream's setup/build/deploy composite actions and
platform scripts. Pushes to `rotatrix/**`, PRs to `rotatrix/*`, and manual runs
build Linux x64/ARM64, macOS Intel/ARM64, and Windows x64, in both upstream
single- and double-precision configurations. It runs the OpenAxis regressions,
then uploads upstream portable bundles and installers/AppImages/DMGs as artifacts
named with the source SHA, retained for 14 days. Work-branch pushes create no
release or tag. A missing desktop GL context explicitly skips the GL-only tests.

When ready, clean up the downstream patch stack and create maintained branch
`rotatrix/MeshLab-2025.07`; set that branch as the GitHub default at that time.
Contributions target that maintained branch. Its published history is append-only.
The repository default remains `main` while no maintained Rotatrix branch exists.

`CreateRelease.yml` runs only for explicit `*-rotatrix.*` tags, using the same
upstream build/deploy path. Stable tags such as `MeshLab-2025.07-rotatrix.1` must
point into the matching maintained branch and descend from the upstream tag.
Permanent test releases use `MeshLab-2025.07-rotatrix.1-beta.1` and are published
as prereleases. Existing release assets are never overwritten. Do not move or
delete release tags; increase the suffix instead. No release tag is created by CI.

Stable releases require the existing upstream signing secrets:
`MACOS_CERTIFICATE` (base64), `MACOS_CERT_ID`, `MACOS_CERTIFICATE_PSSW`,
`MACOS_NOTARIZATION_USER`, `MACOS_NOTARIZATION_TEAM_ID`,
`MACOS_NOTARIZATION_PSSW`, `WIN_CERTIFICATE` (base64 PFX), and
`WIN_CERTIFICATE_PSSW`. These are not configured on the fork yet. Stable release
validation fails clearly until they are supplied. Work artifacts and beta builds
can be unsigned; PR runs receive no signing credentials. The Windows path uses
upstream's signing/installer scripts and verifies signatures; macOS uses upstream's
sign-and-notarize action before making the DMG.

Before tagging, test rotation, pan, zoom, native-mouse continuation, both
projections, reconnect, application/modal focus, multiple documents/viewports,
mesh and point-cloud changes, and closing during a gesture with a physical device.
