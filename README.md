# Scrap Mechanic Blueprint Editor
Known Issues: incorrectly flagging upgradable items as survival only, seats,saddles being rendered as a box due to fallback mechanics.
An independent desktop project for viewing and editing Scrap Mechanic blueprints outside the game. It turns a saved blueprint into a 3D scene so you can select parts, change their placement and appearance, and save the result without editing blueprint JSON by hand.

The recent Scrap Mechanic update broke older external blueprint editors. This project is being developed as the currently working standalone editor for the updated game, with direct access to your saved blueprints and the game’s part models.

## What it does

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

The editor saves directly to the selected blueprint folder. Make a backup of important blueprints before editing.

## Selection and movement

- Click a part to select it. Hold **Shift** and click other parts to add them to the selection.
- Drag one of the **+X**, **-X**, **+Y**, **-Y**, **+Z**, or **-Z** arrows to move the entire selection along that axis.
- The Inspector’s position fields and movement buttons also move the whole selection.
- The cyan outline marks the active part; other selected parts have their own outlines.
- Rotation controls edit the active part.
- Press **Delete** or click **Remove selected parts** to remove the selection.

Scrap Mechanic uses **Z as up**. X and Y are the horizontal axes. For resizable blocks, local Z is height and local Y is depth. When multiple parts are selected, the Dimensions panel shows the combined world-space width (X), depth (Y), and height (Z) of the selection.

## Color and dimensions

- Type a six-digit hex color such as `3E9FFE`, or click the color swatch to open the color picker.
- Color edits apply to every selected part.
- With one supported built-in block selected, use Width, Height, and Depth to resize it.
- With multiple parts selected, Dimensions shows their combined size instead of individual resize controls.

## View controls

| Action | Control |
| --- | --- |
| Orbit the camera | Right-drag |
| Pan the camera | Middle-drag |
| Zoom the camera | Mouse wheel |
| Return to the default camera view | **Reset view** |
| Save the blueprint | **Ctrl+S** or **Save Blueprint** |

## Blueprint compatibility

Parts marked **Survival-only** cannot be spawned in Creative mode. The editor identifies these parts and lets you select and remove them. Remove incompatible parts before using the creation in Creative, or load it in a Survival world.

Some parts, including parts from mods or unsupported game assets, may not have a model the editor can display. The blueprint data can still be edited, but the viewport may show a fallback shape for those parts.

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
