#version 450

// Vertex shader for rendering particles as points
// Reads directly from compute shader output buffers

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 mvpMatrix;
    float pointSize;
} pc;

void main() {
    // Transform position to clip space
    gl_Position = pc.mvpMatrix * vec4(inPosition, 0.0, 1.0);

    // Flip Y: particles use OpenGL convention (y=0 at bottom) matching original RenderScript.
    // The mvpMatrix maps y=0 → NDC y=-1 (top), so negate to place y=0 at bottom.
    gl_Position.y = -gl_Position.y;

    // Set point size
    gl_PointSize = pc.pointSize;

    // Pass color to fragment shader
    fragColor = inColor;
}
