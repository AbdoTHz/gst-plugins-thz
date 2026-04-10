__kernel void blur_image(
    __global int* src,        // Changed: Now a raw pointer (Buffer)
    __global int* dst,        // Changed: Now a raw pointer (Buffer)
    int strength)                // Kept same
{
    // Get 2D coordinates
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    // You need the width to calculate the linear index
    // Usually passed as a param or grabbed via get_global_size(0)
    int width = get_global_size(0);
    int index = y * width + x;

    // Direct memory assignment (The "Fast" part)
    //dst[index] = 0xFFFF0000; // ABGR
    dst[index] = src[index]; // ABGR
}