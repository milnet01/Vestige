/// @file terrain_shadow.vert.glsl
/// @brief Terrain shadow depth vertex shader — position + heightmap + skirts.
#version 450 core

layout(location = 0) in vec3 a_gridPosAndSkirt;  // xy = grid pos, z = skirt flag

// Per-node uniforms
uniform vec2 u_nodeOffset;
uniform float u_nodeScale;
uniform int u_lodLevel;

// Terrain uniforms
uniform sampler2D u_heightmap;
uniform float u_heightScale;
uniform vec2 u_terrainSize;
uniform vec2 u_terrainOrigin;
uniform int u_gridResolution;

// LOD ranges for per-vertex morphing
uniform float u_lodRanges[8];
uniform int u_maxLodLevels;
uniform vec3 u_cameraPos;

// Light
uniform mat4 u_lightSpaceMatrix;

void main()
{
    vec2 gridPos = a_gridPosAndSkirt.xy;
    float isSkirt = a_gridPosAndSkirt.z;

    vec2 worldXZ = u_nodeOffset + gridPos * u_nodeScale;

    // Pre-sample height at unmorphed position for 3D morph distance
    vec2 preSampleUV = (worldXZ - u_terrainOrigin) / u_terrainSize;
    preSampleUV = clamp(preSampleUV, vec2(0.0), vec2(1.0));
    float preHeight = texture(u_heightmap, preSampleUV).r * u_heightScale;

    // Per-vertex morphing (must match main pass exactly)
    float vertexDist = distance(u_cameraPos, vec3(worldXZ.x, preHeight, worldXZ.y));
    float lodRange = u_lodRanges[u_lodLevel];
    float prevRange = (u_lodLevel > 0) ? u_lodRanges[u_lodLevel - 1] : 0.0;
    float rangeSpan = lodRange - prevRange;
    float morphFactor = 0.0;
    if (rangeSpan > 0.0)
    {
        float t = (vertexDist - prevRange) / rangeSpan;
        morphFactor = clamp((t - 0.5) / 0.45, 0.0, 1.0);
    }

    float step = u_nodeScale / float(u_gridResolution - 1);
    vec2 worldInGrid = (worldXZ - u_nodeOffset) / step;
    worldInGrid = floor(worldInGrid + 0.5);
    vec2 morphOffset = mod(worldInGrid, 2.0) * step;
    worldXZ -= morphOffset * morphFactor;

    vec2 terrainUV = (worldXZ - u_terrainOrigin) / u_terrainSize;
    terrainUV = clamp(terrainUV, vec2(0.0), vec2(1.0));
    float height = texture(u_heightmap, terrainUV).r * u_heightScale;

    if (isSkirt > 0.5)
    {
        height -= u_nodeScale * 0.3;
    }

    vec3 worldPos = vec3(worldXZ.x, height, worldXZ.y);
    gl_Position = u_lightSpaceMatrix * vec4(worldPos, 1.0);
}
