// Thin helpers over raw OpenGL objects.
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "gfx/gl.h"

class Shader {
public:
    GLuint id = 0;
    bool build(const std::string& vs, const std::string& fs, const char* name);
    void destroy();
    void use() const { glUseProgram(id); }
    GLint loc(const char* name);
    void set(const char* n, int v) { glUniform1i(loc(n), v); }
    void set(const char* n, float v) { glUniform1f(loc(n), v); }
    void set(const char* n, vec2 v) { glUniform2f(loc(n), v.x, v.y); }
    void set(const char* n, vec3 v) { glUniform3f(loc(n), v.x, v.y, v.z); }
    void set(const char* n, vec4 v) { glUniform4f(loc(n), v.x, v.y, v.z, v.w); }
    void set(const char* n, const mat4& m) { glUniformMatrix4fv(loc(n), 1, GL_FALSE, m.m); }
    void setMats(const char* n, const mat4* m, int count) { glUniformMatrix4fv(loc(n), count, GL_FALSE, m->m); }
    void setVec3s(const char* n, const vec3* v, int count) { glUniform3fv(loc(n), count, &v->x); }
    void setVec4s(const char* n, const vec4* v, int count) { glUniform4fv(loc(n), count, &v->x); }

private:
    std::unordered_map<std::string, GLint> m_locs;
};

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei count = 0;
    void destroy();
    void draw() const;
    void drawRange(GLsizei first, GLsizei n) const;
};

// Describes one float/normalized vertex attribute for buildMesh.
struct VertexAttrib {
    GLint size;
    GLenum type;
    bool normalized;
    size_t offset;
};
GpuMesh buildMesh(const void* verts, size_t vertBytes, size_t stride, const std::vector<VertexAttrib>& attribs,
                  const uint32_t* idx, size_t idxCount, GLenum usage = GL_STATIC_DRAW);

GLuint makeTexture2D(int w, int h, GLenum internal, GLenum format, GLenum type, const void* data, bool mips,
                     GLenum filter, GLenum wrap);
GLuint makeTextureArray(int w, int h, int layers, GLenum internal, GLenum format, GLenum type,
                        const std::vector<const void*>& layerData, float anisotropy);
float maxAnisotropy();

struct RenderTarget {
    GLuint fbo = 0, color = 0, depth = 0;
    int w = 0, h = 0, samples = 0;
    GLenum colorFormat = 0;
    // colorFormat 0 = no color; depthTexture true = sampleable depth texture instead of renderbuffer.
    bool create(int w, int h, GLenum colorFormat, bool withDepth, int samples = 0, bool depthTexture = false,
                GLenum filter = GL_LINEAR);
    void destroy();
    void bind() const;
};

void bindDefaultFramebuffer(int w, int h);
bool savePNGFromFramebuffer(const std::string& path, GLuint fbo, int w, int h);
void checkGLError(const char* where);
