#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "scrap_parser.hpp"

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720

std::vector<GLuint> iconTextures;
json blueprint;
json items;
int iconWidth = 0;
int iconHeight = 0;
int programStatus = 1;
int selectedBlueprintIndex = -1;
// Loads an image file and returns an OpenGL Texture ID
GLuint LoadTextureFromFile(const char* filename, int* out_width, int* out_height) {
	int width, height, channels;
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

void render_imgui() {
	if (programStatus == 1) {
		ImGui::Begin("Blueprint Loader");
		ImGui::Text("Select a blueprint to load:");
		int index = -1;
		for (auto& iconTexture : iconTextures) {
			index++;
			ImGui::PushID(index); // Push a unique ID for each button to avoid ID conflicts
			ImTextureID texID = (ImTextureID)(intptr_t)iconTexture;
			if (ImGui::ImageButton("##bpIcon", texID, ImVec2(64, 64))) {
				selectedBlueprintIndex = index;
			}
			ImGui::PopID(); // Pop the unique ID after the button
		}
		ImGui::End();
	}
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	// Only update the viewport matrix here
	glViewport(0, 0, width, height);
}

int main() {
	if (!glfwInit()) return -1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "OpenGL Window", NULL, NULL);
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

	glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");


	// load blueprint folders and their icons
	std::vector<Parser::BlueprintPaths> blueprintFolders = Parser::getBlueprintFolders(); // Call the function to get blueprint folders

	for (auto& bp : blueprintFolders) {
		GLuint textureID = LoadTextureFromFile(bp.iconPath.string().c_str(), &iconWidth, &iconHeight);
		iconTextures.push_back(textureID);
	}



	// Main loop
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		// 1. Start ImGui Frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// 2. Process UI commands
		render_imgui();

		// 3. Finalize ImGui geometry data
		ImGui::Render();

		// 4. OpenGL Background Render
		int display_w, display_h;
		glfwGetFramebufferSize(window, &display_w, &display_h);
		glViewport(0, 0, display_w, display_h);
		glClear(GL_COLOR_BUFFER_BIT);

		// 5. Draw ImGui on top of OpenGL scene
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// 6. Handle Multi-Viewport OS windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

		// 7. Single Buffer Swap
		glfwSwapBuffers(window);
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

/*
int main() {
	std::ifstream input("blueprint.json");
	if (!input.is_open()) {
		std::cerr << "Failed to open blueprint.json" << std::endl;
		return 1;
	}

	json blueprint;
	input >> blueprint;
	input.close();

	input.open("items.json");
	if (!input.is_open()) {
		std::cerr << "Failed to open items.json" << std::endl;
		return 1;
	}

	json items;
	input >> items;
	input.close();

	std::vector<Block> blockList;

	// Iterate through bodies array, then childs array
	int bodyIndex = -1;
	int childIndex = -1;
	if (blueprint.contains("bodies") && blueprint["bodies"].is_array()) {
		for (const auto& body : blueprint["bodies"]) {
			bodyIndex++;
			if (body.contains("childs") && body["childs"].is_array()) {
				for (const auto& blockJson : body["childs"]) {
					childIndex++;
					Block block = parseBlock(blockJson, bodyIndex, childIndex);
					blockList.push_back(block);
				}
			}
		}
	}
	// example: Recolor all blocks to white and move every "Toilet" up by 3 units in the z-axis
	for (auto& block : blockList) {
		block.color = "ffffff";
		if (block.shapeID == findShapeIDByBlockName("Toilet", items)) {
			block.pos.z += 3;
		}
	}
	applyBlockListToNode(blockList, blueprint);
	saveBlueprint("blueprint.json", blueprint);

	return 0;
}


*/