#ifndef GRAPHICS_TYPES_HPP
#define GRAPHICS_TYPES_HPP

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

// ============================================================================
// Graphics Types - Color, geometry, transforms for professional GUI
// ============================================================================

// Color representation with RGBA
class Color {
public:
    uint8_t red, green, blue, alpha;
    
    Color() : red(0), green(0), blue(0), alpha(255) {}
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : red(r), green(g), blue(b), alpha(a) {}
    
    // Create from hex (0xRRGGBBAA)
    static Color fromHex(uint32_t hex) {
        return Color((hex >> 24) & 0xFF, (hex >> 16) & 0xFF, 
                    (hex >> 8) & 0xFF, hex & 0xFF);
    }
    
    // Predefined colors
    static Color BLACK;
    static Color WHITE;
    static Color TRANSPARENT;
    static Color PRIMARY;      // Primary brand color
    static Color SECONDARY;    // Secondary brand color
    static Color ACCENT;       // Accent color
    static Color SURFACE;      // Background surface color
    static Color TEXT;         // Text color
    static Color TEXT_SECONDARY;  // Secondary text
    static Color SUCCESS;
    static Color WARNING;
    static Color ERROR;
    
    // Blend colors
    static Color blend(Color a, Color b, float t);
};

// Point in 2D space
struct Point {
    float x, y;
    
    Point() : x(0), y(0) {}
    Point(float x, float y) : x(x), y(y) {}
    
    Point operator+(const Point& p) const { return Point(x + p.x, y + p.y); }
    Point operator-(const Point& p) const { return Point(x - p.x, y - p.y); }
    Point operator*(float scale) const { return Point(x * scale, y * scale); }
};

// Size representation
struct Size {
    float width, height;
    
    Size() : width(0), height(0) {}
    Size(float w, float h) : width(w), height(h) {}
    
    bool isEmpty() const { return width <= 0 || height <= 0; }
};

// Rectangle (position + size)
struct Rect {
    float x, y, width, height;
    
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(float x, float y, float w, float h) : x(x), y(y), width(w), height(h) {}
    
    Point getTopLeft() const { return Point(x, y); }
    Point getBottomRight() const { return Point(x + width, y + height); }
    Point getCenter() const { return Point(x + width/2, y + height/2); }
    
    bool contains(Point p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
    
    bool intersects(const Rect& other) const {
        return !(x + width < other.x || x > other.x + other.width ||
                 y + height < other.y || y > other.y + other.height);
    }
};

// Padding/margin
struct Spacing {
    float top, right, bottom, left;
    
    Spacing() : top(0), right(0), bottom(0), left(0) {}
    Spacing(float uniform) : top(uniform), right(uniform), bottom(uniform), left(uniform) {}
    Spacing(float vertical, float horizontal) 
        : top(vertical), right(horizontal), bottom(vertical), left(horizontal) {}
    Spacing(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}
    
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
};

// Shadow effects
struct Shadow {
    float offset_x, offset_y;
    float blur_radius;
    Color color;
    float opacity;
    
    Shadow() : offset_x(0), offset_y(0), blur_radius(0), color(0, 0, 0), opacity(0) {}
    Shadow(float ox, float oy, float blur, Color c, float op)
        : offset_x(ox), offset_y(oy), blur_radius(blur), color(c), opacity(op) {}
};

// Gradient definition
enum class GradientType {
    LINEAR,
    RADIAL,
    SWEEP
};

struct Gradient {
    GradientType type;
    Color color1, color2;
    float angle;  // For linear gradients
    
    Gradient(GradientType t = GradientType::LINEAR, Color c1 = Color::WHITE, Color c2 = Color::BLACK, float a = 0)
        : type(t), color1(c1), color2(c2), angle(a) {}
};

// Text attributes
enum class FontWeight {
    THIN = 100,
    LIGHT = 300,
    NORMAL = 400,
    SEMI_BOLD = 600,
    BOLD = 700,
    EXTRA_BOLD = 900
};

enum class TextAlign {
    LEFT,
    CENTER,
    RIGHT
};

struct TextStyle {
    std::string font_family = "Segoe UI";
    float font_size = 14;
    FontWeight weight = FontWeight::NORMAL;
    bool italic = false;
    Color color = Color::TEXT;
    TextAlign alignment = TextAlign::LEFT;
    float line_spacing = 1.2f;
};

// Transform matrix (2D affine transformation)
struct Transform {
    float m[3][3];  // 3x3 matrix for 2D transforms
    
    Transform() {
        // Identity matrix
        m[0][0] = 1; m[0][1] = 0; m[0][2] = 0;
        m[1][0] = 0; m[1][1] = 1; m[1][2] = 0;
        m[2][0] = 0; m[2][1] = 0; m[2][2] = 1;
    }
    
    // Create translation
    static Transform translate(float tx, float ty);
    
    // Create scale
    static Transform scale(float sx, float sy);
    
    // Create rotation (in degrees)
    static Transform rotate(float degrees);
    
    // Matrix multiplication
    Transform operator*(const Transform& other) const;
    
    // Apply to point
    Point apply(const Point& p) const;
};

#endif // GRAPHICS_TYPES_HPP
