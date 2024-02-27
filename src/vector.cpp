#include "vector.h"
#include "common.h"
#include <math.h>
#include <stdio.h>

Vec3f32 randomVec3f32()
{
  Vec3f32 res;
  res.x = RANDOM_DOUBLE;
  res.y = RANDOM_DOUBLE;
  res.z = RANDOM_DOUBLE;
  return res;
}
Vec3f32 randomVec3f32InRange(f32 min, f32 max)
{
  Vec3f32 res;
  res.x = RANDOM_DOUBLE_IN_RANGE(min, max);
  res.y = RANDOM_DOUBLE_IN_RANGE(min, max);
  res.z = RANDOM_DOUBLE_IN_RANGE(min, max);
  return res;
}

Vec3f32 randomVec3f32InUnitSphere()
{
  while (true)
  {
    Vec3f32 p = randomVec3f32InRange(-1, 1);
    if (lengthSquaredVec3f32(&p) < 1)
    {
      return p;
    }
  }
}
Vec3f32 randomUnitVector()
{
  Vec3f32 res;
  Vec3f32 random = randomVec3f32InUnitSphere();
  normalizeVec3f32(&res, &random);
  return res;
}

Vec3f32 randomVec3f32OnHemisphere(Vec3f32 normal)
{
  Vec3f32 unitSphereVec = randomUnitVector();
  if (dotVec3f32(&unitSphereVec, &normal) > 0.0)
  {
    return unitSphereVec;
  }
  else
  {
    return CREATE_VEC3f32(-unitSphereVec.x, -unitSphereVec.y, -unitSphereVec.z);
  }
}
Vec3f32 randomVec3f32InUnitDisk()
{
  while (true)
  {
    Vec3f32 p;
    p.x = RANDOM_DOUBLE_IN_RANGE(-1, 1);
    p.y = RANDOM_DOUBLE_IN_RANGE(-1, 1);
    p.z = 0;
    if (lengthSquaredVec3f32(&p) < 1)
    {
      return p;
    }
  }
}
f32 reflectanceVec3f32(f32 cosine, f32 refIdx)
{
  f32 r0 = (1 - refIdx) / (1 + refIdx);
  r0     = r0 * r0;
  return r0 + (1 - r0) * pow(1 - cosine, 5);
}

f32 clamp(Interval i, f32 x)
{
  if (x < i.min)
  {
    return i.min;
  }
  if (x > i.max)
  {
    return i.max;
  }
  return x;
}

void scaleVec3f32(Vec3f32* res, Vec3f32* v, f32 t)
{
  res->x = v->x * t;
  res->y = v->y * t;
  res->z = v->z * t;
}
void addVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1)
{
  res->x = v0->x + v1->x;
  res->y = v0->y + v1->y;
  res->z = v0->z + v1->z;
}
void subVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1)
{
  res->x = v0->x - v1->x;
  res->y = v0->y - v1->y;
  res->z = v0->z - v1->z;
}
void mulVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1)
{
  res->x = v0->x * v1->x;
  res->y = v0->y * v1->y;
  res->z = v0->z * v1->z;
}
void divideVec3f32(Vec3f32* res, Vec3f32* v0, Vec3f32* v1)
{
  res->x = v0->x / v1->x;
  res->y = v0->y / v1->y;
  res->z = v0->z / v1->z;
}
f32 lengthVec3f32(Vec3f32* v)
{
  return sqrtf(lengthSquaredVec3f32(v));
}
f32 lengthSquaredVec3f32(Vec3f32* v)
{
  return v->x * v->x + v->y * v->y + v->z * v->z;
}
f32 dotVec3f32(Vec3f32* v0, Vec3f32* v1)
{
  return v0->x * v1->x + v0->y * v1->y + v0->z * v1->z;
}
void crossVec3f32(Vec3f32* __restrict__ res, Vec3f32* v0, Vec3f32* v1)
{
  res->x = v0->y * v1->z - v0->z * v1->y;
  res->y = v0->z * v1->x - v0->x * v1->z;
  res->z = v0->x * v1->y - v0->y * v1->x;
}
void normalizeVec3f32(Vec3f32* res, Vec3f32* v)
{
  scaleVec3f32(res, v, 1 / lengthVec3f32(v));
}

void refractVec3f32(Vec3f32* res, Vec3f32* uv, Vec3f32* n, f32 etaiOverEtat)
{
  Vec3f32 negUV    = NEG_VEC3f32((*uv));
  f32     cosTheta = fmin(dotVec3f32(&negUV, n), 1.0f);

  Vec3f32 nScaled;
  scaleVec3f32(&nScaled, n, cosTheta);

  Vec3f32 p;
  addVec3f32(&p, uv, &nScaled);

  Vec3f32 rOutPerp;
  scaleVec3f32(&rOutPerp, &p, etaiOverEtat);

  Vec3f32 rOutParallel;
  scaleVec3f32(&rOutParallel, n, -sqrt(fabs(1.0 - lengthSquaredVec3f32(&rOutPerp))));

  addVec3f32(res, &rOutPerp, &rOutParallel);
}

void reflectVec3f32(Vec3f32* res, Vec3f32* v, Vec3f32* n)
{

  Vec3f32 dot;
  scaleVec3f32(&dot, n, 2 * dotVec3f32(v, n));

  subVec3f32(res, v, &dot);
}

bool nearZeroVec3f32(Vec3f32 v)
{
  f32 s = 1e-8;
  return fabs(v.x) < s && fabs(v.y) < s && fabs(v.z) < s;
}


void debugVec3f32(Vec3f32 v)
{
  printf("%lf %lf %lf\n", v.x, v.y, v.z);
}
