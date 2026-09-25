#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>

#include "core/math.h"

struct Input {
    bool keys[GLFW_KEY_LAST + 1] = {};
    bool prevKeys[GLFW_KEY_LAST + 1] = {};
    bool mouse[8] = {};
    bool prevMouse[8] = {};
    vec2 mousePos;    // framebuffer pixels, origin top-left
    vec2 mouseDelta;  // raw motion accumulated this frame
    float scroll = 0;
    std::u32string text;

    bool down(int k) const { return k >= 0 && k <= GLFW_KEY_LAST && keys[k]; }
    bool pressed(int k) const { return down(k) && !prevKeys[k]; }
    bool released(int k) const { return k >= 0 && k <= GLFW_KEY_LAST && !keys[k] && prevKeys[k]; }
    bool mouseDown(int b) const { return mouse[b]; }
    bool mousePressed(int b) const { return mouse[b] && !prevMouse[b]; }
    bool mouseReleased(int b) const { return !mouse[b] && prevMouse[b]; }
};

class Window {
public:
    bool create(int w, int h, const char* title, bool fullscreen, bool visible = true);
    void destroy();
    bool shouldClose() const;
    void requestClose();
    void beginFrame();
    void swap();
    void setCursorLocked(bool locked);
    bool cursorLocked() const { return m_locked; }
    void setVsync(bool on);
    void setFullscreen(bool on);
    bool fullscreen() const { return m_fullscreen; }
    int width() const { return m_fbW; }
    int height() const { return m_fbH; }
    double time() const;
    GLFWwindow* handle() const { return m_win; }

    Input input;
    bool focused = true;

private:
    static void onKey(GLFWwindow* w, int key, int sc, int action, int mods);
    static void onMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void onCursor(GLFWwindow* w, double x, double y);
    static void onScroll(GLFWwindow* w, double dx, double dy);
    static void onChar(GLFWwindow* w, unsigned int cp);
    static void onFbSize(GLFWwindow* w, int width, int height);
    static void onFocus(GLFWwindow* w, int focused);

    GLFWwindow* m_win = nullptr;
    int m_fbW = 0, m_fbH = 0;
    bool m_locked = false, m_fullscreen = false;
    int m_winX = 80, m_winY = 80, m_winW = 1600, m_winH = 900;
    double m_lastX = 0, m_lastY = 0;
    bool m_haveLast = false;
};
