#include "gfx/glutil.h"

#include <cstring>

#include "core/common.h"
#include "stb_image_write.h"

static GLuint compileStage(GLenum type, const std::string& src, const char* name) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        logError("Shader '%s' (%s) compile failed:\n%s", name, type == GL_VERTEX_SHADER ? "vs" : "fs", log);
    }
    return s;
}

bool Shader::build(const std::string& vs, const std::string& fs, const char* name) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, name);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, name);
    id = glCreateProgram();
    glAttachShader(id, v);
    glAttachShader(id, f);
    glLinkProgram(id);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(id, sizeof(log), nullptr, log);
        logError("Program '%s' link failed:\n%s", name, log);
        return false;
    }
    m_locs.clear();
    return true;
}

void Shader::destroy() {
    if (id) glDeleteProgram(id);
    id = 0;
}

GLint Shader::loc(const char* name) {
    auto it = m_locs.find(name);
    if (it != m_locs.end()) return it->second;
    GLint l = glGetUniformLocation(id, name);
    m_locs.emplace(name, l);
    return l;
}

void GpuMesh::destroy() {
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vao = vbo = ibo = 0;
    count = 0;
}

void GpuMesh::draw() const {
    if (!vao || !count) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

void GpuMesh::drawRange(GLsizei first, GLsizei n) const {
    if (!vao || !n) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, n, GL_UNSIGNED_INT, (const void*)(sizeof(uint32_t) * (size_t)first));
}

GpuMesh buildMesh(const void* verts, size_t vertBytes, size_t stride, const std::vector<VertexAttrib>& attribs,
                  const uint32_t* idx, size_t idxCount, GLenum usage) {
    GpuMesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vertBytes, verts, usage);
    for (size_t i = 0; i < attribs.size(); i++) {
        const auto& a = attribs[i];
        glEnableVertexAttribArray((GLuint)i);
        glVertexAttribPointer((GLuint)i, a.size, a.type, a.normalized ? GL_TRUE : GL_FALSE, (GLsizei)stride,
                              (const void*)a.offset);
    }
    if (idx) {
        glGenBuffers(1, &m.ibo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(idxCount * sizeof(uint32_t)), idx, usage);
    }
    m.count = (GLsizei)idxCount;
    glBindVertexArray(0);
    return m;
}

float maxAnisotropy() {
    static float cached = -1;
    if (cached < 0) {
        cached = 1;
        if (GLAD_GL_EXT_texture_filter_anisotropic || GLAD_GL_ARB_texture_filter_anisotropic)
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &cached);
    }
    return cached;
}

GLuint makeTexture2D(int w, int h, GLenum internal, GLenum format, GLenum type, const void* data, bool mips,
                     GLenum filter, GLenum wrap) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
    if (mips) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter == GL_NEAREST ? GL_NEAREST_MIPMAP_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
    }
    return t;
}

GLuint makeTextureArray(int w, int h, int layers, GLenum internal, GLenum format, GLenum type,
                        const std::vector<const void*>& layerData, float anisotropy) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D_ARRAY, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (GLint)internal, w, h, layers, 0, format, type, nullptr);
    for (int i = 0; i < layers && i < (int)layerData.size(); i++)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, w, h, 1, format, type, layerData[i]);
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (anisotropy > 1.0f && maxAnisotropy() > 1.0f)
        glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY, std::min(anisotropy, maxAnisotropy()));
    return t;
}

bool RenderTarget::create(int w_, int h_, GLenum colorFormat_, bool withDepth, int samples_, bool depthTexture,
                          GLenum filter) {
    destroy();
    w = w_;
    h = h_;
    samples = samples_;
    colorFormat = colorFormat_;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (colorFormat) {
        if (samples > 0) {
            glGenRenderbuffers(1, &color);
            glBindRenderbuffer(GL_RENDERBUFFER, color);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, colorFormat, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
        } else {
            glGenTextures(1, &color);
            glBindTexture(GL_TEXTURE_2D, color);
            GLenum fmt = GL_RGBA, type = GL_UNSIGNED_BYTE;
            if (colorFormat == GL_RGBA16F || colorFormat == GL_R11F_G11F_B10F || colorFormat == GL_RGB16F || colorFormat == GL_RG16F ||
                colorFormat == GL_R16F)
                type = GL_FLOAT;
            if (colorFormat == GL_R8 || colorFormat == GL_R16F) fmt = GL_RED;
            if (colorFormat == GL_RG16F) fmt = GL_RG;
            glTexImage2D(GL_TEXTURE_2D, 0, (GLint)colorFormat, w, h, 0, fmt, type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        }
    } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    if (withDepth) {
        if (depthTexture) {
            glGenTextures(1, &depth);
            glBindTexture(GL_TEXTURE_2D, depth);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border[4] = {1, 1, 1, 1};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        } else {
            glGenRenderbuffers(1, &depth);
            glBindRenderbuffer(GL_RENDERBUFFER, depth);
            if (samples > 0)
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, w, h);
            else
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        }
    }
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        logError("Framebuffer incomplete (0x%x) %dx%d fmt=0x%x samples=%d", st, w, h, colorFormat, samples);
        return false;
    }
    return true;
}

void RenderTarget::destroy() {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    if (color) {
        if (samples > 0) glDeleteRenderbuffers(1, &color);
        else glDeleteTextures(1, &color);
    }
    if (depth) {
        GLint isTex = glIsTexture(depth);
        if (isTex) glDeleteTextures(1, &depth);
        else glDeleteRenderbuffers(1, &depth);
    }
    fbo = color = depth = 0;
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
}

void bindDefaultFramebuffer(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}

bool savePNGFromFramebuffer(const std::string& path, GLuint fbo, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 4), flipped((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    if (fbo == 0) glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; y++) std::memcpy(&flipped[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    int ok = stbi_write_png(path.c_str(), w, h, 4, flipped.data(), w * 4);
    if (ok) logInfo("Saved screenshot %s (%dx%d)", path.c_str(), w, h);
    return ok != 0;
}

void checkGLError(const char* where) {
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR) logError("GL error 0x%x at %s", e, where);
}
