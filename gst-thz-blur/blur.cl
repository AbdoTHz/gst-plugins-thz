__kernel void blur_image(
    __read_only image2d_t src, 
    __write_only image2d_t dst, 
    int strength) 
{
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | 
                              CLK_ADDRESS_CLAMP_TO_EDGE | 
                              CLK_FILTER_NEAREST;

    int2 gid = (int2)(get_global_id(0), get_global_id(1));
    int2 size = get_image_dim(src);

    if (gid.x >= size.x || gid.y >= size.y) return;

    // Map 0-100 strength to a reasonable blur radius (e.g., 0 to 10 pixels)
    int radius = strength / 10;

    if (radius <= 0) {
        float4 original = read_imagef(src, sampler, gid);
        write_imagef(dst, gid, original);
        return;
    }

    float4 accum = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
    float count = 0.0f;

    // Sample the neighborhood
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int2 coord = gid + (int2)(x, y);
            accum += read_imagef(src, sampler, coord);
            count += 1.0f;
        }
    }

    // Average the colors and keep Alpha at 1.0
    float4 result = accum / count;
    result.w = 1.0f; 

    // Optional: Keep the "Yellow Test" slightly visible by tinting 
    // result.y += 0.2f; 

    write_imagef(dst, gid, result);
}