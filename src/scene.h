#pragma once                                          // include guard: tells the compiler to process this header at most once per translation unit, preventing duplicate type definitions

// Scene graph data structures.
//
// Pure data -- no rendering logic, no UI logic. The renderer reads this each
// frame, the UI layer mutates it in response to user input. See Project_spec.md
// "Three-Layer Design" for the architectural rationale.

#include "imgui.h"                                    // needed for ImVec2 and ImVec4 which we use as field types below

#include <cstdint>                                    // provides fixed-width integer aliases like uint64_t used for object IDs
#include <string>                                     // provides std::string, the dynamically-allocated text type used for name / content / font_family
#include <vector>                                     // provides std::vector, the dynamic array used to hold the scene's object list

enum class ObjectType {                               // C++11 scoped enum: values live inside ObjectType:: and don't pollute the global namespace
    Text,                                             // a drawable string of text -- the only type actually rendered in Phase 1
    Shape,  // reserved for Phase 4                   // placeholder for rectangles / ellipses / polygons coming in Phase 4
    Image,  // reserved for Phase 4                   // placeholder for imported bitmap images coming in Phase 4
    Group   // reserved for Phase 4                   // placeholder for a container that holds child objects coming in Phase 4
};                                                    // closing brace + semicolon -- semicolons are mandatory after enum / struct / class definitions

enum class TextAlignment {                            // separate enum just for text horizontal alignment options
    Left,                                             // pin text to the left edge of its bounding box
    Center,                                           // center the text horizontally within its bounding box
    Right                                             // pin text to the right edge of its bounding box
};                                                    // end enum; semicolon required

// Single drawable object in the scene. Holds base properties plus the
// type-specific fields (only Text is populated in Phase 1). Later phases will
// either split this into a polymorphic hierarchy or keep it as a flat struct
// with a union -- the decision can wait until we have more than one type.
struct SceneObject {                                  // plain-data aggregate; all members are public by default in a struct
    // --- Base properties (Project_spec.md > Base Object Properties) ---
    uint64_t id          = 0;                         // 64-bit unique identifier; 0 means "unassigned", real IDs start at 1
    std::string name;                                 // human-readable label shown in the layer panel; default-constructs to empty string
    ObjectType type      = ObjectType::Text;          // discriminator tag telling us which type-specific fields below are meaningful
    ImVec2 position      = {0.0f, 0.0f};              // world-space position of this object's anchor point (top-left for text)
    float  rotation      = 0.0f;     // degrees       // rotation around the anchor, 0 = upright; Phase 1 ignores this during render
    ImVec2 scale         = {1.0f, 1.0f};              // per-axis scale multiplier, 1.0 = original size on both axes
    float  opacity       = 1.0f;                      // global alpha multiplier 0..1, applied on top of color.w at draw time
    bool   visible       = true;                      // when false the renderer skips this object entirely
    bool   locked        = false;                     // when true the UI should prevent selection/editing (not yet enforced)
    int    layer_order   = 0;                         // higher values draw on top; ties broken by insertion order
    // modifiers: std::vector<Modifier> -- Phase 3    // note for future-me: the modifier stack will live here once Phase 3 starts

    // --- Text-specific properties ---
    std::string   content       = "Hello, World!";    // the actual text string to render on the canvas
    std::string   font_family   = "Default";          // requested font family name; Phase 1 uses the loaded canvas font regardless
    float         font_size     = 48.0f;              // font size in world units (gets multiplied by the canvas zoom at draw time)
    int           font_weight   = 400;                // CSS-style weight: 400 = regular, 700 = bold; Phase 1 ignores this
    ImVec4        color         = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA color, each channel 0..1; default = opaque white
    float         line_height   = 1.2f;               // multiplier of font_size controlling distance between wrapped lines
    TextAlignment alignment     = TextAlignment::Left;  // horizontal alignment within the text's bounding box
};                                                    // end struct; semicolon required

struct Scene {                                        // top-level container holding everything the renderer needs to draw
    std::vector<SceneObject> objects;                 // flat list of scene objects; iteration order currently equals draw order
    uint64_t next_id     = 1;                         // monotonic counter used to assign IDs; starts at 1 because 0 means "no object"
    uint64_t selected_id = 0;                         // id of the currently selected object; 0 means "nothing selected"

    uint64_t addTextObject(const std::string& text, ImVec2 pos) {   // convenience helper that creates a text object and appends it
        SceneObject obj;                              // default-construct a new object with all the defaults listed above
        obj.id       = next_id++;                     // take the current counter value as this object's id, then increment for the next call
        obj.name     = "Text " + std::to_string(obj.id);  // synthesize a default name like "Text 1", "Text 2", ...
        obj.type     = ObjectType::Text;              // explicitly set the discriminator even though Text is already the default, for clarity
        obj.content  = text;                          // copy the caller's string into the object's content field
        obj.position = pos;                           // copy the caller's world position into the object's position field
        objects.push_back(obj);                       // append a copy of the fully-populated object to the back of the vector
        return obj.id;                                // return the new id so the caller can reference this object in the future
    }                                                 // end addTextObject

    SceneObject* findById(uint64_t id) {              // linear search by id; returns nullptr if not found (or if id is 0)
        if (id == 0) return nullptr;                  // fast-path: the sentinel "no object" value can never match anything
        for (SceneObject& o : objects) {              // scan every object in the vector
            if (o.id == id) return &o;                // found it -- return a pointer to the storage inside the vector
        }                                             // end loop
        return nullptr;                                // no match; let the caller decide what to do with a null
    }                                                 // end findById

    SceneObject* getSelected() {                      // convenience: return pointer to the currently-selected object or nullptr
        return findById(selected_id);                 // delegate to findById so the 0-means-none rule lives in one place
    }                                                 // end getSelected

    void deleteObject(uint64_t id) {                  // remove the object with the given id from the vector (no-op if not found)
        for (auto it = objects.begin(); it != objects.end(); ++it) {  // standard iterator loop so we can pass the iterator to erase
            if (it->id == id) {                        // match: this is the element to remove
                objects.erase(it);                     // vector::erase shifts following elements down; iterator becomes invalid after this
                return;                                // leave immediately -- iterator invalid, and ids are unique so nothing else to find
            }                                          // end match branch
        }                                              // end search loop
    }                                                  // end deleteObject
};                                                    // end Scene; semicolon required
