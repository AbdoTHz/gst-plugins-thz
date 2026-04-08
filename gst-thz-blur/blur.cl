__kernel void blur_image(
    __read_only image2d_t src, 
    __write_only image2d_t dst, 
    int strength) 
{
    int2 gid = (int2)(get_global_id(0), get_global_id(1));
    
    // Hardcode a bright red pixel: (Red=1.0, Green=0.0, Blue=0.0, Alpha=1.0)
    float4 test_color = (float4)(1.0f, 1.0f, 0.0f, 1.0f);

    write_imagef(dst, gid, test_color);
}