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

    // Set point size
    gl_PointSize = pc.pointSize;

    // Pass color to fragment shader
    fragColor = inColor;
}
