#version 450 core

// Meadow GPU grass — vertex-shader-generated quadratic Bézier blade ribbon.
// Design: docs/phases/phase_10_meadow_gpu_grass_design.md §5.1.
//
// No vertex attributes: each vertex is computed from gl_VertexID + gl_InstanceID and the
// per-blade seed fetched from the SSBO. An N-segment blade is a GL_TRIANGLE_STRIP of
// 2N+1 verts (rows 0..N-1 emit a left+right pair, row N a single centred tip).
//
// This math MUST stay identical to Vestige::grassBladeVertex in
// engine/environment/grass_blade.h — that CPU mirror is the Rule-7 parity seam (§8).

// Per-blade seed — std430, 32 bytes, field-for-field identical to Vestige::GrassBlade.
struct GrassBlade
{
    vec3  rootPos;      float height;                 // bytes 0..15
    float facingAngle;  float lean;  float width;  uint hash;  // bytes 16..31
};

layout(std430, binding = 0) readonly buffer Blades
{
    GrassBlade blades[];
};

uniform mat4 u_viewProjection;
uniform int  u_segments;   // N — LOD segment count (G1: single near tier)

out vec3 v_color;

void main()
{
    GrassBlade b = blades[gl_InstanceID];

    int N = u_segments;
    int v = gl_VertexID;
    int row;
    int side;
    if (v < 2 * N)
    {
        row  = v / 2;
        side = (v % 2 == 0) ? -1 : 1;   // even vertex = left, odd = right
    }
    else
    {
        row  = N;    // single tip vertex
        side = 0;
    }

    // --- quadratic Bézier ribbon (mirror of grass_blade.h) ---
    vec3 up  = vec3(0.0, 1.0, 0.0);
    vec3 dir = vec3(cos(b.facingAngle), 0.0, sin(b.facingAngle));

    vec3 p0 = b.rootPos;
    vec3 p1 = p0 + up * b.height;
    vec3 p2 = p1 + dir * (b.height * b.lean);

    float t = float(row) / float(N);
    float u = 1.0 - t;
    vec3 curve = (u * u) * p0 + (2.0 * u * t) * p1 + (t * t) * p2;

    // Width axis: horizontal, perpendicular to the facing direction = cross(up, dir).
    vec3 widthAxis = vec3(sin(b.facingAngle), 0.0, -cos(b.facingAngle));
    float halfW = 0.5 * (b.width * (1.0 - t));

    vec3 pos = curve + (float(side) * halfW) * widthAxis;

    // G1 flat-lit: a root→tip green gradient so the blades read as distinct 3-D shapes
    // before real shading lands in G4.
    v_color = mix(vec3(0.04, 0.16, 0.02), vec3(0.30, 0.60, 0.16), t);

    gl_Position = u_viewProjection * vec4(pos, 1.0);
}
