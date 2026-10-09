@vs vs_text
layout(binding=0) uniform text_vs_params {
    vec2 u_resolution;
};

in vec2 pos;
in vec2 uv;
in vec4 color;

out vec2 v_uv;
out vec4 v_color;

void main() {
    // Convert from pixel coordinates to Normalized Device Coordinates (NDC)
    vec2 ndc_pos = (pos / u_resolution) * 2.0 - 1.0;
    ndc_pos.y = -ndc_pos.y; // Flip Y
    
    gl_Position = vec4(ndc_pos, 0.0, 1.0);
    v_uv = uv;
    v_color = color;
}
@end

@fs fs_text
layout(binding=0) uniform texture2D tex;
layout(binding=0) uniform sampler smp;

in vec2 v_uv;
in vec4 v_color;

out vec4 frag_color;

void main() {
    float alpha = texture(sampler2D(tex, smp), v_uv).r;
    frag_color = vec4(v_color.rgb, v_color.a * alpha);
}
@end

@program text vs_text fs_text
