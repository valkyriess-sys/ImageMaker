#pragma once
#include <vector>
#include <cstdint>

struct Point2D {
    float x, y;
    Point2D() : x(0), y(0) {}
    Point2D(float x_, float y_) : x(x_), y(y_) {}
};

struct PointerEvent {
    Point2D position;
    float pressure;  // 0.0 ~ 1.0
    uint64_t timestamp_ms;
    bool is_down;    // true: press/move, false: release
};

struct Stroke {
    std::vector<PointerEvent> points;
    float thickness;  // derived from pressure or UI slider
    bool closed;      // true if start/end points are close

    void add(const PointerEvent& e) {
        points.push_back(e);
    }

    Point2D start() const { return points.front().position; }
    Point2D end() const { return points.back().position; }
    size_t size() const { return points.size(); }
};
