#define NOMINMAX
#include <Windows.h>
#include <glad/glad.h>
#include "blueprint_viewport.hpp"
#include "fbx_inflate.hpp"
#include "stb_image.h"
#include "../imgui/imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace BlueprintViewport {
	namespace {
		constexpr float pi = 3.14159265358979323846f;
		struct V3 { float x{}, y{}, z{}; };
		V3 operator+(V3 a, V3 b) { return { a.x + b.x,a.y + b.y,a.z + b.z }; }
		V3 operator-(V3 a, V3 b) { return { a.x - b.x,a.y - b.y,a.z - b.z }; }
		V3 operator*(V3 a, float b) { return { a.x * b,a.y * b,a.z * b }; }
		float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y,a.z * b.x - a.x * b.z,a.x * b.y - a.y * b.x }; }
		V3 norm(V3 a) { float n = std::sqrt(dot(a, a)); return n > 1e-7f ? a * (1.f / n) : V3{ 0,1,0 }; }

		struct Mesh { std::vector<V3> vertices; std::vector<V3> normals; std::vector<std::array<float, 2>> uvs; std::vector<uint32_t> indices; V3 boundsMin{}, boundsMax{}; bool hasBounds{}; bool loaded{}; std::string detail; };
		struct Entry { std::vector<std::filesystem::path> models; std::filesystem::path diffuseTexture; std::string name; };
		struct State {
			std::unordered_map<std::string, Entry> catalog;
			std::unordered_set<std::string> blockShapes;
			std::unordered_set<std::string> footprintAnchoredParts;
			std::unordered_map<std::string, V3> shapeSizes;
			std::unordered_map<std::string, std::string> blockColors;
			std::unordered_map<std::string, std::filesystem::path> shapeDiffuseTextures;
			std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
			std::unordered_map<std::wstring, std::shared_ptr<Mesh>> meshFiles;
			std::unordered_map<std::wstring, GLuint> textures;
			size_t meshCacheBytes{};
			size_t textureCacheBytes{};
			std::filesystem::path game;
			std::filesystem::path currentBlueprint;
			int selection = -1;
			std::unordered_set<int> selectionSet;
			std::vector<int> dragSelection;
			float yaw = 0.72f, pitch = 0.42f, zoom = 1.f;
			ImVec2 pan{ 0,0 };
			int dragAxis = 0;
			ImVec2 lastDragMouse{};
			ImVec2 dragScreenAxis{};
			float dragWorldPerPixel{};
			float dragRemainder{};
			bool catalogLoaded = false;
			bool catalogAvailable = false;
			std::string error;
			GLuint sceneProgram{};
			GLuint sceneVao{};
			GLuint sceneVbo{};
			GLuint sceneFramebuffer{};
			GLuint sceneColorTexture{};
			GLuint sceneDepthBuffer{};
			int sceneTargetWidth{};
			int sceneTargetHeight{};
			std::string renderError;
		} state;

		struct SceneVertex { float x, y, z, r, g, b, u, v; };
		struct SceneBatch { GLuint texture{}; std::vector<SceneVertex> vertices; };

		struct GlStateSnapshot {
			GLint drawFramebuffer{}, readFramebuffer{}, viewport[4]{}, program{}, vertexArray{}, arrayBuffer{}, renderbuffer{};
			GLint activeTexture{}, activeTextureBinding{}, texture0Binding{}, depthFunction{}, scissorBox[4]{};
			GLboolean depthTest{}, depthWrite{}, scissorTest{}, blend{}, cullFace{}, colorWrite[4]{};
			GLfloat clearColor[4]{};
		};

		GlStateSnapshot captureGlState() {
			GlStateSnapshot saved;
			glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saved.drawFramebuffer);
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &saved.readFramebuffer);
			glGetIntegerv(GL_VIEWPORT, saved.viewport);
			glGetIntegerv(GL_CURRENT_PROGRAM, &saved.program);
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved.vertexArray);
			glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &saved.arrayBuffer);
			glGetIntegerv(GL_RENDERBUFFER_BINDING, &saved.renderbuffer);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &saved.activeTexture);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved.activeTextureBinding);
			glActiveTexture(GL_TEXTURE0);
			glGetIntegerv(GL_TEXTURE_BINDING_2D, &saved.texture0Binding);
			if (saved.activeTexture != GL_TEXTURE0)glActiveTexture((GLenum)saved.activeTexture);
			glGetIntegerv(GL_DEPTH_FUNC, &saved.depthFunction);
			glGetIntegerv(GL_SCISSOR_BOX, saved.scissorBox);
			saved.depthTest = glIsEnabled(GL_DEPTH_TEST); glGetBooleanv(GL_DEPTH_WRITEMASK, &saved.depthWrite);
			saved.scissorTest = glIsEnabled(GL_SCISSOR_TEST); saved.blend = glIsEnabled(GL_BLEND); saved.cullFace = glIsEnabled(GL_CULL_FACE);
			glGetBooleanv(GL_COLOR_WRITEMASK, saved.colorWrite); glGetFloatv(GL_COLOR_CLEAR_VALUE, saved.clearColor);
			return saved;
		}

		void restoreGlState(const GlStateSnapshot& saved) {
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)saved.drawFramebuffer);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved.readFramebuffer);
			glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
			glUseProgram((GLuint)saved.program);
			glBindVertexArray((GLuint)saved.vertexArray);
			glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved.arrayBuffer);
			glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)saved.renderbuffer);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, (GLuint)saved.texture0Binding);
			if (saved.activeTexture != GL_TEXTURE0) { glActiveTexture((GLenum)saved.activeTexture); glBindTexture(GL_TEXTURE_2D, (GLuint)saved.activeTextureBinding); }
			glDepthFunc((GLenum)saved.depthFunction); glDepthMask(saved.depthWrite);
			glScissor(saved.scissorBox[0], saved.scissorBox[1], saved.scissorBox[2], saved.scissorBox[3]);
			glColorMask(saved.colorWrite[0], saved.colorWrite[1], saved.colorWrite[2], saved.colorWrite[3]);
			glClearColor(saved.clearColor[0], saved.clearColor[1], saved.clearColor[2], saved.clearColor[3]);
			if (saved.depthTest)glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
			if (saved.scissorTest)glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
			if (saved.blend)glEnable(GL_BLEND); else glDisable(GL_BLEND);
			if (saved.cullFace)glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
		}

		GLuint compileSceneShader(GLenum type, const char* source, std::string& error) {
			GLuint shader = glCreateShader(type);
			glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
			GLint compiled = GL_FALSE; glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
			if (!compiled) { GLint length = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length); std::string log((size_t)std::max(1, length), '\0'); glGetShaderInfoLog(shader, length, nullptr, log.data()); error = "Could not compile scene shader: " + log; glDeleteShader(shader); return 0; }
			return shader;
		}

		bool ensureSceneRenderer() {
			if (state.sceneProgram && state.sceneVao && state.sceneVbo)return true;
			static constexpr char vertexSource[] = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
layout(location=2) in vec2 aUv;
out vec3 vColor;
out vec2 vUv;
void main(){gl_Position=vec4(aPosition,1.0);vColor=aColor;vUv=aUv;}
)GLSL";
			static constexpr char fragmentSource[] = R"GLSL(#version 330 core
in vec3 vColor;
in vec2 vUv;
uniform sampler2D uDiffuse;
uniform bool uTextured;
out vec4 outColor;
void main(){vec4 texel=uTextured?texture(uDiffuse,vUv):vec4(1.0);outColor=vec4(vColor*texel.rgb,1.0);}
)GLSL";
			std::string error;
			GLuint vertex = compileSceneShader(GL_VERTEX_SHADER, vertexSource, error);
			if (!vertex) { state.renderError = error; return false; }
			GLuint fragment = compileSceneShader(GL_FRAGMENT_SHADER, fragmentSource, error);
			if (!fragment) { glDeleteShader(vertex); state.renderError = error; return false; }
			GLuint program = glCreateProgram(); glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
			glDeleteShader(vertex); glDeleteShader(fragment);
			GLint linked = GL_FALSE; glGetProgramiv(program, GL_LINK_STATUS, &linked);
			if (!linked) { GLint length = 0; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length); std::string log((size_t)std::max(1, length), '\0'); glGetProgramInfoLog(program, length, nullptr, log.data()); glDeleteProgram(program); state.renderError = "Could not link scene shader: " + log; return false; }
			GLint oldVao = 0, oldBuffer = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
			GLuint vao = 0, vbo = 0; glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), (void*)offsetof(SceneVertex, x));
			glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), (void*)offsetof(SceneVertex, r));
			glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SceneVertex), (void*)offsetof(SceneVertex, u));
			glBindVertexArray((GLuint)oldVao); glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldBuffer);
			if (!vao || !vbo) { if (vao)glDeleteVertexArrays(1, &vao); if (vbo)glDeleteBuffers(1, &vbo); glDeleteProgram(program); state.renderError = "Could not allocate GPU buffers for the blueprint viewer."; return false; }
			state.sceneProgram = program; state.sceneVao = vao; state.sceneVbo = vbo; state.renderError.clear();
			return true;
		}

		bool ensureSceneTarget(int requestedWidth, int requestedHeight) {
			GLint maxTexture = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
			if (maxTexture <= 0) { state.renderError = "The graphics driver reported an invalid maximum texture size."; return false; }
			const float scale = std::min({ 1.f,(float)maxTexture / (float)requestedWidth,(float)maxTexture / (float)requestedHeight,
				std::sqrt(4194304.f / (float)((int64_t)requestedWidth * requestedHeight)) });
			const int width = std::max(1, (int)std::floor(requestedWidth * scale));
			const int height = std::max(1, (int)std::floor(requestedHeight * scale));
			if (state.sceneFramebuffer && state.sceneColorTexture && state.sceneDepthBuffer && width == state.sceneTargetWidth && height == state.sceneTargetHeight)return true;
			if (!state.sceneFramebuffer)glGenFramebuffers(1, &state.sceneFramebuffer);
			if (!state.sceneColorTexture)glGenTextures(1, &state.sceneColorTexture);
			if (!state.sceneDepthBuffer)glGenRenderbuffers(1, &state.sceneDepthBuffer);
			if (!state.sceneFramebuffer || !state.sceneColorTexture || !state.sceneDepthBuffer) { state.renderError = "Could not allocate the blueprint viewer render target."; return false; }
			glBindFramebuffer(GL_FRAMEBUFFER, state.sceneFramebuffer);
			glBindTexture(GL_TEXTURE_2D, state.sceneColorTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state.sceneColorTexture, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, state.sceneDepthBuffer); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, state.sceneDepthBuffer);
			const GLenum drawBuffer = GL_COLOR_ATTACHMENT0; glDrawBuffers(1, &drawBuffer);
			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			if (status != GL_FRAMEBUFFER_COMPLETE) { char message[96]; snprintf(message, sizeof(message), "Blueprint depth buffer is incomplete (OpenGL status 0x%04X).", (unsigned)status); state.renderError = message; return false; }
			state.sceneTargetWidth = width; state.sceneTargetHeight = height; state.renderError.clear(); return true;
		}

		bool drawDepthBufferedScene(const std::vector<SceneBatch>& batches, ImVec2 origin, ImVec2 size, ImDrawList* drawList) {
			if (batches.empty())return false;
			while (glGetError() != GL_NO_ERROR) {}
			const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
			const int width = std::max(1, (int)std::ceil(size.x * std::max(.25f, framebufferScale.x)));
			const int height = std::max(1, (int)std::ceil(size.y * std::max(.25f, framebufferScale.y)));
			const GlStateSnapshot saved = captureGlState();
			bool success = false;
			if (ensureSceneRenderer() && ensureSceneTarget(width, height)) {
				glBindFramebuffer(GL_FRAMEBUFFER, state.sceneFramebuffer); glViewport(0, 0, state.sceneTargetWidth, state.sceneTargetHeight);
				glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
				glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glClearColor(0.f, 0.f, 0.f, 0.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				glUseProgram(state.sceneProgram); glBindVertexArray(state.sceneVao); glBindBuffer(GL_ARRAY_BUFFER, state.sceneVbo);
				glActiveTexture(GL_TEXTURE0);
				glUniform1i(glGetUniformLocation(state.sceneProgram, "uDiffuse"), 0);
				for (const auto& batch : batches) {
					if (batch.vertices.empty())continue;
					glUniform1i(glGetUniformLocation(state.sceneProgram, "uTextured"), batch.texture ? GL_TRUE : GL_FALSE);
					glBindTexture(GL_TEXTURE_2D, batch.texture);
					glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(batch.vertices.size() * sizeof(SceneVertex)), batch.vertices.data(), GL_STREAM_DRAW);
					glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.vertices.size());
				}
				GLenum drawError = GL_NO_ERROR;
				while (glGetError() != GL_NO_ERROR)drawError = GL_INVALID_OPERATION;
				success = drawError == GL_NO_ERROR;
				if (!success)state.renderError = "OpenGL failed while drawing the depth-buffered blueprint scene.";
			}
			restoreGlState(saved);
			if (success) {
				const ImTextureID texture = (ImTextureID)(intptr_t)state.sceneColorTexture;
				drawList->AddImage(texture, origin, ImVec2(origin.x + size.x, origin.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
			}
			return success;
		}

		V3 orient(V3 v, const Parser::Block& b);
		void buildNormals(Mesh& mesh);
		V3 shapeSize(const Parser::Block& b);
		std::string lower(std::string s);
		V3 blockCenter(const Parser::Block& b) {
			const V3 size = shapeSize(b);
			const auto placement = placementTransform(b, { size.x, size.y, size.z });
			return { placement.translation.x, placement.translation.y, placement.translation.z };
		}

		std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return (char)std::tolower(c); }); return s; }
		bool readFile(const std::filesystem::path& p, json& out) {
			std::ifstream f(p); if (!f)return false;
			try { f >> out; return out.is_object(); }
			catch (...) { return false; }
		}
		std::filesystem::path steamRoot() {
			wchar_t steam[1024]{}; DWORD n = (DWORD)std::size(steam);
			HKEY key{};
			if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
				n = (DWORD)std::size(steam); RegQueryValueExW(key, L"SteamPath", nullptr, nullptr, (LPBYTE)steam, &n); RegCloseKey(key);
				if (n) { auto p = std::filesystem::path(steam); if (std::filesystem::exists(p / L"steamapps")) return p; }
			}
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
				n = (DWORD)std::size(steam); if (RegQueryValueExW(key, L"InstallPath", nullptr, nullptr, (LPBYTE)steam, &n) == ERROR_SUCCESS) {
					RegCloseKey(key); auto p = std::filesystem::path(steam); if (std::filesystem::exists(p / L"steamapps")) return p;
				}
				else RegCloseKey(key);
			}
			wchar_t* pf = nullptr; size_t pfLength = 0;
			if (_wdupenv_s(&pf, &pfLength, L"PROGRAMFILES(X86)") == 0 && pf) {
				auto p = std::filesystem::path(pf) / L"Steam"; free(pf);
				if (std::filesystem::exists(p / L"steamapps")) return p;
			}
			return {};
		}

		std::filesystem::path resolveGameAsset(const std::string& asset) {
			constexpr std::string_view gameToken = "$GAME_DATA/";
			constexpr std::string_view survivalToken = "$SURVIVAL_DATA/";
			if (asset.rfind(gameToken, 0) == 0)return state.game / L"Data" / std::filesystem::path(asset.substr(gameToken.size()));
			if (asset.rfind(survivalToken, 0) == 0)return state.game / L"Survival" / std::filesystem::path(asset.substr(survivalToken.size()));
			return {};
		}

		void loadCatalog() {
			if (state.catalogLoaded)return; state.catalogLoaded = true;
			auto steam = steamRoot();
			if (steam.empty()) { state.error = "Steam installation not found."; return; }
			auto libraries = steam / L"steamapps" / L"libraryfolders.vdf";
			std::vector<std::filesystem::path> roots{ steam };
			std::ifstream lib(libraries); std::string line;
			const std::regex pathPattern(R"rx("path"\s+"([^"]+)")rx");
			while (std::getline(lib, line)) { std::smatch m; if (std::regex_search(line, m, pathPattern)) { std::string v = m[1].str(); std::replace(v.begin(), v.end(), '\\', '/'); auto p = std::filesystem::path(v); if (std::find(roots.begin(), roots.end(), p) == roots.end())roots.push_back(p); } }
			for (const auto& root : roots) {
				auto game = root / L"steamapps" / L"common" / L"Scrap Mechanic";
				const std::array<std::filesystem::path, 2> databases = {
					game / L"Data" / L"Objects" / L"Database" / L"ShapeSets",
					game / L"Survival" / L"Objects" / L"Database" / L"ShapeSets"
				};
				if (!std::filesystem::exists(databases[0]) && !std::filesystem::exists(databases[1]))continue;
				state.game = game;
				for (size_t databaseIndex = 0; databaseIndex < databases.size(); databaseIndex++) {
					const auto& db = databases[databaseIndex];
					if (!std::filesystem::exists(db))continue;
					std::error_code ec;
					for (auto it = std::filesystem::directory_iterator(db, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
						if (!it->is_regular_file() || lower(it->path().extension().string()) != ".shapeset")continue;
						json set; if (!readFile(it->path(), set))continue;
						if (set.contains("blockList") && set["blockList"].is_array())for (const auto& block : set["blockList"]) {
							if (!block.is_object() || !block.contains("uuid") || !block["uuid"].is_string())continue;
							const auto id = lower(block["uuid"].get<std::string>()); state.blockShapes.insert(id);
							if (block.contains("color") && block["color"].is_string())state.blockColors[id] = block["color"].get<std::string>();
						}
						if (!set.contains("partList") || !set["partList"].is_array())continue;
						for (const auto& part : set["partList"]) {
							if (!part.is_object() || !part.contains("uuid") || !part["uuid"].is_string())continue;
							const auto id = lower(part["uuid"].get<std::string>());
							// Register renderable parts from both game databases alike. The
							// editor is a blueprint visualizer; Creative/Survival eligibility
							// must never hide a part's installed game model.
							// Parts without a resizable `box` still publish their
							// placement footprint as `hull`. Using a unit fallback for
							// those objects made assets such as the toilet render at
							// one grid cell even though the game reserves a 3x4x3 hull.
							const json* footprint = nullptr;
							if (part.contains("box") && part["box"].is_object())footprint = &part["box"];
							else if (part.contains("hull") && part["hull"].is_object())footprint = &part["hull"];
							if (footprint) {
								state.footprintAnchoredParts.insert(id);
								auto readSize = [&](const char* axis) {
									if (!footprint->contains(axis) || !(*footprint)[axis].is_number())return 1.f;
									return std::clamp((*footprint)[axis].get<float>(), 1.f, 256.f);
									};
								state.shapeSizes[id] = { readSize("x"),readSize("y"),readSize("z") };
							}
							else if (part.contains("cylinder") && part["cylinder"].is_object()) {
								const auto& cylinder = part["cylinder"];
								auto readCylinderSize = [&](const char* key) {
									if (!cylinder.contains(key) || !cylinder[key].is_number())return 1.f;
									return std::clamp(cylinder[key].get<float>(), 1.f, 256.f);
									};
								const float diameter = readCylinderSize("diameter"), depth = readCylinderSize("depth");
								V3 dimensions{ diameter,diameter,diameter };
								const std::string cylinderAxis = cylinder.contains("axis") && cylinder["axis"].is_string() ? lower(cylinder["axis"].get<std::string>()) : "z";
								if (cylinderAxis == "x")dimensions.x = depth;
								else if (cylinderAxis == "y")dimensions.y = depth;
								else dimensions.z = depth;
								state.shapeSizes[id] = dimensions;
							}
							if (!part.contains("renderable"))continue;
							const json* rend = &part["renderable"];
							json externalRenderable;
							if (rend->is_string()) {
								std::string renderable = rend->get<std::string>();
								auto path = resolveGameAsset(renderable);
								if (path.empty())continue;
								if (!readFile(path, externalRenderable))continue;
								rend = &externalRenderable;
							}
							if (!rend->is_object() || !rend->contains("lodList") || !(*rend)["lodList"].is_array() || (*rend)["lodList"].empty())continue;
							Entry entry;
							if (part.contains("name") && part["name"].is_string())entry.name = part["name"].get<std::string>();
							// Renderables describe diffuse images in either subMeshMap (newer
							// interactive assets) or subMeshList (vehicle and legacy assets).
							for (const auto& lod : (*rend)["lodList"]) {
								if (!lod.is_object())continue;
								auto readDiffuse = [&](const json& material) {
									std::string texture;
									if (material.is_object() && material.contains("textures") && material["textures"].is_object() && material["textures"].contains("diffuse") && material["textures"]["diffuse"].is_string())texture = material["textures"]["diffuse"].get<std::string>();
									else if (material.is_object() && material.contains("textureList") && material["textureList"].is_array() && !material["textureList"].empty() && material["textureList"][0].is_string())texture = material["textureList"][0].get<std::string>();
									if (!texture.empty())entry.diffuseTexture = resolveGameAsset(texture);
								};
								if (lod.contains("subMeshList") && lod["subMeshList"].is_array())for (const auto& material : lod["subMeshList"]) { readDiffuse(material); if (!entry.diffuseTexture.empty())break; }
								if (entry.diffuseTexture.empty() && lod.contains("subMeshMap") && lod["subMeshMap"].is_object())for (auto it = lod["subMeshMap"].begin(); it != lod["subMeshMap"].end(); ++it) {
									readDiffuse(it.value());
									if (!entry.diffuseTexture.empty())break;
								}
								if (!entry.diffuseTexture.empty())break;
							}
							const bool suspensionPart = lower(entry.name).find("suspension") != std::string::npos;
							// Try the highest-quality mesh first, then fall through to the
							// shipped LOD meshes if an asset has a variant this importer cannot
							// decode. Suspensions have an animated pose0 that exposes the spring;
							// the static `mesh` is the retracted/off pose, so prefer pose0 there.
							for (const auto& lod : (*rend)["lodList"]) {
								if (!lod.is_object())continue;
								auto addModel = [&](const char* key) {
									if (!lod.contains(key) || !lod[key].is_string())return;
									auto model = resolveGameAsset(lod[key].get<std::string>());
									if (model.empty() || !std::filesystem::is_regular_file(model))return;
									if (std::find(entry.models.begin(), entry.models.end(), model) == entry.models.end())entry.models.push_back(std::move(model));
								};
								if (suspensionPart)addModel("pose0");
								addModel("mesh");
								if (!suspensionPart)addModel("pose0");
							}
							if (entry.models.empty())continue;
							if (!entry.diffuseTexture.empty())state.shapeDiffuseTextures[id] = entry.diffuseTexture;
							state.catalog[id] = std::move(entry);
						}
					}
				}
				if (!state.catalog.empty() || !state.blockShapes.empty()) { state.catalogAvailable = true; break; }
			}
			if (!state.catalogAvailable)state.error = "Scrap Mechanic shape database was not found in the Steam library.";
		}

		struct FProp { char type{}; std::string text; std::vector<double> arr; };
		struct FNode { std::string name; std::vector<FProp> props; std::vector<FNode> children; };
		struct BinFbx {
			const std::vector<uint8_t>& b; size_t pos = 27; bool wide{}; size_t nodes = 0; size_t properties = 0; size_t arrayElements = 0; std::string error;
			template<class T> bool take(T& v) { if (pos + sizeof(T) > b.size())return false; std::memcpy(&v, b.data() + pos, sizeof(T)); pos += sizeof(T); return true; }
			bool bytes(size_t n, const uint8_t*& p) { if (n > b.size() - pos)return false; p = b.data() + pos; pos += n; return true; }
			bool str(std::string& s, size_t n) { const uint8_t* p; if (!bytes(n, p))return false; s.assign((const char*)p, n); return true; }
			bool prop(FProp& p) {
				if (pos >= b.size()) { error = "Unexpected end of file before property"; return false; }p.type = (char)b[pos++];
				if (p.type == 'S' || p.type == 'R') { uint32_t n; if (!take(n) || n > b.size() - pos)return false; if (p.type == 'S')return str(p.text, n); pos += n; return true; }
				if (p.type == 'Y') { int16_t v; if (!take(v))return false; p.arr.push_back(v); return true; }
				if (p.type == 'C') { uint8_t v; if (!take(v))return false; p.arr.push_back(v); return true; }
				if (p.type == 'I') { int32_t v; if (!take(v))return false; p.arr.push_back(v); return true; }
				if (p.type == 'F') { float v; if (!take(v))return false; p.arr.push_back(v); return true; }
				if (p.type == 'D') { double v; if (!take(v))return false; p.arr.push_back(v); return true; }
				if (p.type == 'L') { int64_t v; if (!take(v))return false; p.arr.push_back((double)v); return true; }
        size_t el = 0; switch (p.type) { case 'f':case 'i':el = 4; break; case 'd':case 'l':el = 8; break; case 'b':case 'c':el = 1; break; default:error = std::string("Unsupported FBX property type '") + p.type + "'"; return false; }
												 constexpr size_t maximumArrayElements = 2000000;
												 constexpr size_t maximumTotalArrayElements = 8000000;
												 uint32_t count;
												 if (!take(count) || count > maximumArrayElements || count > maximumTotalArrayElements - arrayElements) { error = "FBX array exceeds parser element limits"; return false; }
												 uint32_t encoding, length; if (!take(encoding) || !take(length) || length > b.size() - pos) { error = "Invalid FBX array header"; return false; }
												 const size_t decodedBytes = (size_t)count * el;
												 if (decodedBytes > 16ull * 1024ull * 1024ull) { error = "FBX array exceeds the 16 MiB decoded-array limit"; return false; }
												 const uint8_t* data; if (!bytes(length, data)) { error = "Truncated FBX array"; return false; }
												 std::vector<uint8_t> inflated;
												 if (encoding == 1) { inflated.resize(decodedBytes); if (!FbxInflate::decode(data, length, inflated.data(), decodedBytes)) { error = "FBX array decompression or checksum failed"; return false; }data = inflated.data(); }
												 else if (encoding != 0 || decodedBytes != length) { error = "Invalid uncompressed FBX array"; return false; }
												 arrayElements += count;
												 p.arr.reserve(count); for (uint32_t i = 0; i < count; i++) {
													 if (el == 8) { if (p.type == 'd') { double x; std::memcpy(&x, data + i * el, el); p.arr.push_back(x); } else { int64_t x; std::memcpy(&x, data + i * el, el); p.arr.push_back((double)x); } }
													 else if (el == 4) { if (p.type == 'f') { float x; std::memcpy(&x, data + i * el, el); p.arr.push_back(x); } else { int32_t x; std::memcpy(&x, data + i * el, el); p.arr.push_back(x); } }
													 else p.arr.push_back(data[i]);
												 }
												 return true;
			}
			bool node(FNode& out, int depth = 0) {
				auto fail = [&](const std::string& why) {if (error.empty())error = why + " at byte " + std::to_string(pos); return false; };
				if (depth > 64)return fail("FBX node nesting limit exceeded");
				if (++nodes > 250000)return fail("FBX node count limit exceeded");
				uint64_t end = 0, nprop = 0, plen = 0; uint8_t nameLen = 0;
				if (wide) { if (!take(end) || !take(nprop) || !take(plen) || !take(nameLen))return fail("Truncated FBX node header"); }
				else { uint32_t a, c, d; if (!take(a) || !take(c) || !take(d) || !take(nameLen))return fail("Truncated FBX node header"); end = a; nprop = c; plen = d; }
				if (end == 0)return true;
				if (end > b.size() || end<pos || nameLen>end - pos || nprop > 10000 || nprop > 250000 - properties)return fail("Invalid FBX node bounds or property count limit");
				properties += (size_t)nprop;
				if (!str(out.name, nameLen))return fail("Truncated FBX node name");
				const size_t propertyStart = pos;
				out.props.reserve((size_t)nprop); for (uint64_t i = 0; i < nprop; i++) { FProp p; if (!prop(p))return fail("Invalid FBX node property"); out.props.push_back(std::move(p)); }
				if (pos - propertyStart != plen || pos > end)return fail("FBX property list length does not match its node");
				while (pos < end) { if (wide && end - pos < 25)break; if (!wide && end - pos < 13)break; FNode c; size_t before = pos; bool parsed = node(c, depth + 1); if (!c.name.empty())out.children.push_back(std::move(c)); if (!parsed)return fail("Invalid nested FBX node"); if (pos <= before)return fail("FBX parser made no progress"); }
				pos = (size_t)end; return true;
			}
		};
		struct FbxTransform {
			V3 translation{}, rotation{}, scale{ 1.f,1.f,1.f };
			V3 rotationOffset{}, rotationPivot{}, preRotation{}, postRotation{};
			V3 scalingOffset{}, scalingPivot{};
			int rotationOrder{};
		};
		bool numericProperty(const FNode& node, size_t index, int64_t& value) {
			if (index >= node.props.size())return false;
			const auto& p = node.props[index];
			if (!p.arr.empty()) { value = (int64_t)p.arr[0]; return true; }
			if (!p.text.empty()) { try { size_t used = 0; value = std::stoll(p.text, &used); return used == p.text.size(); } catch (...) {} }
			return false;
		}
		V3 propertyVector(const FNode& node, const char* name, V3 fallback) {
			for (const auto& p : node.children) {
				if (p.name != "P" || p.props.empty() || p.props[0].text != name)continue;
				float values[3]{}; int count = 0;
				for (size_t i = 4; i < p.props.size() && count < 3; i++)if (!p.props[i].arr.empty())values[count++] = (float)p.props[i].arr[0];
				if (count == 3)return { values[0],values[1],values[2] };
			}
			return fallback;
		}
		int propertyInt(const FNode& node, const char* name, int fallback) {
			for (const auto& p : node.children)if (p.name == "P" && !p.props.empty() && p.props[0].text == name) {
				int64_t value; if (numericProperty(p, 4, value))return (int)std::clamp<int64_t>(value, 0, 5);
			}
			return fallback;
		}
		FbxTransform transformFromProperties(const FNode& node, const char* translation, const char* rotation, const char* scale) {
			const FNode* properties = nullptr; for (const auto& child : node.children)if (child.name == "Properties70") { properties = &child; break; }
			if (!properties)return {};
			FbxTransform result;
			result.translation = propertyVector(*properties, translation, {});
			result.rotation = propertyVector(*properties, rotation, {});
			result.scale = propertyVector(*properties, scale, { 1.f,1.f,1.f });
			result.rotationOffset = propertyVector(*properties, "RotationOffset", {});
			result.rotationPivot = propertyVector(*properties, "RotationPivot", {});
			result.preRotation = propertyVector(*properties, "PreRotation", {});
			result.postRotation = propertyVector(*properties, "PostRotation", {});
			result.scalingOffset = propertyVector(*properties, "ScalingOffset", {});
			result.scalingPivot = propertyVector(*properties, "ScalingPivot", {});
			result.rotationOrder = propertyInt(*properties, "RotationOrder", 0);
			return result;
		}
		V3 rotateAxis(V3 p, int a, float angle) {
			const float c = std::cos(angle), s = std::sin(angle);
			if (a == 0)return { p.x,c * p.y - s * p.z,s * p.y + c * p.z };
			if (a == 1)return { c * p.x + s * p.z,p.y,-s * p.x + c * p.z };
			return { c * p.x - s * p.y,s * p.x + c * p.y,p.z };
		}
		V3 rotateEuler(V3 p, V3 degrees, int order, bool inverse = false) {
			static constexpr int sequences[6][3] = { {0,1,2},{0,2,1},{1,2,0},{1,0,2},{2,0,1},{2,1,0} };
			const float angles[3] = { degrees.x * pi / 180.f,degrees.y * pi / 180.f,degrees.z * pi / 180.f };
			const int* sequence = sequences[std::clamp(order, 0, 5)];
			for (int i = 0; i < 3; i++) { const int axisIndex = sequence[inverse ? 2 - i : i]; p = rotateAxis(p, axisIndex, angles[axisIndex] * (inverse ? -1.f : 1.f)); }
			return p;
		}
		V3 applyFbxTransform(V3 p, const FbxTransform& t) {
			p = p - t.scalingPivot;
			p = { p.x * t.scale.x,p.y * t.scale.y,p.z * t.scale.z };
			p = p + t.scalingPivot + t.scalingOffset;
			p = p - t.rotationPivot;
			p = rotateEuler(p, t.postRotation, t.rotationOrder, true);
			p = rotateEuler(p, t.rotation, t.rotationOrder);
			p = rotateEuler(p, t.preRotation, t.rotationOrder);
			return p + t.rotationPivot + t.rotationOffset + t.translation;
		}
		const FNode* childNamed(const FNode& node, const char* name) {
			for (const auto& child : node.children)if (child.name == name)return &child;
			return nullptr;
		}
		bool geometryData(const FNode& node, Mesh& mesh, const std::vector<FbxTransform>& modelChain) {
			const FProp* verts = nullptr; const FProp* faces = nullptr;
			for (const auto& c : node.children) { if (c.name == "Vertices" && !c.props.empty())verts = &c.props[0]; if (c.name == "PolygonVertexIndex" && !c.props.empty())faces = &c.props[0]; }
			if (!verts || !faces || verts->arr.size() < 9 || faces->arr.size() < 3 || verts->arr.size() % 3)return false;
			const FProp* uvValues = nullptr; const FProp* uvIndices = nullptr;
			std::string uvMapping, uvReference;
			if (const FNode* layer = childNamed(node, "LayerElementUV"))for (const auto& c : layer->children) {
				if (c.name == "UV" && !c.props.empty())uvValues = &c.props[0];
				else if (c.name == "UVIndex" && !c.props.empty())uvIndices = &c.props[0];
				else if (c.name == "MappingInformationType" && !c.props.empty())uvMapping = c.props[0].text;
				else if (c.name == "ReferenceInformationType" && !c.props.empty())uvReference = c.props[0].text;
			}
			const FbxTransform geometric = transformFromProperties(node, "GeometricTranslation", "GeometricRotation", "GeometricScaling");
			auto transformedPoint = [&](uint32_t index) {
				V3 point{ (float)verts->arr[index * 3],(float)verts->arr[index * 3 + 1],(float)verts->arr[index * 3 + 2] };
				point = applyFbxTransform(point, geometric);
				for (const auto& transform : modelChain)point = applyFbxTransform(point, transform);
				return point;
			};
			auto uvFor = [&](size_t polygonVertex, uint32_t vertexIndex) {
				std::array<float, 2> uv{};
				if (!uvValues || uvValues->arr.size() < 2)return uv;
				int64_t direct = -1;
				if (uvReference == "IndexToDirect" && uvIndices && polygonVertex < uvIndices->arr.size())direct = (int64_t)uvIndices->arr[polygonVertex];
				else direct = uvMapping == "ByVertice" || uvMapping == "ByVertex" ? vertexIndex : (int64_t)polygonVertex;
				if (direct >= 0 && (size_t)direct * 2 + 1 < uvValues->arr.size()) {
					uv[0] = (float)uvValues->arr[(size_t)direct * 2];
					uv[1] = 1.f - (float)uvValues->arr[(size_t)direct * 2 + 1];
				}
				return uv;
			};
			std::vector<std::pair<uint32_t, std::array<float, 2>>> poly;
			size_t polygonVertex = 0;
			for (double d : faces->arr) {
				const int64_t raw = (int64_t)d; const bool end = raw < 0;
				const uint64_t ix = (uint64_t)(end ? -raw - 1 : raw);
				if (ix >= verts->arr.size() / 3) { poly.clear(); ++polygonVertex; continue; }
				poly.emplace_back((uint32_t)ix, uvFor(polygonVertex++, (uint32_t)ix));
				if (end) {
					std::vector<uint32_t> polygonIndices; polygonIndices.reserve(poly.size());
					for (const auto& [vertexIndex, uv] : poly) { polygonIndices.push_back((uint32_t)mesh.vertices.size()); mesh.vertices.push_back(transformedPoint(vertexIndex)); mesh.uvs.push_back(uv); }
					for (size_t k = 1; k + 1 < polygonIndices.size(); k++) { mesh.indices.push_back(polygonIndices[0]); mesh.indices.push_back(polygonIndices[k]); mesh.indices.push_back(polygonIndices[k + 1]); }
					poly.clear();
				}
			}
			return true;
		}
		void loadFbxScene(const std::vector<FNode>& roots, Mesh& mesh) {
			struct Link { int64_t child{}, parent{}; };
			std::unordered_map<int64_t, const FNode*> geometryById, modelById;
			std::unordered_map<int64_t, int64_t> geometryParent, modelParent;
			std::vector<Link> links;
			auto visit = [&](auto&& self, const FNode& node)->void {
				if (node.name == "Geometry" || node.name == "Model") {
					int64_t id; if (numericProperty(node, 0, id))(node.name == "Geometry" ? geometryById : modelById)[id] = &node;
				}
				if (node.name == "C" && node.props.size() >= 3) { int64_t child, parent; if (numericProperty(node, 1, child) && numericProperty(node, 2, parent))links.push_back({ child,parent }); }
				for (const auto& child : node.children)self(self, child);
				};
			for (const auto& root : roots)visit(visit, root);
			for (const auto& link : links) {
				if (geometryById.contains(link.child) && modelById.contains(link.parent))geometryParent[link.child] = link.parent;
				if (modelById.contains(link.child) && modelById.contains(link.parent))modelParent[link.child] = link.parent;
			}
			for (const auto& [id, geometry] : geometryById) {
				std::vector<FbxTransform> chain;
				std::unordered_map<int64_t, bool> seen;
				auto parent = geometryParent.find(id);
				while (parent != geometryParent.end() && modelById.contains(parent->second) && !seen.contains(parent->second)) {
					const int64_t modelId = parent->second; seen[modelId] = true;
					chain.push_back(transformFromProperties(*modelById[modelId], "Lcl Translation", "Lcl Rotation", "Lcl Scaling"));
					parent = modelParent.find(modelId);
				}
				geometryData(*geometry, mesh, chain);
			}
		}
		bool loadFbx(const std::filesystem::path& path, Mesh& mesh) {
			std::error_code sizeError;
			const auto fileSize = std::filesystem::file_size(path, sizeError);
			constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
			if (!sizeError && fileSize > maximumModelFileBytes) { mesh.detail = "FBX exceeds the 32 MiB viewer safety limit"; return false; }
			std::ifstream f(path, std::ios::binary); if (!f) { mesh.detail = "FBX file could not be opened"; return false; }
			std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)), {}); if (b.size() < 32) { mesh.detail = "FBX file is too short"; return false; }
			const char magic[] = "Kaydara FBX Binary  ";
			if (b.size() >= 27 && std::memcmp(b.data(), magic, sizeof(magic) - 1) == 0) {
				uint32_t version = 0; std::memcpy(&version, b.data() + 23, 4); BinFbx p{ b }; p.pos = 27; p.wide = version >= 7500;
				bool parseFailed = false; std::vector<FNode> roots;
				while (p.pos + (p.wide ? 25 : 13) < b.size()) { FNode n; size_t before = p.pos; bool parsed = p.node(n); if (!n.name.empty())roots.push_back(std::move(n)); if (!parsed || p.pos <= before) { parseFailed = true; break; } }
				loadFbxScene(roots, mesh);
				mesh.loaded = !mesh.indices.empty(); if (!mesh.loaded)mesh.detail = p.error.empty() ? (parseFailed ? "FBX node parse stopped at byte " + std::to_string(p.pos) : "No triangulatable Geometry node found") : p.error;
				return mesh.loaded;
			}
			// ASCII FBX exports are useful with modded assets. The stock game's binary
			// FBX meshes take the binary path above.
			std::string s((const char*)b.data(), b.size());
			std::regex vr(R"(Vertices\s*:\s*\*\s*\d+\s*\{\s*a\s*:\s*([^}]*)\})");
			std::regex fr(R"(PolygonVertexIndex\s*:\s*\*\s*\d+\s*\{\s*a\s*:\s*([^}]*)\})");
			std::smatch vm, fm; if (!std::regex_search(s, vm, vr) || !std::regex_search(s, fm, fr)) { mesh.detail = "ASCII FBX has no Vertices/PolygonVertexIndex arrays"; return false; }
			auto nums = [](std::string t) {std::vector<double> v; const std::regex r(R"(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)"); for (std::sregex_iterator i(t.begin(), t.end(), r), e; i != e; ++i) { try { v.push_back(std::stod(i->str())); } catch (...) {} }return v; };
			auto v = nums(vm[1].str()), idx = nums(fm[1].str()); if (v.size() < 9 || v.size() % 3)return false;
			for (size_t i = 0; i < v.size(); i += 3)mesh.vertices.push_back({ (float)v[i],(float)v[i + 1],(float)v[i + 2] });
			std::vector<uint32_t>poly; for (double d : idx) { auto r = (int64_t)d; bool end = r < 0; uint64_t ix = (uint64_t)(end ? -r - 1 : r); if (ix >= mesh.vertices.size()) { poly.clear(); continue; }poly.push_back((uint32_t)ix); if (end) { for (size_t k = 1; k + 1 < poly.size(); k++) { mesh.indices.insert(mesh.indices.end(), { poly[0],poly[k],poly[k + 1] }); }poly.clear(); } }
			mesh.loaded = !mesh.indices.empty(); if (!mesh.loaded)mesh.detail = "ASCII FBX contains no triangulatable polygons"; return mesh.loaded;
		}
		std::string xmlAttribute(const std::string& attributes, const char* name) {
			const std::regex pattern(std::string(R"(\b)") + name + R"(\s*=\s*["']([^"']*)["'])");
			std::smatch match;
			return std::regex_search(attributes, match, pattern) ? match[1].str() : std::string{};
		}
		std::string xmlElementText(const std::string& source, const char* tag) {
			const std::regex pattern(std::string("<") + tag + R"(\b[^>]*>([\s\S]*?)</)" + tag + R"(\s*>)");
			std::smatch match;
			return std::regex_search(source, match, pattern) ? match[1].str() : std::string{};
		}
		bool parseXmlNumbers(const std::string& text, std::vector<double>& values, size_t maximumValues) {
			const char* p = text.c_str();
			const char* end = p + text.size();
			while (p < end) {
				while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ','))++p;
				if (p == end)break;
				if (values.size() >= maximumValues)return false;
				char* next = nullptr;
				const double value = std::strtod(p, &next);
				if (next == p || next > end || !std::isfinite(value))return false;
				values.push_back(value);
				p = next;
			}
			return true;
		}
		bool loadDae(const std::filesystem::path& path, Mesh& mesh) {
			std::error_code sizeError;
			const auto fileSize = std::filesystem::file_size(path, sizeError);
			constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
			if (!sizeError && fileSize > maximumModelFileBytes) { mesh.detail = "COLLADA mesh exceeds the 32 MiB viewer safety limit"; return false; }
			std::ifstream file(path, std::ios::binary);
			if (!file) { mesh.detail = "COLLADA mesh file could not be opened"; return false; }
			const std::string xml((std::istreambuf_iterator<char>(file)), {});
			if (xml.find("<COLLADA") == std::string::npos) { mesh.detail = "Unrecognized COLLADA document"; return false; }

			constexpr size_t maximumVertices = 500000;
			constexpr size_t maximumIndices = 1500000;
			const std::regex geometryPattern(R"(<geometry\b([^>]*)>([\s\S]*?)</geometry\s*>)");
			const std::regex sourcePattern(R"(<source\b([^>]*)>([\s\S]*?)</source\s*>)");
			const std::regex verticesPattern(R"(<vertices\b([^>]*)>([\s\S]*?)</vertices\s*>)");
			const std::regex inputPattern(R"(<input\b([^>]*)/?>)");
			const std::regex primitivePattern(R"(<(triangles|polylist)\b([^>]*)>([\s\S]*?)</\1\s*>)");
			const std::regex pPattern(R"(<p\b[^>]*>([\s\S]*?)</p\s*>)");
			const std::regex vcountPattern(R"(<vcount\b[^>]*>([\s\S]*?)</vcount\s*>)");
			std::string diagnostic;
			for (std::sregex_iterator geometryIt(xml.begin(), xml.end(), geometryPattern), end; geometryIt != end; ++geometryIt) {
				const std::string geometry = (*geometryIt)[2].str();
				std::unordered_map<std::string, std::vector<double>> positions;
				for (std::sregex_iterator sourceIt(geometry.begin(), geometry.end(), sourcePattern); sourceIt != end; ++sourceIt) {
					const std::string id = xmlAttribute((*sourceIt)[1].str(), "id");
					if (id.empty())continue;
					const std::string floats = xmlElementText((*sourceIt)[2].str(), "float_array");
					if (floats.empty())continue;
					std::vector<double> values;
					if (parseXmlNumbers(floats, values, maximumVertices * 3) && values.size() >= 9 && values.size() % 3 == 0)
						positions.emplace(id, std::move(values));
				}

				std::unordered_map<std::string, std::string> vertexPositions;
				for (std::sregex_iterator verticesIt(geometry.begin(), geometry.end(), verticesPattern); verticesIt != end; ++verticesIt) {
					const std::string verticesId = xmlAttribute((*verticesIt)[1].str(), "id");
					if (verticesId.empty())continue;
					const std::string inputs = (*verticesIt)[2].str();
					for (std::sregex_iterator inputIt(inputs.begin(), inputs.end(), inputPattern); inputIt != end; ++inputIt) {
						if (xmlAttribute((*inputIt)[1].str(), "semantic") != "POSITION")continue;
						std::string sourceId = xmlAttribute((*inputIt)[1].str(), "source");
						if (!sourceId.empty() && sourceId.front() == '#')sourceId.erase(sourceId.begin());
						vertexPositions[verticesId] = std::move(sourceId);
						break;
					}
				}

				std::unordered_map<std::string, uint32_t> sourceBases;
				for (std::sregex_iterator primitiveIt(geometry.begin(), geometry.end(), primitivePattern); primitiveIt != end; ++primitiveIt) {
					const std::string kind = (*primitiveIt)[1].str();
					const std::string primitive = (*primitiveIt)[3].str();
					const std::string inputs = primitive;
					int tupleStride = 0, positionOffset = -1;
					std::string positionSource;
					for (std::sregex_iterator inputIt(inputs.begin(), inputs.end(), inputPattern); inputIt != end; ++inputIt) {
						const std::string inputAttributes = (*inputIt)[1].str();
						const std::string semantic = xmlAttribute(inputAttributes, "semantic");
						const std::string offsetText = xmlAttribute(inputAttributes, "offset");
						int offset = 0;
						if (!offsetText.empty()) { try { offset = std::stoi(offsetText); } catch (...) { offset = -1; } }
						if (offset < 0 || offset >= 32)continue;
						tupleStride = std::max(tupleStride, offset + 1);
						if (semantic != "VERTEX" && semantic != "POSITION")continue;
						std::string sourceId = xmlAttribute(inputAttributes, "source");
						if (!sourceId.empty() && sourceId.front() == '#')sourceId.erase(sourceId.begin());
						if (semantic == "VERTEX") {
							const auto vertexSource = vertexPositions.find(sourceId);
							if (vertexSource == vertexPositions.end())continue;
							sourceId = vertexSource->second;
						}
						if (positionOffset < 0 && positions.contains(sourceId)) { positionOffset = offset; positionSource = std::move(sourceId); }
					}
					if (tupleStride <= 0 || positionOffset < 0) { diagnostic = "COLLADA primitive has no supported position input"; continue; }
					const auto positionIt = positions.find(positionSource);
					if (positionIt == positions.end())continue;
					auto baseIt = sourceBases.find(positionSource);
					if (baseIt == sourceBases.end()) {
						const auto& points = positionIt->second;
						if (points.size() / 3 > maximumVertices - mesh.vertices.size()) { mesh.detail = "COLLADA mesh exceeds the 500,000 vertex viewer limit"; return false; }
						const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
						for (size_t i = 0; i + 2 < points.size(); i += 3)mesh.vertices.push_back({ (float)points[i],(float)points[i + 1],(float)points[i + 2] });
						baseIt = sourceBases.emplace(positionSource, base).first;
					}
					const uint32_t base = baseIt->second;
					for (std::sregex_iterator pIt(primitive.begin(), primitive.end(), pPattern); pIt != end; ++pIt) {
						std::vector<double> rawIndices;
						if (!parseXmlNumbers((*pIt)[1].str(), rawIndices, maximumIndices * 4ull)) { diagnostic = "COLLADA primitive has invalid index data"; continue; }
						if (rawIndices.size() % (size_t)tupleStride != 0) { diagnostic = "COLLADA primitive index tuple is truncated"; continue; }
						std::vector<uint32_t> positionIndices;
						positionIndices.reserve(rawIndices.size() / (size_t)tupleStride);
						bool valid = true;
						for (size_t i = (size_t)positionOffset; i < rawIndices.size(); i += (size_t)tupleStride) {
							const double value = rawIndices[i];
							if (value < 0 || value >= positionIt->second.size() / 3 || std::floor(value) != value) { valid = false; break; }
							positionIndices.push_back((uint32_t)value);
						}
						if (!valid) { diagnostic = "COLLADA primitive references an invalid position"; continue; }
						if (kind == "triangles") {
							if (positionIndices.size() % 3 != 0) { diagnostic = "COLLADA triangle list is incomplete"; continue; }
							if (positionIndices.size() > maximumIndices - mesh.indices.size()) { mesh.detail = "COLLADA mesh exceeds the 1,500,000 index viewer limit"; return false; }
							for (uint32_t index : positionIndices)mesh.indices.push_back(base + index);
						}
						else {
							std::smatch vcountMatch;
							if (!std::regex_search(primitive, vcountMatch, vcountPattern)) { diagnostic = "COLLADA polygon list has no vertex counts"; continue; }
							std::vector<double> counts;
							if (!parseXmlNumbers(vcountMatch[1].str(), counts, maximumIndices)) { diagnostic = "COLLADA polygon list has invalid vertex counts"; continue; }
							size_t at = 0;
							for (double countValue : counts) {
								if (countValue < 3 || countValue > positionIndices.size() - at || std::floor(countValue) != countValue) { valid = false; break; }
								const size_t count = (size_t)countValue;
								if ((count - 2) * 3 > maximumIndices - mesh.indices.size()) { mesh.detail = "COLLADA mesh exceeds the 1,500,000 index viewer limit"; return false; }
								for (size_t k = 1; k + 1 < count; k++)mesh.indices.insert(mesh.indices.end(), { base + positionIndices[at],base + positionIndices[at + k],base + positionIndices[at + k + 1] });
								at += count;
							}
							if (!valid || at != positionIndices.size())diagnostic = "COLLADA polygon counts do not match its indices";
						}
					}
				}
			}
			mesh.loaded = !mesh.vertices.empty() && !mesh.indices.empty();
			if (!mesh.loaded)mesh.detail = diagnostic.empty() ? "COLLADA document has no supported triangle geometry" : diagnostic;
			return mesh.loaded;
		}
		// Scrap Mechanic still ships many parts in Ogre's binary v1.8 mesh format.
		// Its mesh/submesh headers use one-byte chunk IDs; vertex geometry retains
		// Ogre's standard two-byte IDs.
		bool loadOgreMesh(const std::filesystem::path& path, Mesh& mesh) {
			std::error_code sizeError;
			const auto fileSize = std::filesystem::file_size(path, sizeError);
			constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
			if (!sizeError && fileSize > maximumModelFileBytes) { mesh.detail = "Ogre mesh exceeds the 32 MiB viewer safety limit"; return false; }
			std::ifstream f(path, std::ios::binary); if (!f) { mesh.detail = "Mesh file could not be opened"; return false; }
			std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)), {});
			const char sig[] = "[MeshSerializer_v1.8]";
			if (b.size() < 32 || std::memcmp(b.data() + 2, sig, sizeof(sig) - 1) != 0) { mesh.detail = "Unrecognized Ogre mesh header"; return false; }
			auto u16 = [&](size_t p, uint16_t& v) {if (p + 2 > b.size())return false; v = (uint16_t)b[p] | ((uint16_t)b[p + 1] << 8); return true; };
			auto u32 = [&](size_t p, uint32_t& v) {if (p + 4 > b.size())return false; v = (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24); return true; };
			size_t p = 25; uint32_t rootLen = 0; if (p >= b.size() || b[p++] != 0x30 || !u32(p, rootLen)) { mesh.detail = "Invalid Ogre mesh root"; return false; }p += 4;
			const size_t rootEnd = std::min(b.size(), p + (size_t)rootLen);
			// Scrap Mechanic's v1.8 writer includes a 16-bit stream header before
			// its compact one-byte submesh chunk IDs.
			if (p + 2 <= rootEnd)p += 2;
			size_t guard = 0;
			while (p + 5 <= rootEnd && guard++ < 10000) {
				uint8_t id = b[p++]; if (id != 0x40)break; uint32_t len; if (!u32(p, len)) { break; }p += 4; size_t end = std::min(b.size(), p + (size_t)len); if (end <= p)break;
				if (id == 0x40) { // Submesh: material C-string, index count, indices, then geometry.
					size_t q = p; while (q < end && b[q])q++; if (q < end)q++;
					const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
					uint32_t count = 0; if (q + 4 <= end && u32(q, count)) {
						q += 4; if (count <= 3000000 && q + (size_t)count * 2 <= end) { for (uint32_t i = 0; i < count; i++) { const size_t at = q + (size_t)i * 2; mesh.indices.push_back(vertexBase + (((uint32_t)b[at] << 8) | b[at + 1])); }q += (size_t)count * 2; }
						// The game writer inserts a one-byte submesh flag before the geometry chunk.
						if (q + 2 < end && b[q + 1] == 0x00 && b[q + 2] == 0x50)q++;
						// Geometry records contain the vertex count, declaration and one or more buffers.
						while (q + 6 <= b.size()) {
							uint16_t cid; uint32_t clen; if (!u16(q, cid) || !u32(q + 2, clen))break; size_t ce = std::min(b.size(), q + 6 + (size_t)clen); if (ce <= q + 6)break;
							if (cid == 0x5000 && q + 11 <= ce) {
								uint32_t vc; u32(q + 6, vc); if (vc > 0 && vc < 500000) {
									size_t g = q + 11; std::vector<std::array<uint16_t, 5>> elems; struct VB { uint16_t bind{}, stride{}; size_t data{}, end{}; }; std::vector<VB> buffers;
									while (g + 5 <= ce) {
										uint8_t gid = b[g]; uint32_t gl; if (!u32(g + 1, gl))break; size_t ge = std::min(ce, g + 5 + (size_t)gl); if (ge <= g + 5)break;
										if (gid == 0x51) { for (size_t e = g + 5; e + 6 <= ge;) { uint16_t eid; uint32_t el; if (!u16(e, eid) || !u32(e + 2, el) || e + 6 + (size_t)el > ge)break; if (eid == 0x5110 && el >= 10) { std::array<uint16_t, 5>a{}; for (int k = 0; k < 5; k++)u16(e + 6 + k * 2, a[k]); elems.push_back(a); }e += 6 + (size_t)el; } }
										else if (gid == 0x52 && g + 15 <= ge) { uint16_t did; uint32_t dl; if (u16(g + 9, did) && u32(g + 11, dl) && did == 0x5210 && g + 15 + (size_t)dl <= ge) { VB v; u16(g + 5, v.bind); u16(g + 7, v.stride); v.data = g + 15; v.end = g + 15 + (size_t)dl; buffers.push_back(v); } }
										g = ge;
									}
									// The game pads some vertex declarations; locate the following
									// buffer record within this bounded geometry chunk as a fallback.
									if (buffers.empty())for (size_t t = q + 10; t + 15 <= ce; t++)if (b[t] == 0x52) { uint32_t bl, dl; uint16_t did; VB v; if (u32(t + 1, bl) && t + 5 + (size_t)bl <= ce && u16(t + 9, did) && u32(t + 11, dl) && did == 0x5210) { u16(t + 5, v.bind); u16(t + 7, v.stride); v.data = t + 15; v.end = std::min(ce, v.data + (size_t)dl); if (v.stride >= 12 && v.data + (size_t)vc * v.stride <= v.end) { buffers.push_back(v); break; } } }
									auto posElem = std::find_if(elems.begin(), elems.end(), [](const auto& e) {return e[2] == 1; });
									if (posElem != elems.end()) { uint16_t source = (*posElem)[0], type = (*posElem)[1], off = (*posElem)[3]; auto vb = std::find_if(buffers.begin(), buffers.end(), [&](const VB& v) {return v.bind == source; }); if (vb != buffers.end() && type == 2 && vb->stride >= off + 12 && vb->data + (size_t)vc * vb->stride <= vb->end) { for (uint32_t i = 0; i < vc; i++) { float xyz[3]; std::memcpy(xyz, b.data() + vb->data + (size_t)i * vb->stride + off, 12); mesh.vertices.push_back({ xyz[0],xyz[1],xyz[2] }); } } }
								}
							}
							q = ce;
						}
					}
				}
				p = end;
			}
			// Some builds store 32-bit indices; reject out-of-range data instead of drawing corrupt triangles.
			mesh.indices.erase(std::remove_if(mesh.indices.begin(), mesh.indices.end(), [&](uint32_t i) {return i >= mesh.vertices.size(); }), mesh.indices.end());
			mesh.indices.resize(mesh.indices.size() / 3 * 3); mesh.loaded = !mesh.vertices.empty() && !mesh.indices.empty();
			if (!mesh.loaded)mesh.detail = "Ogre mesh has no supported position vertex buffer and triangle indices";
			return mesh.loaded;
		}
		std::shared_ptr<Mesh> meshFor(const std::string& uuid) {
			loadCatalog(); auto id = lower(uuid); auto found = state.meshes.find(id); if (found != state.meshes.end())return found->second;
			auto mesh = std::make_shared<Mesh>(); auto entry = state.catalog.find(id);
			if (entry != state.catalog.end()) {
				std::string lastFailure;
				for (const auto& modelPath : entry->second.models) {
					auto asset = state.meshFiles.find(modelPath.lexically_normal().native());
					if (asset != state.meshFiles.end()) { mesh = asset->second; if (mesh->loaded)break; lastFailure = mesh->detail; continue; }
					auto candidate = std::make_shared<Mesh>();
					const auto extension = lower(modelPath.extension().string());
					if (extension == ".fbx")loadFbx(modelPath, *candidate);
					else if (extension == ".dae")loadDae(modelPath, *candidate);
					else loadOgreMesh(modelPath, *candidate);
					if (candidate->vertices.size() > 500000 || candidate->indices.size() > 1500000) {
						candidate->vertices.clear(); candidate->indices.clear(); candidate->loaded = false;
						candidate->detail = "Model exceeds the 500,000 vertex / 1,500,000 index viewer limit";
					}
					if (candidate->loaded)buildNormals(*candidate);
					constexpr size_t maximumMeshCacheBytes = 128ull * 1024ull * 1024ull;
					const size_t meshBytes = candidate->vertices.capacity() * sizeof(V3) + candidate->normals.capacity() * sizeof(V3) + candidate->indices.capacity() * sizeof(uint32_t);
					if (candidate->loaded && meshBytes > maximumMeshCacheBytes - state.meshCacheBytes) {
						candidate->vertices = std::vector<V3>(); candidate->normals = std::vector<V3>(); candidate->indices = std::vector<uint32_t>();
						candidate->loaded = false; candidate->hasBounds = false; candidate->detail = "Per-blueprint mesh cache limit reached; using a cuboid preview";
					}
					else if (candidate->loaded)state.meshCacheBytes += meshBytes;
					if (!candidate->loaded) {
						candidate->vertices = std::vector<V3>(); candidate->normals = std::vector<V3>(); candidate->indices = std::vector<uint32_t>();
						lastFailure = candidate->detail;
					}
					state.meshFiles.emplace(modelPath.lexically_normal().native(), candidate);
					mesh = std::move(candidate);
					if (mesh->loaded)break;
				}
				if (!mesh->loaded && !lastFailure.empty())mesh->detail = lastFailure;
			}
			else if (state.blockShapes.contains(id))mesh->detail = "Built-in Scrap Mechanic block; shown as a voxel cuboid";
			else mesh->detail = "Shape UUID was not found in the installed shape sets";
			state.meshes.emplace(id, mesh); return mesh;
		}
		GLuint diffuseTextureFor(const std::filesystem::path& diffuseTexture) {
			if (diffuseTexture.empty())return 0;
			const auto key = diffuseTexture.lexically_normal().native();
			auto found = state.textures.find(key);
			if (found != state.textures.end())return found->second;
			int width = 0, height = 0, channels = 0;
			GLuint texture = 0;
			if (stbi_info(diffuseTexture.string().c_str(), &width, &height, &channels) && width > 0 && height > 0 && width <= 8192 && height <= 8192 && (uint64_t)width * (uint64_t)height <= 32ull * 1024ull * 1024ull && (size_t)width * (size_t)height * 4 <= 256ull * 1024ull * 1024ull - state.textureCacheBytes) {
				unsigned char* pixels = stbi_load(diffuseTexture.string().c_str(), &width, &height, &channels, 4);
				if (pixels) {
					const GlStateSnapshot saved = captureGlState();
					GLint unpackAlignment = 4; glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
					while (glGetError() != GL_NO_ERROR) {}
					glGenTextures(1, &texture); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
					glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
					glGenerateMipmap(GL_TEXTURE_2D);
					stbi_image_free(pixels);
					const GLenum uploadError = glGetError();
					glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
					restoreGlState(saved);
					if (uploadError != GL_NO_ERROR) { if (texture)glDeleteTextures(1, &texture); texture = 0; }
					else state.textureCacheBytes += (size_t)width * (size_t)height * 4;
				}
			}
			state.textures.emplace(key, texture);
			return texture;
		}

V3 axis(int v) { switch (v) { case 1:return { 1,0,0 }; case -1:return { -1,0,0 }; case 2:return { 0,1,0 }; case -2:return { 0,-1,0 }; case 3:return { 0,0,1 }; case -3:return { 0,0,-1 }; default:return { 1,0,0 }; } }
									V3 orient(V3 v, const Parser::Block& b) {
										V3 x = axis(b.xaxis), z = axis(b.zaxis), y = norm(cross(z, x));
										return x * v.x + y * v.y + z * v.z;
									}
									V3 shapeSize(const Parser::Block& b) {
										const std::string id = lower(b.shapeID);
										if (b.hasBounds || state.blockShapes.contains(id))
											return { (float)std::max(1,b.bounds.x),(float)std::max(1,b.bounds.y),(float)std::max(1,b.bounds.z) };
										const auto found = state.shapeSizes.find(id);
										if (found != state.shapeSizes.end())return found->second;
										return { (float)std::max(1,b.bounds.x),(float)std::max(1,b.bounds.y),(float)std::max(1,b.bounds.z) };
									}
									size_t gridCellCount(const std::vector<Parser::Block>& blocks) {
										size_t count = 0;
										for (const auto& block : blocks) {
											const V3 size = shapeSize(block);
											count += (size_t)std::max(1, (int)std::lround(size.x)) * (size_t)std::max(1, (int)std::lround(size.y)) * (size_t)std::max(1, (int)std::lround(size.z));
										}
										return count;
									}
									int rotateAxisAroundZ(int axisCode, int quarterTurns) {
										if (quarterTurns < 0) {
											switch (axisCode) {
											case 1:return -2; case -2:return -1; case -1:return 2; case 2:return 1;
											case 3:return 3; case -3:return -3; default:return axisCode;
											}
										}
										switch (axisCode) {
										case 1:return 2; case 2:return -1; case -1:return -2; case -2:return 1;
										case 3:return 3; case -3:return -3; default:return axisCode;
										}
									}
									void setOrientation(Parser::Block& block, int xaxis, int zaxis) {
										// Blueprint positions are the part's local grid anchor. Rotate only its
										// orientation so the saved transform matches Scrap Mechanic's placement.
										block.xaxis = xaxis; block.zaxis = zaxis;
									}
									void buildNormals(Mesh& mesh) {
										struct PositionKey {
											int64_t x{}, y{}, z{};
											bool operator==(const PositionKey& other) const { return x == other.x && y == other.y && z == other.z; }
										};
										struct PositionHash {
											size_t operator()(const PositionKey& p) const {
												size_t h = std::hash<int64_t>{}(p.x);
												h ^= std::hash<int64_t>{}(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
												h ^= std::hash<int64_t>{}(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
												return h;
											}
										};
										auto key = [](V3 v) {return PositionKey{ (int64_t)std::llround(v.x * 100000.f),(int64_t)std::llround(v.y * 100000.f),(int64_t)std::llround(v.z * 100000.f) }; };
										mesh.normals.assign(mesh.vertices.size(), V3{});
										if (!mesh.vertices.empty()) {
											mesh.boundsMin = mesh.boundsMax = mesh.vertices.front();
											for (const auto& v : mesh.vertices) {
												mesh.boundsMin.x = std::min(mesh.boundsMin.x, v.x); mesh.boundsMin.y = std::min(mesh.boundsMin.y, v.y); mesh.boundsMin.z = std::min(mesh.boundsMin.z, v.z);
												mesh.boundsMax.x = std::max(mesh.boundsMax.x, v.x); mesh.boundsMax.y = std::max(mesh.boundsMax.y, v.y); mesh.boundsMax.z = std::max(mesh.boundsMax.z, v.z);
											}
											mesh.hasBounds = true;
										}
										std::unordered_map<PositionKey, std::vector<V3>, PositionHash> coincidentFaceNormals;
										for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
											const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
											if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())continue;
											V3 n = cross(mesh.vertices[b] - mesh.vertices[a], mesh.vertices[c] - mesh.vertices[a]);
											if (dot(n, n) < 1e-16f)continue;
											mesh.normals[a] = mesh.normals[a] + n; mesh.normals[b] = mesh.normals[b] + n; mesh.normals[c] = mesh.normals[c] + n;
											coincidentFaceNormals[key(mesh.vertices[a])].push_back(n);
											coincidentFaceNormals[key(mesh.vertices[b])].push_back(n);
											coincidentFaceNormals[key(mesh.vertices[c])].push_back(n);
										}
										// Ogre and FBX exports often duplicate vertices at submesh and UV seams.
										// Blend normals at coincident positions across gentle edges so triangulated
										// faces do not show as a patchwork of flat triangles. Keep true hard edges.
										for (size_t i = 0; i < mesh.vertices.size(); i++) {
											V3 reference = norm(mesh.normals[i]);
											auto found = coincidentFaceNormals.find(key(mesh.vertices[i]));
											if (found != coincidentFaceNormals.end()) {
												V3 smooth{};
												for (V3 candidate : found->second)if (dot(reference, norm(candidate)) >= .5f)smooth = smooth + candidate;
												if (dot(smooth, smooth) > 1e-16f)reference = norm(smooth);
											}
											mesh.normals[i] = reference;
										}
									}
									struct Camera { ImVec2 center{}; float scale{}, cy{}, sy{}, cp{}, sp{}; };
									ImVec2 project(V3 v, const Camera& c, float& depth) {
										// Scrap Mechanic blueprint coordinates are Z-up: X/Y lie on the ground.
										float xx = c.cy * v.x - c.sy * v.y, horizontalDepth = c.sy * v.x + c.cy * v.y;
										float yy = c.cp * v.z - c.sp * horizontalDepth; depth = c.sp * v.z + c.cp * horizontalDepth;
										return { c.center.x + c.scale * xx,c.center.y - c.scale * yy };
									}
									bool colorValue(std::string s, int& out) {
										if (!s.empty() && s[0] == '#')s.erase(0, 1); if (s.size() != 6)return false;
										try { size_t n; unsigned long x = std::stoul(s, &n, 16); if (n != 6)return false; out = (int)x; return true; }
										catch (...) { return false; }
									}
									int displayColor(const Parser::Block& block) {
										int color = 0x8c9bad;
										if (block.hasColor && colorValue(block.color, color))return color;
										const auto defaultColor = state.blockColors.find(lower(block.shapeID));
										if (defaultColor != state.blockColors.end() && colorValue(defaultColor->second, color))return color;
										if (colorValue(block.color, color))return color;
										return 0x8c9bad;
									}
									void nudge(Parser::Block& b, int x, int y, int z) { b.pos.x += x; b.pos.y += y; b.pos.z += z; }
	}

	ModelLoadReport inspectModelFile(const std::filesystem::path& path) {
		Mesh mesh;
		const auto extension = lower(path.extension().string());
		const bool loaded = extension == ".mesh" ? loadOgreMesh(path, mesh) : extension == ".dae" ? loadDae(path, mesh) : loadFbx(path, mesh);
		return { loaded,mesh.vertices.size(),mesh.indices.size() / 3,mesh.detail };
	}
	ModelLoadReport inspectShape(const std::string& shapeId) {
		loadCatalog();
		const auto found = state.catalog.find(lower(shapeId));
		if (found == state.catalog.end()) {
			const auto id = lower(shapeId);
			if (state.blockShapes.contains(id))return { false,8,12,"Built-in Scrap Mechanic block; shown as a voxel cuboid because it has no imported mesh" };
			if (state.shapeSizes.contains(id))return { false,8,12,"Known part without a supported mesh; shown as a cuboid preview" };
			return { false,0,0,"Shape UUID was not found in the installed shape sets" };
		}
		const auto mesh = meshFor(shapeId);
		if (!mesh)return { false,0,0,"Model loader did not create a mesh record" };
		ModelLoadReport report{ mesh->loaded,mesh->vertices.size(),mesh->indices.size() / 3,mesh->detail };
		if (report.detail.empty() && !found->second.models.empty())report.detail = found->second.models.front().string();
		return report;
	}

	void shutdownRenderer() {
		for (const auto& [path, texture] : state.textures)if (texture)glDeleteTextures(1, &texture);
		state.textures.clear();
		state.textureCacheBytes = 0;
		if (state.sceneDepthBuffer)glDeleteRenderbuffers(1, &state.sceneDepthBuffer);
		if (state.sceneFramebuffer)glDeleteFramebuffers(1, &state.sceneFramebuffer);
		if (state.sceneColorTexture)glDeleteTextures(1, &state.sceneColorTexture);
		if (state.sceneVbo)glDeleteBuffers(1, &state.sceneVbo);
		if (state.sceneVao)glDeleteVertexArrays(1, &state.sceneVao);
		if (state.sceneProgram)glDeleteProgram(state.sceneProgram);
		state.sceneDepthBuffer = state.sceneFramebuffer = state.sceneColorTexture = 0;
		state.sceneVbo = state.sceneVao = state.sceneProgram = 0;
		state.sceneTargetWidth = state.sceneTargetHeight = 0;
	}

	void resetSelection() {
		state.selection = -1;
		state.selectionSet.clear();
		state.dragSelection.clear();
		state.dragAxis = 0;
	}

	void render(json& blueprint, const std::filesystem::path& blueprintPath,
		std::vector<Parser::Block>& blocks, const json& items,
		const std::string& blueprintName, bool& backToSelectionRequested) {
		loadCatalog();
		if (state.currentBlueprint != blueprintPath) { state.meshes.clear(); state.meshFiles.clear(); state.meshCacheBytes = 0; state.currentBlueprint = blueprintPath; state.selection = -1; state.selectionSet.clear(); state.dragSelection.clear(); state.dragAxis = 0; state.pan = { 0,0 }; state.zoom = 1.f; }
		for (auto it = state.selectionSet.begin(); it != state.selectionSet.end();)if (*it < 0 || *it >= (int)blocks.size())it = state.selectionSet.erase(it); else ++it;
		if (state.selection < 0 || state.selection >= (int)blocks.size())state.selection = -1;
		if (state.selection >= 0)state.selectionSet.insert(state.selection);
		auto isSelected = [&](int index) {return index == state.selection || state.selectionSet.contains(index); };
		auto synchronizeJointPositions = [&]() {
			Parser::applyBlockListToNode(blocks, blueprint);
			if (!blueprint.contains("joints") || !blueprint["joints"].is_array()) return;
			auto readPosition = [](const json& value) {
				auto component = [&](const char* key) {
					if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer()) return 0;
					try { return value[key].get<int>(); } catch (...) { return 0; }
				};
				return Parser::Position{ component("x"), component("y"), component("z") };
			};
			for (auto& block : blocks) {
				if (!block.isJoint || block.jointIndex < 0 || block.jointIndex >= (int)blueprint["joints"].size()) continue;
				const auto& joint = blueprint["joints"][block.jointIndex];
				if (!joint.is_object() || !joint.contains("posA") || !joint.contains("posB")) continue;
				block.jointPosA = readPosition(joint["posA"]);
				block.jointPosB = readPosition(joint["posB"]);
				block.pos = block.jointPosA;
			}
		};
		auto moveSelection = [&](int x, int y, int z) {
			if (state.selectionSet.empty() && state.selection >= 0)state.selectionSet.insert(state.selection);
			for (int index : state.selectionSet)if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)nudge(blocks[index], x, y, z);
			synchronizeJointPositions();
			};
		auto selectedWorldSize = [&]() {
			const float infinity = std::numeric_limits<float>::infinity();
			V3 minimum{ infinity,infinity,infinity }, maximum{ -infinity,-infinity,-infinity };
			bool found = false;
			for (size_t index = 0; index < blocks.size(); index++)if (isSelected((int)index)) {
				const auto& part = blocks[index];
				const V3 center = blockCenter(part), half = shapeSize(part) * .5f;
				for (int mask = 0; mask < 8; mask++) {
					const V3 corner = center + orient({ mask & 1 ? half.x : -half.x,mask & 2 ? half.y : -half.y,mask & 4 ? half.z : -half.z }, part);
					minimum.x = std::min(minimum.x, corner.x); minimum.y = std::min(minimum.y, corner.y); minimum.z = std::min(minimum.z, corner.z);
					maximum.x = std::max(maximum.x, corner.x); maximum.y = std::max(maximum.y, corner.y); maximum.z = std::max(maximum.z, corner.z);
				}
				found = true;
			}
			return found ? maximum - minimum : V3{};
			};
		auto setSelectionColor = [&](const std::string& color) {
			bool applied = false;
			for (int index : state.selectionSet)if (index >= 0 && index < (int)blocks.size()) {
				blocks[index].color = color;
				blocks[index].hasColor = true;
				applied = true;
			}
			if (!applied && state.selection >= 0 && state.selection < (int)blocks.size()) {
				blocks[state.selection].color = color;
				blocks[state.selection].hasColor = true;
			}
			};
		ImGui::Begin("Blueprint Editor");
		auto saveBlueprint = [&]() {Parser::applyBlockListToNode(blocks, blueprint); if (Parser::saveBlueprint(blueprintPath.string(), blueprint))state.error.clear(); else state.error = "Could not save blueprint. Check file permissions."; };
		auto removeSelection = [&]() {
			std::vector<std::pair<int, int>> children;
			std::vector<int> joints;
			for (size_t i = 0; i < blocks.size(); ++i)if (isSelected((int)i)) {
				if (blocks[i].isJoint) joints.push_back(blocks[i].jointIndex);
				else children.emplace_back(blocks[i].bodyIndex, blocks[i].childIndex);
			}
			if (children.empty() && joints.empty())return false;
			Parser::applyBlockListToNode(blocks, blueprint);
			std::sort(joints.begin(), joints.end(), std::greater<int>());
			bool removed = false;
			for (int joint : joints)removed = Parser::removeJointFromNode(blueprint, joint) || removed;
			std::sort(children.begin(), children.end(), [](const auto& a, const auto& b) {return a.first != b.first ? a.first > b.first:a.second > b.second; });
			for (const auto& child : children)removed = Parser::removeBlockFromNode(blueprint, child.first, child.second) || removed;
			if (removed) { blocks = Parser::parseBlueprint(blueprint); state.selection = -1; state.selectionSet.clear(); state.dragSelection.clear(); state.dragAxis = 0; state.error = "Selected parts removed. Save Blueprint to write the change to disk."; }
			return removed;
			};
		const ImGuiIO& io = ImGui::GetIO();
		const bool shortcutReady = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
		if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))saveBlueprint();
		if (shortcutReady && ImGui::IsKeyPressed(ImGuiKey_Delete, false))removeSelection();
		ImGui::TextWrapped("Editing: %s", blueprintName.c_str());
		if (ImGui::Button("Back to Selection"))backToSelectionRequested = true;
		ImGui::Spacing();
		ImGui::Text("3D blueprint editor"); ImGui::SameLine(); ImGui::TextDisabled("%zu parts  |  %zu grid cells", blocks.size(), gridCellCount(blocks));
		ImGui::SameLine(); if (ImGui::Button("Save Blueprint"))saveBlueprint();
		ImGui::SameLine(); if (ImGui::Button("Reset view")) { state.yaw = .72f; state.pitch = .42f; state.zoom = 1.f; state.pan = { 0,0 }; }
		float avail = ImGui::GetContentRegionAvail().x; float inspector = std::clamp(avail * .25f, 230.f, 330.f);
		ImGui::BeginChild("BlueprintViewport", ImVec2(std::max(120.f, avail - inspector - 8), 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail(); size.x = std::max(size.x, 120.f); size.y = std::max(size.y, 120.f);
		ImDrawList* dl = ImGui::GetWindowDrawList(); dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(25, 28, 33, 255));
		ImGui::InvisibleButton("##scene", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered(); ImVec2 mouse = ImGui::GetIO().MousePos;
		if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) { ImVec2 d = ImGui::GetIO().MouseDelta; state.yaw += d.x * .008f; state.pitch = std::clamp(state.pitch + d.y * .008f, .08f, 1.35f); }
		if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) { ImVec2 d = ImGui::GetIO().MouseDelta; state.pan.x += d.x; state.pan.y += d.y; }
		if (hovered && ImGui::GetIO().MouseWheel != 0)state.zoom = std::clamp(state.zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), .12f, 5.f);

		float minY = std::numeric_limits<float>::max(), maxY = -minY, minX = minY, maxX = -minY, minZ = minY, maxZ = -minY;
		for (const auto& b : blocks) { const V3 center = blockCenter(b), dimensions = shapeSize(b), half = dimensions * .5f; for (int mask = 0; mask < 8; mask++) { const V3 c = center + orient({ mask & 1 ? half.x : -half.x,mask & 2 ? half.y : -half.y,mask & 4 ? half.z : -half.z }, b); minX = std::min(minX, c.x); maxX = std::max(maxX, c.x); minY = std::min(minY, c.y); maxY = std::max(maxY, c.y); minZ = std::min(minZ, c.z); maxZ = std::max(maxZ, c.z); } }
		if (blocks.empty()) { minX = minY = minZ = -2; maxX = maxY = maxZ = 2; }
		V3 focus{ (minX + maxX) * .5f,(minY + maxY) * .5f,(minZ + maxZ) * .5f };
		const float longest = std::max({ maxX - minX,maxY - minY,maxZ - minZ,1.f });
		const float fitScale = std::min(size.x, size.y) * .74f / std::max(8.f, longest * 1.65f);
		Camera camera; camera.center = { origin.x + size.x * .5f + state.pan.x,origin.y + size.y * .52f + state.pan.y }; camera.scale = std::clamp(fitScale * state.zoom, .25f, 140.f); camera.cy = std::cos(state.yaw); camera.sy = std::sin(state.yaw); camera.cp = std::cos(state.pitch); camera.sp = std::sin(state.pitch);
		auto screen = [&](V3 v, float& d) {return project(v - focus, camera, d); };
		// Infinite feeling ground plane, with a restrained scale-aware grid.
		int span = (int)std::clamp(std::max(maxX - minX, maxY - minY) + 8.f, 12.f, 120.f); int stride = span > 48 ? 4 : 1; int gx0 = (int)std::floor(focus.x) - span / 2, gy0 = (int)std::floor(focus.y) - span / 2;
		for (int k = 0; k <= span; k += stride) { float d; auto a = screen({ (float)(gx0 + k),(float)gy0,minZ }, d), b = screen({ (float)(gx0 + k),(float)(gy0 + span),minZ }, d); dl->AddLine(a, b, IM_COL32(66, 72, 82, 90)); a = screen({ (float)gx0,(float)(gy0 + k),minZ }, d); b = screen({ (float)(gx0 + span),(float)(gy0 + k),minZ }, d); dl->AddLine(a, b, IM_COL32(66, 72, 82, 90)); }

		struct WorldTri { std::array<ImVec2, 3> p; std::array<float, 3> vertexDepth; std::array<ImU32, 3> color; std::array<std::array<float, 2>, 3> uv; GLuint texture{}; int block; }; std::vector<WorldTri> triangles;
		std::vector<std::pair<int, std::array<ImVec2, 8>>> selectedOutlines; bool hasSelectedOutline = false; V3 selectedCenter{};
		size_t total = 0; for (const auto& b : blocks) { auto m = meshFor(b.shapeID); total += m->loaded ? m->indices.size() / 3 : 12; }
		// Keep per-frame projection and streamed GPU memory bounded, but let normal
		// creations use the actual part meshes. The former 15k triangle cutoff was
		// low enough that a handful of detailed parts switched the entire scene to
		// boxes, which made ordinary creations appear to have no models at all.
		constexpr size_t maximumSceneTriangles = 120000;
		constexpr size_t maximumPreviewBlocks = 10000;
		const bool coarsePreview = total > maximumSceneTriangles;
		const size_t previewStride = coarsePreview ? std::max<size_t>(1, (blocks.size() + maximumPreviewBlocks - 1) / maximumPreviewBlocks) : 1;
		std::unordered_set<int> coarsePreviewParts;
		if (coarsePreview) {
			for (size_t index = 0; index < blocks.size(); index += previewStride)coarsePreviewParts.insert((int)index);
			std::vector<int> selectedParts;
			if (state.selection >= 0 && state.selection < (int)blocks.size())selectedParts.push_back(state.selection);
			for (int index : state.selectionSet)if (index >= 0 && index < (int)blocks.size() && index != state.selection)selectedParts.push_back(index);
			for (int index : selectedParts) {
				if (coarsePreviewParts.contains(index))continue;
				if (coarsePreviewParts.size() < maximumPreviewBlocks) { coarsePreviewParts.insert(index); continue; }
				auto replace = coarsePreviewParts.end();
				for (auto it = coarsePreviewParts.begin(); it != coarsePreviewParts.end(); ++it)
					if (!isSelected(*it) && (replace == coarsePreviewParts.end() || *it > *replace))replace = it;
				if (replace != coarsePreviewParts.end()) { coarsePreviewParts.erase(replace); coarsePreviewParts.insert(index); }
			}
		}
		const size_t perPartTriangleBudget = coarsePreview ? std::max<size_t>(12, maximumSceneTriangles / std::max<size_t>(1, coarsePreviewParts.size())) : total;
		triangles.reserve(coarsePreview ? maximumSceneTriangles : total);
		for (size_t bi = 0; bi < blocks.size(); bi++) {
			if (coarsePreview && !coarsePreviewParts.contains((int)bi))continue;
			const auto& b = blocks[bi]; auto m = meshFor(b.shapeID); bool imported = m && m->loaded && !m->indices.empty();
			const int col = displayColor(b);
			const auto diffusePath = state.shapeDiffuseTextures.find(lower(b.shapeID));
			const GLuint diffuseTexture = imported && diffusePath != state.shapeDiffuseTextures.end() ? diffuseTextureFor(diffusePath->second) : 0;
			float red = (float)((col >> 16) & 255) / 255.f, green = (float)((col >> 8) & 255) / 255.f, blue = (float)(col & 255) / 255.f;
			const V3 dimensions = shapeSize(b);
			// Fixed render meshes are authored in game-space units and their origins
			// are meaningful attachment pivots. Do not scale them to their collision
			// hull/cylinder or their geometry and pivots drift away from blueprint
			// positions. Explicit blueprint bounds are the only render-scale request.
			float sc[3] = { 1.f,1.f,1.f };
			V3 lo{}, hi{}; if (imported && m->hasBounds) {
				lo = m->boundsMin; hi = m->boundsMax;
				if (b.hasBounds) {
					sc[0] = dimensions.x / std::max(.001f, hi.x - lo.x);
					sc[1] = dimensions.y / std::max(.001f, hi.y - lo.y);
					sc[2] = dimensions.z / std::max(.001f, hi.z - lo.z);
				}
			}
			else {
				// Built-in blocks and missing-model fallbacks use a unit cube mesh.
				// Scale that cube to the blueprint's saved bounds / ShapeSet footprint
				// or multi-cell blocks silently collapse back to one grid cell.
				imported = false;
				sc[0] = dimensions.x;
				sc[1] = dimensions.y;
				sc[2] = dimensions.z;
			}
			const V3 center = blockCenter(b);
			// FBX model vertices are authored around their model pivot. Re-centering
			// each asset to its mesh AABB loses deliberate pivot offsets (notably the
			// thruster's nozzle/body offset) and disagrees with the game's placement
			// transform. Scale the authored coordinates, then place that pivot at the
			// centre of the part's grid footprint.
			auto world = [&](V3 v) {v.x *= sc[0]; v.y *= sc[1]; v.z *= sc[2]; return orient(v, b) + center; };
			if (isSelected((int)bi)) {
				const V3 localMin = imported ? lo : V3{ -.5f,-.5f,-.5f }, localMax = imported ? hi : V3{ .5f,.5f,.5f };
				const V3 corners[8] = { {localMin.x,localMin.y,localMin.z},{localMax.x,localMin.y,localMin.z},{localMax.x,localMax.y,localMin.z},{localMin.x,localMax.y,localMin.z},{localMin.x,localMin.y,localMax.z},{localMax.x,localMin.y,localMax.z},{localMax.x,localMax.y,localMax.z},{localMin.x,localMax.y,localMax.z} };
				std::array<ImVec2, 8> outline{};
				for (int i = 0; i < 8; i++) { float d; outline[i] = screen(world(corners[i]), d); }
				selectedOutlines.emplace_back((int)bi, outline);
				if ((int)bi == state.selection) { selectedCenter = center; hasSelectedOutline = true; }
			}
			auto append = [&](V3 aa, V3 bb, V3 cc, V3 na, V3 nb, V3 nc, std::array<float, 2> ta = {}, std::array<float, 2> tb = {}, std::array<float, 2> tc = {}) {V3 a = world(aa), q = world(bb), r = world(cc); const V3 transformed[3] = { norm(orient({na.x / sc[0],na.y / sc[1],na.z / sc[2]},b)),norm(orient({nb.x / sc[0],nb.y / sc[1],nb.z / sc[2]},b)),norm(orient({nc.x / sc[0],nc.y / sc[1],nc.z / sc[2]},b)) }; const V3 lightDir = norm(V3{ -.4f,.5f,.82f }); ImU32 colors[3]; for (int i = 0; i < 3; i++) { float light = .88f + .12f * std::max(0.f, dot(transformed[i], lightDir)); float shade = isSelected((int)bi) ? std::min(1.f, light * 1.04f) : light; colors[i] = IM_COL32((int)(red * shade * 255), (int)(green * shade * 255), (int)(blue * shade * 255), 255); }float da, db, dc; auto pa = screen(a, da), pb = screen(q, db), pc = screen(r, dc); triangles.push_back({ {pa,pb,pc},{da,db,dc},{colors[0],colors[1],colors[2]},{ta,tb,tc},diffuseTexture,(int)bi }); };
			if (imported) {
				const size_t sourceTriangles = m->indices.size() / 3;
				const size_t triangleStride = coarsePreview ? std::max<size_t>(1, (sourceTriangles + perPartTriangleBudget - 1) / perPartTriangleBudget) : 1;
				for (size_t tri = 0; tri < sourceTriangles; tri += triangleStride) { size_t ti = tri * 3; const auto ia = m->indices[ti], ib = m->indices[ti + 1], ic = m->indices[ti + 2]; if (ia >= m->vertices.size() || ib >= m->vertices.size() || ic >= m->vertices.size())continue; const auto uvAt = [&](uint32_t i) {return i < m->uvs.size() ? m->uvs[i] : std::array<float, 2>{}; }; append(m->vertices[ia], m->vertices[ib], m->vertices[ic], m->normals[ia], m->normals[ib], m->normals[ic], uvAt(ia), uvAt(ib), uvAt(ic)); }
			}
			else {
				V3 v[8] = { {-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f} };
				const int ix[] = { 0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5 }; for (int t = 0; t < 36; t += 3) { const V3 a = v[ix[t]], q = v[ix[t + 1]], r = v[ix[t + 2]], n = norm(cross(q - a, r - a)); append(a, q, r, n, n, n); }
			}
		}
		// Upload projected triangles to a small offscreen target with a real depth
		// buffer. Average-depth triangle sorting cannot correctly resolve large,
		// intersecting surfaces, which made floor blocks paint across other parts.
		float nearestDepth = -std::numeric_limits<float>::infinity();
		float farthestDepth = std::numeric_limits<float>::infinity();
		for (const auto& triangle : triangles)for (float depth : triangle.vertexDepth) { nearestDepth = std::max(nearestDepth, depth); farthestDepth = std::min(farthestDepth, depth); }
		const float depthRange = std::max(1e-5f, nearestDepth - farthestDepth);
		std::vector<SceneBatch> gpuBatches;
		std::unordered_map<GLuint, size_t> batchByTexture;
		for (const auto& triangle : triangles)for (size_t corner = 0; corner < 3; corner++) {
			const ImVec2 point = triangle.p[corner]; const ImU32 packed = triangle.color[corner];
			const float x = 2.f * (point.x - origin.x) / size.x - 1.f;
			const float y = 1.f - 2.f * (point.y - origin.y) / size.y;
			const float z = 2.f * (nearestDepth - triangle.vertexDepth[corner]) / depthRange - 1.f;
			auto batch = batchByTexture.find(triangle.texture);
			if (batch == batchByTexture.end()) { batchByTexture[triangle.texture] = gpuBatches.size(); gpuBatches.push_back({ triangle.texture,{} }); batch = batchByTexture.find(triangle.texture); }
			auto& vertices = gpuBatches[batch->second].vertices;
			vertices.push_back({ x,y,z,(float)((packed >> IM_COL32_R_SHIFT) & 0xff) / 255.f,
				(float)((packed >> IM_COL32_G_SHIFT) & 0xff) / 255.f,(float)((packed >> IM_COL32_B_SHIFT) & 0xff) / 255.f,triangle.uv[corner][0],triangle.uv[corner][1] });
		}
		if (triangles.empty())state.renderError.clear();
		const bool sceneRendered = triangles.empty() || drawDepthBufferedScene(gpuBatches, origin, size, dl);
		if (!sceneRendered) {
			const ImVec2 message{ origin.x + 12.f,origin.y + 12.f };
			dl->AddRectFilled(message, ImVec2(message.x + std::min(size.x - 24.f, 500.f), message.y + 50.f), IM_COL32(56, 31, 24, 240), 5.f);
			dl->AddText(ImVec2(message.x + 9.f, message.y + 9.f), IM_COL32(255, 205, 170, 255), "3D depth renderer could not start; see viewer diagnostics in the inspector.");
		}
		if (coarsePreview) { const ImVec2 label{ origin.x + std::max(12.f,size.x - 390.f),origin.y + 12.f }; dl->AddRectFilled(label, ImVec2(label.x + 378.f, label.y + 28.f), IM_COL32(16, 20, 26, 235), 5.f); dl->AddText(ImVec2(label.x + 8.f, label.y + 7.f), IM_COL32(255, 210, 135, 255), "Large creation: real meshes shown at reduced detail."); }
		struct AxisHandle { ImVec2 center{}, end{}, screenAxis{}; float worldPerPixel{}; };
		auto getAxisHandle = [&](int signedAxis) {
			AxisHandle handle; float centerDepth, axisDepth;
			handle.center = screen(selectedCenter, centerDepth);
			const ImVec2 unitEnd = screen(selectedCenter + axis(signedAxis), axisDepth);
			float dx = unitEnd.x - handle.center.x, dy = unitEnd.y - handle.center.y;
			const float projectedPixels = std::sqrt(dx * dx + dy * dy);
			if (projectedPixels > 1e-3f)handle.screenAxis = { dx / projectedPixels,dy / projectedPixels };
			else {
				// At the singular camera angle the world axis points straight at the
				// camera. Keep its signed handle usable and visibly distinct.
				const int a = std::abs(signedAxis);
				handle.screenAxis = a == 1 ? ImVec2(1.f, 0.f) : a == 2 ? ImVec2(0.f, -1.f) : ImVec2(.707f, -.707f);
				if (signedAxis < 0) { handle.screenAxis.x = -handle.screenAxis.x; handle.screenAxis.y = -handle.screenAxis.y; }
			}
			handle.end = ImVec2(handle.center.x + handle.screenAxis.x * 38.f, handle.center.y + handle.screenAxis.y * 38.f);
			handle.worldPerPixel = std::min(.5f, 1.f / std::max(.02f, projectedPixels));
			return handle;
			};
		auto axisAtMouse = [&](ImVec2 point) {
			if (!hasSelectedOutline || state.selection < 0 || state.selection >= (int)blocks.size() || blocks[state.selection].isJoint)return 0;
			int hitAxis = 0; float nearest = 12.f;
			for (int a = 1; a <= 3; a++)for (int sign : {-1, 1}) {
				const int signedAxis = a * sign; const AxisHandle handle = getAxisHandle(signedAxis);
				const ImVec2 center = handle.center, end = handle.end;
				const float vx = end.x - center.x, vy = end.y - center.y, l2 = vx * vx + vy * vy; if (l2 < 16.f)continue;
				const float t = std::clamp(((point.x - center.x) * vx + (point.y - center.y) * vy) / l2, 0.f, 1.f);
				const float dx = point.x - (center.x + t * vx), dy = point.y - (center.y + t * vy), distance = std::sqrt(dx * dx + dy * dy);
				if (t > .08f && distance < nearest) { nearest = distance; hitAxis = signedAxis; }
			}
			return hitAxis;
			};
		const int hoveredAxis = hovered ? axisAtMouse(mouse) : 0;
		if (hoveredAxis && !state.dragAxis)ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		if (hasSelectedOutline) {
			static constexpr int edges[][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
			for (const auto& selectionOutline : selectedOutlines) {
				const bool active = selectionOutline.first == state.selection;
				const ImU32 outlineColor = active ? IM_COL32(102, 210, 255, 255) : IM_COL32(255, 205, 96, 255);
				for (const auto& edge : edges)dl->AddLine(selectionOutline.second[edge[0]], selectionOutline.second[edge[1]], outlineColor, active ? 2.f : 1.7f);
			}

			const ImU32 colors[3] = { IM_COL32(255,112,112,255),IM_COL32(120,235,155,255),IM_COL32(115,175,255,255) };
			const char* positive[3] = { "+X","+Y","+Z" }; const char* negative[3] = { "-X","-Y","-Z" };
			auto drawArrow = [&](ImVec2 from, ImVec2 to, ImU32 color, const char* label, int signedAxis) {
				const float dx = to.x - from.x, dy = to.y - from.y, len = std::sqrt(dx * dx + dy * dy); if (len < 8.f)return;
				const bool active = state.dragAxis == signedAxis, hot = hoveredAxis == signedAxis;
				const ImU32 visible = active ? IM_COL32(255, 245, 115, 255) : hot ? IM_COL32(255, 255, 255, 255) : color;
				const float ux = dx / len, uy = dy / len, head = std::min(9.f, len * .34f), wing = 4.5f;
				dl->AddLine(from, to, IM_COL32(10, 13, 18, 230), active ? 7.f : 5.f);
				dl->AddLine(from, to, visible, active ? 4.f : hot ? 3.5f : 2.5f);
				dl->AddLine(to, ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing), IM_COL32(10, 13, 18, 230), active ? 7.f : 5.f);
				dl->AddLine(to, ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing), visible, active ? 4.f : hot ? 3.5f : 2.5f);
				dl->AddLine(to, ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing), IM_COL32(10, 13, 18, 230), active ? 7.f : 5.f);
				dl->AddLine(to, ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing), visible, active ? 4.f : hot ? 3.5f : 2.5f);
				const ImVec2 labelPos(to.x + ux * 7.f - 10.f, to.y + uy * 7.f - 8.f);
				dl->AddText(ImVec2(labelPos.x + 1, labelPos.y + 1), IM_COL32(0, 0, 0, 255), label);
				dl->AddText(labelPos, visible, label);
				};
			for (int i = 0; i < 3; i++) {
				const AxisHandle positiveHandle = getAxisHandle(i + 1), negativeHandle = getAxisHandle(-(i + 1));
				drawArrow(positiveHandle.center, positiveHandle.end, colors[i], positive[i], i + 1);
				drawArrow(negativeHandle.center, negativeHandle.end, colors[i], negative[i], -(i + 1));
			}
			if (state.dragAxis) { const int a = std::abs(state.dragAxis) - 1; const char* names[3] = { "X","Y","Z" }; char indicator[64]; snprintf(indicator, sizeof(indicator), "MOVING  %c%s  ·  drag along this axis", state.dragAxis > 0 ? '+' : '-', names[a]); dl->AddRectFilled(ImVec2(origin.x + 12, origin.y + 12), ImVec2(origin.x + 230, origin.y + 40), IM_COL32(16, 20, 26, 235), 5.f); dl->AddText(ImVec2(origin.x + 21, origin.y + 19), IM_COL32(255, 245, 115, 255), indicator); }
		}
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			const int hitAxis = axisAtMouse(mouse);
			int picked = -1; float closestDepth = -std::numeric_limits<float>::max();
			if (hitAxis)picked = state.selection;
			else for (const auto& t : triangles) {
				const auto& a = t.p[0]; const auto& b = t.p[1]; const auto& c = t.p[2];
				const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
				if (std::abs(denom) < 1e-6f)continue;
				const float wa = ((b.y - c.y) * (mouse.x - c.x) + (c.x - b.x) * (mouse.y - c.y)) / denom;
				const float wb = ((c.y - a.y) * (mouse.x - c.x) + (a.x - c.x) * (mouse.y - c.y)) / denom;
				const float wc = 1.f - wa - wb;
				if (wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f) {
					const float depth = wa * t.vertexDepth[0] + wb * t.vertexDepth[1] + wc * t.vertexDepth[2];
					if (depth > closestDepth) { closestDepth = depth; picked = t.block; }
				}
			}
			if (!hitAxis) {
				if (ImGui::GetIO().KeyShift) { if (picked >= 0) { state.selection = picked; state.selectionSet.insert(picked); } }
				else { state.selection = picked; state.selectionSet.clear(); if (picked >= 0)state.selectionSet.insert(picked); }
			}
			state.dragSelection.clear();
			if (hitAxis) {
				state.dragSelection.assign(state.selectionSet.begin(), state.selectionSet.end());
				if (state.dragSelection.empty() && state.selection >= 0)state.dragSelection.push_back(state.selection);
			}
			state.dragAxis = hitAxis; state.lastDragMouse = mouse; state.dragRemainder = 0.f; state.dragWorldPerPixel = 0.f; state.dragScreenAxis = { 0,0 };
			if (hitAxis) { const AxisHandle handle = getAxisHandle(hitAxis); state.dragScreenAxis = handle.screenAxis; state.dragWorldPerPixel = handle.worldPerPixel; }
		}
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { state.dragSelection.clear(); state.dragAxis = 0; }
		if (!state.dragSelection.empty() && state.dragAxis && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			const ImVec2 delta = ImVec2(mouse.x - state.lastDragMouse.x, mouse.y - state.lastDragMouse.y);
			state.lastDragMouse = mouse;
			const int signedAxis = state.dragAxis;
			if (state.dragWorldPerPixel > 0.f) { state.dragRemainder += (delta.x * state.dragScreenAxis.x + delta.y * state.dragScreenAxis.y) * state.dragWorldPerPixel; const int steps = (int)std::trunc(state.dragRemainder); if (steps) { for (int index : state.dragSelection)if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)nudge(blocks[index], signedAxis == 1 ? steps : signedAxis == -1 ? -steps : 0, signedAxis == 2 ? steps : signedAxis == -2 ? -steps : 0, signedAxis == 3 ? steps : signedAxis == -3 ? -steps : 0); synchronizeJointPositions(); state.dragRemainder -= steps; } }
		}
		ImGui::EndChild(); ImGui::SameLine(); ImGui::BeginChild("BlockInspector", ImVec2(inspector, 0), true);
		ImGui::Text("INSPECTOR"); ImGui::Separator();
		if (state.selection < 0 || state.selection >= (int)blocks.size())ImGui::TextDisabled("Click a block in the viewport to edit it.");
		else {
			auto& b = blocks[state.selection];
			const bool multiSelection = state.selectionSet.size() > 1;
			std::string title = multiSelection ? "Multi-selection" : Parser::findBlockNameByShapeID(b.shapeID, items);
			if (title.empty())title = "Unknown part";
			ImGui::TextWrapped("%s", title.c_str());
			if (multiSelection)ImGui::TextDisabled("%zu parts selected · movement and color apply to all", state.selectionSet.size());
			else if (b.isJoint)ImGui::TextDisabled("Joint %d  ·  render pivot at attachment A", b.jointIndex + 1);
			else ImGui::TextDisabled("Part %d of %d  ·  body %d", state.selection + 1, (int)blocks.size(), b.bodyIndex + 1);
			ImGui::Spacing();
			ImGui::Text("Position");
			if (b.isJoint) {
				ImGui::Text("Position");
				ImGui::TextDisabled("A  (%d, %d, %d)", b.jointPosA.x, b.jointPosA.y, b.jointPosA.z);
				ImGui::TextDisabled("B  (%d, %d, %d)", b.jointPosB.x, b.jointPosB.y, b.jointPosB.z);
			}
			else {
				int positionX = b.pos.x, positionY = b.pos.y, positionZ = b.pos.z;
				auto positionDelta = [](int target, int current) {
					const long long delta = (long long)target - (long long)current;
					return (int)std::clamp(delta, (long long)std::numeric_limits<int>::min(), (long long)std::numeric_limits<int>::max());
					};
				ImGui::Text("Position");
				ImGui::SetNextItemWidth(-1); if (ImGui::InputInt("X##pos", &positionX, 0, 0))moveSelection(positionDelta(positionX, b.pos.x), 0, 0);
				ImGui::SetNextItemWidth(-1); if (ImGui::InputInt("Y##pos", &positionY, 0, 0))moveSelection(0, positionDelta(positionY, b.pos.y), 0);
				ImGui::SetNextItemWidth(-1); if (ImGui::InputInt("Z##pos", &positionZ, 0, 0))moveSelection(0, 0, positionDelta(positionZ, b.pos.z));
				if (ImGui::Button("Left -X", ImVec2(-1, 0)))moveSelection(-1, 0, 0); if (ImGui::Button("Right +X", ImVec2(-1, 0)))moveSelection(1, 0, 0);
				if (ImGui::Button("Forward +Y", ImVec2(-1, 0)))moveSelection(0, 1, 0); if (ImGui::Button("Back -Y", ImVec2(-1, 0)))moveSelection(0, -1, 0);
				if (ImGui::Button("Up +Z", ImVec2(-1, 0)))moveSelection(0, 0, 1); if (ImGui::Button("Down -Z", ImVec2(-1, 0)))moveSelection(0, 0, -1);
			}
			if (b.isJoint) {
				ImGui::Spacing(); ImGui::Text("Joint orientation");
				ImGui::TextDisabled("Orientation follows the connected bodies.");
			}
			else {
				ImGui::Spacing(); ImGui::Text("Orientation (Z is up)");
				static constexpr int axisValues[6] = { -3,-1,-2,1,2,3 };
				auto axisIndex = [&](int v) {for (int i = 0; i < 6; i++)if (axisValues[i] == v)return i; return 3; };
				int xi = axisIndex(b.xaxis), zi = axisIndex(b.zaxis);
				ImGui::Text("Local X axis"); ImGui::SetNextItemWidth(-1); if (ImGui::Combo("##localXAxis", &xi, "-Z\0-X\0-Y\0+X\0+Y\0+Z\0")) { int candidate = axisValues[xi]; if (std::abs(dot(axis(candidate), axis(b.zaxis))) < .5f)b.xaxis = candidate; else xi = axisIndex(b.xaxis); }
				ImGui::Text("Local Z axis (up)"); ImGui::SetNextItemWidth(-1); if (ImGui::Combo("##localZAxis", &zi, "-Z\0-X\0-Y\0+X\0+Y\0+Z\0")) { int candidate = axisValues[zi]; if (std::abs(dot(axis(candidate), axis(b.xaxis))) < .5f)b.zaxis = candidate; else zi = axisIndex(b.zaxis); }
				if (ImGui::Button("Rotate +90° around Z (up)", ImVec2(-1, 0))) { setOrientation(b, rotateAxisAroundZ(b.xaxis, 1), rotateAxisAroundZ(b.zaxis, 1)); }if (ImGui::Button("Rotate -90° around Z (up)", ImVec2(-1, 0))) { setOrientation(b, rotateAxisAroundZ(b.xaxis, -1), rotateAxisAroundZ(b.zaxis, -1)); }
				if (ImGui::Button("Reset rotation", ImVec2(-1, 0))) { setOrientation(b, 1, 3); }
			}
			ImGui::Spacing(); ImGui::Text("Dimensions");
			const bool canResize = state.blockShapes.contains(lower(b.shapeID));
			if (multiSelection) {
				const V3 overall = selectedWorldSize();
				ImGui::TextDisabled("Overall size of selected parts");
				ImGui::Text("Width (X): %.1f", overall.x);
				ImGui::Text("Depth (Y): %.1f", overall.y);
				ImGui::Text("Height (Z, up): %.1f", overall.z);
			}
			else if (canResize) {
				bool dimensionsChanged = false;
				ImGui::Text("Width"); ImGui::SetNextItemWidth(-1); dimensionsChanged |= ImGui::InputInt("##dimWidth", &b.bounds.x);
				// Blueprint bounds are stored in local X/Y/Z order. With Scrap
				// Mechanic's Z-up basis, local Z is height and local Y is depth.
				ImGui::Text("Height"); ImGui::SetNextItemWidth(-1); dimensionsChanged |= ImGui::InputInt("##dimHeight", &b.bounds.z);
				ImGui::Text("Depth"); ImGui::SetNextItemWidth(-1); dimensionsChanged |= ImGui::InputInt("##dimDepth", &b.bounds.y);
				b.bounds.x = std::clamp(b.bounds.x, 1, 256); b.bounds.y = std::clamp(b.bounds.y, 1, 256); b.bounds.z = std::clamp(b.bounds.z, 1, 256);
				if (dimensionsChanged)b.hasBounds = b.bounds.x != 1 || b.bounds.y != 1 || b.bounds.z != 1;
			}
			else {
				const V3 dimensions = shapeSize(b);
				ImGui::TextDisabled("Part footprint: %.0f × %.0f × %.0f (fixed by game)", dimensions.x, dimensions.y, dimensions.z);
			}
			const int colorInt = displayColor(b);
			char hex[16];
			snprintf(hex, sizeof(hex), "%06X", colorInt);
			const ImVec4 previewColor(
				((colorInt >> 16) & 255) / 255.f,
				((colorInt >> 8) & 255) / 255.f,
				(colorInt & 255) / 255.f,
				1.f);
			ImGui::TextUnformatted(multiSelection ? "Color · all selected parts" : "Color");
			ImGui::PushID(state.selection);
			if (ImGui::ColorButton("##colorPreview", previewColor, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 22)))
				ImGui::OpenPopup("ColorPickerPopup");
			ImGui::SameLine(); ImGui::TextDisabled("Click square to choose");
			ImGui::SetNextItemWidth(-1);
			if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal) && strlen(hex) == 6) {
				const int value = (int)strtoul(hex, nullptr, 16);
				char normalized[7];
				snprintf(normalized, sizeof(normalized), "%06x", value);
				setSelectionColor(normalized);
			}
			ImGui::TextDisabled("Hex · #RRGGBB");
			if (ImGui::BeginPopup("ColorPickerPopup")) {
				float pickerColor[3] = { previewColor.x, previewColor.y, previewColor.z };
				if (ImGui::ColorPicker3("##colorPicker", pickerColor,
					ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel)) {
					const int red = (int)(pickerColor[0] * 255.f + 0.5f);
					const int green = (int)(pickerColor[1] * 255.f + 0.5f);
					const int blue = (int)(pickerColor[2] * 255.f + 0.5f);
					char normalized[7];
					snprintf(normalized, sizeof(normalized), "%02x%02x%02x", red, green, blue);
					setSelectionColor(normalized);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
			ImGui::Spacing(); auto m = meshFor(b.shapeID); if (coarsePreview && m && m->loaded)ImGui::TextDisabled("Large scene · real mesh detail reduced"); else if (m && m->loaded)ImGui::TextColored(ImVec4(.45f, .84f, .65f, 1), "Game model loaded"); else if (state.blockShapes.contains(lower(b.shapeID)))ImGui::TextDisabled("Built-in block · cuboid preview"); else { ImGui::TextDisabled("Box preview · model unavailable"); if (m && !m->detail.empty())ImGui::TextWrapped("%s", m->detail.c_str()); }
			ImGui::Spacing();
			const char* removeLabel = state.selectionSet.size() > 1 ? "Remove selected parts" : "Remove selected part";
			if (ImGui::Button(removeLabel, ImVec2(-1, 0)) && !removeSelection())state.error = "Could not remove the selected parts from the blueprint.";
		}
		ImGui::Separator(); ImGui::Text("Scene"); size_t loadedParts = 0, blockParts = 0, missingParts = 0; for (const auto& b : blocks) { auto id = lower(b.shapeID); auto m = meshFor(b.shapeID); if (m && m->loaded)loadedParts++; else if (state.blockShapes.contains(id))blockParts++; else missingParts++; }ImGui::Text("%zu parts", blocks.size()); ImGui::Text("%zu mesh assets · %zu built-in block previews", loadedParts, blockParts); if (missingParts)ImGui::TextColored(ImVec4(1.f, .68f, .34f, 1.f), "%zu parts using fallback boxes", missingParts); if (coarsePreview)ImGui::TextDisabled("Large scene shows reduced real-mesh detail for responsiveness."); if (!state.renderError.empty())ImGui::TextColored(ImVec4(1.f, .48f, .36f, 1.f), "3D renderer: %s", state.renderError.c_str());
		if (!state.error.empty())ImGui::TextWrapped("%s", state.error.c_str());
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
		ImGui::TextWrapped("Shift-click adds to selection. Drag an axis arrow to move selected parts along it. Right-drag to orbit, middle-drag to pan, and scroll to zoom.");
		ImGui::PopStyleColor();
		ImGui::EndChild(); ImGui::End();
	}
}
