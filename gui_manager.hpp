#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

namespace GuiManager
{
	class Gui {
		inline static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
			glViewport(0, 0, width, height);
		}
	public:
		GLFWwindow* window = nullptr;
		ImGuiIO* io = nullptr;
		bool isReady() const { return ready; }
		Gui(int width, int height, const char* title);
		~Gui();

		static void SetupDockSpace();

	private:
		bool glfwStarted = false;
		bool imguiContextCreated = false;
		bool glfwBackendInitialized = false;
		bool openglBackendInitialized = false;
		bool ready = false;
	};
	void newFrame();
	void endFrame(GLFWwindow* window, ImGuiIO* io);
	GLuint LoadTextureFromFile(const char* filename, int* out_width, int* out_height);
}

namespace GuiTheme {

	inline ImVec4 RGB(int r, int g, int b, float a = 1.0f) {
		return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
	}

	inline void ApplyUnityDark() {
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* c = style.Colors;

		// Palette (roughly Unity's Pro/Dark editor skin)
		const ImVec4 bgDark = RGB(35, 35, 35);    // inputs, tab bar, title bars
		const ImVec4 bgPanel = RGB(56, 56, 56);    // window / panel background
		const ImVec4 bgElevated = RGB(66, 66, 66);    // buttons, headers
		const ImVec4 bgHover = RGB(80, 80, 80);
		const ImVec4 bgActive = RGB(96, 96, 96);
		const ImVec4 border = RGB(26, 26, 26);
		const ImVec4 text = RGB(210, 210, 210);
		const ImVec4 textDim = RGB(128, 128, 128);
		const ImVec4 accent = RGB(44, 93, 135);   // Unity selection blue
		const ImVec4 accentHover = RGB(58, 121, 173);
		const ImVec4 accentLight = RGB(77, 144, 200);

		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = textDim;
		c[ImGuiCol_WindowBg] = bgPanel;
		c[ImGuiCol_ChildBg] = RGB(56, 56, 56, 0.0f);
		c[ImGuiCol_PopupBg] = RGB(46, 46, 46, 0.98f);
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_BorderShadow] = RGB(0, 0, 0, 0.0f);

		c[ImGuiCol_FrameBg] = bgDark;
		c[ImGuiCol_FrameBgHovered] = RGB(42, 42, 42);
		c[ImGuiCol_FrameBgActive] = RGB(48, 48, 48);

		c[ImGuiCol_TitleBg] = bgDark;
		c[ImGuiCol_TitleBgActive] = bgDark;
		c[ImGuiCol_TitleBgCollapsed] = bgDark;
		c[ImGuiCol_MenuBarBg] = RGB(40, 40, 40);

		c[ImGuiCol_ScrollbarBg] = RGB(0, 0, 0, 0.0f);
		c[ImGuiCol_ScrollbarGrab] = RGB(90, 90, 90);
		c[ImGuiCol_ScrollbarGrabHovered] = RGB(110, 110, 110);
		c[ImGuiCol_ScrollbarGrabActive] = RGB(130, 130, 130);

		c[ImGuiCol_CheckMark] = accentLight;
		c[ImGuiCol_SliderGrab] = RGB(120, 120, 120);
		c[ImGuiCol_SliderGrabActive] = accentLight;

		c[ImGuiCol_Button] = bgElevated;
		c[ImGuiCol_ButtonHovered] = bgHover;
		c[ImGuiCol_ButtonActive] = bgActive;

		c[ImGuiCol_Header] = accent;
		c[ImGuiCol_HeaderHovered] = RGB(70, 70, 70);
		c[ImGuiCol_HeaderActive] = accentHover;

		c[ImGuiCol_Separator] = border;
		c[ImGuiCol_SeparatorHovered] = accentHover;
		c[ImGuiCol_SeparatorActive] = accentLight;

		c[ImGuiCol_ResizeGrip] = RGB(0, 0, 0, 0.0f);
		c[ImGuiCol_ResizeGripHovered] = accentHover;
		c[ImGuiCol_ResizeGripActive] = accentLight;

		// Tabs (names valid for ImGui 1.90.9+)
		c[ImGuiCol_Tab] = bgDark;
		c[ImGuiCol_TabHovered] = bgHover;
		c[ImGuiCol_TabSelected] = bgPanel;
		c[ImGuiCol_TabSelectedOverline] = accentLight;
		c[ImGuiCol_TabDimmed] = bgDark;
		c[ImGuiCol_TabDimmedSelected] = RGB(48, 48, 48);
		c[ImGuiCol_TabDimmedSelectedOverline] = RGB(0, 0, 0, 0.0f);

		// Docking
		c[ImGuiCol_DockingPreview] = RGB(44, 93, 135, 0.7f);
		c[ImGuiCol_DockingEmptyBg] = RGB(30, 30, 30);

		c[ImGuiCol_PlotLines] = RGB(160, 160, 160);
		c[ImGuiCol_PlotLinesHovered] = accentLight;
		c[ImGuiCol_PlotHistogram] = accent;
		c[ImGuiCol_PlotHistogramHovered] = accentLight;

		c[ImGuiCol_TableHeaderBg] = bgDark;
		c[ImGuiCol_TableBorderStrong] = border;
		c[ImGuiCol_TableBorderLight] = RGB(32, 32, 32);
		c[ImGuiCol_TableRowBg] = RGB(0, 0, 0, 0.0f);
		c[ImGuiCol_TableRowBgAlt] = RGB(255, 255, 255, 0.03f);

		c[ImGuiCol_TextSelectedBg] = RGB(44, 93, 135, 0.6f);
		c[ImGuiCol_DragDropTarget] = accentLight;
		c[ImGuiCol_NavCursor] = accentLight;
		c[ImGuiCol_NavWindowingHighlight] = RGB(255, 255, 255, 0.7f);
		c[ImGuiCol_NavWindowingDimBg] = RGB(0, 0, 0, 0.5f);
		c[ImGuiCol_ModalWindowDimBg] = RGB(0, 0, 0, 0.55f);

		// Geometry: flat, tight, slightly rounded like Unity's inspector
		style.WindowPadding = ImVec2(8, 8);
		style.FramePadding = ImVec2(6, 4);
		style.CellPadding = ImVec2(6, 3);
		style.ItemSpacing = ImVec2(8, 5);
		style.ItemInnerSpacing = ImVec2(6, 4);
		style.IndentSpacing = 16.0f;
		style.ScrollbarSize = 12.0f;
		style.GrabMinSize = 10.0f;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize = 1.0f;
		style.PopupBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.TabBorderSize = 0.0f;
		style.TabBarBorderSize = 1.0f;

		style.WindowRounding = 0.0f;   // keep docked windows square
		style.ChildRounding = 0.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 3.0f;
		style.ScrollbarRounding = 6.0f;
		style.GrabRounding = 2.0f;
		style.TabRounding = 2.0f;

		style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
		style.WindowMenuButtonPosition = ImGuiDir_None; // hide the collapse arrow
		style.SeparatorTextBorderSize = 1.0f;

		// With multi-viewports, keep platform windows looking identical to in-app ones
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

} // namespace GuiTheme
