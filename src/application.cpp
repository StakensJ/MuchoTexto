#include "application.h"                                              // pull in our own class declaration so the definitions below are checked against it

#include "imgui.h"                                                    // main Dear ImGui header -- ImGui::Begin, widgets, io, etc.
#include "imgui_internal.h"         // DockBuilder API                // ImGui internals where DockBuilderSplitNode, DockBuilderDockWindow etc. live
#include "imgui_impl_glfw.h"                                          // GLFW platform backend (input, window events)
#include "imgui_impl_opengl3.h"                                       // OpenGL 3 renderer backend (actually draws ImGui via GL)

#include <GLFW/glfw3.h>                                               // GLFW windowing/context/input API; on Windows this also pulls in <GL/gl.h>

#include <cstdio>                                                     // std::fprintf, std::fopen, std::fclose used for error reporting and file probing
#include <cstring>                                                    // std::strncpy / related C string functions (kept for historical reasons)
#include <utility>                                                    // std::swap used by drawLayerPanel for drag-to-reorder

namespace {                                                           // anonymous namespace: everything inside has internal linkage (private to this .cpp)

void glfw_error_callback(int error, const char* description) {        // GLFW calls this whenever it reports an error
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);  // forward the error code + message to standard error so it's visible in the console
}                                                                     // end glfw_error_callback

// Pick the first existing font file from a list of candidate paths. Lets us
// prefer a nice system font but gracefully degrade on machines that don't
// have it.
ImFont* addFontWithFallback(ImFontAtlas* atlas,                       // ImGui's font atlas we're adding to
                            const char* const* paths,                 // array of candidate .ttf file paths
                            int path_count,                           // how many entries are in the paths array
                            float size)                               // the pixel size to rasterize the font at
{                                                                     // function body opens on its own line per the existing style
    for (int i = 0; i < path_count; ++i) {                            // try each candidate in order until one works
        if (FILE* f = std::fopen(paths[i], "rb")) {                   // probe the file: try to open it read-binary; non-null means it exists and is readable
            std::fclose(f);                                           // close the probe handle immediately -- we only wanted to know if it existed
            if (ImFont* font = atlas->AddFontFromFileTTF(paths[i], size)) {  // hand the path to ImGui which will actually load and rasterize it
                return font;                                          // success -- return the loaded font to the caller
            }                                                         // end AddFontFromFileTTF-success branch
        }                                                             // end file-exists branch
    }                                                                 // end candidates loop
    return nullptr;                                                   // none of the candidates worked -- let the caller use its own fallback
}                                                                     // end addFontWithFallback

} // namespace                                                        // close the anonymous namespace

bool Application::init(const char* title, int width, int height) {   // set up window + ImGui; returns false on any failure
    glfwSetErrorCallback(glfw_error_callback);                        // register our error callback before we touch any other GLFW function
    if (!glfwInit()) {                                                // initialize the GLFW library; must be called before creating windows
        std::fprintf(stderr, "glfwInit failed\n");                    // report the failure on stderr
        return false;                                                 // abort init -- caller will exit with non-zero status
    }                                                                 // end glfwInit-failure branch

    // OpenGL 3.3 Core context.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);                    // ask for OpenGL major version 3
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);                    // ask for OpenGL minor version 3 (so: GL 3.3)
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);  // request the core profile (no deprecated fixed-function stuff)
#ifdef __APPLE__                                                      // on macOS only...
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);              // macOS requires forward-compat hint to actually get a 3.3 core context
#endif                                                                // end macOS-specific section

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);  // create the window + matching GL context; nullptrs = no monitor (windowed), no sharing
    if (!window_) {                                                   // null return value means GLFW couldn't create the window
        std::fprintf(stderr, "glfwCreateWindow failed\n");            // report failure
        glfwTerminate();                                              // we called glfwInit, so we must call glfwTerminate before bailing out
        return false;                                                 // abort init
    }                                                                 // end window-creation-failure branch
    glfwMakeContextCurrent(window_);                                  // make this window's GL context current on the calling thread
    glfwSwapInterval(1); // vsync                                     // enable vsync: swap buffers in sync with the monitor refresh (prevents tearing)

    // Dear ImGui setup.
    IMGUI_CHECKVERSION();                                             // asserts that the imgui.h we compiled against matches the runtime library version
    ImGui::CreateContext();                                           // allocate the global ImGui context struct; must happen before any other ImGui:: call
    ImGuiIO& io = ImGui::GetIO();                                     // grab a reference to the ImGui I/O struct where config flags and input state live
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;                 // enable docking support (this is why we use the docking branch of ImGui)
    io.IniFilename = "parametric_design.ini";                         // customize where ImGui persists its layout/window positions between runs

    setupStyle();                                                     // apply our dark color theme and spacing tweaks
    loadFonts();                                                      // register UI + canvas fonts with the atlas (before initializing the GL backend)

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {               // hook ImGui's GLFW backend into our window; 'true' = install callbacks automatically
        std::fprintf(stderr, "ImGui_ImplGlfw_InitForOpenGL failed\n");  // report failure
        return false;                                                 // bail out
    }                                                                 // end GLFW-backend-failure branch
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {                    // hook up the OpenGL 3 renderer backend, with GLSL 330 as the shader version string
        std::fprintf(stderr, "ImGui_ImplOpenGL3_Init failed\n");      // report failure
        return false;                                                 // bail out
    }                                                                 // end GL-backend-failure branch

    initDefaultScene();                                               // seed the scene with one sample text object so the canvas isn't blank
    return true;                                                      // init succeeded
}                                                                     // end Application::init

void Application::run() {                                             // main loop; blocks until the window should close
    while (!glfwWindowShouldClose(window_)) {                         // loop while the user hasn't requested a close (clicked X, Alt+F4, etc.)
        glfwPollEvents();                                             // process pending OS events: keyboard, mouse, window resize, close request, etc.

        beginFrame();                                                 // begin a new ImGui frame (and matching backend frames)

        drawDockspace();                                              // draw the full-viewport docking host and the menu bar
        drawCanvasPanel();                                            // draw the Canvas tab (the viewport itself)
        drawLayerPanel();                                             // draw the Layers tab (scene object list)
        drawPropertiesPanel();                                        // draw the Properties tab (edits object fields)

        endFrame();                                                   // render ImGui data to the GL framebuffer and swap buffers
    }                                                                 // end main loop
}                                                                     // end Application::run

void Application::shutdown() {                                        // tear down everything in reverse order of creation
    if (window_) {                                                    // only clean up ImGui/GL state if we actually got as far as creating a window
        ImGui_ImplOpenGL3_Shutdown();                                 // shut down the OpenGL renderer backend first (it depends on the GL context)
        ImGui_ImplGlfw_Shutdown();                                    // then the GLFW platform backend
        ImGui::DestroyContext();                                      // destroy the global ImGui context
        glfwDestroyWindow(window_);                                   // destroy the window (also destroys its GL context)
        window_ = nullptr;                                            // null out the pointer so a subsequent shutdown() call is a no-op
    }                                                                 // end window-existed branch
    glfwTerminate();                                                  // finally release all GLFW resources; pairs with glfwInit() in init()
}                                                                     // end Application::shutdown

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------

void Application::beginFrame() {                                      // per-frame setup: tell every layer that a new frame has started
    ImGui_ImplOpenGL3_NewFrame();                                     // GL backend: mark start of frame (updates any dirty GL state)
    ImGui_ImplGlfw_NewFrame();                                        // GLFW backend: push fresh input (mouse pos, keys, window size) into ImGui
    ImGui::NewFrame();                                                // ImGui core: reset per-frame state; after this we can call Begin/End and widgets
}                                                                     // end beginFrame

void Application::endFrame() {                                        // per-frame teardown: render ImGui and swap the window's buffers
    ImGui::Render();                                                  // finalize ImGui's draw data for this frame into its internal draw-data structure

    int display_w = 0, display_h = 0;                                 // variables to receive the current framebuffer size in pixels
    glfwGetFramebufferSize(window_, &display_w, &display_h);          // ask GLFW for the framebuffer size (may differ from window size on HiDPI displays)
    glViewport(0, 0, display_w, display_h);                           // tell OpenGL which region of the framebuffer we're drawing to
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);                          // set the clear color to a very dark gray (RGBA, each in [0,1])
    glClear(GL_COLOR_BUFFER_BIT);                                     // clear the color buffer to the above color, wiping the previous frame's image

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());           // hand the ImGui draw data to the GL backend, which issues real GL draw calls

    glfwSwapBuffers(window_);                                         // present the back buffer to the screen (vsync waits here if enabled)
}                                                                     // end endFrame

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void Application::drawDockspace() {                                   // set up the full-viewport docking host window
    const ImGuiViewport* viewport = ImGui::GetMainViewport();         // grab the main viewport (covers the OS window's client area)
    ImGui::SetNextWindowPos(viewport->WorkPos);                       // position the next window at the viewport's working area origin
    ImGui::SetNextWindowSize(viewport->WorkSize);                     // size the next window to fill the viewport's working area
    ImGui::SetNextWindowViewport(viewport->ID);                       // associate the next window with this viewport (needed for multi-viewport docking)

    const ImGuiWindowFlags flags =                                    // compose the set of window flags for the docking host
        ImGuiWindowFlags_NoDocking            |                       // the host itself cannot be docked into another host
        ImGuiWindowFlags_NoTitleBar           |                       // hide the host's title bar (we want it invisible)
        ImGuiWindowFlags_NoCollapse           |                       // can't collapse the host window
        ImGuiWindowFlags_NoResize             |                       // the host fills the viewport; don't let the user resize it
        ImGuiWindowFlags_NoMove               |                       // the host is pinned to the viewport; don't let it be dragged
        ImGuiWindowFlags_NoBringToFrontOnFocus|                       // don't focus-raise this window over its docked children
        ImGuiWindowFlags_NoNavFocus           |                       // exclude from keyboard-nav focus chain
        ImGuiWindowFlags_MenuBar              |                       // reserve space for a menu bar (we'll draw it inside)
        ImGuiWindowFlags_NoBackground;                                // transparent background so docked children show through directly

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);        // zero rounding for the host so its corners align with the OS window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);        // no border on the host
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));  // zero padding so the dockspace reaches the edges
    ImGui::Begin("##DockspaceHost", nullptr, flags);                  // begin the host window; "##" prefix hides the label; no p_open; use the flags we built
    ImGui::PopStyleVar(3);                                            // pop the three style vars we just pushed

    drawMenuBar();                                                    // draw the menu bar inside the host window

    const ImGuiID dockspace_id = ImGui::GetID("MainDockspace");       // get (or create) a stable ImGui ID for our dockspace
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f),                // create the dockspace widget; zero size means "fill remaining space"
                     ImGuiDockNodeFlags_PassthruCentralNode);         // flag that lets the central node be see-through so our background shows through it

    if (!dock_layout_built_) {                                        // on the very first frame only: build the default layout
        dock_layout_built_ = true;                                    // flip the flag so we don't rebuild on subsequent frames

        ImGui::DockBuilderRemoveNode(dockspace_id);                   // clear any previous layout saved in the ini file
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);  // create a fresh dockspace node
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);  // make the node fill the viewport

        ImGuiID dock_main   = dockspace_id;                           // start with the main node = entire area
        ImGuiID dock_right  = ImGui::DockBuilderSplitNode(            // split the main area: take 25% off the right side for properties
            dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);   // returns the new right node; updates dock_main to be what remains
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(            // split the remaining (center) area: take 25% off the bottom for the layer panel
            dock_main, ImGuiDir_Down,  0.25f, nullptr, &dock_main);   // returns the new bottom node; updates dock_main so Canvas gets what's left

        ImGui::DockBuilderDockWindow("Canvas",     dock_main);        // pin the Canvas window into the remaining (top/center) node
        ImGui::DockBuilderDockWindow("Properties", dock_right);       // pin the Properties window into the right-side node
        ImGui::DockBuilderDockWindow("Layers",     dock_bottom);      // pin the Layers window into the bottom node
        ImGui::DockBuilderFinish(dockspace_id);                       // commit the layout we just built
    }                                                                 // end first-frame layout setup

    ImGui::End();                                                     // end the docking host window
}                                                                     // end drawDockspace

void Application::drawMenuBar() {                                     // menu bar drawn inside the docking host window
    if (!ImGui::BeginMenuBar()) return;                               // begin the menu bar; returns false if the window has no MenuBar flag (shouldn't happen)

    if (ImGui::BeginMenu("File")) {                                   // begin the "File" top-level menu; only populates if the menu is open
        if (ImGui::MenuItem("New Text Object", "Ctrl+N")) {           // menu item with a hint shortcut label (not yet wired as a real accelerator)
            const uint64_t id = scene_.addTextObject("New Text", ImVec2(50.0f, 50.0f));  // create a new text object at world (50, 50), get its fresh id
            scene_.selected_id = id;                                   // auto-select the new object so properties panel jumps to it immediately
        }                                                             // end "New Text Object" click branch
        if (ImGui::MenuItem("Delete Selected", "Del", false, scene_.selected_id != 0)) {  // disabled (last arg) when there's no selection
            scene_.deleteObject(scene_.selected_id);                   // remove the selected object from the scene
            scene_.selected_id = 0;                                    // clear selection now that the target is gone
        }                                                             // end Delete click branch
        ImGui::Separator();                                           // horizontal separator line in the menu
        if (ImGui::MenuItem("Exit", "Alt+F4")) {                      // "Exit" item with Alt+F4 as the displayed shortcut hint
            glfwSetWindowShouldClose(window_, GLFW_TRUE);             // request the main loop to exit on the next iteration
        }                                                             // end Exit click branch
        ImGui::EndMenu();                                             // end the "File" menu
    }                                                                 // end if(BeginMenu("File"))

    if (ImGui::BeginMenu("View")) {                                   // "View" menu
        if (ImGui::MenuItem("Reset Canvas View")) {                   // menu item to reset pan/zoom
            canvas_.resetView();                                      // call the Canvas helper to restore defaults
        }                                                             // end click branch
        ImGui::EndMenu();                                             // end "View" menu
    }                                                                 // end if(BeginMenu("View"))

    if (ImGui::BeginMenu("Help")) {                                   // "Help" menu -- just displays hints, no actions
        ImGui::MenuItem("Left click: select / drag",  nullptr, false, false);  // disabled item used purely as a text label (last arg: enabled=false)
        ImGui::MenuItem("Middle/Right mouse: pan",    nullptr, false, false);  // hint line for pan input
        ImGui::MenuItem("Scroll wheel: zoom",         nullptr, false, false);  // hint line for zoom input
        ImGui::MenuItem("Delete key: remove object",  nullptr, false, false);  // hint line for keyboard delete
        ImGui::EndMenu();                                             // end "Help" menu
    }                                                                 // end if(BeginMenu("Help"))

    ImGui::EndMenuBar();                                              // finish the menu bar -- required to pair with BeginMenuBar
}                                                                     // end drawMenuBar

void Application::drawCanvasPanel() {                                 // the "Canvas" dock tab that hosts the viewport
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));  // zero padding so the canvas reaches the tab edges
    ImGui::Begin("Canvas");                                           // begin the dockable "Canvas" window (first time it's used, DockBuilder pins it)
    ImGui::PopStyleVar();                                             // pop the padding we pushed so it only affects this window's frame

    canvas_.draw(scene_, canvas_font_);                               // hand control to the Canvas: it will process input and render

    ImGui::End();                                                     // end the "Canvas" window
}                                                                     // end drawCanvasPanel

void Application::drawLayerPanel() {                                  // the "Layers" dock tab listing scene objects with select / visibility / delete controls
    ImGui::Begin("Layers");                                           // begin the dockable "Layers" window

    if (ImGui::Button("Add Text")) {                                  // toolbar-style button at the top: create a new text object
        const uint64_t id = scene_.addTextObject("New Text", ImVec2(50.0f, 50.0f));  // append the object and grab its fresh id
        scene_.selected_id = id;                                      // auto-select so user can immediately see/edit it in the properties panel
    }                                                                 // end Add button branch
    ImGui::SameLine();                                                // place the next widget on the same row as the previous one
    const bool can_delete = (scene_.selected_id != 0);                // delete is only meaningful when something is selected
    if (!can_delete) ImGui::BeginDisabled();                          // grey out the Delete button when there's no selection
    if (ImGui::Button("Delete")) {                                    // button click: remove the current selection
        scene_.deleteObject(scene_.selected_id);                      // remove from the vector
        scene_.selected_id = 0;                                       // clear selection so other panels follow
    }                                                                 // end Delete button branch
    if (!can_delete) ImGui::EndDisabled();                            // pair with BeginDisabled if it was issued
    ImGui::Separator();                                               // divider line below the toolbar row

    if (scene_.objects.empty()) {                                     // empty-scene fast path: nothing to list
        ImGui::TextDisabled("No objects.");                           // dimmed info text
        ImGui::End();                                                 // close the window
        return;                                                       // bail out
    }                                                                 // end empty-scene branch

    // Walk newest-first so the visually top-most layer is at the top of the list (matches Photoshop / Figma intuition).
    // Vector index 0 = back of draw stack, index N-1 = front. Reverse loop maps index N-1 to row 0.
    bool   has_swap = false;                                          // queued swap action: applied after the loop so we don't mutate mid-iteration
    size_t swap_a   = 0;                                              // first index of the queued swap
    size_t swap_b   = 0;                                              // second index of the queued swap
    for (size_t i = scene_.objects.size(); i-- > 0;) {                // reverse loop using size_t underflow trick: stops when i was 0 before decrement
        SceneObject& obj = scene_.objects[i];                         // reference to the current row's object
        ImGui::PushID(static_cast<int>(obj.id));                      // push a unique ID scope so widgets in different rows don't collide

        ImGui::Checkbox("##vis", &obj.visible);                       // visibility toggle; "##vis" hides the label but keeps a stable ID
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle visibility");  // tiny hint when the user hovers the checkbox
        ImGui::SameLine();                                            // put the name selectable on the same row as the checkbox

        const bool is_selected = (obj.id == scene_.selected_id);      // is this row the current selection?
        if (ImGui::Selectable(obj.name.c_str(), is_selected)) {       // full-width clickable row that highlights when selected
            scene_.selected_id = obj.id;                              // clicking the row selects this object
        }                                                             // end row click branch

        // Drag-to-reorder: when the row is held (active) and the cursor has drifted off it (not hovered),
        // queue a swap with the neighbor in the direction of motion.
        if (!has_swap && ImGui::IsItemActive() && !ImGui::IsItemHovered()) {  // one swap per frame; only fire while a row is held and the mouse left it
            const float dy = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).y;  // accumulated y-drag since the press began (or last reset)
            // Visual up (dy < 0) means the user wants this layer higher in the list -- which is later in the vector (closer to front of draw stack).
            const int    dir    = (dy < 0.0f) ? +1 : -1;              // +1 = move toward end of vector (front), -1 = move toward start (back)
            const size_t i_next = static_cast<size_t>(static_cast<int>(i) + dir);  // candidate neighbor index in vector terms
            if (i_next < scene_.objects.size()) {                     // single bounds check works for both ends because size_t underflow becomes a huge value
                has_swap = true;                                      // record that a swap is pending
                swap_a   = i;                                         // remember the source index
                swap_b   = i_next;                                    // remember the destination index
                ImGui::ResetMouseDragDelta();                         // zero the accumulator so we move at most one slot per drag-step
            }                                                         // end in-bounds branch
        }                                                             // end drag-reorder branch

        ImGui::PopID();                                               // pop the per-row ID scope
    }                                                                 // end reverse iteration over objects

    if (has_swap) {                                                   // apply the queued swap after the loop so the current frame's layout stays stable
        std::swap(scene_.objects[swap_a], scene_.objects[swap_b]);    // swap by value; selection follows automatically because we track by id, not index
    }                                                                 // end swap-apply branch

    ImGui::End();                                                     // pair the End() to match Begin("Layers")
}                                                                     // end drawLayerPanel

void Application::drawPropertiesPanel() {                             // the "Properties" dock tab that edits the selected object
    ImGui::Begin("Properties");                                       // begin the dockable "Properties" window

    SceneObject* sel = scene_.getSelected();                          // pointer to the selected object, or nullptr if nothing is selected
    if (!sel) {                                                       // no selection: show a hint and an Add button as a quick path to a usable scene
        ImGui::TextDisabled("No object selected.");                   // dimmed info text
        ImGui::TextDisabled("Click an object on the canvas, or:");    // tell the user how to make a selection
        if (ImGui::Button("Add Text Object")) {                       // button; returns true on the frame it's clicked
            const uint64_t id = scene_.addTextObject("Hello, World!", ImVec2(0.0f, 0.0f));  // create a text object at the world origin
            scene_.selected_id = id;                                  // immediately select the new object so the next frame shows its properties
        }                                                             // end button click branch
        ImGui::End();                                                 // pair the End() to match the Begin() above
        return;                                                       // early return -- nothing else to show when the scene is empty
    }                                                                 // end no-selection branch

    SceneObject& obj = *sel;                                          // dereference once so the rest of the function can use the same name as before

    ImGui::SeparatorText("Object");                                   // bold separator with inline "Object" label -- section header
    {                                                                 // open a scope so the local char buffer doesn't leak beyond the Name field
        char buf[256];                                                // stack buffer ImGui will write into and read from
        std::snprintf(buf, sizeof(buf), "%s", obj.name.c_str());      // copy the current name into the buffer, null-terminated, size-bounded
        if (ImGui::InputText("Name", buf, sizeof(buf))) {             // text input widget; returns true on the frame the user edits it
            obj.name = buf;                                           // copy the edited text back into the string member
        }                                                             // end edited branch
    }                                                                 // close the scope; buf goes out of existence
    ImGui::DragFloat2("Position", &obj.position.x, 1.0f);             // drag-to-edit two floats; &obj.position.x points to x, y is assumed to follow in memory
    ImGui::DragFloat ("Rotation", &obj.rotation, 0.5f, -360.0f, 360.0f, "%.1f deg");  // drag-edit rotation in [-360, 360] with one-decimal "deg" display format
    ImGui::SliderFloat("Opacity", &obj.opacity, 0.0f, 1.0f);          // slider for opacity in the range [0, 1]
    ImGui::Checkbox("Visible", &obj.visible);                         // checkbox bound directly to the visible flag

    ImGui::SeparatorText("Text");                                     // section header for text-specific fields
    {                                                                 // another scope for a local char buffer
        char buf[1024];                                               // larger buffer for the content (longer strings expected)
        std::snprintf(buf, sizeof(buf), "%s", obj.content.c_str());   // copy current content into the buffer
        if (ImGui::InputTextMultiline("Content", buf, sizeof(buf),    // multi-line text input widget
                                      ImVec2(-1.0f, 80.0f))) {        // -1 width = fill available, 80 pixels tall
            obj.content = buf;                                        // copy edited content back into the string member
        }                                                             // end edited branch
    }                                                                 // close the scope
    ImGui::DragFloat("Font Size", &obj.font_size, 0.5f, 1.0f, 400.0f);  // drag-edit font size in [1, 400] with step 0.5
    ImGui::ColorEdit4("Color", &obj.color.x);                         // four-channel color picker; again relies on ImVec4 memory layout being contiguous

    const char* align_items[] = {"Left", "Center", "Right"};          // labels for the alignment combo in enum-value order
    int align = static_cast<int>(obj.alignment);                      // convert the enum to int so Combo can write the selected index back
    if (ImGui::Combo("Alignment", &align, align_items, IM_ARRAYSIZE(align_items))) {  // combo box; returns true when user picks a new item
        obj.alignment = static_cast<TextAlignment>(align);            // convert the int back into the enum type and store it
    }                                                                 // end combo-changed branch

    ImGui::SeparatorText("View");                                     // section header for read-only canvas state
    ImGui::Text("Zoom: %.0f%%", canvas_.getZoom() * 100.0f);          // printf-style formatted text; %% is a literal percent sign
    const ImVec2 off = canvas_.getOffset();                           // snapshot the offset so we can print its two components
    ImGui::Text("Pan:  %.0f, %.0f", off.x, off.y);                    // show the integer part of the pan offset
    if (ImGui::Button("Reset View")) canvas_.resetView();             // button that calls Canvas::resetView when clicked

    ImGui::End();                                                     // pair the End() to match Begin("Properties")
}                                                                     // end drawPropertiesPanel

// ---------------------------------------------------------------------------
// Setup helpers
// ---------------------------------------------------------------------------

void Application::initDefaultScene() {                                // called from init() to seed the scene
    scene_.addTextObject("Hello, Parametric Design!", ImVec2(40.0f, 40.0f));  // create one sample text object at world (40, 40)
}                                                                     // end initDefaultScene

void Application::loadFonts() {                                       // register fonts with the ImGui atlas (must happen before the OpenGL backend initializes)
    ImGuiIO& io = ImGui::GetIO();                                     // grab the I/O struct; io.Fonts is the atlas we'll add to

    // UI font. First font added becomes the default for all ImGui panels.
    static const char* ui_font_paths[] = {                            // static array of candidate paths; static so it lives for the whole program
        "C:/Windows/Fonts/segoeui.ttf",                               // Windows: preferred modern sans-serif
        "C:/Windows/Fonts/arial.ttf",                                 // Windows fallback if Segoe UI is missing (extremely unlikely)
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",            // Linux fallback (Debian/Ubuntu typical path)
        "/System/Library/Fonts/Helvetica.ttc",                        // macOS fallback
    };                                                                // end ui_font_paths
    ImFont* ui_font = addFontWithFallback(                            // try each path in order and get back the loaded font (or nullptr)
        io.Fonts, ui_font_paths, IM_ARRAYSIZE(ui_font_paths), 16.0f);  // rasterize at 16 pixels -- good size for the panels
    if (!ui_font) {                                                   // none of the candidates worked
        io.Fonts->AddFontDefault();                                   // use ImGui's built-in Proggy font so there's always a usable UI font
    }                                                                 // end UI-font-fallback branch

    // Canvas font: rasterized at a large size so downscaling (the common case)
    // stays sharp. Upscaling past ~2x will be blurry -- acceptable until the
    // Slug / GPU text pipeline lands in a later phase.
    static const char* canvas_font_paths[] = {                        // candidate paths for the canvas text font
        "C:/Windows/Fonts/arial.ttf",                                 // Windows: Arial gives us clean letterforms at large sizes
        "C:/Windows/Fonts/segoeui.ttf",                               // Windows: fall back to Segoe UI if Arial is missing
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",            // Linux fallback
        "/System/Library/Fonts/Helvetica.ttc",                        // macOS fallback
    };                                                                // end canvas_font_paths
    canvas_font_ = addFontWithFallback(                               // try each candidate, store the loaded font in our member pointer
        io.Fonts, canvas_font_paths, IM_ARRAYSIZE(canvas_font_paths), 96.0f);  // rasterize at 96 pixels so downscaling to normal sizes stays sharp
    if (!canvas_font_) {                                              // none of the canvas font candidates loaded
        ImFontConfig cfg;                                             // build a config struct to control how AddFontDefault rasterizes
        cfg.SizePixels = 96.0f;                                       // rasterize the default font at 96 pixels (matching the above target)
        canvas_font_   = io.Fonts->AddFontDefault(&cfg);              // use the built-in Proggy font as a last-ditch canvas font
    }                                                                 // end canvas-font-fallback branch
}                                                                     // end loadFonts

void Application::setupStyle() {                                      // apply our preferred ImGui colors and spacing
    ImGui::StyleColorsDark();                                         // start from the built-in dark theme as a baseline

    ImGuiStyle& s = ImGui::GetStyle();                                // grab the style struct by reference so we can mutate it
    s.WindowRounding    = 4.0f;                                       // soften window corners slightly
    s.FrameRounding     = 3.0f;                                       // soften widget frame corners (inputs, buttons)
    s.GrabRounding      = 3.0f;                                       // soften the draggable "grab" on sliders
    s.ScrollbarRounding = 4.0f;                                       // soften scrollbar ends
    s.TabRounding       = 3.0f;                                       // soften dock tab corners
    s.WindowPadding     = ImVec2(8.0f, 8.0f);                         // inner padding inside windows
    s.FramePadding      = ImVec2(6.0f, 4.0f);                         // inner padding inside widget frames
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);                         // spacing between adjacent widgets

    ImVec4* c = s.Colors;                                             // shorthand pointer to the color array to make the block below compact
    c[ImGuiCol_WindowBg]        = ImVec4(0.13f, 0.13f, 0.15f, 1.00f); // background color of windows -- very dark gray with a hint of blue
    c[ImGuiCol_FrameBg]         = ImVec4(0.20f, 0.20f, 0.23f, 1.00f); // background of widget frames (unfocused)
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.28f, 0.28f, 0.32f, 1.00f); // widget frame when hovered
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.34f, 0.34f, 0.40f, 1.00f); // widget frame while active/pressed
    c[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f); // window title bar (unfocused)
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.16f, 0.16f, 0.19f, 1.00f); // window title bar (focused)
    c[ImGuiCol_Tab]             = ImVec4(0.15f, 0.15f, 0.18f, 1.00f); // inactive dock tab
    c[ImGuiCol_TabHovered]      = ImVec4(0.32f, 0.32f, 0.38f, 1.00f); // dock tab when hovered
    c[ImGuiCol_Header]          = ImVec4(0.22f, 0.22f, 0.28f, 1.00f); // collapsing header / selectable base color
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.30f, 0.30f, 0.38f, 1.00f); // header when hovered
    c[ImGuiCol_Button]          = ImVec4(0.22f, 0.22f, 0.28f, 1.00f); // button base color
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.30f, 0.38f, 1.00f); // button when hovered
    c[ImGuiCol_ButtonActive]    = ImVec4(0.38f, 0.38f, 0.48f, 1.00f); // button while being clicked
    c[ImGuiCol_DockingEmptyBg]  = ImVec4(0.08f, 0.08f, 0.09f, 1.00f); // color of empty areas in the dockspace
}                                                                     // end setupStyle
