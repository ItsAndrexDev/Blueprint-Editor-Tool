#pragma once

#include "imgui/imgui.h"

inline void renderHelpPage(ImGuiID loaderDockId) {

    if (loaderDockId != 0)
        ImGui::SetNextWindowDockID(loaderDockId, ImGuiCond_FirstUseEver);
    ImGui::Begin("Help");
    ImGui::Text("Scrap Mechanic Blueprint Editor");
    ImGui::TextWrapped(
        "Use this guide to browse blueprints, edit parts, manage connections, and save your work.");
    ImGui::Separator();

    ImGui::SeparatorText("1. Open and save a blueprint");
    ImGui::BulletText(
        "On first launch, choose the Scrap Mechanic installation folder containing Data and "
        "Survival. The editor remembers this folder for models and shape data. The Blueprint "
        "Loader scans the Scrap Mechanic user folder when the editor starts. Search "
        "blueprints... filters by name (case-sensitive). Refresh scans the folder again.");
    ImGui::BulletText(
        "Select a blueprint thumbnail to open it. Back to Selection returns to the list. Save "
        "Blueprint writes changes to that blueprint's existing blueprint.json file.");
    ImGui::BulletText(
        "Edits are not written to disk until you save. Undo and Redo change the current editor "
        "history; save after undoing or redoing to store that version.");
    ImGui::BulletText("Blueprints are read from %APPDATA%\\Axolot Games\\Scrap "
                      "Mechanic\\User\\<User_...>\\Blueprints.");
    ImGui::Spacing();

    ImGui::SeparatorText("2. Select and find parts");
    ImGui::BulletText("Click a part in the viewport to select it. Shift-click adds another part. "
                      "Click empty space in normal editing mode to clear the selection. The cyan "
                      "outline marks the active part.");
    ImGui::BulletText(
        "Find parts... in the Inspector searches part names without regard to case. Expand a type "
        "to see its instances, click a row to select one, or click Select All within that type to "
        "select every matching part. Joint parts such as bearings and suspensions are included.");
    ImGui::BulletText("Hold Shift while clicking a part row or Select All to add those parts to "
                      "the current selection instead of replacing it. Some paired parts, such as "
                      "turret-seat pieces, are selected together automatically.");
    ImGui::BulletText("The active part is the last selected part. Multi-selection movement, color, "
                      "removal, and other supported bulk edits use the whole selection.");
    ImGui::Spacing();

    ImGui::SeparatorText("3. Move, orient, resize, and recolor");
    ImGui::BulletText("Scrap Mechanic uses Z as up: X and Y are horizontal. Move the selection by "
                      "dragging an axis arrow, editing the Inspector's X/Y/Z position fields, or "
                      "using the six one-grid-step direction buttons.");
    ImGui::BulletText("For a multi-selection, Stack selected parts at active position aligns "
                      "selected non-joint parts to the active part's position in one step.");
    ImGui::BulletText(
        "For a part, Local X axis and Local Z axis (up) set its orientation. The axes must remain "
        "perpendicular. Rotate +90° and Rotate -90° turn around local Z; Reset rotation restores "
        "the default. Joint orientation follows connected parts.");
    ImGui::BulletText(
        "Supported built-in blocks have Width, Height, and Depth fields (1 to 256 grid units). "
        "Height maps to Z and Depth maps to Y. Other parts show their fixed footprint. "
        "Multi-selection shows its combined world-space dimensions instead of resize fields.");
    ImGui::BulletText("Choose a color with the color swatch or type six hexadecimal digits such as "
                      "3E9FFE. Color changes apply to all selected parts.");
    ImGui::BulletText(
        "Remove selected part(s) deletes the selection; Delete does the same. Connected joints are "
        "updated as needed, and Undo can restore removed objects.");
    ImGui::Spacing();

    ImGui::SeparatorText("4. Logic gates and connections");
    ImGui::BulletText("Logic Gate settings are separate from physical Controller settings. Select "
                      "one or more Logic Gates and choose AND, OR, XOR, NAND, NOR, or XNOR under "
                      "Logic gate settings. The chosen operation applies to selected gates.");
    ImGui::BulletText("Gate Labels: On displays each logic gate's current operation in the "
                      "viewport. Select it again to hide labels.");
    ImGui::BulletText(
        "Start Connection Editing to manage signal and mechanical links. Buttons, switches, "
        "sensors, Logic Gates, and Timers send signals to supported interactive parts. Seats can "
        "connect to buttons and switches; Driver's Seats can also connect to engines, thrusters, "
        "Controllers, and bearings. Signal sources can target Pistons, but Pistons cannot be "
        "sources. Physical Controllers can create sequenced Piston links. Engines connect to "
        "bearings, Driver's Seats to bearings and suspensions, and physical Controllers to "
        "bearings. Gas Engines can drive up to "
        "five bearings.");
    ImGui::BulletText("Click a source, then a compatible target. Signal links show arrows; mechanical "
                      "joint links use dashed lines. Shift-click changes the source. Click a "
                      "connection to remove it. Click empty space or right-click to clear selection.");
    ImGui::BulletText(
        "Show all connections while editing is off by default. With a source selected, it shows "
        "only arrows to or from that source. Turn it on to show every connection. With no source "
        "selected, all connections stay visible so you can choose one. Its button shows On or Off, "
        "is available during Connection Editing, and resets to Off when you exit the mode.");
    ImGui::BulletText("Right-click clears the source and selection while staying in Connection "
                      "Editing. Stop Connection Editing or Esc exits the mode.");
    ImGui::Spacing();

    ImGui::SeparatorText("5. Physical Controller joint sequences");
    ImGui::BulletText("Select the physical Controller part to edit its seconds-per-frame time. "
                      "Zero is allowed; positive values add delay between frames. Open Controller "
                      "settings > Edit Controller joints... to edit bearing rotations and Piston "
                      "sequence settings; this does not edit engines or Logic Gate operations.");
    ImGui::BulletText("Frame 0 is the starting position. Frames 1-10 set bearing rotation in "
                      "degrees or a Piston setting from 0 to 15. Reverse direction flips bearing "
                      "sequences. Connected bearings and Pistons with saved frames are listed.");
    ImGui::Spacing();

    ImGui::SeparatorText("6. Copy, paste, and history");
    ImGui::BulletText(
        "Copy duplicates selected parts and joints. Joints connecting selected parts are included "
        "automatically. Paste places the copy one additional grid unit along +X for each paste and "
        "selects the pasted objects. A partial-body copy stays in its source body; copying every "
        "part in a body creates a separate body copy.");
    ImGui::BulletText(
        "Copy block state (connections) is off by default. Turn it on before pasting to preserve "
        "interactive connections and physical Controller bearing settings. Connections between "
        "copied parts point to their copies; links to unselected parts remain connected to the "
        "originals. With the option off, copied connection and Controller state is reset.");
    ImGui::BulletText("Undo and Redo are available from the toolbar and keyboard. The history is "
                      "for the open blueprint and is not saved until you select Save Blueprint.");
    ImGui::Spacing();

    ImGui::SeparatorText("7. Camera controls");
    if (ImGui::BeginTable("HelpCameraControls",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Control");
        ImGui::TableHeadersRow();
        const char* controls[][2] = {{"Orbit, including below the creation", "Right-drag"},
                                     {"Pan", "Middle-drag"},
                                     {"Zoom", "Mouse wheel"},
                                     {"Move along an axis", "Drag an axis arrow"},
                                     {"Restore the default view", "Reset view button"}};
        for (const auto& row : controls) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row[1]);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();

    ImGui::SeparatorText("8. Keyboard shortcuts");
    if (ImGui::BeginTable("HelpKeyboardShortcuts",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Shortcut");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();
        const char* shortcuts[][2] = {{"Ctrl+S", "Save the open blueprint"},
                                      {"Ctrl+Z", "Undo"},
                                      {"Ctrl+Y", "Redo"},
                                      {"Ctrl+C", "Copy the selection"},
                                      {"Ctrl+V", "Paste copied parts and joints"},
                                      {"Delete", "Remove the selection"},
                                      {"Esc", "Exit Connection Editing"}};
        for (const auto& row : shortcuts) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(row[1]);
        }
        ImGui::EndTable();
    }
    ImGui::BulletText(
        "Keyboard shortcuts work while Blueprint Editor is focused and no text field, UI item, or "
        "popup is taking input. Shift-click is a mouse gesture, not a keyboard shortcut.");
    ImGui::Spacing();

    ImGui::SeparatorText("Models and compatibility");
    ImGui::BulletText("The Scene panel reports loaded game models, built-in block previews, "
                      "fallback boxes, and renderer errors. Unsupported or modded assets may show "
                      "a fallback box even though their blueprint data can still be edited.");
    ImGui::BulletText("Large blueprints may use reduced real-mesh detail to keep the viewport "
                      "responsive. Survival-only parts cannot be spawned in Creative mode; remove "
                      "them or use the blueprint in a Survival world.");
    ImGui::Spacing();
    ImGui::SeparatorText("Help and feedback");
    ImGui::TextWrapped("Send all bug reports and feature requests to Discord user @consthater.");
    ImGui::TextWrapped("Developed by Andrex. This is an independent fan project and is not "
                       "affiliated with Axolot Games or Scrap Mechanic.");
    ImGui::End();
}
