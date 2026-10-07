#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "scrap_parser.hpp"
#include <fstream>
#include <iostream>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

struct BlueprintItem {
	std::string name;
	GLuint textureID;
	Parser::BlueprintPaths paths;
};

std::vector<BlueprintItem> loadedBlueprints;
json blueprint;
json items;
int programStatus = 1;


// Loads an image file and returns an OpenGL Texture ID
GLuint LoadTextureFromFile(const char* filename, int* out_width, int* out_height) {
	int width, height, channels;
	stbi_set_flip_vertically_on_load(false); // Scrap Mechanic PNGs are right side up
	unsigned char* data = stbi_load(filename, &width, &height, &channels, 4); // Force RGBA
	if (!data) return 0;

	GLuint textureID;
	glGenTextures(1, &textureID);
	glBindTexture(GL_TEXTURE_2D, textureID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	stbi_image_free(data);

	if (out_width) *out_width = width;
	if (out_height) *out_height = height;

	return textureID;
}

// Helper: Sets up full-viewport invisible DockSpace so windows can dock anywhere
void SetupDockSpace() {
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags host_window_flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("MainDockSpaceWindow", nullptr, host_window_flags);
	ImGui::PopStyleVar(3);

	ImGuiID dockspace_id = ImGui::GetID("AppDockSpace");
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	ImGui::End();
}

void render_imgui() {
	if (programStatus == 1) {
		ImGui::Begin("Blueprint Loader");
		ImGui::Text("Select a blueprint to load:");
		static char searchBuffer[128] = "";
		ImGui::InputText("Search...", searchBuffer, 128);
		ImGui::Separator();

		int index = -1;
		for (auto& item : loadedBlueprints) {
			index++;
			if (item.name.find(searchBuffer) == std::string::npos) continue; // Skip items that don't match search
			ImGui::PushID(index);

			ImTextureID texID = (ImTextureID)(intptr_t)item.textureID;

			// Display icon button
			if (ImGui::ImageButton("##bpIcon", texID, ImVec2(64, 64))) {
				std::ifstream blueprintFile(item.paths.folderPath / "blueprint.json");
				blueprintFile >> blueprint;
				blueprintFile.close();
				programStatus = 2; // Move to blueprint editor
				std::cout << "Selected Blueprint: " << item.name << std::endl;
			}

			// Draw blueprint name right next to the button
			ImGui::SameLine();
			ImGui::BeginGroup();
			ImGui::TextUnformatted(item.name.c_str());
			ImGui::EndGroup();

			ImGui::PopID();
			ImGui::Separator();
		}

		ImGui::End();
	}
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

int main() {
	if (!glfwInit()) return -1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Scrap Mechanic Blueprint Editor", NULL, NULL);
	if (!window) {
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		glfwTerminate();
		return -1;
	}

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Allow docking
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Allow dragging floating windows outside main window

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Load blueprint folders and read names + icons
	std::vector<Parser::BlueprintPaths> blueprintFolders = Parser::getBlueprintFolders();

	for (auto& bp : blueprintFolders) {
		BlueprintItem item;
		item.paths = bp;
		item.name = bp.folderPath.filename().string(); // Fallback to directory name

		// Parse human-readable name from description.json
		if (std::filesystem::exists(bp.descriptionPath)) {
			std::ifstream descFile(bp.descriptionPath);
			if (descFile.is_open()) {
				try {
					json descJson;
					descFile >> descJson;
					if (descJson.contains("name") && descJson["name"].is_string()) {
						item.name = descJson["name"].get<std::string>();
					}
				}
				catch (...) {}
			}
		}

		// Load PNG icon texture
		int dummyW, dummyH;
		item.textureID = LoadTextureFromFile(bp.iconPath.string().c_str(), &dummyW, &dummyH);

		loadedBlueprints.push_back(item);
	}

	// Main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		// 1. Start ImGui Frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// 2. DockSpace Host (enables free docking anywhere)
		SetupDockSpace();

		// 3. Process UI windows
		render_imgui();

		// 4. Finalize ImGui geometry data
		ImGui::Render();

		// 5. OpenGL Background Render
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClearColor(0.15f, 0.15f, 0.15f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// 6. Draw ImGui on top of OpenGL scene
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// 7. Handle Multi-Viewport OS windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		// 8. Swap Buffers
		glfwSwapBuffers(window);
	}

	// Cleanup textures
	for (auto& item : loadedBlueprints) {
		if (item.textureID != 0) {
			glDeleteTextures(1, &item.textureID);
		}
	}

	// Cleanup ImGui
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}