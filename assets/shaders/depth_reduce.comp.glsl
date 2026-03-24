/// @file depth_reduce.comp.glsl
/// @brief Parallel min/max depth reduction for SDSM (Sample Distribution Shadow Maps).
///
/// Reads the resolved depth buffer and finds the tightest depth range containing
/// actual geometry. Results are stored in an SSBO for CPU readback (one-frame delay).
/// Uses floatBitsToUint for atomic float operations on positive depth values.
///
/// Reverse-Z convention: depth 1.0 = near plane, depth 0.0 = far/sky.
#version 450 core

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D u_depthTexture;

layout(std430, binding = 0) buffer DepthBounds
{
    uint minDepthBits;  // floatBitsToUint of min depth (farthest geometry)
    uint maxDepthBits;  // floatBitsToUint of max depth (nearest geometry)
};

void main()
{
    ivec2 texSize = textureSize(u_depthTexture, 0);
    ivec2 coord = ivec2(gl_GlobalInvocationID.xy);

    if (coord.x >= texSize.x || coord.y >= texSize.y)
        return;

    float depth = texelFetch(u_depthTexture, coord, 0).r;

    // Skip sky/cleared pixels (reverse-Z: depth 0.0 = infinity/sky)
    if (depth <= 0.0001)
        return;

    // For positive IEEE 754 floats, bit ordering matches float ordering,
    // so atomicMin/Max on uint equivalents finds the float min/max.
    uint depthBits = floatBitsToUint(depth);
    atomicMin(minDepthBits, depthBits);
    atomicMax(maxDepthBits, depthBits);
}
