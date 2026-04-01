# Phase 8 Batch 11: Cloth Solver Improvements Design

## Overview
Five improvements to the existing XPBD cloth simulator that enhance realism and quality: dihedral bending constraints, constraint ordering, adaptive damping, collider friction, and thick particle model.

## Research Summary

### 11a: Dihedral Angle Bending Constraints
**Current:** Skip-one distance constraints approximate bending but can't capture curvature properly.
**Improvement:** Dihedral angle constraints measure the actual angle between adjacent triangle normals.

Constraint: `C = φ - φ_rest` where `φ = acos(clamp(dot(n1, n2), -1, 1))`

Given two triangles sharing edge (p2→p3) with wing vertices p0 (triangle 0) and p1 (triangle 1):
```
n1 = normalize(cross(p2 - p0, p3 - p0))   // triangle 0 normal
n2 = normalize(cross(p3 - p1, p2 - p1))   // triangle 1 normal
```

Gradients (Müller et al. 2007, InteractiveComputerGraphics/PositionBasedDynamics):
```
e = p3 - p2
elen = length(e)
invElen = 1.0 / elen

grad0 = elen * n1
grad1 = elen * n2
grad2 = dot(p0 - p3, e) * invElen * n1 + dot(p1 - p3, e) * invElen * n2
grad3 = dot(p2 - p0, e) * invElen * n1 + dot(p2 - p1, e) * invElen * n2
```

XPBD correction:
```
alphaTilde = compliance / dt²
wSum = w0*|grad0|² + w1*|grad1|² + w2*|grad2|² + w3*|grad3|²
lambda = -C / (wSum + alphaTilde)
if (dot(cross(n1, n2), e) > 0) lambda = -lambda;
corr_i = w_i * lambda * grad_i
```

Sources: Müller et al. 2007, Bergou et al. "Discrete Shells" 2006, Carmen Cincotti XPBD bending tutorial.

### 11b: Constraint Ordering (Top-to-Bottom Sweep)
Gauss-Seidel converges faster when constraints are solved from pinned particles downward. Compute BFS depth from pinned particles, sort constraints by minimum depth of their participating particles.

Alternate forward/reverse sweeps on even/odd iterations for symmetry.

### 11c: Adaptive Damping
Speed-dependent damping: `effective_damping = base + factor * avgSpeed`. High damping during fast motion (quick settling), low damping at rest (preserves subtle motion). Clamped to [base, maxDamping].

### 11d: Collider Friction (Coulomb Model)
Decompose velocity into normal and tangential at collision point:
- Static friction: if |v_t| < μs * |v_n|, zero tangential velocity
- Kinetic friction: reduce v_t by factor (1 - μk * |v_n| / |v_t|)

Default coefficients: μs = 0.4, μk = 0.3 (cloth on general surface).

### 11e: Thick Particle Model
Give each particle a radius (half cloth thickness). Adds particleRadius to all collision margins consistently. Default: particleRadius = clothThickness / 2 = 0.01m.

## Modified Files
| File | Changes |
|------|---------|
| `physics/cloth_simulator.h` | DihedralConstraint struct, new members, friction/thickness API |
| `physics/cloth_simulator.cpp` | All five features implemented |

## API Additions
```cpp
// Dihedral bending
void setDihedralBendCompliance(float compliance);
float getDihedralBendCompliance() const;
uint32_t getDihedralConstraintCount() const;

// Adaptive damping
void setAdaptiveDamping(float factor);
float getAdaptiveDamping() const;

// Friction
void setFriction(float staticCoeff, float kineticCoeff);
float getStaticFriction() const;
float getKineticFriction() const;

// Thick particle
void setParticleRadius(float radius);
float getParticleRadius() const;
```

## Test Plan
- Dihedral bending: constraint creation count, flat cloth has zero bending energy, bent cloth corrects
- Constraint ordering: verify sorted order, convergence comparison
- Adaptive damping: fast motion → high damping, slow motion → low damping
- Friction: tangential velocity reduction, static friction sticking
- Thick particle: collision offset includes particle radius
