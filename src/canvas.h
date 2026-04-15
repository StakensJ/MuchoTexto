#pragma once                                       // include guard so the compiler only processes this header once per translation unit

// Canvas viewport with pan/zoom. Phase 1 draws objects via ImGui's ImDrawList
// because we don't yet have a real GPU renderer -- that arrives with the
// modifier system in Phase 3. The world <-> screen transform lives here so
// later phases can swap the drawing backend without touching call sites.

#include "imgui.h"                                 // needed for ImVec2, ImFont, and ImDrawList referenced in the declarations below

struct Scene;                                      // forward declaration: tells compiler "a struct called Scene exists" without pulling in scene.h here
struct SceneObject;                                // forward declaration so we can return SceneObject* from pickObject without including scene.h

class Canvas {                                     // defines a class type called Canvas; all members default to private
public:                                            // everything below this label is part of the public interface visible to other code
    Canvas() = default;                            // explicitly request the compiler-generated default constructor (no custom init logic needed)

    // Draws the canvas into the current ImGui window. `canvas_font` is the
    // large-rasterized font used for scene text; pass nullptr to fall back to
    // ImGui's default.
    void draw(Scene& scene, ImFont* canvas_font);  // the main per-frame entry: handle input and render everything inside the current ImGui window

    void resetView();                              // reset pan offset to zero and zoom back to 100%

    ImVec2 worldToScreen(ImVec2 world) const;      // convert a point in world coordinates to screen pixels; const = promises not to modify *this
    ImVec2 screenToWorld(ImVec2 screen) const;     // the inverse transform, used for mouse picking and coordinate display

    float  getZoom()   const { return zoom_; }     // tiny inline getter exposing current zoom; defined in the header so the compiler can inline it
    ImVec2 getOffset() const { return offset_; }   // tiny inline getter exposing current pan offset

private:                                           // everything below this label is only accessible from inside Canvas's own member functions
    ImVec2 offset_          = {0.0f, 0.0f};        // pan offset in screen pixels; trailing underscore is our convention for private members
    float  zoom_            = 1.0f;                // zoom multiplier; 1.0 means one world unit equals one screen pixel
    ImVec2 canvas_origin_   = {0.0f, 0.0f};        // top-left of the canvas area in absolute screen coordinates, recomputed each frame
    bool   dragging_object_ = false;               // true while the user is mid-drag on a selected object with the left mouse button

    void handleInput(Scene& scene, ImVec2 canvas_pos, ImVec2 canvas_size, ImFont* canvas_font);  // process mouse / keyboard for pan, zoom, select, drag, delete
    void drawGrid(ImDrawList* dl, ImVec2 canvas_pos, ImVec2 canvas_size);   // private helper: emit the background grid and origin axes
    void drawObjects(ImDrawList* dl, Scene& scene, ImFont* canvas_font);    // private helper: iterate scene.objects and draw each visible one
    SceneObject* pickObject(Scene& scene, ImVec2 screen_pos, ImFont* canvas_font);  // hit-test: return the top-most object under the given screen pixel, or nullptr
};                                                 // end class Canvas; semicolon required
