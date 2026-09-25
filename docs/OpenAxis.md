# MeshLab / Rotatrix OpenAxis preview

Branch `rotatrix/main` integrates the OpenAxis C++ SDK release `cpp/v1.0.0-rc.1`
(commit `acc4da095cde6747556245b4b6c110c16b968b6b`). The design follows
`rotatrix/PrusaSlicer`'s `backport-2.9.6` integration: the SDK owns WebSocket
transport, connection retries, gesture coordination and reconciliation; a host
adapter supplies camera and scene data on the GUI thread.

Start Rotatrix 1.6 or newer and select a profile matching `app.meshlab`.
The client reports its PID, the `workspace.modeling` tag and navigation capability.
The SDK connects to the local service on port 6607. Open a mesh and activate its
viewport. Use **Ctrl+Shift+O** with viewport focus to open the modeless
**OpenAxis Diagnostics** window. It shows connection/focus/gesture status, the
connection error and automatic retry countdown, navigation diagnostics and recent
events. **Reconnect** restarts the connection immediately; **Copy diagnostics**
copies the displayed details and the last 64 KiB of the SDK log; **Open log** opens
the session log. On Windows, logs are in `%LOCALAPPDATA%/Rotatrix/logs/meshlab-*.log`.
A green marker shows the active pivot.
Enable **Viewport diagnostics** in that window to draw the SDK presentation:
semantic-colored text rows, labeled cursor/center crosshairs, candidate bounds and
point crosses, and any supplied world-orientation geometry. The checkbox remains
active when the window is closed. Disable it to remove the diagnostic overlay;
the normal pivot remains independent. Coincident query labels stack beside one
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
Scene changes and viewport changes invalidate gestures. Each viewport owns its
connection and queued callbacks are invalidated on shutdown.

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
The CI preview builds all plugins that do not need optional external libraries.
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

`.github/workflows/openaxis-build.yml` runs on pushes and pull requests to
`rotatrix/main`, and supports manual dispatch. It compiles Windows x64, Linux x64
and macOS Intel packages, runs camera, depth-picking, visual-overlay and scheduler tests, and uploads artifacts.
Depth-picking tests report a skip if the runner cannot create an OpenGL context.
Only successful non-PR runs on `rotatrix/main` create a **draft prerelease** with
all three archives attached. Nothing is automatically published. Packages are
unsigned; the Linux preview requires the usual system OpenGL/X11 libraries.

Before publishing, test rotation, pan, zoom, native-mouse continuation, both
projections, reconnect, application/modal focus, multiple viewports, mesh changes,
and closing the application while a gesture is active with a physical device.
