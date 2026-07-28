#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.h>

#include "mesh_generator.hpp"
#include "mesh_postprocess.hpp"
#include "gltf_export.hpp"
#include "expression_template.hpp"
#include "phoneme_to_viseme.hpp"
#include "input.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <cstring>
#include <iostream>
#include <vector>
#include <fstream>
#include <optional>
#include <set>
#include <algorithm>
#include <cmath>
#include <chrono>

// ─── Constants ───────────────────────────────────────────────────────
const int WIDTH = 1280, HEIGHT = 960;
const int MAX_FRAMES_IN_FLIGHT = 2;

// ─── SPIR-V loader ───────────────────────────────────────────────────
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file) { std::cerr << "Failed to open " << filename << std::endl; exit(1); }
    size_t fsize = file.tellg();
    std::vector<char> buf(fsize);
    file.seekg(0);
    file.read(buf.data(), fsize);
    return buf;
}

// ─── Vulkan helpers ──────────────────────────────────────────────────
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData, void*) {
    std::cerr << "Vulkan: " << pData->pMessage << std::endl;
    return VK_FALSE;
}

struct QueueFamilyIndices { std::optional<uint32_t> graphics, present;
    bool complete() const { return graphics.has_value() && present.has_value(); } };
struct SwapChainDetails { VkSurfaceCapabilitiesKHR caps; std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> modes; };

// ─── Shader module helper ────────────────────────────────────────────
static VkShaderModule createShaderModule(VkDevice dev, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule sm;
    vkCreateShaderModule(dev, &ci, nullptr, &sm);
    return sm;
}

// ─── Uniform Buffer Object ───────────────────────────────────────────
struct UBO { glm::mat4 view, proj; glm::vec4 camPos; };

// ─── Push Constants ──────────────────────────────────────────────────
struct PushConst { glm::mat4 model; glm::vec4 color; };

// ─── Grid/Line vertex ────────────────────────────────────────────────
struct GVertex { glm::vec3 pos; glm::vec4 color; };

// =====================================================================
// VulkanApp - Main Application
// =====================================================================
class VulkanApp {
public:
    void run() {
        initWindow();
        initVulkan();
        initMeshes();
        mainLoop();
        cleanup();
    }

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMsgr = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue gfxQueue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    VkFormat swapFormat; VkExtent2D swapExtent;
    std::vector<VkImageView> swapViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    VkPipeline meshPipe = VK_NULL_HANDLE, gridPipe = VK_NULL_HANDLE;
    VkPipeline toonPipe = VK_NULL_HANDLE, outlinePipe = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    // Depth
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMem = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs;

    // Buffers
    VkBuffer vbo = VK_NULL_HANDLE, ibo = VK_NULL_HANDLE;
    VkDeviceMemory vboMem = VK_NULL_HANDLE, iboMem = VK_NULL_HANDLE;
    uint32_t meshIdxCount = 0;

    VkBuffer gridVbo = VK_NULL_HANDLE, gridIbo = VK_NULL_HANDLE;
    VkDeviceMemory gridVboMem = VK_NULL_HANDLE, gridIboMem = VK_NULL_HANDLE;
    uint32_t gridIdxCount = 0;

    VkBuffer axisVbo = VK_NULL_HANDLE, axisIbo = VK_NULL_HANDLE;
    VkDeviceMemory axisVboMem = VK_NULL_HANDLE, axisIboMem = VK_NULL_HANDLE;
    uint32_t axisIdxCount = 0;

    std::vector<VkBuffer> uboBufs;
    std::vector<VkDeviceMemory> uboMems;
    std::vector<void*> uboMapped;

    VkDescriptorPool descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descSets;

    // Sync
    std::vector<VkSemaphore> imgAvail, renderDone;
    std::vector<VkFence> inFlight;
    uint32_t curFrame = 0;

    // Camera / model state
    glm::vec3 camTarget{0, 0, 0};
    float camDist = 8.0f;
    float camTheta = 0.5f, camPhi = 0.7f;  // spherical coords
    glm::quat modelRot{1, 0, 0, 0};        // identity quaternion
    glm::vec3 modelPos{0, 0, 0};
    float zoomLevel = 1.0f;

    // UI state
    enum Axis { AXIS_NONE, AXIS_X, AXIS_Y, AXIS_Z };
    enum Action { ACT_NONE, ACT_ROTATE, ACT_MOVE, ACT_ORBIT, ACT_ZOOM };
    Axis selAxis = AXIS_NONE;
    Action selAction = ACT_NONE;
    int selObject = -1;  // -1: none, 0: first object

    // M6: Stroke-to-mesh tube mode
    bool strokeMode = false;            // L key toggle: stroke→tube vs sculpt
    float tubeRadius = 0.15f;           // tube cross-section radius
    std::vector<Point2D> strokePath2D;  // raw 2D path collected during drag

    // M5: Toon shading
    bool toonMode = false;

    // Color palette (ImGui ColorPicker4)
    bool paletteOpen = false;
    glm::vec4 meshColor = glm::vec4(0.4f, 0.6f, 0.9f, 1.0f);

    // Input state (track pressed keys no longer needed - using select pattern)
    // (keyPressed booleans removed — replaced by selectAxis/selectAction radio-toggle)

    // Mouse drag state (for stroke drawing)
    bool leftDragging = false;
    double lastMouseX = 0, lastMouseY = 0;
    double lastClickTime = 0;
    static constexpr double DOUBLE_CLICK_THRESH = 0.3; // seconds
    std::vector<GVertex> strokeVerts;
    std::vector<uint32_t> strokeIdxs;
    VkBuffer strokeVbo = VK_NULL_HANDLE, strokeIbo = VK_NULL_HANDLE;
    VkDeviceMemory strokeVboMem = VK_NULL_HANDLE, strokeIboMem = VK_NULL_HANDLE;
    uint32_t strokeIdxCount = 0;
    bool strokeDirty = false; // need to update GPU buffers

    // M3: Sculpt state
    std::vector<Vertex> baseVertices;  // original mesh for undo (per-frame update on change)
    std::vector<glm::vec3> sculptHits; // surface hit points during stroke
    bool meshDirty = false;            // need to update mesh VBO
    float brushThickness = 0.15f;      // U key toggles add/subtract, thickness from stroke spread
    std::vector<Vertex> currentMeshVertices; // live mesh data (mutated by sculpt)
    std::vector<uint32_t> meshIndices;       // CPU copy of index buffer for ray tracing

    // M4: Post-processing + export state
    DecimatedMesh decimated;           // decimated version of current mesh
    bool showDecimated = false;        // toggle: show decimated instead of original
    std::string exportDir = "exports"; // output directory for glTF files

    // M7: Expression template system
    Mesh faceBase;                      // standard face base mesh (neutral)
    BlendDelta expressionDeltas[6];     // joy, anger, sadness, surprise, fear, disgust
    int activeExpression = -1;          // -1=neutral, 0=joy, 1=anger, 2=sadness, 3=surprise, 4=fear, 5=disgust
    float expressionWeight = 1.0f;      // blend weight (0.0 ~ 1.0)
    bool expressionMode = false;        // whether expression system is active
    bool expressionsInitialized = false;

    // M8: Lip-sync system
    BlendDelta visemeDeltas[VISEME_COUNT]; // pre-generated viseme deltas
    LipSyncTrack lipSyncTrack;            // current animation track
    bool lipSyncReady = false;            // visemes generated
    bool lipSyncPlaying = false;          // playback active
    float lipSyncTime = 0.0f;             // current playback time
    int currentViseme = -1;               // current viseme index (-1 = none)
    bool lipSyncUIOpen = false;           // ImGui panel toggle
    char lipSyncText[512] = "Hello world!"; // input text buffer

    // ─── Window ──────────────────────────────────────────────────────
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "ImageMaker M7/8 | F=Face G=LipSync 1-6=Expressions | L/scroll/+/-", nullptr, nullptr);
        if (!window) { std::cerr << "Failed to create GLFW window (no display?)\n"; glfwTerminate(); exit(1); }
        glfwSetWindowUserPointer(window, this);
        glfwSetKeyCallback(window, keyCB);
        glfwSetScrollCallback(window, scrollCB);
        glfwSetMouseButtonCallback(window, mouseCB);
        glfwSetCursorPosCallback(window, cursorCB);
    }

    static void keyCB(GLFWwindow* w, int key, int, int action, int) {
        auto* app = (VulkanApp*)glfwGetWindowUserPointer(w);
        if (action == GLFW_PRESS) {
            if (key == GLFW_KEY_X) { app->selectAxis(AXIS_X); }
            if (key == GLFW_KEY_Y) { app->selectAxis(AXIS_Y); }
            if (key == GLFW_KEY_Z) { app->selectAxis(AXIS_Z); }
            if (key == GLFW_KEY_R) { app->selectAction(ACT_ROTATE); }
            if (key == GLFW_KEY_M) { app->selectAction(ACT_MOVE); }
            if (key == GLFW_KEY_S) { app->selectAction(ACT_ORBIT); }
            if (key == GLFW_KEY_T) { app->selectAction(ACT_ZOOM); }
            if (key == GLFW_KEY_U) { app->brushThickness = -app->brushThickness; }
            if (key == GLFW_KEY_B) { app->baseVertices = app->currentMeshVertices; }
            if (key == GLFW_KEY_D) { app->toggleDecimate(); }
            if (key == GLFW_KEY_E) { app->exportMesh(); }
            if (key == GLFW_KEY_V) { app->toonMode = !app->toonMode; app->updateTitle(); }
            if (key == GLFW_KEY_C) { app->paletteOpen = !app->paletteOpen; app->updateTitle(); }
            if (key == GLFW_KEY_F) { app->toggleExpressionMode(); }
            if (key == GLFW_KEY_G) { app->toggleLipSyncUI(); }
            if (key == GLFW_KEY_1) { app->selectExpression(0); }
            if (key == GLFW_KEY_2) { app->selectExpression(1); }
            if (key == GLFW_KEY_3) { app->selectExpression(2); }
            if (key == GLFW_KEY_4) { app->selectExpression(3); }
            if (key == GLFW_KEY_5) { app->selectExpression(4); }
            if (key == GLFW_KEY_6) { app->selectExpression(5); }
            if (key == GLFW_KEY_GRAVE_ACCENT) { app->selectExpression(-1); } // backtick = neutral
            if (key == GLFW_KEY_LEFT_BRACKET) { app->adjustExpressionWeight(-0.1f); }
            if (key == GLFW_KEY_RIGHT_BRACKET) { app->adjustExpressionWeight(0.1f); }
            if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) app->applyDelta(0.1f);
            if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) app->applyDelta(-0.1f);
            if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, true);
        }
    }
    static void scrollCB(GLFWwindow* w, double, double yoff) {
        auto* app = (VulkanApp*)glfwGetWindowUserPointer(w);
        app->applyDelta((float)yoff * 0.1f);
    }
    static void mouseCB(GLFWwindow* w, int btn, int action, int) {
        auto* app = (VulkanApp*)glfwGetWindowUserPointer(w);
        if (btn == GLFW_MOUSE_BUTTON_LEFT) {
            if (action == GLFW_PRESS) {
                double now = glfwGetTime();
                if (now - app->lastClickTime < DOUBLE_CLICK_THRESH) {
                    // Double-click: toggle object selection
                    app->selObject = (app->selObject == -1) ? 0 : -1;
                    app->updateTitle();
                }
                app->lastClickTime = now;
                // Start drag
                glfwGetCursorPos(w, &app->lastMouseX, &app->lastMouseY);
                app->leftDragging = true;
            } else if (action == GLFW_RELEASE) {
                // M3: Apply sculpt displacement on stroke end
                if (!app->sculptHits.empty()) {
                    app->sculptDisplace(app->sculptHits);
                    app->sculptHits.clear();
                }
                app->leftDragging = false;
            }
        } else if (btn == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            // Right-click: clear strokes
            app->strokeVerts.clear();
            app->strokeIdxs.clear();
            app->strokeDirty = true;
            app->selObject = -1;
            app->updateTitle();
        }
    }
    static void cursorCB(GLFWwindow* w, double x, double y) {
        auto* app = (VulkanApp*)glfwGetWindowUserPointer(w);
        if (!app->leftDragging) {
            app->lastMouseX = x;
            app->lastMouseY = y;
            return;
        }
        // Add stroke line segment
        float ox = (float)app->lastMouseX / WIDTH * 2.0f - 1.0f;
        float oy = -((float)app->lastMouseY / HEIGHT * 2.0f - 1.0f);
        float nx = (float)x / WIDTH * 2.0f - 1.0f;
        float ny = -((float)y / HEIGHT * 2.0f - 1.0f);

        if (abs(nx - ox) < 0.001f && abs(ny - oy) < 0.001f) return;

        glm::vec4 col(1.0f, 1.0f, 1.0f, 1.0f);
        uint32_t base = (uint32_t)app->strokeVerts.size();
        app->strokeVerts.push_back({{ox, oy, 0.0f}, col});
        app->strokeVerts.push_back({{nx, ny, 0.0f}, col});
        app->strokeIdxs.push_back(base);
        app->strokeIdxs.push_back(base + 1);
        app->strokeDirty = true;

        // M3: Also trace ray against mesh for sculpt
        if (app->selObject >= 0) {
            auto ray = app->screenToRay((float)x, (float)y);
            glm::vec3 hitPt;
            if (app->rayMeshHit(ray, hitPt)) {
                app->sculptHits.push_back(hitPt);
            }
        }

        app->lastMouseX = x;
        app->lastMouseY = y;
    }

    // Key already pressed → toggle deselection; different key → switch
    void selectAxis(Axis ax) {
        if (selAxis == ax) selAxis = AXIS_NONE;
        else selAxis = ax;
        updateTitle();
    }
    void selectAction(Action act) {
        if (selAction == act) selAction = ACT_NONE;
        else selAction = act;
        updateTitle();
    }

    void updateTitle() {
        const char* ax[] = {"none","X","Y","Z"};
        const char* ac[] = {"none","Rotate","Move","Orbit","Zoom"};
        const char* exprNames[] = {"neutral","joy","anger","sadness","surprise","fear","disgust"};
        const char* exprName = (activeExpression >= 0 && activeExpression <= 5) ? exprNames[activeExpression + 1] : "neutral";
        char buf[256];
        if (lipSyncPlaying) {
            snprintf(buf, sizeof(buf),
                "ImageMaker M8 | LipSync: %s t=%.1fs | G=toggle",
                VisemeConfig::visemeName(currentViseme >= 0 ? currentViseme : 0),
                lipSyncTime);
        } else if (lipSyncUIOpen) {
            snprintf(buf, sizeof(buf),
                "ImageMaker M8 | LipSync Ready: \"%s\" (%.1fs) | G=toggle",
                lipSyncTrack.transcript.c_str(), lipSyncTrack.totalDuration);
        } else {
            snprintf(buf, sizeof(buf),
                "ImageMaker M7 | Axis:%s Action:%s Expr:%s w=%.1f Mode:%s | 1-6/`/[/]/F/G/C/D/E/V",
                ax[selAxis], ac[selAction], exprName, expressionWeight,
                expressionMode ? "FACE" : "SCULPT");
        }
        glfwSetWindowTitle(window, buf);
    }

    // ─── M7: Expression system helpers ─────────────────────────────────
    void initExpressions() {
        if (expressionsInitialized) return;

        faceBase = ExpressionTemplateSystem::generateFaceBase(1.0f);
        expressionDeltas[0] = ExpressionTemplateSystem::joy(faceBase);
        expressionDeltas[1] = ExpressionTemplateSystem::anger(faceBase);
        expressionDeltas[2] = ExpressionTemplateSystem::sadness(faceBase);
        expressionDeltas[3] = ExpressionTemplateSystem::surprise(faceBase);
        expressionDeltas[4] = ExpressionTemplateSystem::fear(faceBase);
        expressionDeltas[5] = ExpressionTemplateSystem::disgust(faceBase);

        // Also save templates to disk
        ExpressionTemplateSystem::initializeTemplates("templates");

        expressionsInitialized = true;
        printf("M7: Expressions initialized (6 emotions + neutral)\n");
        printf("    Keys: 1=joy 2=anger 3=sadness 4=surprise 5=fear 6=disgust `=neutral [/]=weight\n");
    }

    void toggleExpressionMode() {
        if (!expressionsInitialized) initExpressions();

        expressionMode = !expressionMode;
        if (expressionMode) {
            // Switch to face base mesh
            currentMeshVertices = faceBase.vertices;
            meshIndices = faceBase.indices;
            baseVertices = faceBase.vertices;
            meshIdxCount = (uint32_t)meshIndices.size();
            meshDirty = true;
            activeExpression = -1;
            expressionWeight = 1.0f;
        } else {
            // Reset to default sphere
            Mesh m = PrimitiveGenerator::generateIcoSphere(1.0f, 3);
            currentMeshVertices = m.vertices;
            meshIndices = m.indices;
            baseVertices = m.vertices;
            meshIdxCount = (uint32_t)m.indices.size();
            meshDirty = true;
            activeExpression = -1;
            showDecimated = false;
        }
        updateTitle();
    }

    void selectExpression(int idx) {
        if (!expressionMode || !expressionsInitialized) return;
        if (idx >= 6 || idx < -1) return;

        if (activeExpression == idx) {
            // Deselect
            activeExpression = -1;
            // Reset to neutral face
            currentMeshVertices = faceBase.vertices;
            meshIndices = faceBase.indices;
            baseVertices = faceBase.vertices;
            meshDirty = true;
        } else {
            activeExpression = idx;
            applyCurrentExpression();
        }
        updateTitle();
    }

    void adjustExpressionWeight(float delta) {
        if (!expressionMode) return;
        expressionWeight = std::max(0.0f, std::min(1.0f, expressionWeight + delta));
        if (activeExpression >= 0) {
            applyCurrentExpression();
        }
        updateTitle();
    }

    void applyCurrentExpression() {
        if (!expressionsInitialized || activeExpression < 0 || activeExpression >= 6) return;

        // Start from neutral face base
        currentMeshVertices = faceBase.vertices;
        meshIndices = faceBase.indices;

        // Apply expression delta
        ExpressionTemplateSystem::applyExpression(
            currentMeshVertices,
            expressionDeltas[activeExpression],
            expressionWeight
        );

        // Recompute normals for the modified mesh
        recomputeMeshNormals();

        meshIdxCount = (uint32_t)meshIndices.size();
        meshDirty = true;
    }

    void recomputeMeshNormals() {
        for (auto& v : currentMeshVertices) { v.nx = 0; v.ny = 0; v.nz = 0; }
        for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
            auto& v0 = currentMeshVertices[meshIndices[i]];
            auto& v1 = currentMeshVertices[meshIndices[i+1]];
            auto& v2 = currentMeshVertices[meshIndices[i+2]];
            glm::vec3 e1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
            glm::vec3 e2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
            glm::vec3 n = glm::cross(e1, e2);
            v0.nx += n.x; v0.ny += n.y; v0.nz += n.z;
            v1.nx += n.x; v1.ny += n.y; v1.nz += n.z;
            v2.nx += n.x; v2.ny += n.y; v2.nz += n.z;
        }
        for (auto& v : currentMeshVertices) {
            float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
            if (len > 1e-6f) { v.nx /= len; v.ny /= len; v.nz /= len; }
            else { v.nx = 0; v.ny = 1.0f; v.nz = 0; }
        }
    }

    // ─── M8: Lip-sync helpers ────────────────────────────────────────
    void initVisemes() {
        if (lipSyncReady) return;
        if (!expressionsInitialized) initExpressions();

        VisemeConfig::generateAllVisemes(faceBase, visemeDeltas);
        lipSyncReady = true;
        printf("M8: Visemes initialized (16 viseme shapes)\n");
    }

    void applyViseme(int visemeIdx, float weight) {
        if (!lipSyncReady || visemeIdx < 0 || visemeIdx >= VISEME_COUNT) return;
        if (!expressionMode || !expressionsInitialized) return;

        // Start from neutral face base
        currentMeshVertices = faceBase.vertices;
        meshIndices = faceBase.indices;

        // Apply viseme delta
        ExpressionTemplateSystem::applyExpression(
            currentMeshVertices,
            visemeDeltas[visemeIdx],
            weight
        );

        recomputeMeshNormals();
        meshIdxCount = (uint32_t)meshIndices.size();
        meshDirty = true;
        currentViseme = visemeIdx;
    }

    void toggleLipSyncUI() {
        if (!expressionsInitialized) initExpressions();
        if (!lipSyncReady) initVisemes();
        if (!expressionMode) toggleExpressionMode(); // need face mode
        lipSyncUIOpen = !lipSyncUIOpen;
        if (!lipSyncUIOpen) {
            lipSyncPlaying = false;
            currentViseme = -1;
            applyViseme(VISEME_REST, 1.0f);
            updateTitle();
        }
    }

    void lipSyncGenerate() {
        if (!lipSyncReady) initVisemes();
        if (!expressionMode) toggleExpressionMode();

        lipSyncTrack = buildLipSync(lipSyncText);
        lipSyncTime = 0.0f;
        lipSyncPlaying = false;
        currentViseme = -1;
        applyViseme(VISEME_REST, 1.0f);
        printf("M8: Lip-sync track built: \"%s\" (%zu keys, %.2fs)\n",
               lipSyncText, lipSyncTrack.keys.size(), lipSyncTrack.totalDuration);
    }

    void updateLipSync(float dt) {
        if (!lipSyncPlaying || !lipSyncReady) return;
        if (!expressionMode) return;

        lipSyncTime += dt;
        if (lipSyncTime >= lipSyncTrack.totalDuration) {
            // Playback finished
            lipSyncPlaying = false;
            lipSyncTime = lipSyncTrack.totalDuration;
            applyViseme(VISEME_REST, 1.0f);
            printf("M8: Lip-sync finished (%.2fs)\n", lipSyncTrack.totalDuration);
            return;
        }

        // Find current viseme and apply
        int vi = lipSyncTrack.sample(lipSyncTime);
        if (vi != currentViseme) {
            applyViseme(vi, 1.0f);
        }
    }

    void exportExpressionGLB() {
        if (!expressionsInitialized) return;

        std::string mkdirCmd = "mkdir -p " + exportDir;
        if (system(mkdirCmd.c_str()) != 0) {
            fprintf(stderr, "Warning: failed to create exports dir\n");
        }

        // Export base face + all 6 expression morph targets
        std::string path = exportDir + "/face_expressions.glb";
        GLTFExporter::exportGLBWithMorphTargets(
            path, faceBase.vertices, faceBase.indices,
            expressionDeltas, 6, meshColor);

        printf("Expression GLB exported: %s\n", path.c_str());
    }



    // M5: Color cycle helper (replaced by ImGui ColorPicker4)
    // ─── M5: Toon Shading Helpers ─────────────────────────────────────
    void applyDelta(float delta) {
        if (selAction == ACT_NONE) return;
        if (selAction == ACT_ZOOM || selAction == ACT_ORBIT) {
            // Camera operations (ignore axis)
            if (selAction == ACT_ZOOM) {
                camDist = std::max(1.0f, std::min(50.0f, camDist - delta * 2.0f));
            } else {
                camTheta += delta;
            }
        } else if (selAxis != AXIS_NONE) {
            glm::vec3 euler(0);
            if (selAxis == AXIS_X) euler.x = delta;
            else if (selAxis == AXIS_Y) euler.y = delta;
            else euler.z = delta;
            glm::quat dq(euler);
            if (selAction == ACT_ROTATE) {
                modelRot = glm::normalize(dq * modelRot);
            } else if (selAction == ACT_MOVE) {
                modelPos += euler * 0.5f;
            }
        }
    }

    // ─── M3: Sculpt helpers ───────────────────────────────────────────
    glm::mat4 currentViewProj() const {
        float cx = camDist * sin(camPhi) * cos(camTheta);
        float cy = camDist * cos(camPhi);
        float cz = camDist * sin(camPhi) * sin(camTheta);
        glm::vec3 eye(cx, cy, cz);
        glm::vec3 up(0, 1, 0);
        glm::mat4 view = glm::lookAt(eye + camTarget, camTarget, up);
        float aspect = swapExtent.width / (float)swapExtent.height;
        glm::mat4 proj = glm::perspective(glm::radians(45.0f / zoomLevel), aspect, 0.1f, 100.0f);
        proj[1][1] *= -1;
        return proj * view;
    }

    struct Ray { glm::vec3 origin, dir; };

    Ray screenToRay(float sx, float sy) const {
        float ndcX = (sx / WIDTH) * 2.0f - 1.0f;
        float ndcY = -((sy / HEIGHT) * 2.0f - 1.0f);
        glm::mat4 invPV = glm::inverse(currentViewProj());
        glm::vec4 nearP = invPV * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        nearP /= nearP.w;
        glm::vec4 farP = invPV * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
        farP /= farP.w;
        return {glm::vec3(nearP), glm::normalize(glm::vec3(farP) - glm::vec3(nearP))};
    }

    bool rayTri(const glm::vec3& ro, const glm::vec3& rd,
                const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                float& t, float maxDist = 1.5f) const {
        glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
        glm::vec3 p = glm::cross(rd, e2);
        float det = glm::dot(e1, p);
        if (fabs(det) < 1e-7f) return false;
        float invDet = 1.0f / det;
        glm::vec3 tv = ro - v0;
        float u = glm::dot(tv, p) * invDet;
        if (u < 0 || u > 1) return false;
        glm::vec3 q = glm::cross(tv, e1);
        float v = glm::dot(rd, q) * invDet;
        if (v < 0 || u + v > 1) return false;
        t = glm::dot(e2, q) * invDet;
        return t > 0.001f && t < maxDist;
    }

    bool rayMeshHit(const Ray& ray, glm::vec3& hitPoint) const {
        if (currentMeshVertices.empty() || meshIndices.empty()) return false;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), modelPos) * glm::mat4_cast(modelRot);
        float bestT = 1e9f;
        bool hit = false;
        for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
            const auto& v0 = currentMeshVertices[meshIndices[i]];
            const auto& v1 = currentMeshVertices[meshIndices[i+1]];
            const auto& v2 = currentMeshVertices[meshIndices[i+2]];
            // Transform to world space
            glm::vec3 wv0 = glm::vec3(model * glm::vec4(v0.x, v0.y, v0.z, 1.0f));
            glm::vec3 wv1 = glm::vec3(model * glm::vec4(v1.x, v1.y, v1.z, 1.0f));
            glm::vec3 wv2 = glm::vec3(model * glm::vec4(v2.x, v2.y, v2.z, 1.0f));
            float t;
            if (rayTri(ray.origin, ray.dir, wv0, wv1, wv2, t) && t < bestT) {
                bestT = t;
                hitPoint = ray.origin + ray.dir * t;
                hit = true;
            }
        }
        return hit;
    }

    void sculptDisplace(const std::vector<glm::vec3>& hits) {
        if (hits.empty() || currentMeshVertices.empty()) return;
        glm::mat4 invModel = glm::inverse(
            glm::translate(glm::mat4(1.0f), modelPos) * glm::mat4_cast(modelRot));
        float strength = fabs(brushThickness);
        int sign = (brushThickness > 0) ? 1 : -1;

        for (auto& v : currentMeshVertices) {
            glm::vec3 modelP(v.x, v.y, v.z);
            float minDist = 1e9f;
            for (auto& h : hits) {
                glm::vec3 localH = glm::vec3(invModel * glm::vec4(h, 1.0f));
                float d = glm::distance(modelP, localH);
                if (d < minDist) minDist = d;
            }
            if (minDist < strength * 2.0f) {
                float falloff = 1.0f - (minDist / (strength * 2.0f));
                falloff = falloff * falloff;
                v.x += v.nx * strength * falloff * sign;
                v.y += v.ny * strength * falloff * sign;
                v.z += v.nz * strength * falloff * sign;
            }
        }
        meshDirty = true;
    }

    // ─── M6: Stroke → Tube Mesh ──────────────────────────────────────

    // Project a 2D screen point to 3D world space.
    // Places the point on a plane through camTarget, perpendicular to view direction.
    glm::vec3 projectToWorld(float sx, float sy) const {
        Ray ray = screenToRay(sx, sy);
        // Compute view direction
        float cx = camDist * sin(camPhi) * cos(camTheta);
        float cy = camDist * cos(camPhi);
        float cz = camDist * sin(camPhi) * sin(camTheta);
        glm::vec3 eye(cx, cy, cz);
        glm::vec3 viewDir = glm::normalize(camTarget - eye);
        // Ray-plane intersection: plane through camTarget with normal = viewDir
        float denom = glm::dot(ray.dir, viewDir);
        if (fabs(denom) < 1e-6f) return camTarget;
        float t = glm::dot(camTarget - ray.origin, viewDir) / denom;
        return ray.origin + ray.dir * t;
    }

    // Generate a tube mesh from a sequence of 3D world-space points.
    // Uses Catmull-Rom spline resampling, Frenet frames, and circular cross-sections.
    Mesh generateTubeMesh(const std::vector<glm::vec3>& path, float radius, int radialSegs = 16) {
        Mesh result;
        if (path.size() < 2) return result;

        // --- Detect closed curve ---
        bool closed = false;
        std::vector<glm::vec3> pts = path;
        if (pts.size() >= 3 && glm::distance(pts.front(), pts.back()) < radius * 2.0f) {
            closed = true;
        }

        // --- Catmull-Rom spline resampling ---
        auto catmullRom = [](const glm::vec3& p0, const glm::vec3& p1,
                              const glm::vec3& p2, const glm::vec3& p3, float t) -> glm::vec3 {
            float t2 = t * t, t3 = t2 * t;
            return 0.5f * ((2.0f * p1) +
                           (-p0 + p2) * t +
                           (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                           (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        };

        int n = (int)pts.size();
        int resampleCount = closed ? 64 : std::min(64, n * 4);
        std::vector<glm::vec3> spline(resampleCount);

        for (int i = 0; i < resampleCount; i++) {
            float t = (float)i / (float)(resampleCount - (closed ? 0 : 1));
            float globalT = t * (float)(closed ? n : n - 1);
            int seg = (int)glm::clamp(globalT, 0.0f, (float)(closed ? n - 1 : n - 2));
            float localT = globalT - (float)seg;

            int i0 = seg - 1, i1 = seg, i2 = seg + 1, i3 = seg + 2;
            if (closed) {
                i0 = (i0 + n) % n;
                i1 = i1 % n;
                i2 = i2 % n;
                i3 = i3 % n;
            } else {
                i0 = std::max(0, i0);
                i2 = std::min(n - 1, i2);
                i3 = std::min(n - 1, i3);
            }
            spline[i] = catmullRom(pts[i0], pts[i1], pts[i2], pts[i3], localT);
        }

        // --- Frenet frames + ring generation ---
        struct Frame { glm::vec3 N, B; };
        std::vector<glm::vec3> tangents(resampleCount);
        std::vector<Frame> frames(resampleCount);

        for (int i = 0; i < resampleCount; i++) {
            int next = (i + 1) % resampleCount;
            int prev = (i - 1 + resampleCount) % resampleCount;
            if (!closed && i == 0) prev = 0;
            if (!closed && i == resampleCount - 1) next = i;

            tangents[i] = glm::normalize(spline[next] - spline[prev]);
            if (glm::length(tangents[i]) < 1e-6f) {
                tangents[i] = glm::vec3(0, 1, 0);
            }
        }

        // First frame: pick arbitrary normal, then orthogonalize
        glm::vec3 up(0, 1, 0);
        if (fabs(glm::dot(tangents[0], up)) > 0.99f) up = glm::vec3(1, 0, 0);
        frames[0].N = glm::normalize(glm::cross(tangents[0], up));
        frames[0].B = glm::normalize(glm::cross(tangents[0], frames[0].N));

        // Propagate frames along curve (parallel transport / rotation minimization)
        for (int i = 1; i < resampleCount; i++) {
            glm::vec3 prevT = tangents[i-1];
            glm::vec3 curT = tangents[i];
            glm::vec3 prevN = frames[i-1].N;
            glm::vec3 prevB = frames[i-1].B;

            // Rotate previous frame to align with current tangent
            glm::vec3 axis = glm::cross(prevT, curT);
            float angle = acos(glm::clamp(glm::dot(prevT, curT), -1.0f, 1.0f));
            if (glm::length(axis) < 1e-6f) {
                // Parallel tangents, just copy
                frames[i].N = prevN;
                frames[i].B = prevB;
            } else {
                axis = glm::normalize(axis);
                glm::quat rot = glm::angleAxis(angle, axis);
                frames[i].N = glm::normalize(rot * prevN);
                frames[i].B = glm::normalize(rot * prevB);
            }
        }

        // --- Generate vertices (circular rings) ---
        for (int i = 0; i < resampleCount; i++) {
            for (int j = 0; j < radialSegs; j++) {
                float theta = 2.0f * M_PI * j / radialSegs;
                float ct = cos(theta), st = sin(theta);
                glm::vec3 normal = ct * frames[i].N + st * frames[i].B;
                glm::vec3 pos = spline[i] + normal * radius;

                Vertex v;
                v.x = pos.x; v.y = pos.y; v.z = pos.z;
                v.nx = normal.x; v.ny = normal.y; v.nz = normal.z;
                result.vertices.push_back(v);
            }
        }

        // --- Connect rings with triangles ---
        for (int i = 0; i < resampleCount; i++) {
            int nextRing = (i + 1) % resampleCount;
            if (!closed && i == resampleCount - 1) continue; // last ring: no next

            for (int j = 0; j < radialSegs; j++) {
                int a = i * radialSegs + j;
                int b = i * radialSegs + (j + 1) % radialSegs;
                int c = nextRing * radialSegs + j;
                int d = nextRing * radialSegs + (j + 1) % radialSegs;

                result.indices.push_back(a);
                result.indices.push_back(c);
                result.indices.push_back(b);

                result.indices.push_back(b);
                result.indices.push_back(c);
                result.indices.push_back(d);
            }
        }

        // --- End caps (for open curves) ---
        if (!closed) {
            auto addCap = [&](int ringIdx, bool isStart) {
                int centerIdx = (int)result.vertices.size();
                glm::vec3 center = spline[ringIdx];
                Vertex cv;
                cv.x = center.x; cv.y = center.y; cv.z = center.z;
                // Cap normal points along the tangent direction (outward)
                glm::vec3 capN = isStart ? -tangents[ringIdx] : tangents[ringIdx];
                cv.nx = capN.x; cv.ny = capN.y; cv.nz = capN.z;
                result.vertices.push_back(cv);

                for (int j = 0; j < radialSegs; j++) {
                    int a = ringIdx * radialSegs + j;
                    int b = ringIdx * radialSegs + (j + 1) % radialSegs;
                    if (isStart) {
                        result.indices.push_back(centerIdx);
                        result.indices.push_back(b);
                        result.indices.push_back(a);
                    } else {
                        result.indices.push_back(centerIdx);
                        result.indices.push_back(a);
                        result.indices.push_back(b);
                    }
                }
            };
            addCap(0, true);                          // start cap
            addCap(resampleCount - 1, false);         // end cap
        }

        return result;
    }

    // Convert collected 2D stroke path to 3D tube mesh and replace the current mesh.
    void generateTubeFromStroke() {
        if (strokePath2D.size() < 2) return;

        // Project 2D screen points to 3D world space
        std::vector<glm::vec3> worldPath;
        worldPath.reserve(strokePath2D.size());
        for (auto& p : strokePath2D) {
            worldPath.push_back(projectToWorld(p.x, p.y));
        }

        // Generate tube mesh
        Mesh tube = generateTubeMesh(worldPath, tubeRadius, 16);
        if (tube.vertices.empty()) return;

        // Replace current mesh with tube
        currentMeshVertices = tube.vertices;
        meshIndices = tube.indices;
        baseVertices = tube.vertices;  // reset sculpt baseline
        meshIdxCount = (uint32_t)tube.indices.size();
        meshDirty = true;
        selObject = 0;  // auto-select the new tube

        printf("Tube: %zu vertices, %zu triangles, %zu path points\n",
               tube.vertices.size(), tube.indices.size() / 3, strokePath2D.size());

        // Clear the 2D path for next stroke
        strokePath2D.clear();
    }

    // ─── M4: Decimate toggle ──────────────────────────────────────────
    void toggleDecimate() {
        if (currentMeshVertices.empty()) return;

        if (!showDecimated) {
            // Decimate current mesh
            decimated = MeshPostProcess::decimate(currentMeshVertices, meshIndices, 0.10f);
            printf("Decimated: %u→%u triangles (%.1f%%)\n",
                decimated.originalTris, decimated.reducedTris, decimated.reductionRatio * 100.0f);
            if (decimated.vertices.empty()) {
                printf("Decimation produced empty mesh, aborting\n");
                return;
            }
        }
        showDecimated = !showDecimated;
        meshDirty = true; // triggers VBO rebuild with correct mesh
        updateTitle();
    }

    // ─── M4/M7: Export mesh as glTF ──────────────────────────────────────
    void exportMesh() {
        if (currentMeshVertices.empty()) return;

        // Create exports dir if needed
        std::string mkdirCmd = "mkdir -p " + exportDir;
        if (system(mkdirCmd.c_str()) != 0) {
            fprintf(stderr, "Warning: failed to create exports dir\n");
        }

        // M7: If in expression mode, export face + morph targets
        if (expressionMode && expressionsInitialized) {
            exportExpressionGLB();
            return;
        }

        // Export original (high-poly) with mesh color
        std::string origPath = exportDir + "/imagemaker_original.glb";
        GLTFExporter::exportOriginalGLB(origPath, currentMeshVertices, meshIndices, meshColor);

        // Decimate and export game-ready version with mesh color
        auto dec = MeshPostProcess::decimate(currentMeshVertices, meshIndices, 0.10f);
        if (!dec.vertices.empty()) {
            std::string gamePath = exportDir + "/imagemaker_game.glb";
            GLTFExporter::exportGLB(gamePath, dec.vertices, dec.indices, meshColor);

            // Store for preview
            decimated = dec;
            showDecimated = true;
            meshDirty = true;
            updateTitle();
            printf("Export complete: %s + %s\n", origPath.c_str(), gamePath.c_str());
        } else {
            printf("Decimation failed, exported original only\n");
        }
    }

    void updateMeshVBO() {
        if (!meshDirty) return;

        std::vector<Vertex> gpuVertices;
        std::vector<uint32_t> gpuIndices;

        if (showDecimated && !decimated.vertices.empty()) {
            // Convert UVVertex → Vertex for GPU upload
            gpuVertices.reserve(decimated.vertices.size());
            for (auto& dv : decimated.vertices) {
                Vertex v;
                v.x = dv.x; v.y = dv.y; v.z = dv.z;
                v.nx = dv.nx; v.ny = dv.ny; v.nz = dv.nz;
                gpuVertices.push_back(v);
            }
            gpuIndices = decimated.indices;
        } else if (!currentMeshVertices.empty()) {
            gpuVertices = currentMeshVertices;
            gpuIndices = meshIndices;
        } else {
            meshDirty = false;
            return;
        }

        meshIdxCount = (uint32_t)gpuIndices.size();

        VkDeviceSize vSize = sizeof(Vertex) * gpuVertices.size();
        VkDeviceSize iSize = sizeof(uint32_t) * gpuIndices.size();

        vkDeviceWaitIdle(device);
        vkDestroyBuffer(device, vbo, nullptr);
        vkFreeMemory(device, vboMem, nullptr);
        vkDestroyBuffer(device, ibo, nullptr);
        vkFreeMemory(device, iboMem, nullptr);

        createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem);
        createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem);
        copyToBuffer(vbo, gpuVertices.data(), vSize);
        copyToBuffer(ibo, gpuIndices.data(), iSize);

        // Update CPU-side index copy if showing original (for ray tracing)
        if (!showDecimated) {
            // currentMeshVertices and meshIndices already reflect the live mesh
        }

        meshDirty = false;
    }

    // ─── Vulkan Init ─────────────────────────────────────────────────
    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createDepthResources();
        createDescriptorSetLayout();
        createPipelines();
        createFramebuffers();
        createCommandPool();
        initImGui();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    void createInstance() {
        VkApplicationInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName = "ImageMaker"; ai.apiVersion = VK_API_VERSION_1_3;
        uint32_t glfwExtCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
        std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCount);
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        VkInstanceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &ai;
        ci.enabledExtensionCount = (uint32_t)exts.size(); ci.ppEnabledExtensionNames = exts.data();
        const char* layer = "VK_LAYER_KHRONOS_validation";
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availLayers.data());
        bool hasValidation = false;
        for (auto& l : availLayers) {
            if (strcmp(l.layerName, layer) == 0) { hasValidation = true; break; }
        }
        std::vector<const char*> enabledLayers;
        if (hasValidation) enabledLayers.push_back(layer);
        ci.enabledLayerCount = (uint32_t)enabledLayers.size();
        ci.ppEnabledLayerNames = enabledLayers.data();

        // Debug messenger create info
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dbgCI.pfnUserCallback = debugCallback;
        if (hasValidation) ci.pNext = &dbgCI;

        vkCreateInstance(&ci, nullptr, &instance);
        if (hasValidation) {
            auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
            vkCreateDebugUtilsMessengerEXT(instance, &dbgCI, nullptr, &debugMsgr);
        }
    }

    void createSurface() { glfwCreateWindowSurface(instance, window, nullptr, &surface); }

    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(instance, &count, devs.data());
        physDev = devs[0]; // Take first (discrete GPU preferred)
    }

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice pd) {
        QueueFamilyIndices idx;
        uint32_t cnt = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &cnt, nullptr);
        std::vector<VkQueueFamilyProperties> qf(cnt);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &cnt, qf.data());
        for (uint32_t i = 0; i < cnt; i++) {
            if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) idx.graphics = i;
            VkBool32 present = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &present);
            if (present) idx.present = i;
            if (idx.complete()) break;
        }
        return idx;
    }

    void createLogicalDevice() {
        auto qfi = findQueueFamilies(physDev);
        std::set<uint32_t> unique = {qfi.graphics.value(), qfi.present.value()};
        std::vector<VkDeviceQueueCreateInfo> qcis;
        float prio = 1.0f;
        for (auto u : unique) {
            VkDeviceQueueCreateInfo qc{};
            qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qc.queueFamilyIndex = u; qc.queueCount = 1; qc.pQueuePriorities = &prio;
            qcis.push_back(qc);
        }
        std::vector<const char*> devExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = (uint32_t)qcis.size(); ci.pQueueCreateInfos = qcis.data();
        ci.enabledExtensionCount = (uint32_t)devExts.size(); ci.ppEnabledExtensionNames = devExts.data();
        vkCreateDevice(physDev, &ci, nullptr, &device);
        vkGetDeviceQueue(device, qfi.graphics.value(), 0, &gfxQueue);
        vkGetDeviceQueue(device, qfi.present.value(), 0, &presentQueue);
    }

    SwapChainDetails querySwapChain(VkPhysicalDevice pd) {
        SwapChainDetails d;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &d.caps);
        uint32_t cnt = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &cnt, nullptr);
        d.formats.resize(cnt);
        vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &cnt, d.formats.data());
        cnt = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &cnt, nullptr);
        d.modes.resize(cnt);
        vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &cnt, d.modes.data());
        return d;
    }

    void createSwapChain() {
        auto d = querySwapChain(physDev);
        VkSurfaceFormatKHR fmt = d.formats[0];
        for (auto& f : d.formats)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                { fmt = f; break; }
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto& m : d.modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR) { mode = m; break; }
        swapExtent = d.caps.currentExtent;
        swapFormat = fmt.format;

        VkSwapchainCreateInfoKHR ci{}; ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = surface; ci.minImageCount = d.caps.minImageCount + 1;
        ci.imageFormat = fmt.format; ci.imageColorSpace = fmt.colorSpace;
        ci.imageExtent = swapExtent; ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        auto qfi = findQueueFamilies(physDev);
        uint32_t idxs[] = {qfi.graphics.value(), qfi.present.value()};
        if (idxs[0] != idxs[1]) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2; ci.pQueueFamilyIndices = idxs;
        } else ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.preTransform = d.caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = mode; ci.clipped = VK_TRUE;
        vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);
        uint32_t cnt = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, nullptr);
        swapImages.resize(cnt);
        vkGetSwapchainImagesKHR(device, swapchain, &cnt, swapImages.data());
    }

    void createImageViews() {
        swapViews.resize(swapImages.size());
        for (size_t i = 0; i < swapImages.size(); i++) {
            VkImageViewCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image = swapImages[i]; ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = swapFormat;
            ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.levelCount = ci.subresourceRange.layerCount = 1;
            vkCreateImageView(device, &ci, nullptr, &swapViews[i]);
        }
    }

    void createRenderPass() {
        VkAttachmentDescription ca{}; ca.format = swapFormat;
        ca.samples = VK_SAMPLE_COUNT_1_BIT;
        ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ca.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription da{};
        da.format = VK_FORMAT_D32_SFLOAT;
        da.samples = VK_SAMPLE_COUNT_1_BIT;
        da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        da.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        da.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ar{}; ar.attachment = 0; ar.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference dr{}; dr.attachment = 1; dr.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &ar;
        sp.pDepthStencilAttachment = &dr;

        VkAttachmentDescription atts[] = {ca, da};
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 2; ci.pAttachments = atts;
        ci.subpassCount = 1; ci.pSubpasses = &sp;
        ci.dependencyCount = 1; ci.pDependencies = &dep;
        vkCreateRenderPass(device, &ci, nullptr, &renderPass);
    }

    void createDepthResources() {
        VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = depthFormat;
        ici.extent = {swapExtent.width, swapExtent.height, 1};
        ici.mipLevels = 1; ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(device, &ici, nullptr, &depthImage);
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, depthImage, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkAllocateMemory(device, &mai, nullptr, &depthMem);
        vkBindImageMemory(device, depthImage, depthMem, 0);

        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = depthImage; vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = depthFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vci.subresourceRange.levelCount = vci.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &vci, nullptr, &depthView);
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding bnd{};
        bnd.binding = 0; bnd.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bnd.descriptorCount = 1; bnd.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1; ci.pBindings = &bnd;
        vkCreateDescriptorSetLayout(device, &ci, nullptr, &dsetLayout);
    }

    void createPipelines() {
        auto meshVert = readFile("shaders/mesh.vert.spv");
        auto meshFrag = readFile("shaders/mesh.frag.spv");
        auto toonFrag = readFile("shaders/toon.frag.spv");
        auto outlineVert = readFile("shaders/outline.vert.spv");
        auto outlineFrag = readFile("shaders/outline.frag.spv");
        auto gridVert = readFile("shaders/grid.vert.spv");
        auto gridFrag = readFile("shaders/grid.frag.spv");

        VkShaderModule mvs = createShaderModule(device, meshVert);
        VkShaderModule mfs = createShaderModule(device, meshFrag);
        VkShaderModule tfs = createShaderModule(device, toonFrag);
        VkShaderModule ovs = createShaderModule(device, outlineVert);
        VkShaderModule ofs = createShaderModule(device, outlineFrag);
        VkShaderModule gvs = createShaderModule(device, gridVert);
        VkShaderModule gfs = createShaderModule(device, gridFrag);

        // Pipeline layout
        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0; pcr.size = sizeof(PushConst);
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1; plci.pSetLayouts = &dsetLayout;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &pcr;
        vkCreatePipelineLayout(device, &plci, nullptr, &pipeLayout);

        // Build mesh pipeline (Phong)
        meshPipe = createGraphicsPipeline(mvs, mfs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            sizeof(Vertex), offsetof(Vertex, x), offsetof(Vertex, nx));
        // Build toon pipeline (cel shading + rim light)
        toonPipe = createGraphicsPipeline(mvs, tfs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            sizeof(Vertex), offsetof(Vertex, x), offsetof(Vertex, nx));
        // Build outline pipeline (inverted hull, backfaces only)
        outlinePipe = createGraphicsPipeline(ovs, ofs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            sizeof(Vertex), offsetof(Vertex, x), offsetof(Vertex, nx));
        // Build grid pipeline (for lines)
        gridPipe = createGraphicsPipeline(gvs, gfs, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            sizeof(glm::vec3) + sizeof(glm::vec4), 0, sizeof(glm::vec3));

        vkDestroyShaderModule(device, mvs, nullptr);
        vkDestroyShaderModule(device, mfs, nullptr);
        vkDestroyShaderModule(device, tfs, nullptr);
        vkDestroyShaderModule(device, ovs, nullptr);
        vkDestroyShaderModule(device, ofs, nullptr);
        vkDestroyShaderModule(device, gvs, nullptr);
        vkDestroyShaderModule(device, gfs, nullptr);
    }

    VkPipeline createGraphicsPipeline(VkShaderModule vs, VkShaderModule fs,
        VkPrimitiveTopology topo, uint32_t stride, uint32_t posOff, uint32_t normOff,
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE) {
        VkPipelineShaderStageCreateInfo ssi[2]{};
        ssi[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssi[0].stage = VK_SHADER_STAGE_VERTEX_BIT; ssi[0].module = vs; ssi[0].pName = "main";
        ssi[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ssi[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; ssi[1].module = fs; ssi[1].pName = "main";

        VkVertexInputBindingDescription vib{};
        vib.binding = 0; vib.stride = stride; vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        VkVertexInputAttributeDescription via[2]{};
        via[0].binding = 0; via[0].location = 0; via[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        via[0].offset = posOff;
        via[1].binding = 0; via[1].location = 1; via[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        via[1].offset = normOff;

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount = 1; vis.pVertexBindingDescriptions = &vib;
        vis.vertexAttributeDescriptionCount = 2; vis.pVertexAttributeDescriptions = via;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = topo;

        VkViewport vp{}; vp.x = vp.y = 0; vp.width = (float)swapExtent.width;
        vp.height = (float)swapExtent.height; vp.minDepth = 0; vp.maxDepth = 1;
        VkRect2D sc{}; sc.extent = swapExtent;
        VkPipelineViewportStateCreateInfo vps{};
        vps.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vps.viewportCount = 1; vps.pViewports = &vp; vps.scissorCount = 1; vps.pScissors = &sc;

        VkPipelineRasterizationStateCreateInfo rss{};
        rss.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rss.lineWidth = 1.0f; rss.polygonMode = VK_POLYGON_MODE_FILL;
        rss.cullMode = cullMode; rss.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo mss{};
        mss.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        mss.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
            VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1; cbs.pAttachments = &cba;

        VkPipelineDepthStencilStateCreateInfo dss{};
        dss.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        dss.depthTestEnable = VK_TRUE; dss.depthWriteEnable = VK_TRUE;
        dss.depthCompareOp = VK_COMPARE_OP_LESS;

        VkGraphicsPipelineCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        ci.stageCount = 2; ci.pStages = ssi;
        ci.pVertexInputState = &vis; ci.pInputAssemblyState = &ias;
        ci.pViewportState = &vps; ci.pRasterizationState = &rss;
        ci.pMultisampleState = &mss; ci.pColorBlendState = &cbs;
        ci.pDepthStencilState = &dss;
        ci.layout = pipeLayout; ci.renderPass = renderPass; ci.subpass = 0;

        VkPipeline pipe;
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe);
        return pipe;
    }

    void createFramebuffers() {
        framebuffers.resize(swapViews.size());
        for (size_t i = 0; i < swapViews.size(); i++) {
            VkImageView att[] = {swapViews[i], depthView};
            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = renderPass; ci.attachmentCount = 2; ci.pAttachments = att;
            ci.width = swapExtent.width; ci.height = swapExtent.height; ci.layers = 1;
            vkCreateFramebuffer(device, &ci, nullptr, &framebuffers[i]);
        }
    }

    void createCommandPool() {
        auto qfi = findQueueFamilies(physDev);
        VkCommandPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = qfi.graphics.value();
        ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCreateCommandPool(device, &ci, nullptr, &cmdPool);
    }

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    void initImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;

        ImGui_ImplGlfw_InitForVulkan(window, true);

        // Descriptor pool for ImGui
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
            {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
        };
        VkDescriptorPoolCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = 1000 * IM_ARRAYSIZE(poolSizes);
        pi.poolSizeCount = IM_ARRAYSIZE(poolSizes);
        pi.pPoolSizes = poolSizes;
        vkCreateDescriptorPool(device, &pi, nullptr, &imguiPool);

        ImGui_ImplVulkan_InitInfo vi{};
        vi.Instance = instance;
        vi.PhysicalDevice = physDev;
        vi.Device = device;
        vi.QueueFamily = findQueueFamilies(physDev).graphics.value();
        vi.Queue = gfxQueue;
        vi.DescriptorPool = imguiPool;
        vi.PipelineCache = VK_NULL_HANDLE;
        vi.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        vi.ImageCount = MAX_FRAMES_IN_FLIGHT;
        vi.PipelineInfoMain.RenderPass = renderPass;
        vi.PipelineInfoMain.Subpass = 0;
        vi.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&vi);
    }

    VkCommandBuffer beginSingleTimeCommands() {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = cmdPool; ai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(device, &ai, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        return cb;
    }

    void endSingleTimeCommands(VkCommandBuffer cb) {
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(gfxQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(gfxQueue);
        vkFreeCommandBuffers(device, cmdPool, 1, &cb);
    }

    // ─── Buffer helpers ───────────────────────────────────────────────
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((typeFilter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        return 0;
    }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                      VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &ci, nullptr, &buf);
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, buf, &mr);
        VkMemoryAllocateInfo ai{}; ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits, props);
        vkAllocateMemory(device, &ai, nullptr, &mem);
        vkBindBufferMemory(device, buf, mem, 0);
    }

    void copyToBuffer(VkBuffer dst, const void* data, VkDeviceSize size) {
        VkBuffer staging; VkDeviceMemory stagingMem;
        createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingMem);
        void* ptr;
        vkMapMemory(device, stagingMem, 0, size, 0, &ptr);
        memcpy(ptr, data, size);
        vkUnmapMemory(device, stagingMem);

        // One-time submit
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool = cmdPool; ai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(device, &ai, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy bc{}; bc.size = size;
        vkCmdCopyBuffer(cb, staging, dst, 1, &bc);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(gfxQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(gfxQueue);
        vkFreeCommandBuffers(device, cmdPool, 1, &cb);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
    }

    // ─── Mesh initialization ─────────────────────────────────────────
    void initMeshes() {
        // Generate test mesh (sphere)
        Mesh m = PrimitiveGenerator::generateIcoSphere(1.0f, 3);
        meshIdxCount = (uint32_t)m.indices.size();
        baseVertices = m.vertices;
        currentMeshVertices = m.vertices;
        meshIndices = m.indices;
        VkDeviceSize vSize = sizeof(Vertex) * m.vertices.size();
        VkDeviceSize iSize = sizeof(uint32_t) * m.indices.size();
        createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vbo, vboMem);
        createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ibo, iboMem);
        copyToBuffer(vbo, m.vertices.data(), vSize);
        copyToBuffer(ibo, m.indices.data(), iSize);

        // Generate grid (lines on XZ plane)
        initGrid();

        // Generate axis lines (X=red, Y=green, Z=blue)
        initAxis();
    }

    void initGrid() {
        std::vector<GVertex> verts;
        std::vector<uint32_t> idxs;
        float s = 5.0f, step = 1.0f;
        glm::vec4 gray(0.3f, 0.3f, 0.3f, 1.0f);
        for (float i = -s; i <= s; i += step) {
            uint32_t base = (uint32_t)verts.size();
            verts.push_back({glm::vec3(i, 0, -s), gray});
            verts.push_back({glm::vec3(i, 0, s), gray});
            idxs.push_back(base); idxs.push_back(base+1);
            base = (uint32_t)verts.size();
            verts.push_back({glm::vec3(-s, 0, i), gray});
            verts.push_back({glm::vec3(s, 0, i), gray});
            idxs.push_back(base); idxs.push_back(base+1);
        }
        gridIdxCount = (uint32_t)idxs.size();
        VkDeviceSize vSize = sizeof(GVertex) * verts.size();
        VkDeviceSize iSize = sizeof(uint32_t) * idxs.size();
        createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gridVbo, gridVboMem);
        createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gridIbo, gridIboMem);
        copyToBuffer(gridVbo, verts.data(), vSize);
        copyToBuffer(gridIbo, idxs.data(), iSize);
    }

    void initAxis() {
        std::vector<GVertex> verts;
        std::vector<uint32_t> idxs;
        float len = 3.0f;
        glm::vec4 red(1,0,0,1), green(0,1,0,1), blue(0,0,1,1);
        // X axis (red)
        uint32_t base = (uint32_t)verts.size();
        verts.push_back({glm::vec3(0,0,0), red});
        verts.push_back({glm::vec3(len,0,0), red});
        idxs.push_back(base); idxs.push_back(base+1);
        // Y axis (green)
        base = (uint32_t)verts.size();
        verts.push_back({glm::vec3(0,0,0), green});
        verts.push_back({glm::vec3(0,len,0), green});
        idxs.push_back(base); idxs.push_back(base+1);
        // Z axis (blue)
        base = (uint32_t)verts.size();
        verts.push_back({glm::vec3(0,0,0), blue});
        verts.push_back({glm::vec3(0,0,len), blue});
        idxs.push_back(base); idxs.push_back(base+1);
        axisIdxCount = (uint32_t)idxs.size();
        VkDeviceSize vSize = sizeof(GVertex) * verts.size();
        VkDeviceSize iSize = sizeof(uint32_t) * idxs.size();
        createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, axisVbo, axisVboMem);
        createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, axisIbo, axisIboMem);
        copyToBuffer(axisVbo, verts.data(), vSize);
        copyToBuffer(axisIbo, idxs.data(), iSize);
    }

    // ─── Uniform buffers ─────────────────────────────────────────────
    void createUniformBuffers() {
        VkDeviceSize sz = sizeof(UBO);
        uboBufs.resize(MAX_FRAMES_IN_FLIGHT);
        uboMems.resize(MAX_FRAMES_IN_FLIGHT);
        uboMapped.resize(MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            createBuffer(sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                uboBufs[i], uboMems[i]);
            vkMapMemory(device, uboMems[i], 0, sz, 0, &uboMapped[i]);
        }
    }

    void createDescriptorPool() {
        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; ps.descriptorCount = MAX_FRAMES_IN_FLIGHT;
        VkDescriptorPoolCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.maxSets = MAX_FRAMES_IN_FLIGHT; ci.poolSizeCount = 1; ci.pPoolSizes = &ps;
        vkCreateDescriptorPool(device, &ci, nullptr, &descPool);
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, dsetLayout);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = descPool; ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        ai.pSetLayouts = layouts.data();
        descSets.resize(MAX_FRAMES_IN_FLIGHT);
        vkAllocateDescriptorSets(device, &ai, descSets.data());
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorBufferInfo bi{};
            bi.buffer = uboBufs[i]; bi.offset = 0; bi.range = sizeof(UBO);
            VkWriteDescriptorSet wd{};
            wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wd.dstSet = descSets[i]; wd.dstBinding = 0; wd.descriptorCount = 1;
            wd.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            wd.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device, 1, &wd, 0, nullptr);
        }
    }

    void createCommandBuffers() {
        cmdBufs.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)cmdBufs.size();
        vkAllocateCommandBuffers(device, &ai, cmdBufs.data());
    }

    void createSyncObjects() {
        imgAvail.resize(MAX_FRAMES_IN_FLIGHT);
        renderDone.resize(MAX_FRAMES_IN_FLIGHT);
        inFlight.resize(MAX_FRAMES_IN_FLIGHT);
        VkSemaphoreCreateInfo si{}; si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fi{}; fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(device, &si, nullptr, &imgAvail[i]);
            vkCreateSemaphore(device, &si, nullptr, &renderDone[i]);
            vkCreateFence(device, &fi, nullptr, &inFlight[i]);
        }
    }

    // ─── Main loop ───────────────────────────────────────────────────
    void mainLoop() {
        auto lastTime = glfwGetTime();
        int frameCount = 0;
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            // Start ImGui frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Color picker window (C key toggle)
            if (paletteOpen) {
                ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_FirstUseEver);
                ImGui::Begin("Color Picker##M5", &paletteOpen,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
                ImGui::ColorPicker4("##picker", (float*)&meshColor,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayRGB |
                    ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueWheel);
                ImGui::End();
            }

            // M8: Lip-sync panel (G key toggle)
            if (lipSyncUIOpen) {
                ImGui::SetNextWindowPos(ImVec2(360, 10), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(380, 300), ImGuiCond_FirstUseEver);
                ImGui::Begin("Lip-Sync##M8", &lipSyncUIOpen,
                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
                ImGui::Text("Text to animate:");
                ImGui::InputTextMultiline("##text", lipSyncText, sizeof(lipSyncText),
                    ImVec2(-1, 60));
                if (ImGui::Button("Generate Track", ImVec2(-1, 0))) {
                    lipSyncGenerate();
                }
                ImGui::Separator();
                if (lipSyncTrack.totalDuration > 0.0f) {
                    ImGui::Text("Duration: %.2fs | Keys: %zu | Viseme: %s",
                        lipSyncTrack.totalDuration, lipSyncTrack.keys.size(),
                        currentViseme >= 0 ? VisemeConfig::visemeName(currentViseme) : "-");
                    ImGui::Text("Transcript: %s", lipSyncTrack.transcript.c_str());

                    // Playback controls
                    if (!lipSyncPlaying) {
                        if (ImGui::Button("Play##ls", ImVec2(70, 0))) {
                            lipSyncTime = 0.0f;
                            lipSyncPlaying = true;
                        }
                        ImGui::SameLine();
                    } else {
                        if (ImGui::Button("Stop##ls", ImVec2(70, 0))) {
                            lipSyncPlaying = false;
                            lipSyncTime = 0.0f;
                            applyViseme(VISEME_REST, 1.0f);
                            updateTitle();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Pause##ls", ImVec2(70, 0))) {
                            lipSyncPlaying = false;
                        }
                        ImGui::SameLine();
                    }
                    if (ImGui::Button("Reset##ls", ImVec2(70, 0))) {
                        lipSyncPlaying = false;
                        lipSyncTime = 0.0f;
                        currentViseme = -1;
                        applyViseme(VISEME_REST, 1.0f);
                        updateTitle();
                    }

                    // Timeline progress bar
                    float progress = lipSyncTime / lipSyncTrack.totalDuration;
                    ImGui::ProgressBar(progress, ImVec2(-1, 20));
                    ImGui::Text("Time: %.2f / %.2fs", lipSyncTime, lipSyncTrack.totalDuration);

                    // Viseme preview: show which viseme maps to which keys
                    if (ImGui::CollapsingHeader("Viseme Keys")) {
                        for (size_t i = 0; i < lipSyncTrack.keys.size() && i < 50; i++) {
                            bool active = (currentViseme == lipSyncTrack.keys[i].viseme);
                            if (active) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
                            ImGui::Text("%3zu: t=%.2f %s",
                                i, lipSyncTrack.keys[i].time,
                                VisemeConfig::visemeName(lipSyncTrack.keys[i].viseme));
                            if (active) ImGui::PopStyleColor();
                        }
                        if (lipSyncTrack.keys.size() > 50) {
                            ImGui::Text("... (%zu total keys)", lipSyncTrack.keys.size());
                        }
                    }
                } else {
                    ImGui::Text("Enter text and click 'Generate Track'.");
                }
                ImGui::End();
            }

            // M8: Update lip-sync animation
            if (lipSyncPlaying) {
                static auto lastLipTime = glfwGetTime();
                auto nowLipTime = glfwGetTime();
                float dt = (float)(nowLipTime - lastLipTime);
                lastLipTime = nowLipTime;
                updateLipSync(dt);
            }

            // Render
            ImGui::Render();
            drawFrame();
            frameCount++;
            auto now = glfwGetTime();
            if (now - lastTime >= 1.0) {
                if (lipSyncPlaying) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "ImageMaker M8 | LipSync: %s t=%.1f/%.1fs | FPS:%d",
                        VisemeConfig::visemeName(currentViseme >= 0 ? currentViseme : 0),
                        lipSyncTime, lipSyncTrack.totalDuration, frameCount);
                    glfwSetWindowTitle(window, buf);
                } else {
                    const char* exprNames[] = {"neutral","joy","anger","sadness","surprise","fear","disgust"};
                    const char* exprName = (activeExpression >= 0 && activeExpression <= 5) ? exprNames[activeExpression+1] : "neutral";
                    char buf[128];
                    snprintf(buf, sizeof(buf), "ImageMaker M7 | Axis:%s Action:%s Expr:%s w=%.1f Mode:%s | FPS:%d",
                        (const char*[]){"none","X","Y","Z"}[selAxis],
                        (const char*[]){"none","Rotate","Move","Orbit","Zoom"}[selAction],
                        exprName, expressionWeight,
                        expressionMode ? "FACE" : "SCULPT", frameCount);
                    glfwSetWindowTitle(window, buf);
                }
                frameCount = 0;
                lastTime = now;
            }
        }
        vkDeviceWaitIdle(device);
    }

    void drawFrame() {
        vkWaitForFences(device, 1, &inFlight[curFrame], VK_TRUE, UINT64_MAX);
        vkResetFences(device, 1, &inFlight[curFrame]);

        uint32_t imgIdx;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imgAvail[curFrame],
            VK_NULL_HANDLE, &imgIdx);

        // Update stroke GPU buffers if dirty
        if (strokeDirty) {
            updateStrokeBuffers();
            strokeDirty = false;
        }
        // Update mesh VBO if sculpted
        if (meshDirty) {
            updateMeshVBO();
        }

        // Update UBO
        updateUniformBuffer(curFrame);

        // Record command buffer
        vkResetCommandBuffer(cmdBufs[curFrame], 0);
        recordCommandBuffer(cmdBufs[curFrame], imgIdx);

        VkSubmitInfo si{}; si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkSemaphore waitSem[] = {imgAvail[curFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = waitSem; si.pWaitDstStageMask = waitStages;
        si.commandBufferCount = 1; si.pCommandBuffers = &cmdBufs[curFrame];
        VkSemaphore sigSem[] = {renderDone[curFrame]};
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = sigSem;
        vkQueueSubmit(gfxQueue, 1, &si, inFlight[curFrame]);

        VkPresentInfoKHR pi{}; pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = sigSem;
        VkSwapchainKHR sc[] = {swapchain};
        pi.swapchainCount = 1; pi.pSwapchains = sc; pi.pImageIndices = &imgIdx;
        vkQueuePresentKHR(presentQueue, &pi);

        curFrame = (curFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void updateUniformBuffer(uint32_t idx) {
        UBO ubo{};
        // Camera: spherical → cartesian
        float cx = camDist * sin(camPhi) * cos(camTheta);
        float cy = camDist * cos(camPhi);
        float cz = camDist * sin(camPhi) * sin(camTheta);
        glm::vec3 eye(cx, cy, cz);
        glm::vec3 up(0, 1, 0);
        ubo.view = glm::lookAt(eye + camTarget, camTarget, up);
        float aspect = swapExtent.width / (float)swapExtent.height;
        ubo.proj = glm::perspective(glm::radians(45.0f / zoomLevel), aspect, 0.1f, 100.0f);
        ubo.proj[1][1] *= -1; // Vulkan Y flip
        ubo.camPos = glm::vec4(eye + camTarget, 1.0f);
        memcpy(uboMapped[idx], &ubo, sizeof(UBO));
    }

    void updateStrokeBuffers() {
        strokeIdxCount = (uint32_t)strokeIdxs.size();
        if (strokeIdxCount == 0) return;

        VkDeviceSize vSize = sizeof(GVertex) * strokeVerts.size();
        VkDeviceSize iSize = sizeof(uint32_t) * strokeIdxs.size();

        // Destroy old
        if (strokeVbo != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            vkDestroyBuffer(device, strokeVbo, nullptr);
            vkFreeMemory(device, strokeVboMem, nullptr);
            vkDestroyBuffer(device, strokeIbo, nullptr);
            vkFreeMemory(device, strokeIboMem, nullptr);
        }

        createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, strokeVbo, strokeVboMem);
        createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, strokeIbo, strokeIboMem);
        copyToBuffer(strokeVbo, strokeVerts.data(), vSize);
        copyToBuffer(strokeIbo, strokeIdxs.data(), iSize);
    }

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cb, &bi);

        VkClearValue clear = {{{0.1f, 0.1f, 0.15f, 1.0f}}};
        VkClearValue depthClear; depthClear.depthStencil = {1.0f, 0};
        VkClearValue clears[] = {clear, depthClear};

        VkRenderPassBeginInfo rpi{};
        rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass = renderPass; rpi.framebuffer = framebuffers[imgIdx];
        rpi.renderArea.extent = swapExtent;
        rpi.clearValueCount = 2; rpi.pClearValues = clears;
        vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout, 0, 1,
            &descSets[curFrame], 0, nullptr);

        // ─── Draw grid ───────────────────────────────────────────────
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipe);
        VkDeviceSize gOff = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &gridVbo, &gOff);
        vkCmdBindIndexBuffer(cb, gridIbo, 0, VK_INDEX_TYPE_UINT32);
        PushConst gpc{}; gpc.model = glm::mat4(1.0f); gpc.color = glm::vec4(1);
        vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConst), &gpc);
        vkCmdDrawIndexed(cb, gridIdxCount, 1, 0, 0, 0);

        // ─── Draw axis ────────────────────────────────────────────────
        vkCmdBindVertexBuffers(cb, 0, 1, &axisVbo, &gOff);
        vkCmdBindIndexBuffer(cb, axisIbo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, axisIdxCount, 1, 0, 0, 0);

        // Draw selected axis highlight (thicker via repeated draws)
        if (selAxis != AXIS_NONE) {
            // Only draw the selected axis segment
            uint32_t selStart = 0, selCount = 0;
            if (selAxis == AXIS_X) { selStart = 0; selCount = 2; }
            else if (selAxis == AXIS_Y) { selStart = 2; selCount = 2; }
            else { selStart = 4; selCount = 2; }
            // Overdraw with bright color
            PushConst hpc{};
            hpc.model = glm::mat4(1.0f);
            hpc.color = glm::vec4(2.0f, 2.0f, 2.0f, 1.0f); // bright
            vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConst), &hpc);
            vkCmdDrawIndexed(cb, selCount, 1, selStart, 0, 0);
        }

        // Reset push constant for subsequent draws
        PushConst rpc{};
        rpc.model = glm::mat4(1.0f);
        rpc.color = glm::vec4(1.0f);
        vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConst), &rpc);

        // ─── Draw strokes (lines) ─────────────────────────────────────
        if (strokeIdxCount > 0 && strokeVbo != VK_NULL_HANDLE) {
            vkCmdBindVertexBuffers(cb, 0, 1, &strokeVbo, &gOff);
            vkCmdBindIndexBuffer(cb, strokeIbo, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, strokeIdxCount, 1, 0, 0, 0);
        }

        // ─── Color palette overlay (screen-space, no depth) ───────────
        // (Replaced by ImGui ColorPicker4)

        // ─── Draw mesh ────────────────────────────────────────────────
        glm::mat4 model = glm::translate(glm::mat4(1.0f), modelPos) * glm::mat4_cast(modelRot);
        PushConst mpc{};
        mpc.model = model;
        mpc.color = (selObject >= 0) ? meshColor : glm::vec4(0.4f, 0.6f, 0.9f, 1.0f);

        if (toonMode) {
            // Draw outline first (inverted hull)
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, outlinePipe);
            VkDeviceSize off2 = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off2);
            vkCmdBindIndexBuffer(cb, ibo, 0, VK_INDEX_TYPE_UINT32);
            PushConst opc{};
            opc.model = model;
            opc.color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(PushConst), &opc);
            vkCmdDrawIndexed(cb, meshIdxCount, 1, 0, 0, 0);

            // Draw toon-shaded mesh on top
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, toonPipe);
        } else {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe);
        }

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
        vkCmdBindIndexBuffer(cb, ibo, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConst), &mpc);
        vkCmdDrawIndexed(cb, meshIdxCount, 1, 0, 0, 0);

        // ImGui draw data
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    // ─── Cleanup ─────────────────────────────────────────────────────
    void cleanup() {
        vkDeviceWaitIdle(device);

        // ImGui cleanup
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        if (imguiPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, imguiPool, nullptr);
        }

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroySemaphore(device, imgAvail[i], nullptr);
            vkDestroySemaphore(device, renderDone[i], nullptr);
            vkDestroyFence(device, inFlight[i], nullptr);
            vkDestroyBuffer(device, uboBufs[i], nullptr);
            vkFreeMemory(device, uboMems[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, dsetLayout, nullptr);

        // Mesh buffers
        vkDestroyBuffer(device, vbo, nullptr); vkFreeMemory(device, vboMem, nullptr);
        vkDestroyBuffer(device, ibo, nullptr); vkFreeMemory(device, iboMem, nullptr);
        vkDestroyBuffer(device, gridVbo, nullptr); vkFreeMemory(device, gridVboMem, nullptr);
        vkDestroyBuffer(device, gridIbo, nullptr); vkFreeMemory(device, gridIboMem, nullptr);
        vkDestroyBuffer(device, axisVbo, nullptr); vkFreeMemory(device, axisVboMem, nullptr);
        vkDestroyBuffer(device, axisIbo, nullptr); vkFreeMemory(device, axisIboMem, nullptr);
        // Stroke buffers
        if (strokeVbo != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, strokeVbo, nullptr);
            vkFreeMemory(device, strokeVboMem, nullptr);
            vkDestroyBuffer(device, strokeIbo, nullptr);
            vkFreeMemory(device, strokeIboMem, nullptr);
        }

        vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyImageView(device, depthView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMem, nullptr);
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyPipeline(device, meshPipe, nullptr);
        vkDestroyPipeline(device, toonPipe, nullptr);
        vkDestroyPipeline(device, outlinePipe, nullptr);
        vkDestroyPipeline(device, gridPipe, nullptr);
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto iv : swapViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        if (debugMsgr != VK_NULL_HANDLE) {
            auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            vkDestroyDebugUtilsMessengerEXT(instance, debugMsgr, nullptr);
        }
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

int main() {
    try { VulkanApp app; app.run(); }
    catch (const std::exception& e) { std::cerr << "Error: " << e.what() << std::endl; return 1; }
    return 0;
}
