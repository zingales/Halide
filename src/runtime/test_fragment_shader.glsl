// FRAGMENT SHADER

#version 110

uniform float fade_factor;
uniform sampler2D textures[2];

// linked to texcoord from vertex shader
// varying variable become inputs here, is linked to vertex shader's
// varying variable of the same name

varying vec2 texcoord;

void main()
{
    gl_FragColor = mix(
        texture2D(textures[0], texcoord),
        texture2D(textures[1], texcoord),
        fade_factor
    );
}