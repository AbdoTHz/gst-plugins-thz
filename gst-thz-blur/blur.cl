__kernel void blur_image(
    __global int* src,
    __global int* dst,
    int strength) 
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    int width = get_global_size(0);
    int height = get_global_size(1);
    int index = y * width + x;

    // Calculate radius: e.g., strength of 20 results in a radius of 2
    int radius = strength/8;
    
    // If radius is 0, just copy the pixel (optionally dim it)
    if (radius < 1) {
        int p = src[index];
        uchar a = (p >> 24) & 0xFF;
        uchar r = (uchar)(((p >> 16) & 0xFF));
        uchar g = (uchar)(((p >> 8)  & 0xFF));
        uchar b = (uchar)(( p        & 0xFF));
        dst[index] = (a << 24) | (r << 16) | (g << 8) | b;
        return;
    }

    float sumR = 0, sumG = 0, sumB = 0;
    int count = 0;

    // Dynamic loops based on calculated radius
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int nx = x + dx;
            int ny = y + dy;

            // Clamp coordinates to edges (Edge Padding)
            // This is often smoother than skipping pixels
            nx = clamp(nx, 0, width - 1);
            ny = clamp(ny, 0, height - 1);

            int neighborPixel = src[ny * width + nx];

            sumR += (float)((neighborPixel >> 16) & 0xFF);
            sumG += (float)((neighborPixel >> 8)  & 0xFF);
            sumB += (float)(neighborPixel & 0xFF);
            count++;
        }
    }

    // Average the totals, then apply the 0.5 dimming factor
    uchar r = (uchar)((sumR / count));
    uchar g = (uchar)((sumG / count));
    uchar b = (uchar)((sumB / count));
    
    // Extract original Alpha from center
    uchar a = (src[index] >> 24) & 0xFF;

    dst[index] = (a << 24) | (r << 16) | (g << 8) | b;
}