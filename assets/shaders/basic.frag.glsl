#version 450 core

in vec3 v_color;
in vec3 v_normal;
in vec3 v_fragPosition;

out vec4 fragColor;

void main()
{
    // Simple directional light for basic shading
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 norm = normalize(v_normal);

    // Ambient light — base illumination so nothing is fully black
    float ambient = 0.15;

    // Diffuse light — surfaces facing the light are brighter
    float diffuse = max(dot(norm, lightDir), 0.0);

    // Combine lighting with vertex color
    vec3 result = (ambient + diffuse) * v_color;
    fragColor = vec4(result, 1.0);
}
