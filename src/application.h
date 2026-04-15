#pragma once                                  // include guard so this header is processed at most once per translation unit

// Application orchestration: owns the GLFW window, the ImGui context, the
// scene graph, and the canvas. Entry point is init() -> run() -> shutdown().

#include "canvas.h"                           // we hold a Canvas by value, so the compiler needs its full definition here
#include "scene.h"                            // we hold a Scene by value, same reason -- full definition required

struct GLFWwindow;                            // forward declaration so we don't have to drag <GLFW/glfw3.h> into this header (that header is heavy)
struct ImFont;                                // forward declaration so we don't have to drag <imgui.h> into this header either

class Application {                           // application-level class that owns all per-program state
public:                                       // public interface used exclusively by main.cpp
    bool init(const char* title, int width, int height);  // create the window, initialize GL + ImGui, load fonts; returns false on any failure
    void run();                               // enter the main loop; returns when the user closes the window
    void shutdown();                          // tear down in reverse order of creation; safe to call even after a failed init

private:                                      // internal state and helper methods -- not visible outside this class
    GLFWwindow* window_      = nullptr;       // owning pointer to the GLFW window handle; nullptr until init() succeeds
    Scene       scene_;                       // the scene graph (pure data); default-constructs empty
    Canvas      canvas_;                      // the viewport state (pan/zoom); default-constructs at 100% zoom, 0 pan
    ImFont*     canvas_font_ = nullptr; // large-rasterized font for scene text   // non-owning pointer; ImGui's font atlas owns the font itself
    bool        dock_layout_built_ = false;   // sentinel so we run DockBuilder setup exactly once on the first frame

    void setupStyle();                        // configure ImGui colors and spacing to match our dark theme
    void loadFonts();                         // register UI + canvas fonts with ImGui's font atlas, with platform fallbacks
    void initDefaultScene();                  // seed the scene with one sample text object so there's something to look at

    void beginFrame();                        // per-frame boilerplate: ImGui + GLFW NewFrame calls
    void endFrame();                          // per-frame boilerplate: render ImGui data, swap OpenGL buffers

    void drawDockspace();                     // full-viewport docking host window plus the menu bar
    void drawMenuBar();                       // File / View / Help menus
    void drawCanvasPanel();                   // the "Canvas" dock tab that hosts the Canvas viewport
    void drawLayerPanel();                    // the "Layers" dock tab listing scene objects with select / visibility / delete controls
    void drawPropertiesPanel();               // the "Properties" dock tab that edits the selected object's fields
};                                            // end class Application; semicolon required
