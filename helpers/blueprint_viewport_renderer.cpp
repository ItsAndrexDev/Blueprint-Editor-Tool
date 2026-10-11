#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace BlueprintViewport::Internal {
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
  if (saved.activeTexture != GL_TEXTURE0)
    glActiveTexture((GLenum)saved.activeTexture);
  glGetIntegerv(GL_DEPTH_FUNC, &saved.depthFunction);
  glGetIntegerv(GL_SCISSOR_BOX, saved.scissorBox);
  saved.depthTest = glIsEnabled(GL_DEPTH_TEST);
  glGetBooleanv(GL_DEPTH_WRITEMASK, &saved.depthWrite);
  saved.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
  saved.blend = glIsEnabled(GL_BLEND);
  saved.cullFace = glIsEnabled(GL_CULL_FACE);
  glGetBooleanv(GL_COLOR_WRITEMASK, saved.colorWrite);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, saved.clearColor);
  return saved;
}

void restoreGlState(const GlStateSnapshot &saved) {
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)saved.drawFramebuffer);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)saved.readFramebuffer);
  glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2],
             saved.viewport[3]);
  glUseProgram((GLuint)saved.program);
  glBindVertexArray((GLuint)saved.vertexArray);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)saved.arrayBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)saved.renderbuffer);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, (GLuint)saved.texture0Binding);
  if (saved.activeTexture != GL_TEXTURE0) {
    glActiveTexture((GLenum)saved.activeTexture);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved.activeTextureBinding);
  }
  glDepthFunc((GLenum)saved.depthFunction);
  glDepthMask(saved.depthWrite);
  glScissor(saved.scissorBox[0], saved.scissorBox[1], saved.scissorBox[2],
            saved.scissorBox[3]);
  glColorMask(saved.colorWrite[0], saved.colorWrite[1], saved.colorWrite[2],
              saved.colorWrite[3]);
  glClearColor(saved.clearColor[0], saved.clearColor[1], saved.clearColor[2],
               saved.clearColor[3]);
  if (saved.depthTest)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (saved.scissorTest)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
  if (saved.blend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (saved.cullFace)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
}

GLuint compileSceneShader(GLenum type, const char *source, std::string &error) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log((size_t)std::max(1, length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    error = "Could not compile scene shader: " + log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool ensureSceneRenderer() {
  if (state.sceneProgram && state.sceneVao && state.sceneVbo)
    return true;
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
void main(){vec3 surfaceColor=vColor;if(uTextured){vec4 texel=texture(uDiffuse,vUv);surfaceColor=mix(vColor,texel.rgb,texel.a);}outColor=vec4(surfaceColor,1.0);}
)GLSL";
  std::string error;
  GLuint vertex = compileSceneShader(GL_VERTEX_SHADER, vertexSource, error);
  if (!vertex) {
    state.renderError = error;
    return false;
  }
  GLuint fragment =
      compileSceneShader(GL_FRAGMENT_SHADER, fragmentSource, error);
  if (!fragment) {
    glDeleteShader(vertex);
    state.renderError = error;
    return false;
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log((size_t)std::max(1, length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    state.renderError = "Could not link scene shader: " + log;
    return false;
  }
  GLint oldVao = 0, oldBuffer = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
  GLuint vao = 0, vbo = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                        (void *)offsetof(SceneVertex, x));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                        (void *)offsetof(SceneVertex, r));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                        (void *)offsetof(SceneVertex, u));
  glBindVertexArray((GLuint)oldVao);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)oldBuffer);
  if (!vao || !vbo) {
    if (vao)
      glDeleteVertexArrays(1, &vao);
    if (vbo)
      glDeleteBuffers(1, &vbo);
    glDeleteProgram(program);
    state.renderError =
        "Could not allocate GPU buffers for the blueprint viewer.";
    return false;
  }
  state.sceneProgram = program;
  state.sceneVao = vao;
  state.sceneVbo = vbo;
  state.renderError.clear();
  return true;
}

bool ensureSceneTarget(int requestedWidth, int requestedHeight) {
  GLint maxTexture = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
  if (maxTexture <= 0) {
    state.renderError =
        "The graphics driver reported an invalid maximum texture size.";
    return false;
  }
  const float scale =
      std::min({1.f, (float)maxTexture / (float)requestedWidth,
                (float)maxTexture / (float)requestedHeight,
                std::sqrt(4194304.f /
                          (float)((int64_t)requestedWidth * requestedHeight))});
  const int width = std::max(1, (int)std::floor(requestedWidth * scale));
  const int height = std::max(1, (int)std::floor(requestedHeight * scale));
  if (state.sceneFramebuffer && state.sceneColorTexture &&
      state.sceneDepthBuffer && width == state.sceneTargetWidth &&
      height == state.sceneTargetHeight)
    return true;
  if (!state.sceneFramebuffer)
    glGenFramebuffers(1, &state.sceneFramebuffer);
  if (!state.sceneColorTexture)
    glGenTextures(1, &state.sceneColorTexture);
  if (!state.sceneDepthBuffer)
    glGenRenderbuffers(1, &state.sceneDepthBuffer);
  if (!state.sceneFramebuffer || !state.sceneColorTexture ||
      !state.sceneDepthBuffer) {
    state.renderError =
        "Could not allocate the blueprint viewer render target.";
    return false;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, state.sceneFramebuffer);
  glBindTexture(GL_TEXTURE_2D, state.sceneColorTexture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         state.sceneColorTexture, 0);
  glBindRenderbuffer(GL_RENDERBUFFER, state.sceneDepthBuffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, state.sceneDepthBuffer);
  const GLenum drawBuffer = GL_COLOR_ATTACHMENT0;
  glDrawBuffers(1, &drawBuffer);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    char message[96];
    snprintf(message, sizeof(message),
             "Blueprint depth buffer is incomplete (OpenGL status 0x%04X).",
             (unsigned)status);
    state.renderError = message;
    return false;
  }
  state.sceneTargetWidth = width;
  state.sceneTargetHeight = height;
  state.renderError.clear();
  return true;
}

bool drawDepthBufferedScene(const std::vector<SceneBatch> &batches,
                            ImVec2 origin, ImVec2 size, ImDrawList *drawList) {
  if (batches.empty())
    return false;
  while (glGetError() != GL_NO_ERROR) {
  }
  const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
  const int width =
      std::max(1, (int)std::ceil(size.x * std::max(.25f, framebufferScale.x)));
  const int height =
      std::max(1, (int)std::ceil(size.y * std::max(.25f, framebufferScale.y)));
  const GlStateSnapshot saved = captureGlState();
  bool success = false;
  if (ensureSceneRenderer() && ensureSceneTarget(width, height)) {
    glBindFramebuffer(GL_FRAMEBUFFER, state.sceneFramebuffer);
    glViewport(0, 0, state.sceneTargetWidth, state.sceneTargetHeight);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(state.sceneProgram);
    glBindVertexArray(state.sceneVao);
    glBindBuffer(GL_ARRAY_BUFFER, state.sceneVbo);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(state.sceneProgram, "uDiffuse"), 0);
    for (const auto &batch : batches) {
      if (batch.vertices.empty())
        continue;
      glUniform1i(glGetUniformLocation(state.sceneProgram, "uTextured"),
                  batch.texture ? GL_TRUE : GL_FALSE);
      glBindTexture(GL_TEXTURE_2D, batch.texture);
      glBufferData(GL_ARRAY_BUFFER,
                   (GLsizeiptr)(batch.vertices.size() * sizeof(SceneVertex)),
                   batch.vertices.data(), GL_STREAM_DRAW);
      glDrawArrays(GL_TRIANGLES, 0, (GLsizei)batch.vertices.size());
    }
    GLenum drawError = GL_NO_ERROR;
    while (glGetError() != GL_NO_ERROR)
      drawError = GL_INVALID_OPERATION;
    success = drawError == GL_NO_ERROR;
    if (!success)
      state.renderError =
          "OpenGL failed while drawing the depth-buffered blueprint scene.";
  }
  restoreGlState(saved);
  if (success) {
    const ImTextureID texture = (ImTextureID)(intptr_t)state.sceneColorTexture;
    drawList->AddImage(texture, origin,
                       ImVec2(origin.x + size.x, origin.y + size.y),
                       ImVec2(0, 1), ImVec2(1, 0));
  }
  return success;
}

void shutdownSceneRenderer() {
  if (state.sceneDepthBuffer)
    glDeleteRenderbuffers(1, &state.sceneDepthBuffer);
  if (state.sceneFramebuffer)
    glDeleteFramebuffers(1, &state.sceneFramebuffer);
  if (state.sceneColorTexture)
    glDeleteTextures(1, &state.sceneColorTexture);
  if (state.sceneVbo)
    glDeleteBuffers(1, &state.sceneVbo);
  if (state.sceneVao)
    glDeleteVertexArrays(1, &state.sceneVao);
  if (state.sceneProgram)
    glDeleteProgram(state.sceneProgram);
  state.sceneDepthBuffer = state.sceneFramebuffer = state.sceneColorTexture = 0;
  state.sceneVbo = state.sceneVao = state.sceneProgram = 0;
  state.sceneTargetWidth = state.sceneTargetHeight = 0;
}
} // namespace BlueprintViewport::Internal
