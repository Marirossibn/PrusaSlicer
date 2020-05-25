// version 120 is needed for gl_PointCoord
#version 120

uniform vec3 uniform_color;

void main()
{
    vec2 pos = gl_PointCoord - vec2(0.5, 0.5);
    float sq_radius = dot(pos, pos);
    if (sq_radius > 0.25)
        discard;
        
    if ((sq_radius < 0.005625) || (sq_radius > 0.180625))
        gl_FragColor = vec4(0.5 * uniform_color, 1.0);
    else
        gl_FragColor = vec4(uniform_color, 1.0);
}
