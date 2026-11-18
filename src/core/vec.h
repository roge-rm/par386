/* 2D and 3D vectors in 16.16. */
#ifndef VEC_H
#define VEC_H

#include "trig.h"

typedef struct { fixed x, y; } vec2;
typedef struct { fixed x, y, z; } vec3;

vec2 SEC_CORE vec2_make(fixed x, fixed y);
vec2 SEC_CORE vec2_add(vec2 a, vec2 b);
vec2 SEC_CORE vec2_sub(vec2 a, vec2 b);
/* v * s with fix_mulf, so each |component * s| < 32768. */
vec2 SEC_CORE vec2_scale(vec2 v, fixed s);
/* Dot product, saturating. */
fixed SEC_CORE vec2_dot(vec2 a, vec2 b);
/* Length for any vector; a few low bits are lost past 64.0 per component. */
fixed SEC_CORE vec2_len(vec2 v);
/* Turns v by a toward +y. */
vec2 SEC_CORE vec2_rotate(vec2 v, angle a);
/* Unit vector at angle a. */
vec2 SEC_CORE vec2_dir(angle a);
angle SEC_CORE vec2_angle(vec2 v);

vec3 SEC_CORE vec3_make(fixed x, fixed y, fixed z);
vec3 SEC_CORE vec3_add(vec3 a, vec3 b);
vec3 SEC_CORE vec3_sub(vec3 a, vec3 b);
/* v * s with fix_mulf, so each |component * s| < 32768. */
vec3 SEC_CORE vec3_scale(vec3 v, fixed s);
/* Dot product, saturating, and cross product, whose parts must fit. */
fixed SEC_CORE vec3_dot(vec3 a, vec3 b);
vec3 SEC_CORE vec3_cross(vec3 a, vec3 b);
fixed SEC_CORE vec3_len(vec3 v);
/* v scaled to length 1, or zero for a zero vector. */
vec3 SEC_CORE vec3_norm(vec3 v);

#endif
