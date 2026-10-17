#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <utility>
#include "helpers/blueprint_viewport.hpp"
struct BlueprintItem {
	std::string name;
	GLuint textureID;
	Parser::BlueprintPaths paths;
};
enum ProgramStatus
{
	BLUEPRINT_SELECT,
	BLUEPRINT_EDIT
};
// Takes the output json by reference instead of returning a reference to a local
void renderBlueprintLoader(std::vector<BlueprintItem>& loadedPrints,
	ProgramStatus& status,
	json& outBlueprint,
	BlueprintItem& itemSelected,
	std::vector<Parser::Block>& blocks,
	bool& refreshRequested,
	ImGuiID& helpDockId) {
	ImGui::Begin("Blueprint Loader");
	helpDockId = ImGui::GetWindowDockID();
	ImGui::TextWrapped("Select a blueprint to load:");
	static char searchBuffer[128] = "";
	const ImGuiStyle& style = ImGui::GetStyle();
	const float refreshWidth = ImGui::CalcTextSize("Refresh").x + style.FramePadding.x * 2.0f;
	const float searchWidth = std::max(64.0f, ImGui::GetContentRegionAvail().x - refreshWidth - style.ItemSpacing.x);
	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##BlueprintSearch", "Search blueprints...", searchBuffer, sizeof(searchBuffer));
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) refreshRequested = true;
	ImGui::SameLine();
	ImGui::TextDisabled("%zu blueprints", loadedPrints.size());
	ImGui::Separator();
	std::vector<size_t> visibleItems;
	visibleItems.reserve(loadedPrints.size());
	for (size_t i = 0; i < loadedPrints.size(); ++i) {
		if (loadedPrints[i].name.find(searchBuffer) != std::string::npos) visibleItems.push_back(i);
	}
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(visibleItems.size()), 102.0f);
	while (clipper.Step()) for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex) {
		auto& item = loadedPrints[visibleItems[visibleIndex]];
		ImGui::PushID(static_cast<int>(visibleItems[visibleIndex]));

		ImTextureID texID = (ImTextureID)(intptr_t)item.textureID;

		if (ImGui::ImageButton("##bpIcon", texID, ImVec2(88, 88))) {
			std::ifstream blueprintFile(item.paths.folderPath / "blueprint.json");
			if (blueprintFile.is_open()) {
				try {
					json candidateBlueprint;
					blueprintFile >> candidateBlueprint;
					auto candidateBlocks = Parser::parseBlueprint(candidateBlueprint);
					outBlueprint = std::move(candidateBlueprint);
					blocks = std::move(candidateBlocks);
					itemSelected = item;
					BlueprintViewport::resetSelection();
					BlueprintViewport::notify("Blueprint loaded: " + item.name);
					status = BLUEPRINT_EDIT;
					std::cout << "Selected Blueprint: " << item.name << std::endl;
				}
				catch (const std::exception& e) {
					std::cerr << "Failed to parse blueprint: " << e.what() << std::endl;
				}
			}
			else {
				std::cerr << "Failed to open blueprint: " << item.paths.folderPath / "blueprint.json" << std::endl;
			}
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextWrapped("%s", item.name.c_str());
		ImGui::EndGroup();

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::End();   // moved outside the loop
}
void renderBlueprintEditor(ProgramStatus& status, json& blueprint, BlueprintItem& itemSelected, std::vector<Parser::Block>& blocks, const json& items) {
	bool backToSelectionRequested = false;
	BlueprintViewport::render(blueprint, itemSelected.paths.folderPath / "blueprint.json", blocks, items,
		itemSelected.name, backToSelectionRequested);
	if (backToSelectionRequested) status = BLUEPRINT_SELECT;
}


void renderHelpPage(ImGuiID loaderDockId) {
	if (loaderDockId != 0) ImGui::SetNextWindowDockID(loaderDockId, ImGuiCond_FirstUseEver);
	ImGui::Begin("Help");
	ImGui::Text("Scrap Mechanic Blueprint Editor");
	ImGui::TextWrapped("Use this guide to browse blueprints, edit parts, manage connections, and save your work.");
	ImGui::Separator();

	ImGui::SeparatorText("1. Open and save a blueprint");
	ImGui::BulletText("The Blueprint Loader scans the Scrap Mechanic user folder when the editor starts. Search blueprints... filters by name (case-sensitive). Refresh scans the folder again.");
	ImGui::BulletText("Select a blueprint thumbnail to open it. Back to Selection returns to the list. Save Blueprint writes changes to that blueprint's existing blueprint.json file.");
	ImGui::BulletText("Edits are not written to disk until you save. Undo and Redo change the current editor history; save after undoing or redoing to store that version.");
	ImGui::BulletText("Blueprints are read from %APPDATA%\\Axolot Games\\Scrap Mechanic\\User\\<User_...>\\Blueprints.");
	ImGui::Spacing();

	ImGui::SeparatorText("2. Select and find parts");
	ImGui::BulletText("Click a part in the viewport to select it. Shift-click adds another part. Click empty space in normal editing mode to clear the selection. The cyan outline marks the active part.");
	ImGui::BulletText("Find parts... in the Inspector searches part names without regard to case. Expand a type to see its instances, click a row to select one, or click Select All within that type to select every matching part. Joint parts such as bearings and suspensions are included.");
	ImGui::BulletText("Hold Shift while clicking a part row or Select All to add those parts to the current selection instead of replacing it. Some paired parts, such as turret-seat pieces, are selected together automatically.");
	ImGui::BulletText("The active part is the last selected part. Multi-selection movement, color, removal, and other supported bulk edits use the whole selection.");
	ImGui::Spacing();

	ImGui::SeparatorText("3. Move, orient, resize, and recolor");
	ImGui::BulletText("Scrap Mechanic uses Z as up: X and Y are horizontal. Move the selection by dragging an axis arrow, editing the Inspector's X/Y/Z position fields, or using the six one-grid-step direction buttons.");
	ImGui::BulletText("For a multi-selection, Stack selected parts at active position aligns selected non-joint parts to the active part's position in one step.");
	ImGui::BulletText("For a part, Local X axis and Local Z axis (up) set its orientation. The axes must remain perpendicular. Rotate +90° and Rotate -90° turn around local Z; Reset rotation restores the default. Joint orientation follows connected parts.");
	ImGui::BulletText("Supported built-in blocks have Width, Height, and Depth fields (1 to 256 grid units). Height maps to Z and Depth maps to Y. Other parts show their fixed footprint. Multi-selection shows its combined world-space dimensions instead of resize fields.");
	ImGui::BulletText("Choose a color with the color swatch or type six hexadecimal digits such as 3E9FFE. Color changes apply to all selected parts.");
	ImGui::BulletText("Remove selected part(s) deletes the selection; Delete does the same. Connected joints are updated as needed, and Undo can restore removed objects.");
	ImGui::Spacing();

	ImGui::SeparatorText("4. Logic gates and connections");
	ImGui::BulletText("Logic Gate settings are separate from physical Controller settings. Select one or more Logic Gates and choose AND, OR, XOR, NAND, NOR, or XNOR under Logic gate settings. The chosen operation applies to selected gates.");
	ImGui::BulletText("Gate Labels: On displays each logic gate's current operation in the viewport. Select it again to hide labels.");
	ImGui::BulletText("Start Logic Editing to edit directed connections between interactive parts. Click a connectable part to choose a source, then click a target to connect it. Shift-click another part to switch the source; click the source itself to connect it to itself.");
	ImGui::BulletText("Show all connections while editing is off by default. With a source selected, it shows only arrows to or from that source. Turn it on to show every connection. With no source selected, all connections stay visible so you can choose one. Its button shows On or Off, is available during Logic Editing, and resets to Off when you exit the mode.");
	ImGui::BulletText("Click a connection arrow to remove that connection. Click empty viewport space or right-click to clear the source and selection while staying in Logic Editing. Stop Logic Editing or Esc exits the mode.");
	ImGui::Spacing();

	ImGui::SeparatorText("5. Physical Controller bearing rotations");
	ImGui::BulletText("Select the physical Controller part and open Controller settings > Edit bearing rotations... in the Inspector. This edits connected bearing rotations; it does not edit engines or logic-gate operations.");
	ImGui::BulletText("Frame 0 is the bearing's starting angle. Frames 1-10 are the rotation steps, in degrees. Reverse direction flips that bearing's sequence. Only connected bearings with saved settings are listed.");
	ImGui::Spacing();

	ImGui::SeparatorText("6. Copy, paste, and history");
	ImGui::BulletText("Copy duplicates selected parts and joints. Joints connecting selected parts are included automatically. Paste places the copy one additional grid unit along +X for each paste and selects the pasted objects. A partial-body copy stays in its source body; copying every part in a body creates a separate body copy.");
	ImGui::BulletText("Copy block state (connections) is off by default. Turn it on before pasting to preserve interactive connections and physical Controller bearing settings. Connections between copied parts point to their copies; links to unselected parts remain connected to the originals. With the option off, copied connection and Controller state is reset.");
	ImGui::BulletText("Undo and Redo are available from the toolbar and keyboard. The history is for the open blueprint and is not saved until you select Save Blueprint.");
	ImGui::Spacing();

	ImGui::SeparatorText("7. Camera controls");
	if (ImGui::BeginTable("HelpCameraControls", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Action");
		ImGui::TableSetupColumn("Control");
		ImGui::TableHeadersRow();
		const char* controls[][2] = { {"Orbit, including below the creation", "Right-drag"}, {"Pan", "Middle-drag"}, {"Zoom", "Mouse wheel"}, {"Move along an axis", "Drag an axis arrow"}, {"Restore the default view", "Reset view button"} };
		for (const auto& row : controls) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(row[0]);
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row[1]);
		}
		ImGui::EndTable();
	}
	ImGui::Spacing();

	ImGui::SeparatorText("8. Keyboard shortcuts");
	if (ImGui::BeginTable("HelpKeyboardShortcuts", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupColumn("Shortcut");
		ImGui::TableSetupColumn("Action");
		ImGui::TableHeadersRow();
		const char* shortcuts[][2] = { {"Ctrl+S", "Save the open blueprint"}, {"Ctrl+Z", "Undo"}, {"Ctrl+Y", "Redo"}, {"Ctrl+C", "Copy the selection"}, {"Ctrl+V", "Paste copied parts and joints"}, {"Delete", "Remove the selection"}, {"Esc", "Exit Logic Editing"} };
		for (const auto& row : shortcuts) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(row[0]);
			ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row[1]);
		}
		ImGui::EndTable();
	}
	ImGui::BulletText("Keyboard shortcuts work while Blueprint Editor is focused and no text field, UI item, or popup is taking input. Shift-click is a mouse gesture, not a keyboard shortcut.");
	ImGui::Spacing();

	ImGui::SeparatorText("Models and compatibility");
	ImGui::BulletText("The Scene panel reports loaded game models, built-in block previews, fallback boxes, and renderer errors. Unsupported or modded assets may show a fallback box even though their blueprint data can still be edited.");
	ImGui::BulletText("Large blueprints may use reduced real-mesh detail to keep the viewport responsive. Survival-only parts cannot be spawned in Creative mode; remove them or use the blueprint in a Survival world.");
	ImGui::Spacing();
	ImGui::SeparatorText("Help and feedback");
	ImGui::TextWrapped("Send all bug reports and feature requests to Discord user @consthater.");
	ImGui::TextWrapped("Developed by Andrex. This is an independent fan project and is not affiliated with Axolot Games or Scrap Mechanic.");
	ImGui::End();
}
