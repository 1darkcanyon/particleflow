#version 450

// Fragment shader for rendering particles
// Simple pass-through of color from vertex shader

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Output the particle color
    outColor = fragColor;
}
