#pragma once
#include "input.hpp"
#include "stroke_processor.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

enum class ShapeType {
    SPHERE,
    ELLIPSOID,
    BOX,
    CYLINDER,
    CONE,
    TUBE,
    UNKNOWN
};

struct ShapeParams {
    ShapeType type;
    float diameter;
    float height;
    float ellipsoid_ratio;  // major/minor axis ratio
};

class ShapeClassifier {
public:
    static ShapeParams classify(const Stroke& stroke) {
        Stroke processed = StrokeProcessor::smooth(stroke);
        processed = StrokeProcessor::resample(processed, 50);

        // Check closed on original stroke (resampling can change start/end points)
        bool closed = StrokeProcessor::isClosed(stroke);
        BoundingBox bbox = computeBoundingBox(processed);
        float area = computeArea(processed);
        float perimeter = computePerimeter(processed);

        // Circle detection: closed curve, area/perimeter^2 ≈ 1/(4π)
        // Use original stroke for area/perimeter (resampling can distort)
        if (closed) {
            float orig_area = computeArea(stroke);
            float orig_perimeter = computePerimeter(stroke);
            float circularity = (orig_perimeter > 0) ? (4.0f * M_PI * orig_area) / (orig_perimeter * orig_perimeter) : 0;

            if (circularity > 0.85f) {
                // Check ellipsoid: aspect ratio of bbox
                float aspect = bbox.width / bbox.height;
                if (aspect > 1.3f || aspect < 0.77f) {
                    return {ShapeType::ELLIPSOID, bbox.maxDim() * 0.5f, 0, aspect};
                }
                return {ShapeType::SPHERE, bbox.maxDim() * 0.5f, 0, 1.0f};
            }
        }

        // Rectangle detection: 4 corners, closed
        // Use original stroke for corner detection (resampling can lose corners)
        if (closed && detectCorners(stroke, 4)) {
            return {ShapeType::BOX, bbox.maxDim() * 0.5f, 0, 1.0f};
        }

        // Line detection: straight line
        if (detectLine(processed)) {
            return {ShapeType::CYLINDER, bbox.minDim() * 0.5f, bbox.maxDim(), 1.0f};
        }

        // Triangle: 3 corners
        if (closed && detectCorners(stroke, 3)) {
            return {ShapeType::CONE, bbox.minDim() * 0.5f, bbox.maxDim(), 1.0f};
        }

        // Free curve with thickness: tube
        return {ShapeType::TUBE, bbox.minDim() * 0.5f, bbox.maxDim(), 1.0f};
    }

private:
    struct BoundingBox {
        float min_x, min_y, max_x, max_y;
        float width, height;

        float maxDim() const { return std::max(width, height); }
        float minDim() const { return std::min(width, height); }
    };

    static BoundingBox computeBoundingBox(const Stroke& stroke) {
        BoundingBox bbox;
        bbox.min_x = bbox.min_y = 1e9f;
        bbox.max_x = bbox.max_y = -1e9f;

        for (const auto& e : stroke.points) {
            bbox.min_x = std::min(bbox.min_x, e.position.x);
            bbox.min_y = std::min(bbox.min_y, e.position.y);
            bbox.max_x = std::max(bbox.max_x, e.position.x);
            bbox.max_y = std::max(bbox.max_y, e.position.y);
        }

        bbox.width = bbox.max_x - bbox.min_x;
        bbox.height = bbox.max_y - bbox.min_y;
        return bbox;
    }

    static float computeArea(const Stroke& stroke) {
        // Shoelace formula (for closed curves)
        if (stroke.size() < 3) return 0;
        float area = 0;
        for (size_t i = 0; i < stroke.size(); i++) {
            size_t j = (i + 1) % stroke.size();
            area += stroke.points[i].position.x * stroke.points[j].position.y;
            area -= stroke.points[j].position.x * stroke.points[i].position.y;
        }
        return std::abs(area) / 2.0f;
    }

    static float computePerimeter(const Stroke& stroke) {
        float perimeter = 0;
        for (size_t i = 1; i < stroke.size(); i++) {
            float dx = stroke.points[i].position.x - stroke.points[i-1].position.x;
            float dy = stroke.points[i].position.y - stroke.points[i-1].position.y;
            perimeter += std::sqrt(dx*dx + dy*dy);
        }
        return perimeter;
    }

    static bool detectLine(const Stroke& stroke) {
        if (stroke.size() < 3) return false;

        // Check if points are approximately collinear
        // Using distance from line fitting
        Point2D start = stroke.points.front().position;
        Point2D end = stroke.points.back().position;

        float max_dist = 0;
        for (const auto& e : stroke.points) {
            float dist = pointToLineDistance(e.position, start, end);
            max_dist = std::max(max_dist, dist);
        }

        float line_length = std::sqrt(
            (end.x - start.x)*(end.x - start.x) + (end.y - start.y)*(end.y - start.y)
        );

        return (max_dist / line_length) < 0.15f;  // within 15% of line length
    }

    static float pointToLineDistance(const Point2D& p, const Point2D& a, const Point2D& b) {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float length = std::sqrt(dx*dx + dy*dy);
        if (length == 0) return std::sqrt((p.x-a.x)*(p.x-a.x) + (p.y-a.y)*(p.y-a.y));

        // Distance = |(p-a) × (b-a)| / |b-a|
        float cross = (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
        return std::abs(cross) / length;
    }

    static bool detectCorners(const Stroke& stroke, int expected_corners) {
        // Simplified: count direction changes > threshold
        // Allow fewer points for simple shapes (original stroke with few points)
        if (stroke.size() < expected_corners + 1) return false;

        int corners = 0;
        float angle_threshold = M_PI * 0.3f;  // ~72 degrees (more sensitive)

        // First pass: count corners
        for (size_t i = 1; i < stroke.size() - 1; i++) {
            Point2D v1 = {
                stroke.points[i].position.x - stroke.points[i-1].position.x,
                stroke.points[i].position.y - stroke.points[i-1].position.y
            };
            Point2D v2 = {
                stroke.points[i+1].position.x - stroke.points[i].position.x,
                stroke.points[i+1].position.y - stroke.points[i].position.y
            };

            float dot = v1.x * v2.x + v1.y * v2.y;
            float mag1 = std::sqrt(v1.x*v1.x + v1.y*v1.y);
            float mag2 = std::sqrt(v2.x*v2.x + v2.y*v2.y);

            if (mag1 > 0 && mag2 > 0) {
                float angle = std::acos(std::abs(dot / (mag1 * mag2)));
                if (angle > angle_threshold) {
                    corners++;
                }
            }
        }

        // Second pass: if no corners found with angle threshold,
        // try detecting corners by direction change (for resampled strokes)
        if (corners == 0) {
            corners = detectCornersByDirection(stroke, expected_corners);
        }

        return corners >= expected_corners - 1;
    }

    static int detectCornersByDirection(const Stroke& stroke, int expected_corners) {
        // Detect corners by finding points where direction changes significantly
        // Works for resampled strokes where consecutive points may be identical
        if (stroke.size() < 3) return 0;

        int corners = 0;
        Point2D prev_dir = {0, 0};
        float dir_threshold = 0.3f;  // cosine similarity threshold

        for (size_t i = 1; i < stroke.size(); i++) {
            Point2D dir = {
                stroke.points[i].position.x - stroke.points[i-1].position.x,
                stroke.points[i].position.y - stroke.points[i-1].position.y
            };
            float mag = std::sqrt(dir.x*dir.x + dir.y*dir.y);
            if (mag < 0.01f) continue;  // skip zero-length segments

            if (prev_dir.x != 0 || prev_dir.y != 0) {
                float prev_mag = std::sqrt(prev_dir.x*prev_dir.x + prev_dir.y*prev_dir.y);
                if (prev_mag > 0.01f) {
                    // Cosine similarity
                    float cos_sim = (dir.x * prev_dir.x + dir.y * prev_dir.y) / (mag * prev_mag);
                    // Sharp turn if cos_sim < threshold (angle > ~72 degrees)
                    if (cos_sim < dir_threshold) {
                        corners++;
                    }
                }
            }
            prev_dir = dir;
        }

        return corners;
    }
};
