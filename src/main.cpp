#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.h>

#include "mesh_generator.hpp"
#include "input.hpp"

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
struct UBO { glm::mat4 view, proj; };

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

    // Input state
    bool keyXPressed = false, keyYPressed = false, keyZPressed = false;
    bool keyRPressed = false, keyMPressed = false, keySPressed = false, keyTPressed = false;

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

    // ─── Window ──────────────────────────────────────────────────────
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIDTH, HEIGHT, "ImageMaker M2 | Axis:none Action:none | scroll/+-", nullptr, nullptr);
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
            if (key == GLFW_KEY_X) { app->keyXPressed = !app->keyXPressed; app->updateAxis(); }
            if (key == GLFW_KEY_Y) { app->keyYPressed = !app->keyYPressed; app->updateAxis(); }
            if (key == GLFW_KEY_Z) { app->keyZPressed = !app->keyZPressed; app->updateAxis(); }
            if (key == GLFW_KEY_R) { app->keyRPressed = !app->keyRPressed; app->updateAction(); }
            if (key == GLFW_KEY_M) { app->keyMPressed = !app->keyMPressed; app->updateAction(); }
            if (key == GLFW_KEY_S) { app->keySPressed = !app->keySPressed; app->updateAction(); }
            if (key == GLFW_KEY_T) { app->keyTPressed = !app->keyTPressed; app->updateAction(); }
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

        app->lastMouseX = x;
        app->lastMouseY = y;
    }

    void updateAxis() {
        if (keyXPressed) { keyYPressed = keyZPressed = false; selAxis = AXIS_X; }
        else if (keyYPressed) { keyXPressed = keyZPressed = false; selAxis = AXIS_Y; }
        else if (keyZPressed) { keyXPressed = keyYPressed = false; selAxis = AXIS_Z; }
        else selAxis = AXIS_NONE;
        updateTitle();
    }
    void updateAction() {
        if (keyRPressed) { keyMPressed = keySPressed = keyTPressed = false; selAction = ACT_ROTATE; }
        else if (keyMPressed) { keyRPressed = keySPressed = keyTPressed = false; selAction = ACT_MOVE; }
        else if (keySPressed) { keyRPressed = keyMPressed = keyTPressed = false; selAction = ACT_ORBIT; }
        else if (keyTPressed) { keyRPressed = keyMPressed = keySPressed = false; selAction = ACT_ZOOM; }
        else selAction = ACT_NONE;
        updateTitle();
    }

    void updateTitle() {
        const char* ax[] = {"none","X","Y","Z"};
        const char* ac[] = {"none","Rotate","Move","Orbit","Zoom"};
        char buf[128];
        snprintf(buf, sizeof(buf), "ImageMaker M2 | Axis:%s Action:%s Obj:%s | scroll/+/-",
            ax[selAxis], ac[selAction], selObject >= 0 ? "#0" : "none");
        glfwSetWindowTitle(window, buf);
    }

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
        ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = &layer;

        // Debug messenger create info
        VkDebugUtilsMessengerCreateInfoEXT dbgCI{};
        dbgCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCI.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCI.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        dbgCI.pfnUserCallback = debugCallback;
        ci.pNext = &dbgCI;

        vkCreateInstance(&ci, nullptr, &instance);
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        vkCreateDebugUtilsMessengerEXT(instance, &dbgCI, nullptr, &debugMsgr);
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
        bnd.descriptorCount = 1; bnd.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1; ci.pBindings = &bnd;
        vkCreateDescriptorSetLayout(device, &ci, nullptr, &dsetLayout);
    }

    void createPipelines() {
        auto meshVert = readFile("shaders/mesh.vert.spv");
        auto meshFrag = readFile("shaders/mesh.frag.spv");
        auto gridVert = readFile("shaders/grid.vert.spv");
        auto gridFrag = readFile("shaders/grid.frag.spv");

        VkShaderModule mvs = createShaderModule(device, meshVert);
        VkShaderModule mfs = createShaderModule(device, meshFrag);
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

        // Build mesh pipeline
        meshPipe = createGraphicsPipeline(mvs, mfs, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            sizeof(Vertex), offsetof(Vertex, x), offsetof(Vertex, nx));
        // Build grid pipeline (for lines)
        gridPipe = createGraphicsPipeline(gvs, gfs, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            sizeof(glm::vec3) + sizeof(glm::vec4), 0, sizeof(glm::vec3));

        vkDestroyShaderModule(device, mvs, nullptr);
        vkDestroyShaderModule(device, mfs, nullptr);
        vkDestroyShaderModule(device, gvs, nullptr);
        vkDestroyShaderModule(device, gfs, nullptr);
    }

    VkPipeline createGraphicsPipeline(VkShaderModule vs, VkShaderModule fs,
        VkPrimitiveTopology topo, uint32_t stride, uint32_t posOff, uint32_t normOff) {
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
        rss.cullMode = VK_CULL_MODE_NONE; rss.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
            drawFrame();
            frameCount++;
            auto now = glfwGetTime();
            if (now - lastTime >= 1.0) {
                char buf[128];
                snprintf(buf, sizeof(buf), "ImageMaker M2 | Axis:%s Action:%s Obj:%s | FPS:%d",
                    (const char*[]){"none","X","Y","Z"}[selAxis],
                    (const char*[]){"none","Rotate","Move","Orbit","Zoom"}[selAction],
                    selObject >= 0 ? "#0" : "none", frameCount);
                glfwSetWindowTitle(window, buf);
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

        // ─── Draw mesh ────────────────────────────────────────────────
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipe);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vbo, &off);
        vkCmdBindIndexBuffer(cb, ibo, 0, VK_INDEX_TYPE_UINT32);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), modelPos) * glm::mat4_cast(modelRot);
        PushConst mpc{};
        mpc.model = model;
        mpc.color = (selObject >= 0) ? glm::vec4(1.0f, 0.8f, 0.2f, 1.0f) : glm::vec4(0.4f, 0.6f, 0.9f, 1.0f);
        vkCmdPushConstants(cb, pipeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(PushConst), &mpc);
        vkCmdDrawIndexed(cb, meshIdxCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    // ─── Cleanup ─────────────────────────────────────────────────────
    void cleanup() {
        vkDeviceWaitIdle(device);
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
        vkDestroyPipeline(device, gridPipe, nullptr);
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (auto iv : swapViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        auto vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        vkDestroyDebugUtilsMessengerEXT(instance, debugMsgr, nullptr);
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
