# Scrap Mechanic Blueprint Editor

Scrap Mechanic Blueprint Editor is a Windows desktop tool for viewing and editing creations outside the game. It reads blueprints from your Scrap Mechanic user folder, displays them in a 3D viewport, and saves your edits back to the selected blueprint.

It is an independent fan project and is not affiliated with Axolot Games.

![Blueprint loader](https://github.com/user-attachments/assets/8506f7a5-f254-4771-9e5d-e89aa9e03d49)

## Contents

- [Open a blueprint](#open-a-blueprint)
- [Selection and part search](#selection-and-part-search)
- [Move, rotate, resize, and recolor parts](#move-rotate-resize-and-recolor-parts)
- [Logic gates and logic connections](#logic-gates-and-logic-connections)
- [Controller bearing rotations](#controller-bearing-rotations)
- [Copy, paste, undo, and save](#copy-paste-undo-and-save)
- [Camera and keyboard shortcuts](#camera-and-keyboard-shortcuts)
- [Models and compatibility](#models-and-compatibility)
- [Build from source](#build-from-source)
- [Help and feedback](#help-and-feedback)

## Open a blueprint

1. Start the editor. The **Blueprint Loader** scans the Scrap Mechanic user folder for saved blueprints.
2. Type all or part of a blueprint name in **Search blueprints...** to filter the list. Search matches are case-sensitive.
3. Select a blueprint’s thumbnail to open it. The count beside **Refresh** shows how many blueprints are loaded.
4. Select **Refresh** after adding or removing blueprints to scan the folder again.
5. In the editor, select **Back to Selection** to return to the loader.

The editor reads blueprints from `%APPDATA%\Axolot Games\Scrap Mechanic\User\<User_...>\Blueprints`. It writes changes to the selected blueprint’s existing `blueprint.json` file when you save. Back up important blueprints before editing.

## Selection and part search

![Blueprint editor](https://github.com/user-attachments/assets/580f48db-67dd-4ae0-bb5a-2372f3a7925d)

- Click a part in the viewport to select it. **Shift-click** adds another part without clearing the current selection.
- The cyan outline marks the active part. Other selected parts are outlined too. Click empty space in normal editing mode to clear the selection.
- Select **Find parts...** in the Inspector to search part names and browse the matching part types. Search results expand while you type. Select a row to choose one part; select **Select All** inside a type group to select every instance of that type. Hold Shift while selecting a row to add it to the current selection.
- Some paired parts, such as the two pieces of a turret seat, are selected together automatically.
- Most Inspector edits apply to the active part. Position changes, color changes, removal, and logic gate operations apply to the full selection where appropriate.
- For multiple selected parts, **Stack selected parts at active position** moves selected non-joint parts onto the active part’s position in one step.

## Move, rotate, resize, and recolor parts

Scrap Mechanic uses a **Z-up** coordinate system: X and Y are on the ground plane, and Z is vertical.

### Move

- Drag a colored axis arrow in the viewport to move the selection along that axis.
- Enter X, Y, or Z coordinates in the Inspector to move the selection to that position.
- Use **Left -X**, **Right +X**, **Forward +Y**, **Back -Y**, **Up +Z**, or **Down -Z** to move one grid unit.
- Joints show their two attachment positions and orientation. Moving a connected part keeps the joint endpoints in sync.

### Orientation

- **Local X axis** and **Local Z axis (up)** set the active part’s orientation. The chosen axes must be perpendicular.
- **Rotate +90°** and **Rotate -90°** rotate the part around its local Z axis. **Reset rotation** restores its default orientation.
- Joint orientation follows the connected parts and is displayed for inspection.

### Dimensions

- Supported built-in blocks can be resized with **Width**, **Height**, and **Depth**. Values are limited to 1–256 grid units. In Scrap Mechanic’s Z-up coordinates, Height maps to Z and Depth maps to Y.
- Fixed-size parts show their footprint instead of resize controls.
- A multi-selection shows its combined world-space Width (X), Depth (Y), and Height (Z); it does not provide resize fields.

### Color

- Select the color swatch to open the picker, or type a six-digit hexadecimal value such as `3E9FFE` in the color field.
- Color changes apply to all selected parts. The Inspector accepts the six hexadecimal digits without the `#` prefix.

## Logic gates and logic connections

Logic gates and physical Controllers are different parts and have separate settings.

### Logic gate settings

- Select one or more Logic Gates and choose **Gate operation** in **Logic gate settings**. The operation is applied to every selected Logic Gate: **AND**, **OR**, **XOR**, **NAND**, **NOR**, or **XNOR**.
- Select **Gate Labels: On** to show each gate’s current operation in the viewport. Select it again to hide the labels.

### Logic connections

1. Select **Start Logic Editing**.
2. Click a connectable interactive part to choose the source. Click another part to add a directed connection from the source to that target.
3. **Shift-click** a different connectable part to switch the source. Click the source itself to connect it to itself.
4. Click a connection arrow to remove that connection. Click empty viewport space or **right-click** to clear the source and selection while staying in Logic Editing.
5. Select **Stop Logic Editing** or press **Esc** to leave the mode.

Logic Editing changes connections between interactive parts. It does not edit a physical Controller’s bearing rotation settings.

## Controller bearing rotations

Select a physical **Controller** part, then use **Controller settings → Edit bearing rotations...** in the Inspector. The editor lists connected bearings with saved settings.

- **Frame 0 · Start** is the bearing’s starting angle.
- **Frames 1–10** define the rotation sequence in degrees. Edit the displayed frame values directly.
- **Reverse direction** reverses that bearing’s sequence.

These settings apply to bearings connected to the physical Controller. Engines are not Controllers and do not use this editor. If no bearings appear, connect a bearing to the Controller in Scrap Mechanic and reopen the blueprint.

## Copy, paste, undo, and save

- **Copy** or **Ctrl+C** copies the selected parts. Joints between copied parts are included automatically; selecting a joint also copies it.
- **Paste** or **Ctrl+V** pastes the copied parts and joints into the same open blueprint. Each paste is offset one additional grid unit along +X, and the pasted objects become the selection.
- **Copy block state (connections)** is off by default. Turn it on before pasting to copy interactive-part connections and Controller bearing settings. References between copied parts are redirected to the pasted copies; references to unselected parts continue to point to the originals. With the option off, copied connections and Controller settings are reset.
- **Undo** or **Ctrl+Z** reverses the last editor change. **Redo** or **Ctrl+Y** reapplies an undone change. Undo history belongs to the currently open blueprint and is not written to disk until saved.
- **Save Blueprint** or **Ctrl+S** writes edits to the selected blueprint’s `blueprint.json`.
- **Delete** or **Remove selected part(s)** removes the current selection. Connected joints are updated as needed; Undo can restore removed objects.

## Camera and keyboard shortcuts

### Mouse controls

| Action | Control |
| --- | --- |
| Orbit the camera | Right-drag over the viewport; orbiting can go below the creation |
| Pan the camera | Middle-drag |
| Zoom the camera | Mouse wheel |
| Move a selected part along an axis | Drag the matching axis arrow |
| Add a part to the selection | Shift-click |
| Clear the selection in Logic Editing | Right-click (a right-drag still orbits the camera) |

### Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| **Ctrl+S** | Save the open blueprint |
| **Ctrl+Z** | Undo |
| **Ctrl+Y** | Redo |
| **Ctrl+C** | Copy the selection |
| **Ctrl+V** | Paste copied parts and joints |
| **Delete** | Remove the selection |
| **Esc** | Exit Logic Editing |

Shortcuts work while the **Blueprint Editor** window is focused and no text field, other UI control, or popup is capturing input. Mouse controls work while the pointer is over the 3D viewport. There are no keyboard shortcuts for moving or rotating parts; use the Inspector or axis arrows.

## Models and compatibility

- The **Scene** section reports the part count, loaded game meshes, built-in block previews, and any parts shown with fallback boxes.
- The editor loads available Scrap Mechanic game models when possible. Unsupported assets or modded parts may use a fallback box; their blueprint data can still be edited.
- Large scenes may use reduced real-mesh detail to keep the viewport responsive.
- Some blueprints contain Survival-only parts that cannot be spawned in Creative mode. The editor identifies these parts so you can remove them or use the blueprint in a Survival world.

## Build from source

The project is a C++20 Windows application built with Visual Studio 2022, the v143 C++ toolset, and the Windows SDK. It uses vcpkg dependencies from the `x64-windows` and `x64-windows-static-md` triplets. Set `VCPKG_ROOT` to your vcpkg installation, or place vcpkg beside the project as expected by the project file.

Install dependencies if needed:

```powershell
vcpkg install glad:x64-windows nlohmann-json:x64-windows glfw3:x64-windows-static-md
```

Open `Blueprint Editor.sln`, choose **Release | x64**, and build. `items.json` is embedded during the build. GLFW is statically linked; a Microsoft Visual C++ runtime may still be required on the target system.

## Help and feedback

Use the in-app **Help** window for an editing guide. Send **all bug reports and feature requests** to Discord user **@consthater**.

Developed by Andrex. This is an independent fan project and is not affiliated with Axolot Games or Scrap Mechanic.
