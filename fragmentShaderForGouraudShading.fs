#version 330 core
out vec4 FragColor;

in vec4 LightingColor;

void main()
{
   FragColor = LightingColor;
}


