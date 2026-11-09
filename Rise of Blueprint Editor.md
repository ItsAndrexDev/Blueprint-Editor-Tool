# Rise of Blueprint Editor

I started this project with a fairly direct question: could I look at a Scrap Mechanic blueprint and make sense of it without having to load it in the game? What began as a parser and a small desktop experiment grew into a Windows editor where I can open a creation, inspect it in 3D, change parts and wiring, and save the result back to the same blueprint.

The commit history starts on October 7, 2026. At first, I was laying the groundwork: **9ecaf4f** brought in the Visual Studio project and connected Dear ImGui, GLFW, and OpenGL; **e7e467d** added the first parser and an example blueprint. Then **95377b4** gave the project a way into my actual creations by scanning Scrap Mechanic’s user folder and showing the saved blueprint icons. Those steps sound modest, but they set up the pattern the editor still follows: read the game’s data, show it in a useful form, and write back only the fields I mean to change.

## Getting the blueprint into view

A Scrap Mechanic blueprint is JSON. It stores bodies, parts, positions, orientations, colors, joints, and controller data. I needed a bridge between that nested structure and the things I wanted to click on screen, so the parser turns each part into a small editor record. The record remembers the part’s shape ID and transform, but also where it came from in the original JSON: which body and which child entry.

That last detail is important. When I move something, the editor can update its position in place and leave the rest of the blueprint’s data alone. It doesn’t need to understand every field in the file to preserve it. Joints are parsed into the same list of selectable objects, so I can inspect the physical connections alongside the parts themselves.

The loader looks under the Scrap Mechanic user directory in `%APPDATA%`, reads the blueprint description for its display name, and loads its icon. I can choose a creation from a thumbnail, edit it, then return to the list. It’s a small convenience, but it makes the tool feel connected to the game rather than like a generic JSON viewer.

## Making the viewport feel right

The early UI work arrived in **507ddc7**, simply titled “UI.” From there, the viewport became the center of the project. It uses Scrap Mechanic’s Z-up coordinates—X and Y on the ground and Z for height—and projects the parts into a camera view with orbit, pan, and zoom. There’s a grid under the creation, and the initial camera framing is calculated from the scene’s bounds so the blueprint opens at a useful scale.

One of the fixes I remember clearly is **7f32077**, “fixed blueprint mirroring.” The scene looked backward compared with the game’s preview. The solution was to reflect the horizontal screen projection, keeping the saved positions and orientations untouched. It’s a good example of the kind of visual bug that can be deceptively confusing: the data can be perfectly correct while the view makes the whole creation feel wrong.

I also wanted actual game parts in the viewport, not a pile of generic cubes. The asset catalog searches the installed game’s ShapeSet data for models, textures, colors, and part footprints. The editor includes readers for FBX, COLLADA/DAE, and Ogre mesh files, then sends the resulting geometry through an OpenGL renderer with depth testing. Where a model can’t be loaded—especially for a modded or unsupported part—the editor shows a fallback box. The underlying blueprint entry is still there and can still be edited.

That rendering work involves a lot of practical details. A part’s grid footprint and its visible mesh aren’t always the same size or centered on the same point. The catalog reads box, hull, sphere, or cylinder data to estimate placement dimensions. The renderer preserves authored pivots for models instead of blindly centering every mesh, because doing that can make a part appear offset from where the game places it. For very large creations, it limits scene detail and reports when it has switched to a coarser preview so the viewport stays responsive.

## Editing without losing the connections

Once I could see the blueprint, I wanted to move things around. The Inspector handles exact position, orientation, supported block dimensions, and color. In the viewport, I can drag an axis arrow to move the selection along X, Y, or Z. The drag code converts movement on screen back into whole grid steps, preserving partial movement until it adds up to a full unit.

Selection grew beyond one part. I can Shift-click to add parts, search by part name in the Inspector, and select every instance of a type. Paired turret-seat pieces are selected together because they behave as a unit. With multiple parts selected, I can move or recolor them as a group, or stack non-joint parts onto the active part’s position.

Moving a connected part means updating more than its position. The parser keeps track of joint endpoints, and when a part moves or rotates, it transforms the attachment point from the old part orientation into the new one. This is the sort of quiet bookkeeping that makes the tool useful: I can work at the level of parts, while the editor keeps the blueprint’s structural references aligned.

The history works by storing JSON snapshots as I edit. Undo and Redo restore a snapshot and rebuild the editor’s part list from it. Continuous actions such as a drag are grouped, and the history is capped at 64 states. Saving writes the formatted JSON to a temporary file and then replaces the blueprint file, so the edit goes to the selected creation rather than to some unrelated export.

## Logic editing became its own mode

A major leap came with **5c87643**, “logic manipulation implemented.” Logic connections are stored as controller IDs, so the editor builds a map between those IDs and visible parts. In Logic Editing mode, I click a source, then targets, and the viewport draws directed arrows between them. Clicking an arrow removes that connection. I can even connect a part to itself.

This mode took care to keep the data and the picture separate. **afe8acf**, “Show all connections button,” added a visibility option for busy diagrams. With it off, the editor can focus on connections entering or leaving the selected source; with it on, it draws the whole network. The button changes what I see, not the wiring saved in the blueprint.

Logic Gates have their own settings. I can set selected gates to AND, OR, XOR, NAND, NOR, or XNOR, and turn on labels to see each gate’s operation in the viewport. These are separate from physical Controllers, which store bearing rotation instructions. Keeping those concepts separate in the Inspector helped me avoid treating every interactive part as if it had the same data.

The Controller editor arrived in **b7bd7cd**. It follows the selected Controller’s saved joint references, finds the associated bearings, and exposes their frame angles and direction flag. When I change a frame, the code updates its target angle plus the derived start and end angles. It’s a small niche feature, but it lets me adjust a creation’s mechanical behavior without opening the game just to change a rotation sequence.

## Copy and paste was more complicated than it looked

I added the first copy-and-paste work in **8c9a7d0**. The commit title even admits that connection copying still didn’t work. That turned out to be a useful reminder: copying the visible parts is the easy bit. A blueprint also contains body membership, joint endpoints, controller IDs, logic links, and bearing settings that have to make sense after duplication.

The finished paste flow gives new IDs to copied controllers and joints, moves the parts along +X, and repairs the flattened child indices used by joints. If I copy only a few children from a body, they stay in that body. If I copy every child, the paste creates a separate body. An optional block-state setting decides whether the copied logic connections and controller bearing settings should travel too. When it is enabled, references to other copied parts are redirected to the new copies; references to unselected parts can continue pointing to their originals.

That’s why this feature belongs in the parser layer as much as in the UI. The button says “Paste,” but the work is really about making a second, internally consistent piece of blueprint data.

## The project grew up, and the code moved with it

By October 11, the project had help content, a more complete README, gate editing, and copy/paste. The October 12 commits **071fc68** and **b511c41** added gate labels and made state copying work. **f9beda7** later improved Shift-click selection and the part search workflow. Then came **bb32b9f**, the large cleanup commit that split the viewport and parser into focused files for actions, rendering, asset loading, input, gizmos, logic connections, clipboard operations, and saving. Finally, **2e41be0** moved the UI into its own folder and organized the embedded resources, including the app icon and bundled item data.

Looking back through those commits, I can see the project’s shape change. It started as a way to parse a file and show an image. Then I kept running into the next thing I wanted to do: see the actual models, select a group, understand why a bearing was wired a certain way, duplicate an assembly, or fix a gate without rebuilding the creation in-game. Each feature pushed the editor closer to feeling like a workshop for blueprints.

That’s the rise of Blueprint Editor for me: not one grand rewrite, but a string of practical questions answered in code. How do I preserve data I don’t understand? How do I show the creation the way the game does? How do I move a part without breaking its joints? How do I duplicate a network without its IDs colliding? The project’s best pieces came from following those questions all the way through—from the JSON on disk to the part on screen, and back again.
