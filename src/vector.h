#ifndef VECTOR_H
#define VECTOR_H

#include "common.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define CREATE_VEC4f32(x, y, z, w) ((struct Vec4f32){x, y, z, w})
#define CREATE_VEC3f32(x, y, z)    ((struct Vec3f32){x, y, z})
#define CREATE_VEC2f32(x, y)       ((struct Vec2f32){x, y})
#define CREATE_INTERVAL(x, y)      CREATE_VEC2f32(x, y)
#define NEG_VEC3f32(vec)           (CREATE_VEC3f32(-vec.x, -vec.y, -vec.z))

struct Vec2f32
{
  union
  {
    f32 pos[2];
    struct
    {
      f32 x;
      f32 y;
    };
    struct
    {
      f32 min;
      f32 max;
    };
  };
};

struct Vec3f32
{
  union
  {
    f32 pos[3];
    struct
    {
      f32 x;
      f32 y;
      f32 z;
    };
    struct
    {
      f32 r;
      f32 g;
      f32 b;
    };
  };
};

struct Vec4f32
{
  union
  {
    f32 pos[4];
    struct
    {
      f32 x;
      f32 y;
      f32 z;
      f32 w;
    };
    struct
    {
      f32 r;
      f32 g;
      f32 b;
      f32 a;
    };
  };
};

struct Matrix4x4
{
  union
  {
    f32 m[4][4];
    struct
    {
      f32 i[4];
      f32 j[4];
      f32 k[4];
      f32 l[4];
    };
  };
};
struct Matrix3x3
{
  union
  {
    f32 m[3][3];
    struct
    {
      f32 i[3];
      f32 j[3];
      f32 k[3];
    };
  };
};

typedef struct Vec3f32 Point;
typedef struct Vec3f32 Color;
typedef struct Vec2f32 Interval;

#define EMPTY_INTERVAL           ((Interval)(INFINITY, -INFINITY))
#define UNIVERSE_INTERVAL        ((Interval)(-INFINITY, INFINITY))

#define INTERVAL_CONTAINS(i, x)  ((i.min <= x && x <= i.max))
#define INTERVAL_SURROUNDS(i, x) ((i.min < x && x < i.max))

bool    nearZeroVec3f32(Vec3f32 v);
void    reflectVec3f32(Vec3f32* res, Vec3f32* v, Vec3f32* n);
void    refractVec3f32(Vec3f32* res, Vec3f32* uv, Vec3f32* n, f32 etaiOverEtat);
f32     reflectanceVec3f32(f32 cosine, f32 refIdx);
Vec3f32 randomVec3f32InUnitDisk();

Vec3f32 randomUnitVector();
Vec3f32 randomVec3f32OnHemisphere(Vec3f32 normal);
Vec3f32 randomVec3f32InUnitSphere();
Vec3f32 randomVec3f32();
Vec3f32 randomVec3f32WithinRange(f32 min, f32 max);
void    scaleVec3f32(Vec3f32* res, Vec3f32* v, f32 t);
void    addVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1);
void    subVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1);
void    mulVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1);
void    divideVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1);
f32     lengthVec3f32(Vec3f32* v);
f32     lengthSquaredVec3f32(Vec3f32* v);
f32     dotVec3f32(Vec3f32* v0, Vec3f32* v1);
void    crossVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1);
void    normalizeVec3f32(Vec3f32* res, Vec3f32* v);
void    debugVec3f32(struct Vec3f32 v);
void    writeColor(FILE* filePtr, Color v, i32 samples);
f32 clamp(Interval i, f32 x);

#endif
