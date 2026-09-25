#include "platform/window.h"

#include <cstring>

#include "core/common.h"
#include "gfx/gl.h"

static Window* self(GLFWwindow* w) { return static_cast<Window*>(glfwGetWindowUserPointer(w)); }

bool Window::create(int w, int h, const char* title, bool fullscreen, bool visible) {
    glfwSetErrorCallback([](int code, const char* msg) { logError("GLFW %d: %s", code, msg); });
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 0);  // anti-aliasing is done in our own framebuffers
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    m_winW = w;
    m_winH = h;
    GLFWmonitor* mon = nullptr;
    if (fullscreen) {
        mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        w = mode->width;
        h = mode->height;
    }
    m_win = glfwCreateWindow(w, h, title, mon, nullptr);
    if (!m_win) return false;
    m_fullscreen = fullscreen;
    glfwSetWindowUserPointer(m_win, this);
    glfwMakeContextCurrent(m_win);
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        logError("Failed to load OpenGL functions");
        return false;
    }
    glfwSetKeyCallback(m_win, onKey);
    glfwSetMouseButtonCallback(m_win, onMouseButton);
    glfwSetCursorPosCallback(m_win, onCursor);
    glfwSetScrollCallback(m_win, onScroll);
    glfwSetCharCallback(m_win, onChar);
    glfwSetFramebufferSizeCallback(m_win, onFbSize);
    glfwSetWindowFocusCallback(m_win, onFocus);
    glfwGetFramebufferSize(m_win, &m_fbW, &m_fbH);
    if (glfwRawMouseMotionSupported()) glfwSetInputMode(m_win, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    logInfo("OpenGL %s | %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    return true;
}

void Window::destroy() {
    if (m_win) glfwDestroyWindow(m_win);
    m_win = nullptr;
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(m_win); }
void Window::requestClose() { glfwSetWindowShouldClose(m_win, GLFW_TRUE); }

void Window::beginFrame() {
    std::memcpy(input.prevKeys, input.keys, sizeof(input.keys));
    std::memcpy(input.prevMouse, input.mouse, sizeof(input.mouse));
    input.mouseDelta = {0, 0};
    input.scroll = 0;
    input.text.clear();
    glfwPollEvents();
}

void Window::swap() { glfwSwapBuffers(m_win); }

void Window::setCursorLocked(bool locked) {
    if (locked == m_locked) return;
    m_locked = locked;
    glfwSetInputMode(m_win, GLFW_CURSOR, locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    m_haveLast = false;
}

void Window::setVsync(bool on) { glfwSwapInterval(on ? 1 : 0); }

void Window::setFullscreen(bool on) {
    if (on == m_fullscreen) return;
    if (on) {
        glfwGetWindowPos(m_win, &m_winX, &m_winY);
        glfwGetWindowSize(m_win, &m_winW, &m_winH);
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode = glfwGetVideoMode(mon);
        glfwSetWindowMonitor(m_win, mon, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_win, nullptr, m_winX, m_winY, m_winW, m_winH, 0);
    }
    m_fullscreen = on;
    glfwGetFramebufferSize(m_win, &m_fbW, &m_fbH);
}

double Window::time() const { return glfwGetTime(); }

void Window::onKey(GLFWwindow* w, int key, int, int action, int) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) self(w)->input.keys[key] = true;
    else if (action == GLFW_RELEASE) self(w)->input.keys[key] = false;
}

void Window::onMouseButton(GLFWwindow* w, int button, int action, int) {
    if (button < 0 || button >= 8) return;
    self(w)->input.mouse[button] = action == GLFW_PRESS;
}

void Window::onCursor(GLFWwindow* w, double x, double y) {
    Window* s = self(w);
    int ww, wh;
    glfwGetWindowSize(w, &ww, &wh);
    float sx = ww > 0 ? (float)s->m_fbW / ww : 1.0f, sy = wh > 0 ? (float)s->m_fbH / wh : 1.0f;
    if (s->m_haveLast) s->input.mouseDelta += vec2((float)(x - s->m_lastX), (float)(y - s->m_lastY));
    s->m_lastX = x;
    s->m_lastY = y;
    s->m_haveLast = true;
    s->input.mousePos = {(float)x * sx, (float)y * sy};
}

void Window::onScroll(GLFWwindow* w, double, double dy) { self(w)->input.scroll += (float)dy; }
void Window::onChar(GLFWwindow* w, unsigned int cp) { self(w)->input.text.push_back((char32_t)cp); }
void Window::onFbSize(GLFWwindow* w, int width, int height) {
    self(w)->m_fbW = width;
    self(w)->m_fbH = height;
}
void Window::onFocus(GLFWwindow* w, int f) {
    Window* s = self(w);
    s->focused = f != 0;
    if (!s->focused) {
        std::memset(s->input.keys, 0, sizeof(s->input.keys));
        std::memset(s->input.mouse, 0, sizeof(s->input.mouse));
    }
}
