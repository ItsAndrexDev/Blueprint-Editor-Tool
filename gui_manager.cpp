#define STB_IMAGE_IMPLEMENTATION
#include "gui_manager.hpp"
#include "helpers/stb_image.h"
#include "imgui/imgui_internal.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {
bool hasLegacyBlueprintWindow(const char* iniFilename) {
	if (!iniFilename || !*iniFilename) return false;
	std::ifstream settings(iniFilename);
	std::string line;
	while (std::getline(settings, line))
		if (line == "[Window][Blueprint]") return true;
	return false;
}
}

GuiManager::Gui::Gui(int width, int height, const char* title) {
	if (!glfwInit()) return;
	glfwStarted = true;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_DEPTH_BITS, 24);

	window = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!window) {
		glfwTerminate();
		glfwStarted = false;
		return;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		glfwDestroyWindow(window);
		window = nullptr;
		glfwTerminate();
		glfwStarted = false;
		return;
	}

	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// Initialize ImGui
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	imguiContextCreated = true;
	io = &ImGui::GetIO(); (void)io;
	std::error_code iniError;
	const bool hasSavedLayout = io->IniFilename && *io->IniFilename &&
		std::filesystem::exists(std::filesystem::path(io->IniFilename), iniError);
	defaultDockLayoutPending = !hasSavedLayout || hasLegacyBlueprintWindow(io->IniFilename);
	io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Allow docking
	io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();

	if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
		ImGui::DestroyContext();
		imguiContextCreated = false;
		glfwDestroyWindow(window);
		window = nullptr;
		glfwTerminate();
		glfwStarted = false;
		io = nullptr;
		return;
	}
	glfwBackendInitialized = true;
	if (!ImGui_ImplOpenGL3_Init("#version 330")) {
		ImGui_ImplGlfw_Shutdown();
		glfwBackendInitialized = false;
		ImGui::DestroyContext();
		imguiContextCreated = false;
		glfwDestroyWindow(window);
		window = nullptr;
		glfwTerminate();
		glfwStarted = false;
		io = nullptr;
		return;
	}
	openglBackendInitialized = true;
	GuiTheme::ApplyUnityDark();
	ready = true;
}

GuiManager::Gui::~Gui() {
	if (openglBackendInitialized) ImGui_ImplOpenGL3_Shutdown();
	if (glfwBackendInitialized) ImGui_ImplGlfw_Shutdown();
	if (imguiContextCreated) ImGui::DestroyContext();
	if (window) glfwDestroyWindow(window);
	if (glfwStarted) glfwTerminate();
}

void GuiManager::Gui::SetupDockSpace() {
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
	if (defaultDockLayoutPending) {
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
		// Keep all primary pages together as tabs. The loader is selected on
		// first launch because it is the app's initial page.
		ImGui::DockBuilderDockWindow("Help", dockspace_id);
		ImGui::DockBuilderDockWindow("Blueprint Editor", dockspace_id);
		ImGui::DockBuilderDockWindow("Blueprint Loader", dockspace_id);
		if (ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(dockspace_id))
			dockNode->SelectedTabId = ImHashStr("Blueprint Loader");
		ImGui::DockBuilderFinish(dockspace_id);
		defaultDockLayoutPending = false;
	}

	ImGui::End();
}


void GuiManager::newFrame()
{
	glfwPollEvents();
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void GuiManager::endFrame(GLFWwindow* window, ImGuiIO* io)
{
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
	if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		GLFWwindow* backup_current_context = glfwGetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		glfwMakeContextCurrent(backup_current_context);
	}

	// 8. Swap Buffers
	glfwSwapBuffers(window);

	// Some drivers ignore swap interval; keep an idle editor from spinning at
	// an unlimited frame rate and consuming a full CPU core.
	using FrameClock = std::chrono::steady_clock;
	static auto previousFrameEnd = FrameClock::now();
	constexpr auto frameBudget = std::chrono::microseconds(16667);
	const auto now = FrameClock::now();
	if (now - previousFrameEnd < frameBudget)
		std::this_thread::sleep_for(frameBudget - (now - previousFrameEnd));
	previousFrameEnd = FrameClock::now();
}


GLuint GuiManager::LoadTextureFromFile(const char* filename, int* out_width, int* out_height) {
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
