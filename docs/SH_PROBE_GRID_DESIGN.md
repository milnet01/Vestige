# SH Probe Grid Design Document

**Status:** Implemented. `engine/renderer/sh_probe_grid.{h,cpp}` ships the L2 SH probe grid (9 coefficients × 3 channels per probe; 7× RGBA16F 3D textures with hardware trilinear interpolation; texture units 17–23); scene-shader integration in `assets/shaders/scene.frag.glsl`; unit coverage in `tests/test_sh_probe_grid.cpp`. This document is retained as the original design record.

## Problem

The current light probe system uses a single cubemap probe per zone. This has limitations:
- Only two states: inside or outside the probe volume (no smooth spatial variation)
- Each cubemap probe costs ~2.5 MB (irradiance + prefilter)
- Specular reflections from cubemap probes still shift with camera (view-dependent)
- No fine-grained spatial variation in ambient lighting

## Solution: L2 Spherical Harmonics Probe Grid

Replace cubemap probes with a 3D grid of Spherical Harmonic probes. Each probe stores 9 L2 SH coefficients per RGB channel (27 floats), encoding the diffuse irradiance at that position. Surfaces sample the grid with hardware trilinear interpolation for smooth ambient lighting everywhere.

## References

- Ramamoorthi & Hanrahan (2001), "An Efficient Representation for Irradiance Environment Maps" — proves L2 SH captures irradiance with ~1% error
- Sloan (2008), "Stupid Spherical Harmonics Tricks" (GDC) — practical SH reference
- Frostbite GDC 2018, "Precomputed Global Illumination in Frostbite" — radiosity + SH probes in production
- DDGI (Majercik et al., JCGT 2019) — probe interpolation with visibility
- Rory Driscoll, "Cubemap Texel Solid Angle" — exact solid angle formula

## SH Math

### L2 Basis Functions (9 coefficients)

For a unit direction `(x, y, z)`:
```
Band 0 (1 coeff):  Y00 = 0.282095
Band 1 (3 coeffs): Y1m1 = 0.488603 * y
                    Y10  = 0.488603 * z
                    Y11  = 0.488603 * x
Band 2 (5 coeffs): Y2m2 = 1.092548 * x * y
                    Y2m1 = 1.092548 * y * z
                    Y20  = 0.315392 * (3z² - 1)
                    Y21  = 1.092548 * x * z
                    Y22  = 0.546274 * (x² - y²)
```

### Cubemap → SH Projection

For each texel in a 6-face cubemap:
1. Convert (face, u, v) → unit direction
2. Compute solid angle weight: `4.0 / (sqrt(1 + u² + v²) * (1 + u² + v²))`
3. Evaluate 9 SH basis functions for this direction
4. Accumulate: `shCoeffs[i] += color * basis[i] * weight`
5. Normalize: `shCoeffs[i] *= 4π / weightSum`

### Radiance → Irradiance Cosine Convolution

Multiply each coefficient by its band's cosine lobe constant:
```
Band 0: A₀ = π       = 3.141593
Band 1: A₁ = 2π/3    = 2.094395
Band 2: A₂ = π/4     = 0.785398
```

Pre-bake these into stored coefficients for zero runtime cost.

### GLSL Evaluation (Ramamoorthi/Hanrahan optimized)

```glsl
vec3 evaluateIrradianceSH(vec3 L[9], vec3 n)
{
    const float c1 = 0.429043;
    const float c2 = 0.511664;
    const float c3 = 0.743125;
    const float c4 = 0.886227;
    const float c5 = 0.247708;

    return
        c4 * L[0] +
        2.0 * c2 * (L[1]*n.y + L[2]*n.z + L[3]*n.x) +
        2.0 * c1 * (L[4]*n.x*n.y + L[5]*n.y*n.z + L[7]*n.x*n.z) +
        c3 * L[8] * (n.x*n.x - n.y*n.y) +
        c5 * L[6] * (3.0*n.z*n.z - 1.0);
}
```

Cost: 9 multiply-adds per pixel.

## GPU Storage: 3D Textures with RGBA16F

Store SH coefficients in 3D textures with hardware trilinear interpolation (free 8-probe blending).

### Layout: 7 RGBA16F 3D Textures

Each texture has dimensions `(gridResX, gridResY, gridResZ)`:

| Texture | R | G | B | A |
|---------|---|---|---|---|
| shTex0 | L[0].r | L[0].g | L[0].b | L[1].r |
| shTex1 | L[1].g | L[1].b | L[2].r | L[2].g |
| shTex2 | L[2].b | L[3].r | L[3].g | L[3].b |
| shTex3 | L[4].r | L[4].g | L[4].b | L[5].r |
| shTex4 | L[5].g | L[5].b | L[6].r | L[6].g |
| shTex5 | L[6].b | L[7].r | L[7].g | L[7].b |
| shTex6 | L[8].r | L[8].g | L[8].b | (unused) |

### Alternative: 3 Textures (one per color channel)

Each texture stores 3 layers of 3 coefficients per Z-slice:
```
Texture R (RGBA16F, dims: resX, resY, resZ*3):
  slice z*3+0: [coeff0, coeff1, coeff2, coeff3]
  slice z*3+1: [coeff4, coeff5, coeff6, coeff7]
  slice z*3+2: [coeff8, padding, padding, padding]
```

**Decision:** Use the 7-texture approach. It's slightly more texture bindings but each texel maps directly to a coefficient — simpler packing and unpacking. Uses texture units 17-23 (above the existing 0-16 range).

### Memory

For a 10×5×15 grid: 750 probes × 7 textures × 4 channels × 2 bytes = **42 KB total**. Trivial.

## Architecture

### New Files

- `engine/renderer/sh_probe_grid.h/.cpp` — SHProbeGrid class
- `assets/shaders/sh_probe_common.glsl` — Shared SH evaluation functions (included by scene shader)

### Modified Files

- `assets/shaders/scene.frag.glsl` — Replace cubemap probe IBL with SH grid lookup
- `engine/renderer/renderer.h/.cpp` — SH grid binding, texture unit management
- `engine/core/engine.cpp` — Grid placement, capture trigger

### SHProbeGrid Class

```cpp
class SHProbeGrid
{
public:
    struct Config
    {
        glm::vec3 worldMin;      // Grid AABB minimum
        glm::vec3 worldMax;      // Grid AABB maximum
        glm::ivec3 resolution;   // Number of probes per axis
    };

    bool initialize(const Config& config);
    void upload();               // Upload to GPU (3D textures)

    // Fill from cubemap captures at each grid position
    void captureFromScene(Renderer& renderer, const SceneRenderData& data,
                          const Camera& camera);

    // Fill from radiosity results (future)
    void fillFromRadiosity(const RadiosityData& data);

    // Bind textures for scene shader
    void bind() const;

    // Uniforms to set on the scene shader
    glm::vec3 getWorldMin() const;
    glm::vec3 getWorldMax() const;
    glm::ivec3 getResolution() const;

    // Access probe data (for radiosity injection)
    void setProbeIrradiance(int x, int y, int z, const glm::vec3 coeffs[9]);

private:
    Config m_config;
    // CPU storage: [z][y][x][coeff][channel]
    std::vector<glm::vec3> m_probeData;  // size = resX*resY*resZ * 9
    GLuint m_textures[7] = {};
    bool m_ready = false;
};
```

### Scene Shader Integration

```glsl
// SH Probe Grid uniforms
uniform bool u_hasSHGrid;
uniform vec3 u_shGridWorldMin;
uniform vec3 u_shGridWorldMax;
uniform sampler3D u_shTex[7];   // Units 17-23

vec3 sampleSHGrid(vec3 worldPos, vec3 normal)
{
    // Convert world position to grid UV [0,1]
    vec3 gridUV = (worldPos - u_shGridWorldMin) / (u_shGridWorldMax - u_shGridWorldMin);
    gridUV = clamp(gridUV, 0.0, 1.0);

    // Sample 7 textures (hardware trilinear interpolation)
    vec4 t0 = texture(u_shTex[0], gridUV);
    vec4 t1 = texture(u_shTex[1], gridUV);
    vec4 t2 = texture(u_shTex[2], gridUV);
    vec4 t3 = texture(u_shTex[3], gridUV);
    vec4 t4 = texture(u_shTex[4], gridUV);
    vec4 t5 = texture(u_shTex[5], gridUV);
    vec4 t6 = texture(u_shTex[6], gridUV);

    // Unpack 9 vec3 coefficients from 7 vec4s
    vec3 L[9];
    L[0] = vec3(t0.r, t0.g, t0.b);
    L[1] = vec3(t0.a, t1.r, t1.g);
    L[2] = vec3(t1.b, t1.a, t2.r);  // Note: t1.a maps to t2.r for channel layout
    // ... (exact mapping depends on packing choice)

    // Evaluate irradiance from SH
    return evaluateIrradianceSH(L, normal);
}
```

The SH grid replaces the cubemap probe for diffuse ambient. The global IBL prefilter cubemap is kept for specular reflections on outdoor surfaces.

## Capture Pipeline

For initial data (before radiosity is implemented):

1. Place grid covering the scene volume
2. For each probe position (x, y, z):
   a. Render 6-face cubemap at 64×64 from probe position
   b. Project cubemap → 9 L2 SH radiance coefficients
   c. Apply cosine convolution → irradiance coefficients
   d. Store in grid
3. Upload grid to 3D textures
4. Scene shader samples grid instead of cubemap probes

## Radiosity Integration (Future)

When radiosity is implemented, the pipeline becomes:
1. Discretize scene into patches
2. Solve radiosity (hemicube progressive shooting)
3. At each probe position, render cubemap of radiosity-lit scene
4. Project to SH and store in grid
5. Upload to GPU

## Performance

| Metric | Value |
|--------|-------|
| Grid size (10×5×15) | 750 probes |
| Memory | ~42 KB |
| Shader cost | 7 texture samples + 9 MADs |
| Capture time (750 × 6 faces × 64×64) | ~2-5 seconds |
| Capture time with radiosity | Add ~1 second for radiosity solve |

## Implementation Steps

1. Create `SHProbeGrid` class with CPU storage and cubemap→SH projection
2. Add 3D texture upload and binding
3. Modify scene shader to support SH grid sampling
4. Add grid capture method (render cubemaps at grid positions)
5. Wire into engine.cpp (replace cubemap probe with SH grid)
6. Add radiosity integration point (future step)
7. Remove cubemap probe code (or keep as fallback for specular)

## Mesa AMD Considerations

- 7 new sampler3D uniforms must always have valid textures bound
- Use fallback 1×1×1 3D textures on units 17-23 in beginFrame()
- The `u_hasSHGrid` bool prevents sampling when grid isn't ready
