#pragma once

// ---------------------------------------------------------------
// Common math helpers
// ---------------------------------------------------------------

float max3(float x, float y, float z) { return max(x, max(y, z)); }

float length2(float3 vec) { return dot(vec, vec); }

