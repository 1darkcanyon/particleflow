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
    gl_Position = pc.mvpMatrix * vec4(inPosition, 0.0, 1.0);
    gl_Position.y = -gl_Position.y;  // particle y=0 is bottom; Vulkan NDC y=-1 is top
    gl_PointSize = pc.pointSize;
    fragColor = inColor;
}
