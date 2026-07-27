#pragma once
#include "input.hpp"
#include <algorithm>
#include <cmath>

// Stroke preprocessing: noise reduction + resampling
class StrokeProcessor {
public:
    // Resample stroke to fixed number of points (uniform spacing)
    static Stroke resample(const Stroke& stroke, size_t target_points = 50) {
        if (stroke.size() < 2) return stroke;

        Stroke result;
        result.thickness = stroke.thickness;

        // Calculate total path length
        float total_length = 0;
        for (size_t i = 1; i < stroke.size(); i++) {
            total_length += dist(stroke.points[i-1].position, stroke.points[i].position);
        }

        if (total_length == 0) return stroke;

        float step = total_length / (target_points - 1);
        float accumulated = 0;
        size_t idx = 0;

        for (size_t i = 0; i < target_points; i++) {
            float target_dist = i * step;
            while (idx < stroke.size() - 1 && accumulated < target_dist) {
                float seg_len = dist(stroke.points[idx].position, stroke.points[idx+1].position);
                if (accumulated + seg_len >= target_dist) break;
                accumulated += seg_len;
                idx++;
            }
            result.add(stroke.points[idx]);
        }

        return result;
    }

    // Simple moving average noise reduction
    static Stroke smooth(const Stroke& stroke, int window = 3) {
        if (stroke.size() < 3) return stroke;

        Stroke result;
        result.thickness = stroke.thickness;

        for (size_t i = 0; i < stroke.size(); i++) {
            float sum_x = 0, sum_y = 0;
            int count = 0;
            for (int j = -(window/2); j <= window/2; j++) {
                int idx = (int)i + j;
                if (idx >= 0 && idx < (int)stroke.size()) {
                    sum_x += stroke.points[idx].position.x;
                    sum_y += stroke.points[idx].position.y;
                    count++;
                }
            }
            PointerEvent e;
            e.position = Point2D(sum_x/count, sum_y/count);
            e.pressure = stroke.points[i].pressure;
            e.timestamp_ms = stroke.points[i].timestamp_ms;
            e.is_down = stroke.points[i].is_down;
            result.add(e);
        }

        return result;
    }

    // Check if stroke is closed (start/end within threshold)
    static bool isClosed(const Stroke& stroke, float threshold = 50.0f) {
        if (stroke.size() < 3) return false;
        return dist(stroke.start(), stroke.end()) < threshold;
    }

private:
    static float dist(const Point2D& a, const Point2D& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return std::sqrt(dx*dx + dy*dy);
    }
};
