/// @file terrain.vert.glsl
/// @brief CDLOD terrain vertex shader — heightmap displacement with per-vertex morphing and edge skirts.
#version 450 core

layout(location = 0) in vec3 a_gridPosAndSkirt;  // xy = grid pos (0..1), z = skirt flag (0 or 1)

// Per-node uniforms
uniform vec2 u_nodeOffset;      // World XZ offset of this node (top-left corner)
uniform float u_nodeScale;      // World size of this node (full extent)
uniform int u_lodLevel;

// Terrain uniforms
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform vec2 u_terrainSize;     // Total terrain world size (XZ)
uniform vec2 u_terrainOrigin;   // World position of heightmap origin
uniform int u_gridResolution;

// LOD ranges for per-vertex morphing
uniform float u_lodRanges[8];
uniform int u_maxLodLevels;

// Camera
uniform mat4 u_viewProjection;
uniform mat4 u_view;
uniform vec3 u_cameraPos;

// Outputs
out vec2 v_terrainUV;
out vec3 v_worldPos;
out float v_viewDepth;

void main()
{
    vec2 gridPos = a_gridPosAndSkirt.xy;
    float isSkirt = a_gridPosAndSkirt.z;

    // Scale grid position [0,1] to world space for this node
    vec2 worldXZ = u_nodeOffset + gridPos * u_nodeScale;

    // Pre-sample height at unmorphed position for 3D morph distance
    vec2 preSampleUV = (worldXZ - u_terrainOrigin) / u_terrainSize;
    preSampleUV = clamp(preSampleUV, vec2(0.0), vec2(1.0));
    float preHeight = texture(u_heightmap, preSampleUV).r * u_heightScale;

    // Per-vertex morph factor using 3D distance (better on steep terrain)
    float vertexDist = distance(u_cameraPos, vec3(worldXZ.x, preHeight, worldXZ.y));
    float lodRange = u_lodRanges[u_lodLevel];
    float prevRange = (u_lodLevel > 0) ? u_lodRanges[u_lodLevel - 1] : 0.0;
    float rangeSpan = lodRange - prevRange;
    float morphFactor = 0.0;
    if (rangeSpan > 0.0)
    {
        float t = (vertexDist - prevRange) / rangeSpan;
        // Morph starts at 50%, completes at 95% of range — safety margin
        // ensures full morph before LOD transition (node selection uses
        // closest-point-on-AABB which differs from per-vertex distance)
        morphFactor = clamp((t - 0.5) / 0.45, 0.0, 1.0);
    }

    // Vertex morphing: snap every-other vertex toward the coarser grid
    float step = u_nodeScale / float(u_gridResolution - 1);
    vec2 worldInGrid = (worldXZ - u_nodeOffset) / step;
    // Round to nearest integer to avoid fract() floating-point precision issues
    worldInGrid = floor(worldInGrid + 0.5);
    // mod(2) gives 1.0 for odd vertices (morph targets), 0.0 for even (anchors)
    vec2 morphOffset = mod(worldInGrid, 2.0) * step;
    worldXZ -= morphOffset * morphFactor;

    // Compute terrain UV for heightmap/normal/splat lookup
    v_terrainUV = (worldXZ - u_terrainOrigin) / u_terrainSize;
    v_terrainUV = clamp(v_terrainUV, vec2(0.0), vec2(1.0));

    // Sample heightmap for vertical displacement
    float height = texture(u_heightmap, v_terrainUV).r * u_heightScale;

    // Skirt: push edge duplicates downward to fill gaps between LOD levels
    if (isSkirt > 0.5)
    {
        height -= u_nodeScale * 0.3;  // Drop proportional to node size
    }

    v_worldPos = vec3(worldXZ.x, height, worldXZ.y);
    v_viewDepth = -(u_view * vec4(v_worldPos, 1.0)).z;

    gl_Position = u_viewProjection * vec4(v_worldPos, 1.0);
}
