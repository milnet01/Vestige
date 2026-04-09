/// @file equirect_preview.frag.glsl
/// @brief Equirectangular HDRI preview with rotation and exposure control.
#version 450 core

in vec2 v_texCoord;

out vec4 fragColor;

uniform sampler2D u_equirectMap;
uniform float u_exposure;    // EV stops
uniform float u_yaw;         // Horizontal rotation in radians
uniform float u_pitch;       // Vertical rotation in radians

const float PI = 3.14159265359;

void main()
{
    // Convert UV to spherical direction
    // UV (0,0) = bottom-left, (1,1) = top-right
    float theta = (v_texCoord.x - 0.5) * 2.0 * PI;  // [-PI, PI] longitude
    float phi = (v_texCoord.y - 0.5) * PI;            // [-PI/2, PI/2] latitude

    // Apply yaw rotation
    theta += u_yaw;

    // Apply pitch rotation
    phi += u_pitch;
    phi = clamp(phi, -PI * 0.5, PI * 0.5);

    // Spherical to Cartesian
    vec3 dir;
    dir.x = cos(phi) * sin(theta);
    dir.y = sin(phi);
    dir.z = cos(phi) * cos(theta);

    // Convert direction back to equirectangular UV for sampling
    float sampleTheta = atan(dir.x, dir.z);
    float samplePhi = asin(clamp(dir.y, -1.0, 1.0));
    vec2 sampleUV = vec2(sampleTheta / (2.0 * PI) + 0.5,
                         samplePhi / PI + 0.5);

    vec3 color = texture(u_equirectMap, sampleUV).rgb;

    // Apply exposure
    color *= pow(2.0, u_exposure);

    // Reinhard tonemapping + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    fragColor = vec4(color, 1.0);
}
