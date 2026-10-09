@vs vs_shape
layout(binding=0) uniform vs_params {
    vec2 u_resolution;
};

in vec2 pos;
in vec2 uv;
in vec2 size;
in vec2 radius_border;
in vec4 fill_color;
in vec4 border_color;

out vec2 v_uv;
out vec2 v_size;
out vec2 v_radius_border;
out vec4 v_fill_color;
out vec4 v_border_color;

void main() {
    // Convert from pixel coordinates to Normalized Device Coordinates (NDC)
    vec2 ndc_pos = (pos / u_resolution) * 2.0 - 1.0;
    ndc_pos.y = -ndc_pos.y; // Flip Y for graphics API standard
    
    gl_Position = vec4(ndc_pos, 0.0, 1.0);
    
    v_uv = uv;
    v_size = size;
    v_radius_border = radius_border;
    v_fill_color = fill_color;
    v_border_color = border_color;
}
@end

@fs fs_shape
in vec2 v_uv;
in vec2 v_size;
in vec2 v_radius_border;
in vec4 v_fill_color;
in vec4 v_border_color;

out vec4 frag_color;

float rounded_box_sdf(vec2 center_pos, vec2 size, float radius) {
    vec2 q = abs(center_pos) - size + vec2(radius);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    // Calculate SDF
    vec2 half_size = v_size * 0.5;
    vec2 center_pos = (v_uv * v_size) - half_size;
    float dist = rounded_box_sdf(center_pos, half_size, v_radius_border.x);
    
    // Anti-aliasing
    float smoothed_alpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    
    vec4 final_color = v_fill_color;
    
    // Borders (if border > 0)
    if (v_radius_border.y > 0.0) {
        float inner_dist = rounded_box_sdf(center_pos, half_size - vec2(v_radius_border.y), max(0.0, v_radius_border.x - v_radius_border.y));
        float border_alpha = smoothstep(-1.0, 1.0, inner_dist);
        // Mix border color and fill color
        final_color = mix(v_border_color, v_fill_color, border_alpha);
    }
    
    frag_color = final_color * smoothed_alpha;
}
@end

@program shape vs_shape fs_shape
