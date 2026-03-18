#version 450 core

layout(location = 0) in vec3 position;

// Per-instance model matrix (locations 6-9)
layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

uniform mat4 u_model;
uniform mat4 u_lightSpaceMatrix;
uniform bool u_useInstancing;

void main()
{
    mat4 model;
    if (u_useInstancing)
    {
        model = mat4(instanceModelCol0, instanceModelCol1,
                     instanceModelCol2, instanceModelCol3);
    }
    else
    {
        model = u_model;
    }

    gl_Position = u_lightSpaceMatrix * model * vec4(position, 1.0);
}
