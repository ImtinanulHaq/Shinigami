#include "../../utilities/graphics/graphics_types.hpp"
#include <cmath>

// Color predefinitions
Color Color::BLACK(0, 0, 0);
Color Color::WHITE(255, 255, 255);
Color Color::TRANSPARENT(0, 0, 0, 0);
Color Color::PRIMARY(33, 150, 243);      // Blue
Color Color::SECONDARY(156, 39, 176);    // Purple
Color Color::ACCENT(255, 152, 0);        // Orange
Color Color::SURFACE(245, 245, 245);     // Light gray
Color Color::TEXT(33, 33, 33);           // Dark gray
Color Color::TEXT_SECONDARY(117, 117, 117);  // Medium gray
Color Color::SUCCESS(76, 175, 80);       // Green
Color Color::WARNING(255, 193, 7);       // Yellow
Color Color::ERROR(244, 67, 54);         // Red

Color Color::blend(Color a, Color b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return Color(
        static_cast<uint8_t>(a.red + (b.red - a.red) * t),
        static_cast<uint8_t>(a.green + (b.green - a.green) * t),
        static_cast<uint8_t>(a.blue + (b.blue - a.blue) * t),
        static_cast<uint8_t>(a.alpha + (b.alpha - a.alpha) * t)
    );
}

Transform Transform::translate(float tx, float ty) {
    Transform t;
    t.m[0][2] = tx;
    t.m[1][2] = ty;
    return t;
}

Transform Transform::scale(float sx, float sy) {
    Transform t;
    t.m[0][0] = sx;
    t.m[1][1] = sy;
    return t;
}

Transform Transform::rotate(float degrees) {
    Transform t;
    float radians = degrees * M_PI / 180.0f;
    float cos_a = std::cos(radians);
    float sin_a = std::sin(radians);
    
    t.m[0][0] = cos_a;
    t.m[0][1] = -sin_a;
    t.m[1][0] = sin_a;
    t.m[1][1] = cos_a;
    
    return t;
}

Transform Transform::operator*(const Transform& other) const {
    Transform result;
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            result.m[i][j] = 0;
            for (int k = 0; k < 3; k++) {
                result.m[i][j] += m[i][k] * other.m[k][j];
            }
        }
    }
    
    return result;
}

Point Transform::apply(const Point& p) const {
    float x = p.x * m[0][0] + p.y * m[0][1] + m[0][2];
    float y = p.x * m[1][0] + p.y * m[1][1] + m[1][2];
    return Point(x, y);
}
