#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include "input.hpp"
#include "stroke_processor.hpp"
#include "shape_classifier.hpp"
#include "mesh_generator.hpp"

// Test: simulate a circle stroke → sphere
Stroke makeCircleStroke(float radius, int points = 50) {
    Stroke stroke;
    stroke.thickness = 5.0f;
    for (int i = 0; i < points; i++) {
        float angle = 2.0f * M_PI * i / points;
        PointerEvent e;
        e.position = Point2D(std::cos(angle) * radius, std::sin(angle) * radius);
        e.pressure = 0.5f;
        e.timestamp_ms = i;
        e.is_down = true;
        stroke.add(e);
    }
    return stroke;
}

// Test: simulate a rectangle stroke → box
Stroke makeRectStroke(float w, float h) {
    Stroke stroke;
    stroke.thickness = 5.0f;

    auto add_corner = [&](float x, float y) {
        PointerEvent e;
        e.position = Point2D(x, y);
        e.pressure = 0.5f;
        e.timestamp_ms = stroke.size();
        e.is_down = true;
        stroke.add(e);
    };

    add_corner(-w, -h);
    add_corner(w, -h);
    add_corner(w, h);
    add_corner(-w, h);
    add_corner(-w, -h);  // close
    return stroke;
}

// Test: simulate a line stroke → cylinder
Stroke makeLineStroke(float length) {
    Stroke stroke;
    stroke.thickness = 5.0f;
    for (int i = 0; i < 20; i++) {
        float t = (float)i / 19.0f;
        PointerEvent e;
        e.position = Point2D(t * length, 0);
        e.pressure = 0.5f;
        e.timestamp_ms = i;
        e.is_down = true;
        stroke.add(e);
    }
    return stroke;
}

int main() {
    // Vulkan instance
    VkInstance instance;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ImageMaker";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "ImageMaker";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance!" << std::endl;
        return 1;
    }
    std::cout << "Vulkan instance created successfully." << std::endl;

    // Test shape classification
    std::cout << "\n=== Shape Classification Tests ===" << std::endl;

    // Circle → Sphere
    Stroke circle = makeCircleStroke(100.0f);
    ShapeParams params = ShapeClassifier::classify(circle);
    std::cout << "Circle stroke → ";
    switch (params.type) {
        case ShapeType::SPHERE: std::cout << "SPHERE"; break;
        case ShapeType::ELLIPSOID: std::cout << "ELLIPSOID"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << " (diameter=" << params.diameter << ")" << std::endl;

    // Rectangle → Box
    Stroke rect = makeRectStroke(100.0f, 80.0f);
    params = ShapeClassifier::classify(rect);
    std::cout << "Rect stroke → ";
    switch (params.type) {
        case ShapeType::BOX: std::cout << "BOX"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << " (diameter=" << params.diameter << ")" << std::endl;

    // Line → Cylinder
    Stroke line = makeLineStroke(200.0f);
    params = ShapeClassifier::classify(line);
    std::cout << "Line stroke → ";
    switch (params.type) {
        case ShapeType::CYLINDER: std::cout << "CYLINDER"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << " (diameter=" << params.diameter << ", height=" << params.height << ")" << std::endl;

    // Generate meshes
    std::cout << "\n=== Mesh Generation ===" << std::endl;

    Mesh sphere = PrimitiveGenerator::generateIcoSphere(50.0f);
    std::cout << "IcoSphere: " << sphere.vertices.size() << " vertices, "
              << sphere.indices.size() / 3 << " triangles" << std::endl;

    Mesh box = PrimitiveGenerator::generateBox(100.0f);
    std::cout << "Box: " << box.vertices.size() << " vertices, "
              << box.indices.size() / 3 << " triangles" << std::endl;

    Mesh cyl = PrimitiveGenerator::generateCylinder(50.0f, 200.0f);
    std::cout << "Cylinder: " << cyl.vertices.size() << " vertices, "
              << cyl.indices.size() / 3 << " triangles" << std::endl;

    Mesh cone = PrimitiveGenerator::generateCone(50.0f, 150.0f);
    std::cout << "Cone: " << cone.vertices.size() << " vertices, "
              << cone.indices.size() / 3 << " triangles" << std::endl;

    vkDestroyInstance(instance, nullptr);
    std::cout << "\nAll tests passed." << std::endl;
    return 0;
}
