# Rotatrix navigation in MeshLab

Start Rotatrix 1.6 or newer and select a profile matching `app.meshlab`.
The client reports its PID, the `workspace.modeling` tag and navigation capability.
The SDK connects to the local service on port 6607. Open a mesh and activate its
viewport. Use **View → OpenAxis Diagnostics**, **⌘⇧O** on macOS, or
**Ctrl+Shift+O** on Windows/Linux to toggle diagnostics in the current viewport.
The checkmark reflects that viewport's visibility. Connection/focus/gesture status,
connection errors and SDK diagnostic evidence appear directly over the scene.
Connection and retries are automatic; there is no reconnect button or separate
status window. On Windows, logs are in `%LOCALAPPDATA%/Rotatrix/logs/meshlab-*.log`.
A green marker shows the active pivot independently of diagnostics visibility.
The overlay includes semantic-colored rows, labeled cursor/center crosshairs,
candidate bounds and point crosses, and supplied world-orientation geometry.
Turning the toggle off removes diagnostics without changing logging or the pivot.
Coincident query labels stack beside one
crosshair. Geometry follows native camera motion, clips to the viewport/frustum,
and clears with its captured context or SDK expiry. Diagnostics never enter depth
picking, bounds, selection, undo or saved content. No pick rays or camera axes are added.
Text is rasterized before compositing to avoid the blank OpenGL text overlay.

The renderer follows the [OpenAxis rendering contract](https://openaxis.rotatrix.com/reference/diagnostic-rendering/)
for viewport diagnostics. The separate pivot follows the
[pivot appearance guidance](https://openaxis.rotatrix.com/experience/pivots-diagnostics/):
a four-logical-pixel lime disc with a black rim, with opaque visible fragments
and 23% opacity behind geometry. It never writes scene depth.

Perspective and orthographic poses map to the native trackball, including its
scale and nonzero center. Native mouse navigation remains available. Only the
current, visible viewport in the active window accepts input; modal dialogs,
popups, raster mode, snapshots and busy documents suspend navigation.
Scene changes and viewport changes invalidate gestures. One shared connection
and navigation session serve all document windows and split panes in a MeshLab
process. The active main window's current document and pane select the target;
document-local current-pane flags cannot independently claim focus. Switching
targets cancels the old gesture and changes its context generation, so delayed
requests cannot act on a previous or closed viewport. Losing application focus
reports unfocused while the transport and retry scheduler continue running.
The last viewport closing tears down the connection. Separate MeshLab processes
have separate connections and process IDs.

Model bounds use visible transformed meshes. Selection bounds use explicitly
selected faces and vertices across visible layers, including layer transforms.
An active layer alone is not a geometry selection. Cursor and viewport-center picks use MeshLab's upstream
depth-buffer picker (`vcg::Pick`) during a current-camera repaint, before the
trackball overlay is drawn. This works with both rendered meshes and point clouds,
without a first-gesture spatial-index build. Selection-only picks render selected
faces and vertices into an offscreen buffer without changing visible depth or selection, preserving
the viewport projection so rendered point sizes remain pickable.
Background pixels use Rotatrix's configured fallback.
Navigation remains available while an editing tool is idle or suspended with
Escape. It pauses while a mouse button is held in an active editing tool, to
avoid moving the camera during operations such as rectangle selection. Multi-click
or keyboard-driven editors do not have additional tool-specific guards.
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

## Packages

Windows packages are portable ZIPs: extract into a writable folder and run
`meshlab.exe`. No installer or administrator rights are needed. Windows packages
are unsigned. Linux downloads provide tar archives and AppImages. macOS downloads
contain `meshlab.app`; extract the archive and move the app to Applications.

Rotatrix packages use single precision and include the full plugin set.
The complete source revision is recorded in `BUILD_COMMIT.txt` (under
`Contents/Resources` on macOS).
