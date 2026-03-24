/// @file frustum_cull.comp.glsl
/// @brief GPU frustum culling — tests object AABBs against 6 frustum planes.
///
/// Sets instanceCount=0 for culled objects in the indirect draw command buffer.
/// Each thread processes one object. Objects inside the frustum keep their
/// original instanceCount unchanged.
#version 450 core

layout(local_size_x = 64) in;

// Draw command structure (matches DrawElementsIndirectCommand)
struct DrawCommand
{
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

// Object bounding data
struct ObjectData
{
    vec4 center;   // xyz = AABB center
    vec4 extent;   // xyz = AABB half-extents
};

layout(std430, binding = 1) buffer DrawCommands
{
    DrawCommand commands[];
};

layout(std430, binding = 2) buffer Objects
{
    ObjectData objects[];
};

uniform vec4 u_frustumPlanes[6];  // Normalized plane equations (nx, ny, nz, d)
uniform int u_objectCount;

/// Test if an AABB is completely outside a plane.
/// Returns true if the AABB is entirely in the negative half-space.
bool isOutsidePlane(vec4 plane, vec3 center, vec3 extent)
{
    // Project the extent onto the plane normal to find the "most positive" corner
    float r = dot(extent, abs(plane.xyz));
    // Distance from center to plane
    float d = dot(plane.xyz, center) + plane.w;
    // If the farthest corner is behind the plane, the entire AABB is outside
    return (d + r) < 0.0;
}

void main()
{
    uint idx = gl_GlobalInvocationID.x;

    if (idx >= uint(u_objectCount))
        return;

    vec3 center = objects[idx].center.xyz;
    vec3 extent = objects[idx].extent.xyz;

    // Test against all 6 frustum planes
    // If the AABB is outside ANY plane, the object is culled
    for (int i = 0; i < 6; i++)
    {
        if (isOutsidePlane(u_frustumPlanes[i], center, extent))
        {
            commands[idx].instanceCount = 0u;
            return;
        }
    }

    // Object is inside the frustum — keep original instanceCount (unchanged)
}
