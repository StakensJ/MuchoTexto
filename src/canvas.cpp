#include "canvas.h"                                                           // pull in our own class declaration so the definitions below are checked against it
#include "scene.h"                                                            // we iterate Scene and read SceneObject fields, so we need the full definitions

#include <algorithm>                                                          // provides std::clamp used to keep the zoom within a sane range
#include <cmath>                                                              // provides std::fmod used to align the grid to the pan offset

void Canvas::draw(Scene& scene, ImFont* canvas_font) {                        // main per-frame entry; called from Application::drawCanvasPanel inside an ImGui window
    const ImVec2 canvas_pos  = ImGui::GetCursorScreenPos();                   // top-left of the available drawing area in absolute screen pixels
    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();                // width and height remaining in the current ImGui window
    if (canvas_size.x <= 0.0f || canvas_size.y <= 0.0f) return;               // bail early if the window is collapsed or zero-sized -- nothing to draw

    canvas_origin_ = canvas_pos;                                              // cache origin so worldToScreen / screenToWorld know where the canvas starts this frame

    // Invisible button captures all input over the canvas area.
    ImGui::InvisibleButton(                                                    // create a clickable area that has no visual but reserves space and accepts input
        "##canvas_input", canvas_size,                                         // "##" prefix tells ImGui this is an internal ID, not a visible label
        ImGuiButtonFlags_MouseButtonLeft  |                                    // the button should react to left clicks (selection in a future phase)
        ImGuiButtonFlags_MouseButtonMiddle|                                    // it should react to middle clicks (panning)
        ImGuiButtonFlags_MouseButtonRight);                                    // and right clicks (also panning, for users without a scroll-wheel button)

    handleInput(scene, canvas_pos, canvas_size, canvas_font);                 // now that the InvisibleButton is the active item, process select/drag/pan/zoom input

    ImDrawList* dl = ImGui::GetWindowDrawList();                              // fetch the per-window draw list -- a command buffer we can append rects/lines/text to
    const ImVec2 canvas_max(canvas_pos.x + canvas_size.x,                     // compute the bottom-right corner of the canvas (x component)
                            canvas_pos.y + canvas_size.y);                    // compute the bottom-right corner of the canvas (y component)

    dl->PushClipRect(canvas_pos, canvas_max, true);                           // clip all subsequent draw commands to the canvas bounds; true = intersect with existing clip
    dl->AddRectFilled(canvas_pos, canvas_max, IM_COL32(28, 28, 32, 255));     // fill the whole canvas with the dark background color; IM_COL32 packs RGBA into uint32

    drawGrid(dl, canvas_pos, canvas_size);                                    // overlay the faint grid lines and origin axes on top of the background
    drawObjects(dl, scene, canvas_font);                                      // iterate the scene and draw each visible object

    // Border
    dl->AddRect(canvas_pos, canvas_max, IM_COL32(60, 60, 70, 255));           // thin outline around the canvas so it's visually framed within the docking area
    dl->PopClipRect();                                                        // restore the previous clip rect so later ImGui widgets aren't clipped to our bounds
}                                                                             // end Canvas::draw

void Canvas::resetView() {                     // public method exposed via the menu bar "View > Reset Canvas View"
    offset_ = {0.0f, 0.0f};                    // zero out the pan offset so world origin lands at the top-left of the canvas
    zoom_   = 1.0f;                            // restore zoom to 100%
}                                              // end resetView

void Canvas::handleInput(Scene& scene, ImVec2 canvas_pos, ImVec2 /*canvas_size*/, ImFont* canvas_font) {  // scene/font now needed for hit-test + drag; canvas_size still unused
    const bool hovered = ImGui::IsItemHovered();                              // true if the mouse is currently over the InvisibleButton we just created
    const bool active  = ImGui::IsItemActive();                               // true if the user is currently holding a mouse button down over it
    ImGuiIO& io        = ImGui::GetIO();                                      // grab the I/O state once; we read mouse/key/flags from here several times

    // Left click: hit-test under the cursor. Hit -> select + begin drag. Miss -> deselect.
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {                        // true exactly on the frame the left button goes down while hovering the canvas
        SceneObject* hit = pickObject(scene, io.MousePos, canvas_font);       // walk objects back-to-front; returns the top-most one under the cursor or nullptr
        if (hit) {                                                            // clicked on something
            scene.selected_id = hit->id;                                      // make it the new selection (layer panel + properties panel follow this)
            dragging_object_  = true;                                         // remember that this left-press was on an object so drags move it instead of panning
        } else {                                                              // clicked empty space
            scene.selected_id = 0;                                            // deselect whatever was selected (0 = no object)
            dragging_object_  = false;                                        // left drags from empty space do nothing in Phase 2
        }                                                                     // end hit/miss branch
    }                                                                         // end left-click branch

    // Left drag while dragging an object: translate it in world space.
    if (dragging_object_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {  // only move if the press started on an object
        if (SceneObject* sel = scene.findById(scene.selected_id)) {           // pointer may be stale if something deleted it; re-look-up each frame
            sel->position.x += io.MouseDelta.x / zoom_;                        // convert pixel delta back to world units by dividing by zoom
            sel->position.y += io.MouseDelta.y / zoom_;                        // same for y
        }                                                                     // end selection-exists branch
    }                                                                         // end object-drag branch
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {                      // mouse went up -- end whatever drag was in progress
        dragging_object_ = false;                                             // reset so a future empty-space click doesn't inherit drag state
    }                                                                         // end release branch

    // Delete key: remove the current selection. Gated on canvas hover + no text input so it can't fire while typing in a field.
    if (hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete) && scene.selected_id != 0) {  // only when it's safe
        scene.deleteObject(scene.selected_id);                                // remove from the vector
        scene.selected_id = 0;                                                // clear selection so the properties panel goes blank
        dragging_object_  = false;                                            // and abort any in-progress drag (the target is gone)
    }                                                                         // end delete-key branch

    // Pan: middle or right mouse drag.
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||   // start panning only if the button is active AND a middle-button drag is in progress...
                   ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {   // ...or a right-button drag; 0.0f threshold means any pixel of motion counts
        offset_.x += io.MouseDelta.x;                                         // shift horizontal pan by the same amount so content follows the cursor
        offset_.y += io.MouseDelta.y;                                         // shift vertical pan by the same amount
    }                                                                         // end pan branch

    // Zoom: scroll wheel, centered on mouse position.
    if (hovered) {                                                            // only zoom when the cursor is over the canvas -- don't steal scroll from other widgets
        const float wheel = io.MouseWheel;                                    // wheel delta for this frame; positive = up/in, negative = down/out, 0 = no motion
        if (wheel != 0.0f) {                                                  // skip the math entirely if nothing scrolled
            const ImVec2 mouse_rel(io.MousePos.x - canvas_pos.x,              // mouse position relative to the canvas top-left (x component)
                                   io.MousePos.y - canvas_pos.y);             // mouse position relative to the canvas top-left (y component)

            const float old_zoom    = zoom_;                                  // save the pre-change zoom so the cursor-anchor math below is correct
            const float zoom_factor = (wheel > 0.0f) ? 1.1f : (1.0f / 1.1f);  // 10% step per wheel click: multiply by 1.1 zooming in, divide by 1.1 zooming out
            zoom_ = std::clamp(zoom_ * zoom_factor, 0.05f, 50.0f);            // apply the factor and clamp to [5%, 5000%] so users can't get hopelessly lost

            // Keep the world point under the cursor fixed while zooming.
            const float ratio = zoom_ / old_zoom;                             // actual zoom ratio achieved (may differ from zoom_factor if we hit the clamp)
            offset_.x = mouse_rel.x - (mouse_rel.x - offset_.x) * ratio;      // solve for the pan offset that keeps the world point under the cursor stationary (x)
            offset_.y = mouse_rel.y - (mouse_rel.y - offset_.y) * ratio;      // same equation for y
        }                                                                     // end wheel-moved branch
    }                                                                         // end hovered branch
}                                                                             // end handleInput

SceneObject* Canvas::pickObject(Scene& scene, ImVec2 screen_pos, ImFont* canvas_font) {  // returns the top-most visible object whose bounds contain screen_pos
    const ImVec2 world = screenToWorld(screen_pos);                           // transform the screen pixel into world coordinates once up front
    ImFont* font = canvas_font ? canvas_font : ImGui::GetFont();              // use the canvas font for measurement, falling back to the default UI font

    // Walk back-to-front so the visually top-most object wins when they overlap.
    for (auto it = scene.objects.rbegin(); it != scene.objects.rend(); ++it) {  // reverse iterator: last drawn = first tested
        SceneObject& obj = *it;                                                // reference to the current object
        if (!obj.visible) continue;                                            // skip hidden objects -- they can't be clicked
        if (obj.type != ObjectType::Text) continue;                            // Phase 2 only hit-tests text objects
        const ImVec2 sz = font->CalcTextSizeA(obj.font_size, FLT_MAX, -1.0f, obj.content.c_str());  // text bounds in world units (size = requested font size)
        if (world.x >= obj.position.x && world.x <= obj.position.x + sz.x &&   // x inside [left, right] of the text box
            world.y >= obj.position.y && world.y <= obj.position.y + sz.y) {   // and y inside [top, bottom]
            return &obj;                                                       // hit: return a pointer into the vector (valid until the vector mutates)
        }                                                                      // end bounds check
    }                                                                          // end reverse iteration
    return nullptr;                                                            // no object under the cursor
}                                                                              // end pickObject

void Canvas::drawGrid(ImDrawList* dl, ImVec2 canvas_pos, ImVec2 canvas_size) {  // render the faint background grid plus world-axis overlays
    const float grid_step = 50.0f * zoom_;                                    // distance in screen pixels between adjacent grid lines (50 world units, zoomed)
    if (grid_step < 6.0f) return;                                             // when zoomed way out the lines would be too dense -- skip to avoid a noisy look

    const ImU32 grid_color = IM_COL32(50, 50, 58, 255);                       // slightly lighter than the background -- subtle grid lines
    const ImU32 axis_color = IM_COL32(110, 110, 125, 255);                    // noticeably brighter so the world origin axes stand out

    float start_x = std::fmod(offset_.x, grid_step);                          // align the first vertical line to the current pan offset (mod grid step)
    if (start_x < 0.0f) start_x += grid_step;                                 // fmod can return negative for negative offset_.x; normalize to [0, grid_step)
    float start_y = std::fmod(offset_.y, grid_step);                          // same idea for the first horizontal line's y coordinate
    if (start_y < 0.0f) start_y += grid_step;                                 // same normalization for y

    for (float x = start_x; x < canvas_size.x; x += grid_step) {              // sweep across the canvas stepping by grid_step, drawing vertical lines
        dl->AddLine(                                                          // emit a line segment into the draw list
            ImVec2(canvas_pos.x + x, canvas_pos.y),                           // top endpoint: at the canvas top, offset x from the canvas left edge
            ImVec2(canvas_pos.x + x, canvas_pos.y + canvas_size.y),           // bottom endpoint: at the canvas bottom, same x coordinate
            grid_color);                                                      // use the faint grid color
    }                                                                         // end vertical-lines loop
    for (float y = start_y; y < canvas_size.y; y += grid_step) {              // sweep vertically drawing horizontal lines
        dl->AddLine(                                                          // emit another line segment
            ImVec2(canvas_pos.x,                 canvas_pos.y + y),           // left endpoint: at the canvas left, offset y from the canvas top
            ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + y),           // right endpoint: at the canvas right, same y coordinate
            grid_color);                                                      // same faint color
    }                                                                         // end horizontal-lines loop

    // Origin axes (world 0,0).
    const ImVec2 origin = worldToScreen(ImVec2(0.0f, 0.0f));                  // where does the world origin land on screen given the current pan/zoom?
    if (origin.x >= canvas_pos.x && origin.x <= canvas_pos.x + canvas_size.x) {  // only draw the y-axis line if the origin's x is visible on screen
        dl->AddLine(                                                          // vertical y-axis line
            ImVec2(origin.x, canvas_pos.y),                                   // top endpoint at the origin's x, canvas top
            ImVec2(origin.x, canvas_pos.y + canvas_size.y),                   // bottom endpoint at the origin's x, canvas bottom
            axis_color, 1.5f);                                                // brighter color and slightly thicker (1.5px) so it stands out from the grid
    }                                                                         // end y-axis visibility check
    if (origin.y >= canvas_pos.y && origin.y <= canvas_pos.y + canvas_size.y) {  // only draw the x-axis line if the origin's y is visible on screen
        dl->AddLine(                                                          // horizontal x-axis line
            ImVec2(canvas_pos.x,                 origin.y),                   // left endpoint at canvas left, origin's y
            ImVec2(canvas_pos.x + canvas_size.x, origin.y),                   // right endpoint at canvas right, same y
            axis_color, 1.5f);                                                // same styling as the y-axis
    }                                                                         // end x-axis visibility check
}                                                                             // end drawGrid

void Canvas::drawObjects(ImDrawList* dl, Scene& scene, ImFont* canvas_font) {  // iterate the scene and draw each visible object into the supplied draw list
    ImFont* font = canvas_font ? canvas_font : ImGui::GetFont();              // prefer the large canvas font if we have one, otherwise fall back to ImGui's current UI font
    for (const SceneObject& obj : scene.objects) {                            // range-based for loop over all objects; const reference means no copy and no mutation
        if (!obj.visible) continue;                                           // skip objects the user has hidden
        if (obj.type != ObjectType::Text) continue;                           // Phase 1 only renders text objects; skip anything else

        const ImVec2 screen_pos    = worldToScreen(obj.position);             // transform this object's world position into screen pixels for the draw call
        const float  rendered_size = obj.font_size * zoom_;                   // scale the font size by the current zoom so text tracks the viewport

        // Cull objects that would be sub-pixel.
        if (rendered_size < 2.0f) continue;                                   // don't bother rasterizing text smaller than 2 pixels tall -- it'd be unreadable anyway

        const ImVec4 col4(obj.color.x,                                        // build the final RGBA color starting with the object's red channel
                          obj.color.y,                                        // ...green channel
                          obj.color.z,                                        // ...blue channel
                          obj.color.w * obj.opacity);                         // alpha = base alpha times the global object opacity slider
        const ImU32 col = ImGui::ColorConvertFloat4ToU32(col4);               // pack the four floats into a 32-bit color value that ImDrawList wants

        dl->AddText(font, rendered_size, screen_pos, col, obj.content.c_str());  // finally emit the text draw command; c_str() hands ImGui a null-terminated char pointer

        // Selection outline: drawn after the text so it sits on top.
        if (obj.id == scene.selected_id) {                                    // only the currently-selected object gets a box around it
            const ImVec2 world_size = font->CalcTextSizeA(obj.font_size, FLT_MAX, -1.0f, obj.content.c_str());  // bounds in world units -- matches pickObject
            const ImVec2 p0 = screen_pos;                                     // top-left of the text box in screen space
            const ImVec2 p1(screen_pos.x + world_size.x * zoom_,              // bottom-right x = top-left + (world width scaled by zoom)
                            screen_pos.y + world_size.y * zoom_);             // bottom-right y = top-left + (world height scaled by zoom)
            const ImU32 sel_color = IM_COL32(255, 160, 40, 255);              // warm orange so the outline pops against the dark background
            dl->AddRect(p0, p1, sel_color, 2.0f, 0, 1.5f);                    // rounded (2px) 1.5px-thick rectangle around the text bounds
            const float r = 3.5f;                                             // half-size of each corner handle square
            auto handle = [&](ImVec2 c) {                                     // tiny lambda to emit a filled square centered on a point
                dl->AddRectFilled(ImVec2(c.x - r, c.y - r),                   // square top-left
                                  ImVec2(c.x + r, c.y + r),                   // square bottom-right
                                  sel_color);                                  // same color as the outline for visual consistency
            };                                                                // end handle lambda
            handle(p0);                                                       // top-left corner handle
            handle(ImVec2(p1.x, p0.y));                                       // top-right corner handle
            handle(ImVec2(p0.x, p1.y));                                       // bottom-left corner handle
            handle(p1);                                                       // bottom-right corner handle
        }                                                                     // end selection branch
    }                                                                         // end for-each-object loop
}                                                                             // end drawObjects

ImVec2 Canvas::worldToScreen(ImVec2 world) const {                            // forward transform: world coordinates -> absolute screen pixels
    return ImVec2(                                                            // construct and return the result ImVec2 in place
        canvas_origin_.x + offset_.x + world.x * zoom_,                       // screen x = canvas left edge + pan offset + (world x scaled by zoom)
        canvas_origin_.y + offset_.y + world.y * zoom_);                      // screen y = canvas top edge  + pan offset + (world y scaled by zoom)
}                                                                             // end worldToScreen

ImVec2 Canvas::screenToWorld(ImVec2 screen) const {                           // inverse transform: screen pixels -> world coordinates
    return ImVec2(                                                            // construct and return the result
        (screen.x - canvas_origin_.x - offset_.x) / zoom_,                    // world x = (screen x minus canvas origin and pan offset) divided by zoom
        (screen.y - canvas_origin_.y - offset_.y) / zoom_);                   // world y = (screen y minus canvas origin and pan offset) divided by zoom
}                                                                             // end screenToWorld
