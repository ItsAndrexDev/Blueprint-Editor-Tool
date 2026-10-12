# Scrap Mechanic Blueprint Editor
An independent desktop project for viewing and editing Scrap Mechanic blueprints outside the game. It turns a saved blueprint into a 3D scene so you can select parts, change their placement and appearance, and save the result without editing blueprint JSON by hand.

The recent Scrap Mechanic update broke older external blueprint editors. This project is being developed as the currently working standalone editor for the updated game, with direct access to your saved blueprints and the game’s part models.

## What it does
<img width="1919" height="1029" alt="image" src="https://github.com/user-attachments/assets/8506f7a5-f254-4771-9e5d-e89aa9e03d49" />

- Finds and lists blueprints in your Scrap Mechanic user folder.
- Searches the list and refreshes it when blueprints are added.
- Displays blueprint parts in a 3D viewport, loading FBX or game mesh assets when supported. Parts without a supported model use a fallback preview.
- Selects individual parts or groups of parts for editing.
- Moves parts, rotates their orientation, changes colors, resizes supported blocks, and removes selected parts.
- Saves edits back to the selected blueprint’s existing folder.
- Warns about Survival-only parts that prevent a blueprint from loading in Creative mode.

## Getting started

1. Launch the editor. The **Blueprint Loader** lists blueprints found in your Scrap Mechanic user folder.
2. Search by name, or press **Refresh** to scan the folder again.
3. Select a blueprint to open it in the 3D editor.
4. Select parts in the viewport and edit them with the Inspector or the viewport controls.
5. Press **Save Blueprint** or **Ctrl+S** to write your changes to the blueprint.

Use **Undo** or **Ctrl+Z** to undo an edit, and **Redo** or **Ctrl+Y** to restore it. Undo history stays with the open blueprint in the editor; save to write the current state to disk.

The editor saves directly to the selected blueprint folder. Make a backup of important blueprints before editing.

## Blueprint loader

- Type in **Search blueprints...** to filter the list by blueprint name. The search is case-sensitive.
- Press **Refresh** to scan the Scrap Mechanic blueprint folder again.
- Select a blueprint’s image to open it in the editor. **Back to Selection** returns to the loader.
- The loader shows the number of blueprints currently found.

## Selection and movement
<img width="1908" height="1028" alt="image" src="https://github.com/user-attachments/assets/580f48db-67dd-4ae0-bb5a-2372f3a7925d" />

- Click a part to select it. Hold **Shift** and click other parts to add them to the selection.
- Drag one of the **+X**, **-X**, **+Y**, **-Y**, **+Z**, or **-Z** arrows to move the entire selection along that axis.
- The Inspector’s position fields and movement buttons also move the whole selection.
- The active part is the last part clicked. With multiple parts selected, **Stack selected parts at active position** moves the selected non-joint parts onto the active part’s position. Subsequent movement moves the selection together.
- Use **Copy** / **Paste** or **Ctrl+C** / **Ctrl+V** to duplicate selected parts and joints. Joints between selected parts are included automatically. Pasted parts are selected and move one additional grid unit along +X for each paste; their copied joints follow them.
- **Copy block state (connections)** starts off. Turn it on before pasting to preserve controller settings and logic connections. Links between copied parts point to the pasted copies; links from copied parts to unselected targets stay on the originals. Unselected sources connected to copied targets also connect to the pasted copies. When off, copied controller settings reset to defaults and logic links are cleared.
- **Gate Labels: Off/On** starts off. Turn it on to display each logic gate’s setting (`AND`, `OR`, `XOR`, `NAND`, `NOR`, or `XNOR`) over the gate in the viewport. The Inspector shows the setting for a selected logic gate regardless of this display option.
- **Start Logic Editing** enables connection editing. Click a controller part to choose a source, then click controller targets to add directed connections. Click the source itself to connect it to itself, or Shift-click a different controller to switch sources. Click empty viewport space to clear the source and selection. Click a connection arrow to remove it. **Stop Logic Editing** or **Esc** exits the mode and restores normal selection and movement.
- The cyan outline marks the active part; other selected parts have their own outlines.
- Press **Delete** or click **Remove selected part(s)** in the Inspector to remove the selection. Undo can restore the removal.

Scrap Mechanic uses **Z as up**. X and Y are the horizontal axes. For resizable blocks, local Z is height and local Y is depth. When multiple parts are selected, the Dimensions panel shows the combined world-space width (X), depth (Y), and height (Z) of the selection.



## Color and dimensions

- Type a six-digit hex color such as `3E9FFE`, or click the color swatch to open the color picker.
- Color edits apply to every selected part.
- With one supported built-in block selected, use **Width**, **Height**, and **Depth** to resize it. Unsupported fixed-size parts show their footprint instead.
- With multiple parts selected, Dimensions shows their combined size instead of individual resize controls.
- The Inspector’s orientation controls set the part’s local X and local Z (up) axes. **Rotate +90°** and **Rotate -90°** turn the active part around local Z; **Reset rotation** restores the default orientation. Joints follow the orientation of their connected bodies.

## View controls

| Action | Control |
| --- | --- |
| Orbit the camera | Right-drag |
| Pan the camera | Middle-drag |
| Zoom the camera | Mouse wheel |
| Return to the default camera view | **Reset view** |
| Save the blueprint | **Ctrl+S** or **Save Blueprint** |
| Undo the last edit | **Ctrl+Z** or **Undo** |
| Redo an undone edit | **Ctrl+Y** or **Redo** |
| Copy the selection | **Ctrl+C** or **Copy** |
| Paste the copied objects | **Ctrl+V** or **Paste**; parts move along +X |
| Remove the selection | **Delete** or **Remove selected part(s)** |
| Add a part to the selection | **Shift-click** |
| Exit logic editing | **Esc** or **Stop Logic Editing** |
| Move selected parts | Drag an axis arrow, use position fields, or click a movement button |

Keyboard shortcuts work while the **Blueprint Editor** window is focused and no field, UI item, or popup is taking input. Mouse controls apply while the pointer is over the 3D viewport.

## Blueprint compatibility

Parts marked **Survival-only** cannot be spawned in Creative mode. The editor identifies these parts and lets you select and remove them. Remove incompatible parts before using the creation in Creative, or load it in a Survival world.

Some parts, including parts from mods or unsupported game assets, may not have a model the editor can display. The blueprint data can still be edited, but the viewport may show a fallback shape for those parts.

The Inspector’s **Scene** section reports loaded game models, built-in block previews, and parts shown with fallback boxes. For large blueprints, the editor reduces real-mesh detail to keep the viewport responsive.

## Building from source

The project builds with **Visual Studio 2022**, the **v143 C++ toolset**, and the Windows SDK. The 64-bit project uses vcpkg dependencies from both the `x64-windows` and `x64-windows-static-md` triplets. Set `VCPKG_ROOT` to your vcpkg installation, or place vcpkg next to the project as expected by the project file.

Install the dependencies if they are not already present:

```powershell
vcpkg install glad:x64-windows nlohmann-json:x64-windows glfw3:x64-windows-static-md
```

Open `Blueprint Editor.sln`, select **Release | x64**, and build. `items.json` is embedded into the executable during the build, and GLFW is statically linked, so neither needs to be shipped as a separate file. A Microsoft Visual C++ runtime may still be required on systems where it is not already installed.

## Tech stack

- **C++20** for the application and editor logic.
- **Dear ImGui** for the interface, docking, and editor panels.
- **GLFW** for windowing and input; statically linked in the Windows build.
- **OpenGL 3.3** with **GLAD** for rendering the 3D viewport.
- **nlohmann-json** and the project’s `scrap_parser` for reading and writing blueprint data.
- **Windows resources** to embed `items.json` in the executable.
- **Visual Studio 2022 / MSVC v143** and **vcpkg** for building and dependency management.
