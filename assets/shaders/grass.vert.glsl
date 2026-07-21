#version 450 core

// Meadow GPU grass — vertex-shader-generated quadratic Bézier blade ribbon + clumping.
// Design: docs/phases/phase_10_meadow_gpu_grass_design.md §5.1 (blade) + §5.2a (clumping).
//
// No vertex attributes: each vertex is computed from gl_VertexID + gl_InstanceID and the
// per-blade seed fetched from the SSBO. An N-segment blade is a GL_TRIANGLE_STRIP of
// 2N+1 verts (rows 0..N-1 emit a left+right pair, row N a single centred tip).
//
// Two parity seams with the CPU (§8):
//   * the blade generator below MUST match Vestige::grassBladeVertex (grass_blade.h);
//   * the grassClump() function below MUST match Vestige::grassClump (grass_clump.h) —
//     integer bit-hash, committed constants jitterRadius=0.25·cellSize, kernelR=cellSize.

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

uniform mat4  u_viewProjection;
uniform int   u_segments;      // N — LOD segment count for this chunk (§5.3, per-chunk tier)
uniform uint  u_baseOffset;    // chunk base offset into the shared seed buffer (§5.5)
uniform float u_clumpCellSize; // grassClump cell size = clumpScale (§5.2a)
uniform float u_clumpStrength; // wild↔tidy dial [0,1] (§5.2a)

// --- G3 distance LOD (§5.3): per-blade blade-count fade keyed on grassKeptFraction ---
uniform vec3  u_cameraPos;     // world camera position (per-blade root→camera distance)
uniform float u_chunkCount;    // this chunk's FULL blade count (rank = gl_InstanceID/count)
uniform float u_lodNearMid;    // near→mid tier boundary (m)
uniform float u_lodMidFar;     // mid→far tier boundary (m)
uniform float u_lodBandWidth;  // fade-band width leading up to each boundary
uniform float u_lodNearFrac;   // blades kept close in (also the survivor-widen reference)
uniform float u_lodMidFrac;    // blades kept in the mid band
uniform float u_lodFarFrac;    // blades kept far out
uniform float u_lodRankBand;   // rank-space softness of the per-blade cutoff

// --- G4 wind + shading passthrough (§5.4) ---
uniform mat4  u_view;          // for the view-space depth used to pick a shadow cascade
uniform float u_time;          // seconds — animates the wind sway
uniform vec2  u_windDir;       // normalised world-XZ wind direction (env forces)
uniform float u_windStrength;  // env wind speed (m/s); 0 = dead calm (meadow default)

out vec3  v_worldPos;   // world-space fragment position (shadow lookup + view dir)
out vec3  v_normal;     // vertical-biased blade normal (§5.4 — legibility by value)
out vec3  v_tint;       // base green × per-clump tint drift × per-blade value
out float v_viewDepth;  // view-space depth (cascade select)
out float v_heightAO;   // 0 at root → 1 at tip (inter-blade AO)

// ---------------------------------------------------------------------------------------
// Clump field — GLSL twin of Vestige::grassClump (grass_clump.h). Integer bit-hash so the
// CPU AABB pad and the GPU blade agree on clump membership (§5.2a parity).
// ---------------------------------------------------------------------------------------

const uint SALT_JITTER = 0x1000001u;
const uint SALT_HEIGHT = 0x2000003u;
const uint SALT_LEAN   = 0x3000005u;
const uint SALT_TINT   = 0x4000007u;
const uint SALT_BEND   = 0x5000009u;
const uint SALT_PHASE  = 0x600000Bu;
const float TAU = 6.2831853;

uint grassHashU32(uint x)
{
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x;
}

uint grassCellHash(int cellX, int cellZ, uint salt)
{
    uint h = uint(cellX) * 0x9E3779B1u ^ uint(cellZ) * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
    return grassHashU32(h);
}

float grassU32ToUnit(uint h) { return float(h) * (1.0 / 4294967296.0); }

struct ClumpResult { float height; vec2 leanDir; float tint; float bend; float phase; };

struct ClumpCell { vec2 centre; float height; vec2 leanDir; float tint; float bend; float phase; uint id; };

ClumpCell clumpCell(int cx, int cz, float cellSize, float jitterRadius)
{
    ClumpCell c;
    float jx = grassU32ToUnit(grassCellHash(cx, cz, SALT_JITTER));
    float jr = grassU32ToUnit(grassCellHash(cx, cz, SALT_JITTER + 1u));
    float ang = jx * TAU;
    float rad = jr * jitterRadius;
    vec2 cellCentre = vec2((float(cx) + 0.5) * cellSize, (float(cz) + 0.5) * cellSize);
    c.centre  = cellCentre + vec2(cos(ang), sin(ang)) * rad;
    c.height  = 0.6 + 1.0 * grassU32ToUnit(grassCellHash(cx, cz, SALT_HEIGHT));   // 0.6–1.6
    float la  = grassU32ToUnit(grassCellHash(cx, cz, SALT_LEAN)) * TAU;
    c.leanDir = vec2(cos(la), sin(la));
    c.tint    = grassU32ToUnit(grassCellHash(cx, cz, SALT_TINT)) * 2.0 - 1.0;     // [-1,1]
    c.bend    = grassU32ToUnit(grassCellHash(cx, cz, SALT_BEND));                 // [0,1]
    c.phase   = grassU32ToUnit(grassCellHash(cx, cz, SALT_PHASE)) * TAU;          // [0,2π)
    c.id      = grassCellHash(cx, cz, 0u);
    return c;
}

// Smooth-kernel weighted 3×3 blend; scalar factors are C⁰, lean is a renormalised vector
// blend (antipodal-guarded, deterministic tie-break), phase is nearest-only. Σw<ε fallback
// (unreachable in the committed envelope) guarantees no 0/0 NaN.
ClumpResult grassClump(vec2 worldXZ, float cellSize)
{
    float jitterRadius = 0.25 * cellSize;
    float kernelR      = cellSize;
    float epsilon      = 1.0e-6;

    int baseX = int(floor(worldXZ.x / cellSize));
    int baseZ = int(floor(worldXZ.y / cellSize));

    float sumW = 0.0, sumH = 0.0, sumT = 0.0, sumB = 0.0;
    vec2  sumLean = vec2(0.0);
    float nearestD2 = 3.402823e38;
    float bestW = -1.0;
    // Zero-init (matches the CPU mirror's `{}`): both are always assigned on the first loop
    // iteration (w ≥ 0 > bestW), but a spec-literal compiler still warns without the init.
    ClumpCell nearest  = ClumpCell(vec2(0.0), 0.0, vec2(0.0), 0.0, 0.0, 0.0, 0u);
    ClumpCell dominant = ClumpCell(vec2(0.0), 0.0, vec2(0.0), 0.0, 0.0, 0.0, 0u);

    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            ClumpCell c = clumpCell(baseX + dx, baseZ + dz, cellSize, jitterRadius);
            vec2 delta = worldXZ - c.centre;
            float d2 = dot(delta, delta);
            float d = sqrt(d2);
            float w = 1.0 - smoothstep(0.0, kernelR, d);   // edge0 < edge1 (§5.2a)

            sumW += w; sumH += w * c.height; sumT += w * c.tint; sumB += w * c.bend;
            sumLean += w * c.leanDir;

            if (d2 < nearestD2) { nearestD2 = d2; nearest = c; }
            if (w > bestW || (w == bestW && c.id < dominant.id)) { bestW = w; dominant = c; }
        }
    }

    ClumpResult o;
    if (sumW < epsilon)
    {
        o.height = nearest.height; o.leanDir = nearest.leanDir; o.tint = nearest.tint;
        o.bend = nearest.bend; o.phase = nearest.phase;
        return o;
    }

    float invW = 1.0 / sumW;
    o.height = sumH * invW;
    o.tint   = sumT * invW;
    o.bend   = sumB * invW;
    o.phase  = nearest.phase;   // nearest-only (cyclic — never averaged)

    if (dot(sumLean, sumLean) < 1.0e-12) { o.leanDir = dominant.leanDir; }
    else                                 { o.leanDir = normalize(sumLean); }
    return o;
}

// ---------------------------------------------------------------------------------------
// LOD blade-count fade — GLSL twin of Vestige::grassKeptFraction (grass_lod.h §5.3). The
// CPU evaluates this at the chunk's nearest point (instanceCount); the VS evaluates it per
// blade at each root's distance. Same curve = no seam between the CPU cut and the GPU fade.
// ---------------------------------------------------------------------------------------
float grassKeptFraction(float d)
{
    float tMid = smoothstep(u_lodNearMid - u_lodBandWidth, u_lodNearMid, d);
    float f = mix(u_lodNearFrac, u_lodMidFrac, tMid);
    float tFar = smoothstep(u_lodMidFar - u_lodBandWidth, u_lodMidFar, d);
    f = mix(f, u_lodFarFrac, tFar);
    return f;
}

// ---------------------------------------------------------------------------------------

void main()
{
    GrassBlade b = blades[gl_InstanceID + u_baseOffset];

    // --- G3 blade-count LOD (§5.3): fade the drop set, widen the survivors ---
    // rank ∈ [0,1) over the SHUFFLED chunk order — a prefix is a spatially-uniform subset
    // (§5.2), so a soft rank cutoff thins the field evenly. `kept` shrinks with distance;
    // blades whose rank nears `kept` taper their height+width to 0 rather than pop out at a
    // chunk boundary, and the survivors widen to hold apparent density.
    float bladeDist = distance(u_cameraPos, b.rootPos);
    float rank      = float(gl_InstanceID) / max(u_chunkCount, 1.0);
    float kept      = grassKeptFraction(bladeDist);
    float lodPresence = 1.0 - smoothstep(kept - u_lodRankBand, kept, rank);   // 1 keep, 0 drop
    float lodWiden    = sqrt(u_lodNearFrac / max(kept, 1.0e-3));               // survivor widening

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

    // --- clump influence (§5.2a): blades conform toward their tussock ---
    ClumpResult cl = grassClump(b.rootPos.xz, u_clumpCellSize);
    float s = u_clumpStrength;

    float height = b.height * mix(1.0, cl.height, s) * lodPresence;   // tall/short tussocks (× LOD fade)
    float lean   = b.lean * (1.0 + cl.bend * s);        // clump flop raises the tip lean

    // Facing blended as a DIRECTION (never the wrapping scalar angle, §5.2a): mix the
    // blade's own facing with the clump lean dir, renormalise, back to an angle. Guard the
    // ~antipodal degenerate (mix lands near zero) with the blade's own facing.
    vec2 ownDir   = vec2(cos(b.facingAngle), sin(b.facingAngle));
    vec2 mixedDir = mix(ownDir, cl.leanDir, s);
    float facing  = (dot(mixedDir, mixedDir) < 1.0e-8) ? b.facingAngle
                                                       : atan(mixedDir.y, mixedDir.x);

    // --- quadratic Bézier ribbon (mirror of grass_blade.h) + G4 wind bend (§5.4) ---
    vec3 up  = vec3(0.0, 1.0, 0.0);
    vec3 dir = vec3(cos(facing), 0.0, sin(facing));

    vec3 p0 = b.rootPos;                    // root stays anchored to the ground
    vec3 p1 = p0 + up * height;
    vec3 p2 = p1 + dir * (height * lean);

    // Wind sway (§5.4): a gentle, height-scaled, capped push in the wind direction that
    // bends the blade — P0 fixed, P1 a little, the tip (P2) most. Three octaves compose:
    // the whole tussock sways together (per-clump cl.phase), each blade offsets a touch
    // (per-blade hash), and a faster gust ripples through. windStrength 0 ⇒ dead calm, so
    // the meadow's default no-wind scene leaves the field still (§11 reduced-motion floor).
    const float WIND_FREQ = 1.6;
    float swayM    = 0.05 * min(u_windStrength, 8.0) * height;   // metres, gentle-capped
    float bladeU   = grassU32ToUnit(b.hash);
    float clumpS   = sin(u_time * WIND_FREQ + cl.phase);
    float bladeS   = sin(u_time * WIND_FREQ * 1.7 + bladeU * TAU);
    float gustS    = 0.2 * sin(u_time * WIND_FREQ * 3.1 + cl.phase * 2.0);
    vec2  windPush = u_windDir * (swayM * (0.7 * clumpS + 0.3 * bladeS + gustS));
    p1.xz += windPush * 0.35;
    p2.xz += windPush;

    float t = float(row) / float(N);
    float u = 1.0 - t;
    vec3 curve = (u * u) * p0 + (2.0 * u * t) * p1 + (t * t) * p2;

    // Width axis: horizontal, perpendicular to the facing direction = cross(up, dir).
    vec3 widthAxis = vec3(sin(facing), 0.0, -cos(facing));
    float halfW = 0.5 * (b.width * (1.0 - t) * lodPresence * lodWiden);   // × LOD fade + survivor widen

    vec3 pos = curve + (float(side) * halfW) * widthAxis;

    // Blade normal (§5.4): the ribbon's face normal (cross of the along-blade tangent and
    // the width axis) biased strongly upward, so grass reads as lit-from-sky yet keeps a
    // gentle per-facing variation — legibility carried by value/normal, not hue (§11). The
    // tiny up-nudge keeps the tangent non-zero on a fully LOD-faded (height→0) blade so its
    // degenerate verts never normalize(0)→NaN.
    vec3 tangent = 2.0 * u * (p1 - p0) + 2.0 * t * (p2 - p1) + up * 1.0e-4;
    vec3 faceN   = cross(normalize(tangent), widthAxis);
    v_normal     = normalize(faceN + up * 2.0);

    // Tint: root→tip green, drifted per-tussock (cl.tint ∈ [-1,1], value-only so it stays
    // colour-blind safe) and jittered a hair per blade so no two read identically.
    vec3 tint = mix(vec3(0.05, 0.18, 0.03), vec3(0.34, 0.55, 0.18), t);
    tint *= (1.0 + cl.tint * 0.12 * s);
    tint *= (0.9 + 0.2 * grassU32ToUnit(b.hash ^ 0x55u));
    v_tint = tint;

    v_worldPos  = pos;
    v_viewDepth = -(u_view * vec4(pos, 1.0)).z;
    v_heightAO  = pow(t, 1.2);

    gl_Position = u_viewProjection * vec4(pos, 1.0);
}
