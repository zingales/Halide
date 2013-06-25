// VERTEX SHADER

#version 110

// vertex's screen space position
// inputs come from uniform variables, which supply values from the uniform state
// attribute variables supply per-vertex attributes from the vertex array
// shader assigns its per-vertex outputs to varying variables
// GLSL predefines some varying variables to receive special outputs used by the
// graphics pipeline, including the gl_Position variable we used here.

attribute vec2 position;

varying vec2 texcoord;

// a GLSL shader starts executing from the main function,
// which in GLSL's case takes no arguments and returns void
void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    // map our screen-space positions from screen space (–1 to 1)
    // to texture space (0 to 1) and assigns the result to the vertex's texcoord.
    // vec2(0.5) initializes all elts of vector to 0.5
    texcoord = position * vec2(0.5) + vec2(0.5);
}
