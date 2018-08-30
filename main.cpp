
#include <cstdint>
#include <float.h>
#include <chrono>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "cml/cml.h"
using namespace cml;

#include "pcgrandom.h"

int cNX = 200;
int cNY = 100;
int cMaxDepth = 50;

float cEpsilon = 0.01f;

PCGRandom rnd;

struct Statistics {
   uint32_t numberRays;
} statistics;

class Material;


struct Ray {
   Ray() {}
   Ray(const vector3f& a, const vector3f& b) {
      mOrigin = a;
      mDirection = b;
   }
   vector3f pointAtParameter(float t) const { return mOrigin + t*mDirection; }
   vector3f mOrigin, mDirection;
};

vector3f randomInUnitDisk() {
   vector3f point;
   do {
      point = 2.0f * vector3f(rnd.randomf(), rnd.randomf(), 0.0f) - vector3f(1.0f, 1.0f, 0.0f);
   } while(dot(point,point) >= 1.0f);
   return point;
}

struct Camera {
   Camera() {
      mLowerLeftCorner = vector3f(-2.0f, -1.0f, -1.0f);
      mHorizontal = vector3f(4.0f, 0.0f, 0.0f);
      mVertical = vector3f(0.0f, 2.0f, 0.0f);
      mOrigin = vector3f(0.0f, 0.0f, 0.0f);
   }
   Camera(vector3f lookFrom, vector3f lookAt, vector3f up, float vFov, float aspect, float aperture, float focusDistance) {
      mLensRadius = aperture / 2.0f;
      float theta = vFov * M_PI/180.0;
      float halfHeight = tan(theta/2.0);
      float halfWidth = aspect * halfHeight;
      mOrigin = lookFrom;
      w = (lookFrom - lookAt).normalize();
      u = (cross(up, w)).normalize();
      v = cross(w, u);
      mLowerLeftCorner = mOrigin - halfWidth*focusDistance*u - halfHeight*focusDistance*v - focusDistance*w;
      mHorizontal = 2.0f * halfWidth * focusDistance * u;
      mVertical = 2.0f * halfHeight * focusDistance * v;
   }
   Ray getRay(float s, float t) {
      vector3f random2D = mLensRadius * randomInUnitDisk();
      vector3f offset = u*random2D[0] + v*random2D[1];
      return Ray(mOrigin+offset, mLowerLeftCorner+s*mHorizontal+t*mVertical-mOrigin-offset);
   }
   vector3f mLowerLeftCorner;
   vector3f mHorizontal;
   vector3f mVertical;
   vector3f mOrigin;
   vector3f u,v,w;
   float mLensRadius;
};

// ================================================================================

inline float ffmin(float a, float b) { return a < b ? a : b; }
inline float ffmax(float a, float b) { return a > b ? a : b; }

class AABB {
public:
   AABB() {}
   AABB(vector3f min, vector3f max)
      : mMin(min)
      , mMax(max)
   {}

   bool hit(Ray &ray, float tmin, float tmax) {
#if 0
      for(int a=0; a<3; ++a) {
         float t0 = ffmin((mMin[a] - ray.mOrigin[a]) / ray.mDirection[a],
                          (mMax[a] - ray.mOrigin[a]) / ray.mDirection[a]);
         float t1 = ffmax((mMin[a] - ray.mOrigin[a]) / ray.mDirection[a],
                          (mMax[a] - ray.mOrigin[a]) / ray.mDirection[a]);
         tmin = ffmax(t0, tmin);
         tmax = ffmin(t1, tmax);
         if(tmax <= tmin)
            return false;
      }
      return true;
#else
      for(int a=0; a<3; ++a) {
         float invD = 1.0f / ray.mDirection[a];
         float t0 = (mMin[a] - ray.mOrigin[a]) * invD;
         float t1 = (mMax[a] - ray.mOrigin[a]) * invD;
         if(invD < 0.0f)
            std::swap(t0, t1);
         tmin = t0 > tmin ? t0 : tmin;
         tmax = t1 < tmax ? t1 : tmax;
         if(tmax <= tmin)
            return false;
      }
      return true;
#endif
   }

   vector3f mMin, mMax;
};

AABB surroundingBox(AABB &box0, AABB &box1) {
   vector3f small( fmin(box0.mMin[0], box1.mMin[0]),
                   fmin(box0.mMin[1], box1.mMin[1]),
                   fmin(box0.mMin[2], box1.mMin[2]));
   vector3f big(   fmax(box0.mMax[0], box1.mMax[0]),
                   fmax(box0.mMax[1], box1.mMax[1]),
                   fmax(box0.mMax[2], box1.mMax[2]));
   return AABB(small, big);
}

// ================================================================================

struct HitRecord {
   float time;
   float u,v;
   vector3f point;
   vector3f normal;
   Material *material;
};

class Hitable {
public:
   virtual bool hit(Ray &ray, float timeMin, float timeMax, HitRecord &record) = 0;
   virtual bool boundingBox(float t0, float t1, AABB &aabb) = 0;
};

struct HitableList : public Hitable {
   HitableList(Hitable **list, int size) {
      mList = list;
      mSize = size;
   }
   ~HitableList() {
      for(int i=0; i<mSize; ++i) {
         delete mList[i];
      }
   }
   bool hit(Ray &ray, float timeMin, float timeMax, HitRecord &record) {
      HitRecord tempRecord;
      bool hitAnything = false;
      double closestSoFar = timeMax;
      for(int i=0; i<mSize; ++i) {
         if(mList[i]->hit(ray, timeMin, closestSoFar, tempRecord)) {
            hitAnything = true;
            closestSoFar = tempRecord.time;
            record = tempRecord;
         }
      }
      return hitAnything;
   }

   bool boundingBox(float t0, float t1, AABB &aabb) {
      if(mSize < 1)
         return false;
      AABB tempBox;
      bool firstTrue = mList[0]->boundingBox(t0, t1, tempBox);
      if(!firstTrue)
         return false;
      else
         aabb = tempBox;
      for(int i=0; i<mSize; ++i) {
         if(mList[i]->boundingBox(t0, t1, tempBox)) {
            aabb = surroundingBox(aabb, tempBox);
         } else {
            return false;
         }
      }
      return true;
   }

private:
   Hitable **mList;
   int mSize;
};

int boxXCompare(const void *a, const void *b) {
   AABB boxLeft, boxRight;
   Hitable *ah = *(Hitable**)a;
   Hitable *bh = *(Hitable**)b;
   if(!ah->boundingBox(0.0f, 0.0f, boxLeft) || !bh->boundingBox(0.0f, 0.0f, boxRight)) {
      printf("no bounding box in x-compare!\n");
      exit(-1);
   }
   if(boxLeft.mMin[0] - boxRight.mMin[0] < 0.0)
      return -1;
   else
      return 1;
}
int boxYCompare(const void *a, const void *b) {
   AABB boxLeft, boxRight;
   Hitable *ah = *(Hitable**)a;
   Hitable *bh = *(Hitable**)b;
   if(!ah->boundingBox(0.0f, 0.0f, boxLeft) || !bh->boundingBox(0.0f, 0.0f, boxRight)) {
      printf("no bounding box in x-compare!\n");
      exit(-1);
   }
   if(boxLeft.mMin[1] - boxRight.mMin[1] < 0.0)
      return -1;
   else
      return 1;
}
int boxZCompare(const void *a, const void *b) {
   AABB boxLeft, boxRight;
   Hitable *ah = *(Hitable**)a;
   Hitable *bh = *(Hitable**)b;
   if(!ah->boundingBox(0.0f, 0.0f, boxLeft) || !bh->boundingBox(0.0f, 0.0f, boxRight)) {
      printf("no bounding box in x-compare!\n");
      exit(-1);
   }
   if(boxLeft.mMin[2] - boxRight.mMin[2] < 0.0)
      return -1;
   else
      return 1;
}

class BVHNode : public Hitable {
public:
   BVHNode() {}
   BVHNode(Hitable **list, int size, float time0, float time1) {
      if(size > 1) {
         int axis = int(3*rnd.randomf());
         if(axis == 0) {
            qsort(list, size, sizeof(Hitable*), boxXCompare);
         } else if(axis == 1) {
            qsort(list, size, sizeof(Hitable*), boxYCompare);
         } else {
            qsort(list, size, sizeof(Hitable*), boxZCompare);
         }         
      }
      if(size == 1) {
         mLeft = mRight = list[0];
      } else if(size == 2) {
         mLeft = list[0];
         mRight = list[1];
      } else {
         mLeft = new BVHNode(list, size/2, time0, time1);
         mRight = new BVHNode(list+size/2, size-size/2, time0, time1);
      }

      AABB boxLeft, boxRight;
      if(!mLeft->boundingBox(time0, time1, boxLeft) || !mRight->boundingBox(time0, time1, boxRight)) {
         printf("no bounding box in bvhnode constructor!\n");
         exit(-1);
      }
      mAABB = surroundingBox(boxLeft, boxRight);
   }
   ~BVHNode() {
      delete mLeft;
      delete mRight;
   }

   bool hit(Ray &ray, float timeMin, float timeMax, HitRecord &record) {
      if(mAABB.hit(ray, timeMin, timeMax)) {
         HitRecord leftRecord, rightRecord;
         bool hitLeft = mLeft->hit(ray, timeMin, timeMax, leftRecord);
         bool hitRight = mRight->hit(ray, timeMin, timeMax, rightRecord);
         if(hitLeft && hitRight) {
            if(leftRecord.time < rightRecord.time) {
               record = leftRecord;
            } else {
               record = rightRecord;
            }
            return true;
         } else if(hitLeft) {
            record = leftRecord;
            return true;
         } else if(hitRight) {
            record = rightRecord;
            return true;
         }
         return false;
      }
      return false;
   }

   bool boundingBox(float t0, float t1, AABB &aabb) {
      aabb = mAABB;
      return true;
   }

   Hitable *mLeft;
   Hitable *mRight;
   AABB mAABB;
};

// ================================================================================

class Texture {
public:
   virtual vector3f getTexel(float u, float v, vector3f &point) = 0;
};

class ConstantTexture : public Texture {
public:
   ConstantTexture() {}
   ConstantTexture(vector3f color)
      : mColor(color)
   {}
   vector3f getTexel(float u, float v, vector3f &point) {
      return mColor;
   }

   vector3f mColor;
};

class CheckerTexture : public Texture {
public:
   CheckerTexture() {}
   CheckerTexture(Texture *t0, Texture *t1)
      : mEven(t0)
      , mOdd(t1)
   {}
   ~CheckerTexture() {
      delete mEven;
      delete mOdd;
   }
   vector3f getTexel(float u, float v, vector3f &point) {
      float sines = sin(10.0f*point[0])*sin(10.0f*point[1])*sin(10.0f*point[2]);
      if(sines < 0.0f)
         return mOdd->getTexel(u, v, point);
      else
         return mEven->getTexel(u, v, point);
   }

   Texture *mEven, *mOdd;   
};

// ================================================================================

vector3f randomOnUnitSphere() {
   vector3f point;
   float lengthSquared;
   do {
      point = 2.0f * vector3f(rnd.randomf(), rnd.randomf(), rnd.randomf()) - vector3f(1.0f, 1.0f, 1.0f);
      lengthSquared = point.length_squared();
   } while( (lengthSquared >= 1.0f) || (lengthSquared == 0.0f) );
   return point;
}

vector3f reflect(vector3f &vec, vector3f &normal) {
   return vec - 2 * dot(vec, normal) * normal;
}

bool refract(vector3f &vec, vector3f &normal, float niOverT, vector3f &refracted) {
   vector3f uv = vec.normalize();
   float dt = dot(uv, normal);
   float discriminant = 1.0f - niOverT*niOverT*(1.0f-dt*dt);
   if(discriminant > 0) {
      refracted = niOverT * (uv - normal*dt) - normal*sqrt(discriminant);
      return true;
   } else {
      return false;
   }
}

float schlick(float cosine, float niOverT) {
   float r0 = (1.0f - niOverT) / (1.0f + niOverT);
   r0 = r0*r0;
   return r0 + (1.0f - r0)*pow((1.0f - cosine), 5.0f);
}

class Material {
public:
   virtual bool scatter(Ray &rayIn, HitRecord &record, vector3f &attenuation, Ray &scattered) = 0;
   virtual vector3f emitted(float u, float v, vector3f &point) {
      return vector3f(0.0f, 0.0f, 0.0f);
   }
};

class Lambertian : public Material {
public:
   Lambertian(Texture *albedo)
      : mAlbedo(albedo)
   {}
   ~Lambertian() {
      delete mAlbedo;
   }
   bool scatter(Ray &rayIn, HitRecord &record, vector3f &attenuation, Ray &scattered) {
      vector3f normal = dot(rayIn.mDirection, record.normal) < 0.0f ? record.normal : -record.normal;
      vector3f target = record.point + normal + randomOnUnitSphere();
      scattered = Ray(record.point + cEpsilon*normal, target-record.point);
      attenuation = mAlbedo->getTexel(0.0f, 0.0f, record.point);
      return true;
   }
   Texture *mAlbedo;
};

class Metal : public Material {
public:
   Metal(vector3f color, float fuzziness)
      : mAlbedo(color)
   {
      if(fuzziness < 1)             //don't make it too big to be inside the object
         mFuzziness = fuzziness;
      else
         mFuzziness = 1;
   }
   bool scatter(Ray &rayIn, HitRecord &record, vector3f &attenuation, Ray &scattered) {
      vector3f normal = dot(rayIn.mDirection, record.normal) < 0.0f ? record.normal : -record.normal;
      vector3f reflected = reflect(rayIn.mDirection.normalize(), normal);
      scattered = Ray(record.point + cEpsilon*normal, reflected + mFuzziness*randomOnUnitSphere());
      attenuation = mAlbedo;
      return (dot(scattered.mDirection, normal) > 0);
   }
   vector3f mAlbedo;
   float mFuzziness;
};

class Dielectric : public Material {
public:
   Dielectric(float refIndex)
      : mRefIndex(refIndex)
   {}
   bool scatter(Ray &rayIn, HitRecord &record, vector3f &attenuation, Ray &scattered) {
      vector3f outwardNormal;
      vector3f normal = dot(rayIn.mDirection, record.normal) < 0.0f ? record.normal : -record.normal;
      vector3f reflected = reflect(rayIn.mDirection, normal);
      float niOverT;
      attenuation = vector3f(1.0f, 1.0f, 1.0f);
      vector3f refracted;
      float reflectProb;
      float cosine;
      if(dot(rayIn.mDirection, record.normal) > 0) {
         outwardNormal = -record.normal;
         niOverT = mRefIndex;
         cosine = mRefIndex * dot(rayIn.mDirection, record.normal) / rayIn.mDirection.length();
      } else {
         outwardNormal = record.normal;
         niOverT = 1.0f / mRefIndex;
         cosine = -dot(rayIn.mDirection, record.normal) / rayIn.mDirection.length();
      }

      if(refract(rayIn.mDirection, outwardNormal, niOverT, refracted)) {
         reflectProb = schlick(cosine, niOverT);
      } else {
         reflectProb = 1.0f;
      }

      if(rnd.randomf() < reflectProb) {
         scattered = Ray(record.point + cEpsilon*normal, reflected);
      } else {
         scattered = Ray(record.point - cEpsilon*normal, refracted);
      }
      return true;
   }

   float mRefIndex;
};

class DiffuseLight : public Material {
public:
   DiffuseLight(Texture *tex)
      : mEmit(tex)
   {}
   ~DiffuseLight() {
      delete mEmit;
   }

   bool scatter(Ray &rayIn, HitRecord &record, vector3f &attenuation, Ray &scattered) {
      return false;
   }
   vector3f emitted(float u, float v, vector3f &point) {
      return mEmit->getTexel(u, v, point);
   }

   Texture *mEmit;
};

// ================================================================================

class Sphere : public Hitable {
public:
   Sphere(vector3f center, float radius, Material *material)
      : mCenter(center)
      , mRadius(radius)
      , mMaterial(material)
   {}
   ~Sphere() {
      delete mMaterial;
   }
   void getSphereUV(vector3f p, float &u, float &v) {
      float phi = atan2(p[2], p[0]);
      float theta = asin(p[1]);
      u = 1.0f - (phi+M_PI) / (2*M_PI);
      v = (theta+M_PI/2.0f) / M_PI;
   }
   bool hit(Ray &ray, float timeMin, float timeMax, HitRecord &record) {
      vector3f oc = ray.mOrigin - mCenter;
      float a = dot(ray.mDirection, ray.mDirection);
      float b = dot(oc, ray.mDirection);
      float c = dot(oc, oc) - mRadius*mRadius;
      float discriminant = b*b - a*c;
      if(discriminant > 0) {
         float temp = (-b - sqrt(b*b-a*c))/a;
         if(temp < timeMax && temp > timeMin) {
            record.time = temp;
            record.point = ray.pointAtParameter(temp);
            record.normal = (record.point - mCenter) / mRadius;
            getSphereUV((record.point - mCenter) / mRadius, record.u, record.v);
            record.material = mMaterial;
            return true;
         }
         temp = (-b + sqrt(b*b-a*c))/a;
         if(temp < timeMax && temp > timeMin) {
            record.time = temp;
            record.point = ray.pointAtParameter(temp);
            record.normal = (record.point - mCenter) / mRadius;
            getSphereUV((record.point - mCenter) / mRadius, record.u, record.v);
            record.material = mMaterial;
            return true;
         }
      }
      return false;
   }

   bool boundingBox(float t0, float t1, AABB &aabb) {
      aabb = AABB(mCenter - vector3f(mRadius, mRadius, mRadius), mCenter + vector3f(mRadius, mRadius, mRadius));
      return true;
   }

private:
   vector3f mCenter;
   float mRadius;
   Material *mMaterial;
};

class XYRect : public Hitable {
public:
   XYRect() {}
   XYRect(float x0, float x1, float y0, float y1, float k, Material *material, bool flipNormal = false)
      : mX0(x0)
      , mX1(x1)
      , mY0(y0)
      , mY1(y1)
      , mK(k)
      , mMaterial(material)
      , mFlipNormal(flipNormal)
   {}
   ~XYRect() {
      delete mMaterial;
   }
   bool hit(Ray &ray, float timeMin, float timeMax, HitRecord &record) {
      float t = (mK - ray.mOrigin[2]) / ray.mDirection[2];
      if(t < timeMin || t > timeMax)
         return false;
      float x = ray.mOrigin[0] + t * ray.mDirection[0];
      float y = ray.mOrigin[1] + t * ray.mDirection[1];
      if(x < mX0 || x > mX1 || y < mY0 || y > mY1)
         return false;
      record.u = (x - mX0) / (mX1-mX0);
      record.v = (y - mY0) / (mY1-mY0);
      record.time = t;
      record.material = mMaterial;
      record.point = ray.pointAtParameter(t);
      if(mFlipNormal)
         record.normal = vector3f(0.0f, 0.0f, -1.0f);
      else
         record.normal = vector3f(0.0f, 0.0f, 1.0f);

      return true;
   }
   bool boundingBox(float t0, float t1, AABB &aabb) {
      aabb = AABB(vector3f(mX0, mY0, mK-0.0001f), vector3f(mX1, mY1, mK+0.0001f));
      return true;
   }

   Material *mMaterial;
   float mX0, mX1, mY0, mY1, mK;
   bool mFlipNormal;
};

// ================================================================================

// Plastic Low Discrepancy Sequence
// pseudo-random-sequence: http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/

vector2f plastic(double index) {       //returns vector in [0..1[
   const double p1 = 0.7548776662466927; // inverse of plastic number
   const double p2 = 0.5698402909980532;

   double x = (p1 * index);
   x = x - floor(x);             //get only fractional part
   double y = (p2 * index);
   y = y - floor(y);
   return vector2f(x,y);
}

// https://blog.demofox.org/2017/10/20/generating-blue-noise-sample-points-with-mitchells-best-candidate-algorithm/

// ================================================================================

vector3f computeColor(Ray& ray, Hitable *world, int depth) {
   statistics.numberRays += 1;

   HitRecord record;
   if(world->hit(ray, 0.001f, MAXFLOAT, record)) {
      Ray scattered;
      vector3f attenuation;
      vector3f emitted = record.material->emitted(record.u, record.v, record.point);

      if(depth < cMaxDepth && record.material->scatter(ray, record, attenuation, scattered)) {
         vector3f color = computeColor(scattered, world, depth+1);
         return emitted + vector3f(attenuation[0]*color[0], attenuation[1]*color[1], attenuation[2]*color[2]);
      } else {
         return emitted;
      }
   } else {
      return vector3f(0.0f, 0.0f, 0.0f);
   }
}

int main() {
   Hitable *list[6];
   list[0] = new Sphere(vector3f(0.0f, 0.0f, -1.0f), 0.5f, new Lambertian(new ConstantTexture(vector3f(0.1f, 0.2f, 0.5f))));
   list[1] = new Sphere(vector3f(0.0f, -100.5f, -1.0f), 100.0f, new Lambertian(
      new CheckerTexture(new ConstantTexture(vector3f(0.2f, 0.3f, 0.1f)), new ConstantTexture(vector3f(0.9f,0.9f,0.9f)))));
   list[2] = new Sphere(vector3f(1.0f, 0.0f, -1.0f), 0.5f, new Metal(vector3f(0.8f, 0.6f, 0.2f), 0.3f));
   list[3] = new Sphere(vector3f(-1.0f, 0.0f, -1.0f), 0.5f, new Dielectric(1.5f));
   list[4] = new Sphere(vector3f(-1.0f, 0.0f, -1.0f), -0.45f, new Dielectric(1.5f));

   list[5] = new XYRect(3,5,1,3,-2,new DiffuseLight(new ConstantTexture(vector3f(4,4,4))));
   Hitable *world = new HitableList(list, sizeof(list)/sizeof(list[0]));

   uint32_t *framebuffer = new uint32_t[cNX*cNY];

   Camera camera;

   vector3f lookFrom(3.0f, 3.0f, 2.0f);
   vector3f lookAt(0.0f, 0.0f, -1.0f);
   float distanceToFocus = (lookFrom - lookAt).length();
   float aperture = 2.0f;
//   Camera camera(lookFrom, lookAt, vector3f(0,1,0), 20, float(cNX)/float(cNY), aperture, distanceToFocus);

   statistics.numberRays = 0;

//   int testNumberSamples[] = {1,10,20,30,50,100};
   int testNumberSamples[] = {50};
   for(int currentSample = 0; currentSample < sizeof(testNumberSamples)/sizeof(int); ++currentSample) {
      int numberSamples = testNumberSamples[currentSample];

      printf("rendering with %d samples...\n", numberSamples);

      std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
      for(int y=0; y<cNY; ++y) {
         for(int x=0; x<cNX; ++x) {
            float u = float(x) / float(cNX);
            float v = float(cNY-y) / float(cNY);

            vector3f color(0.0f, 0.0f, 0.0f);
            for(int sample=0; sample<numberSamples; ++sample) {
#if 0
               float u = (float(x)+rnd.randomf()) / float(cNX);
               float v = (float(cNY-y)+rnd.randomf()) / float(cNY);
#else
               static double plasticIndex = 1;
               vector2f p(plastic(plasticIndex));
               ++plasticIndex;
               float u = (float(x) + p[0]) / float(cNX);
               float v = (float(cNY-y) + p[1]) / float(cNY);
#endif
               Ray ray = camera.getRay(u, v);
               color += computeColor(ray, world, 0);
            }
            color /= float(numberSamples);

            color = vector3f(sqrt(color[0]), sqrt(color[1]), sqrt(color[2]));       //gamma correct

            int ir = int(255.99 * color[0]);
            int ig = int(255.99 * color[1]);
            int ib = int(255.99 * color[2]);

            framebuffer[y*cNX + x] = (0xff000000) | (ib<<16) | (ig<<8) | ir;
         }
      }

      std::chrono::high_resolution_clock::time_point raytraceTime = std::chrono::high_resolution_clock::now();

      printf("raytracing with %d samples took %lu ms\n", numberSamples, std::chrono::duration_cast<std::chrono::milliseconds>(raytraceTime - startTime).count());

      char filename[256];
      sprintf(filename, "raytrace_plastic_%03d.png", numberSamples);
      stbi_write_png(filename, cNX, cNY, 4, framebuffer, cNX*sizeof(uint32_t));
   }


   delete[] framebuffer;
   delete world;

   printf("-----------------\n");
   printf("number rays: %d\n", statistics.numberRays);
}
