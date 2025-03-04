#include "parser.h"
#include "ppm.h"
#include <chrono>
#include <math.h>
#include <pthread.h>
#include <thread>

using namespace parser;

typedef unsigned char RGB[3];

#define clip(a) MIN(round(a), 255)

struct Ray {
    Vec3f start, dir;
};

struct Hit {
    bool hitOccur;
    int materialID;
    int hitType;
    int hitID;
    int faceID; //needed only for mesh to store faceID with meshID
    bool replace_all_drawn; //needed for replace all
    Vec3f intersectPoint, normal;
    double t;
};

bool ray_box_intersect(Ray& ray, Box& box)
{
    double minx, miny, minz;
    double maxx, maxy, maxz;
    minx = MIN((box.min.x - ray.start.x) / ray.dir.x, (box.max.x - ray.start.x) / ray.dir.x);
    miny = MIN((box.min.y - ray.start.y) / ray.dir.y, (box.max.y - ray.start.y) / ray.dir.y);
    minz = MIN((box.min.z - ray.start.z) / ray.dir.z, (box.max.z - ray.start.z) / ray.dir.z);
    maxx = MAX((box.min.x - ray.start.x) / ray.dir.x, (box.max.x - ray.start.x) / ray.dir.x);
    maxy = MAX((box.min.y - ray.start.y) / ray.dir.y, (box.max.y - ray.start.y) / ray.dir.y);
    maxz = MAX((box.min.z - ray.start.z) / ray.dir.z, (box.max.z - ray.start.z) / ray.dir.z);
    double tmin, tmax;
    tmin = MAX(MAX(minx, miny), minz);
    tmax = MIN(MIN(maxx, maxy), maxz);
    if (tmin <= tmax && tmax >= 0)
        return true;
    return false;
}

double ray_triangle_intersect(Ray& ray, Face& triangle, Scene& scene)
{
#define e (ray.start)
#define d (ray.dir)
#define a (triangle.v0.coordinates)
#define b (triangle.v1.coordinates)
#define c (triangle.v2.coordinates)
    double det, t, beta, gamma;
    det = ((-d.x) * ((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y)) - (-d.y) * ((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) + (-d.z) * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)));
    if (det == 0)
        return -1;
    t = ((e.x - a.x) * ((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y)) - (e.y - a.y) * ((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) + (e.z - a.z) * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x))) / det;
    beta = ((-d.x) * ((e.y - a.y) * (c.z - a.z) - (e.z - a.z) * (c.y - a.y)) - (-d.y) * ((e.x - a.x) * (c.z - a.z) - (e.z - a.z) * (c.x - a.x)) + (-d.z) * ((e.x - a.x) * (c.y - a.y) - (e.y - a.y) * (c.x - a.x))) / det;
    gamma = ((-d.x) * ((b.y - a.y) * (e.z - a.z) - (b.z - a.z) * (e.y - a.y)) - (-d.y) * ((b.x - a.x) * (e.z - a.z) - (b.z - a.z) * (e.x - a.x)) + (-d.z) * ((b.x - a.x) * (e.y - a.y) - (b.y - a.y) * (e.x - a.x))) / det;
#undef e
#undef d
#undef a
#undef b
#undef c

    if (beta >= 0 && gamma >= 0 && beta + gamma <= 1 && t > 0) {
        return t;
    }

    return -1;
}

double ray_sphere_intersect(Ray& ray, Sphere& sphere, Scene& scene)
{

#define r (sphere.radius)
#define c (sphere.center_vertex)
#define e (ray.start)
#define d (ray.dir)

    //At^2+Bt+C=0
    double A = d.x * d.x + d.y * d.y + d.z * d.z;
    double B = 2 * ((e.x - c.x) * d.x + (e.y - c.y) * d.y + (e.z - c.z) * d.z);
    double C = (e.x - c.x) * (e.x - c.x) + (e.y - c.y) * (e.y - c.y) + (e.z - c.z) * (e.z - c.z) - r * r;

    double delta = B * B - 4 * A * C;

    if (delta >= 0) {
        double mindis = MIN((-B - sqrt(delta)) / (2 * A), (-B + sqrt(delta)) / (2 * A));
        double maxdis = MAX((-B - sqrt(delta)) / (2 * A), (-B + sqrt(delta)) / (2 * A));
        return (mindis > 0 ? mindis : maxdis);
    }

#undef e
#undef d
#undef r
#undef c

    return -1;
}

Ray Generate(Camera& camera, int i, int j)
{
    Vec3f current;
    Ray ret;

    current = camera.topleft + camera.halfpixelD * (2 * i + 1) + camera.halfpixelR * (2 * j + 1);

    ret.dir = current - camera.position;
    ret.start = camera.position;
    return ret;
}

Hit* ClosestHitInBox(Ray& ray, Box* box, Mesh& mesh, Scene& scene)
{
    if (box == NULL)
        return NULL;
    Hit* ret = new Hit;
    double t, tmin = __DBL_MAX__;
    ret->hitOccur = false;
    for (int faceID = box->leftindex; faceID < box->rigthindex; faceID++) {
        Face& triangle = mesh.faces[faceID];
        t = ray_triangle_intersect(ray, triangle, scene);
        if (t >= 0 && t < tmin) {
            tmin = t;
            ret->intersectPoint = ray.start + ray.dir * t;
            ret->normal = triangle.normal;
            ret->materialID = mesh.material_id;
            ret->hitOccur = true;
            ret->t = t;
            ret->hitType = MESHHIT;
            ret->faceID = faceID;
            ret->replace_all_drawn = false;
        }
    }
    if (ret->hitOccur)
        return ret;
    delete ret;
    return NULL;
}

Hit* meshBVH(Ray& ray, Box* box, Mesh& mesh, Scene& scene)
{
    Hit *retl, *retr;
    double tmin = __DBL_MAX__;
    if (!box)
        return NULL;
    if (!ray_box_intersect(ray, *box))
        return NULL;

    if (box->left == NULL && box->right == NULL) {
        return ClosestHitInBox(ray, box, mesh, scene);
    }
    retl = meshBVH(ray, box->left, mesh, scene);
    retr = meshBVH(ray, box->right, mesh, scene);
    if (!retl)
        return retr;
    if (!retr)
        return retl;
    if (retl->t < retr->t) {
        delete retr;
        return retl;
    }
    delete retl;
    return retr;
}

Hit ClosestHit(Ray& ray, Scene& scene)
{
    Hit ret;
    double t, tmin = __DBL_MAX__;
    ret.hitOccur = false;
    // Intersection tests
    //  Mesh intersect
    for (int meshID = 0; meshID < scene.meshes.size(); meshID++) {
        Mesh& mesh = scene.meshes[meshID];
        Hit* meshHit = meshBVH(ray, mesh.head, mesh, scene);
        if (meshHit) {
            if (meshHit->t < tmin) {
                ret = *meshHit;
                ret.hitID = meshID;
                tmin = meshHit->t;
                ret.replace_all_drawn = false;
            }
            delete meshHit;
        }
    }
    //  Triangle intersect
    for (int triangleID = 0; triangleID < scene.triangles.size(); triangleID++) {
        Face& triangle = scene.triangles[triangleID].indices;
        t = ray_triangle_intersect(ray, triangle, scene);
        if (t >= 0 && t < tmin) {
            tmin = t;
            ret.intersectPoint = ray.start + ray.dir * t;
            ret.normal = triangle.normal;
            ret.materialID = scene.triangles[triangleID].material_id;
            ret.hitOccur = true;
            ret.t = t;
            ret.hitType = TRIANGLEHIT;
            ret.hitID = triangleID;
            ret.replace_all_drawn = false;
        }
    }
    //  Sphere intersect
    for (int sphereID = 0; sphereID < scene.spheres.size(); sphereID++) {
        Sphere& sphere = scene.spheres[sphereID];
        t = ray_sphere_intersect(ray, sphere, scene);
        if (t >= 0 && t < tmin) {
            tmin = t;
            ret.intersectPoint = ray.start + ray.dir * t;
            ret.normal = (ret.intersectPoint - sphere.center_vertex).normalize();
            ret.materialID = sphere.material_id;
            ret.hitOccur = true;
            ret.t = t;
            ret.hitType = SPHEREHIT;
            ret.hitID = sphereID;
        }
    }
    return ret;
}

double* Specular(Ray& ray, Hit& hit, PointLight& light, Scene& scene)
{
    Vec3f toSource, halfWay, toLight;
    toSource = (ray.start - hit.intersectPoint).normalize();
    toLight = (light.position - hit.intersectPoint);
    double dSquare = toLight.dot(toLight);
    toLight = toLight.normalize();
    halfWay = (toSource + toLight).normalize();
    double* ret = new double[3];
    double temp = halfWay.dot(hit.normal);
    ret[0] = scene.materials[hit.materialID - 1].specular.x * pow(temp, scene.materials[hit.materialID - 1].phong_exponent) * light.intensity.x / dSquare;
    ret[1] = scene.materials[hit.materialID - 1].specular.y * pow(temp, scene.materials[hit.materialID - 1].phong_exponent) * light.intensity.y / dSquare;
    ret[2] = scene.materials[hit.materialID - 1].specular.z * pow(temp, scene.materials[hit.materialID - 1].phong_exponent) * light.intensity.z / dSquare;
    return ret;
}

Vec2f* uvForTriangle(Ray& ray, Face& triangle)
{
#define e (ray.start)
#define d (ray.dir)
#define a (triangle.v0.coordinates)
#define b (triangle.v1.coordinates)
#define c (triangle.v2.coordinates)
    double det, t, beta, gamma;
    det = ((-d.x) * ((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y)) - (-d.y) * ((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) + (-d.z) * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)));
    t = ((e.x - a.x) * ((b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y)) - (e.y - a.y) * ((b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x)) + (e.z - a.z) * ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x))) / det;
    beta = ((-d.x) * ((e.y - a.y) * (c.z - a.z) - (e.z - a.z) * (c.y - a.y)) - (-d.y) * ((e.x - a.x) * (c.z - a.z) - (e.z - a.z) * (c.x - a.x)) + (-d.z) * ((e.x - a.x) * (c.y - a.y) - (e.y - a.y) * (c.x - a.x))) / det;
    gamma = ((-d.x) * ((b.y - a.y) * (e.z - a.z) - (b.z - a.z) * (e.y - a.y)) - (-d.y) * ((b.x - a.x) * (e.z - a.z) - (b.z - a.z) * (e.x - a.x)) + (-d.z) * ((b.x - a.x) * (e.y - a.y) - (b.y - a.y) * (e.x - a.x))) / det;
#undef e
#undef d
#undef a
#undef b
#undef c
    Vec2f* ret = new Vec2f();
    ret->x = (1 - beta - gamma) * triangle.v0.u + beta * triangle.v1.u + gamma * triangle.v2.u;
    ret->y = (1 - beta - gamma) * triangle.v0.v + beta * triangle.v1.v + gamma * triangle.v2.v;
    return ret;
}

Vec2f* uvForSphere(Hit& hit, Sphere& sphere)
{
    // need matrix multiplication for uvw coordinate system transformation
    matrix M = translate(-sphere.center_vertex.x, -sphere.center_vertex.y, -sphere.center_vertex.z);
    M = CameraT(sphere.u, sphere.v, sphere.w) * M;
    Vec3f hitCoor = hit.intersectPoint * M; //coordinates of hit after coordinate system transformation
    double theta = acos(hitCoor.y / sphere.radius);
    double phi = atan2(hitCoor.z, hitCoor.x);
    Vec2f* ret = new Vec2f();
    ret->x = (M_PI - phi) / (2 * M_PI);
    ret->y = theta / M_PI;
    return ret;
}

double* ColorTexture(Vec2f& UV, Texture& texture)
{
    if (texture.repeatmode == REPEAT) {
        UV.x = UV.x - (int)UV.x;
        UV.y = UV.y - (int)UV.y;
    } else if (texture.repeatmode == CLAMP) {
        UV.x = MIN(UV.x, 1);
        UV.y = MIN(UV.y, 1);
    } else {
        throw - 1.0;
    }
    double* ret = new double[3];
    int pixelx, pixely;
    pixelx = ROUND(UV.x * texture.width);
    pixely = ROUND(UV.y * texture.height);

#define PIXEL(x, y) (3 * ((y)*texture.width + (x)))
    if (texture.interpolation == NEAREST) {
        ret[0] = texture.image[PIXEL(pixelx, pixely)];
        ret[1] = texture.image[PIXEL(pixelx, pixely) + 1];
        ret[2] = texture.image[PIXEL(pixelx, pixely) + 2];
    } else if (texture.interpolation == BILINEAR) {
        double dx = UV.x * texture.width - pixelx;
        double dy = UV.y * texture.height - pixely;
        ret[0] = dx * dy * texture.image[PIXEL(pixelx + 1, pixely + 1)]
            + (1 - dx) * dy * texture.image[PIXEL(pixelx, pixely + 1)]
            + dx * (1 - dy) * texture.image[PIXEL(pixelx + 1, pixely)]
            + (1 - dx) * (1 - dy) * texture.image[PIXEL(pixelx, pixely)];
        ret[1] = dx * dy * texture.image[PIXEL(pixelx + 1, pixely + 1) + 1]
            + (1 - dx) * dy * texture.image[PIXEL(pixelx, pixely + 1) + 1]
            + dx * (1 - dy) * texture.image[PIXEL(pixelx + 1, pixely) + 1]
            + (1 - dx) * (1 - dy) * texture.image[PIXEL(pixelx, pixely) + 1];
        ret[2] = dx * dy * texture.image[PIXEL(pixelx + 1, pixely + 1) + 2]
            + (1 - dx) * dy * texture.image[PIXEL(pixelx, pixely + 1) + 2]
            + dx * (1 - dy) * texture.image[PIXEL(pixelx + 1, pixely) + 2]
            + (1 - dx) * (1 - dy) * texture.image[PIXEL(pixelx, pixely) + 2];
    } else {
        throw true;
    }
    return ret;
#undef PIXEL
}

double* Diffuse(Ray& ray, Hit& hit, PointLight* light, Scene& scene)
{
    Vec3f toSource, toLight;
    Texture* texture = NULL;
    double* ret = NULL;
    Vec2f* UV;
    double dSquare, temp;
    if (hit.hitType == MESHHIT) {
        if (scene.meshes[hit.hitID].texture_id != -1) {
            texture = &scene.textures[scene.meshes[hit.hitID].texture_id - 1];
            UV = uvForTriangle(ray, scene.meshes[hit.hitID].faces[hit.faceID]);
        }
    } else if (hit.hitType == TRIANGLEHIT) {
        if (scene.triangles[hit.hitID].texture_id != -1) {
            texture = &scene.textures[scene.triangles[hit.hitID].texture_id - 1];
            UV = uvForTriangle(ray, scene.triangles[hit.hitID].indices);
        }
    } else if (hit.hitType == SPHEREHIT) {
        if (scene.spheres[hit.hitID].texture_id != -1) {
            texture = &scene.textures[scene.spheres[hit.hitID].texture_id - 1];
            UV = uvForSphere(hit, scene.spheres[hit.hitID]);
        }
    } else {
        throw 'a';
    }
    if (texture) {
        ret = ColorTexture(*UV, *texture);
        if (light == NULL) {
            if (texture->colormode == REPLACE_ALL) {
                return ret;
            } else {
                ret[0] = 0;
                ret[1] = 0;
                ret[2] = 0;
                return ret;
            }
        }
        ret[0] = ret[0] / 255;
        ret[1] = ret[1] / 255;
        ret[2] = ret[2] / 255;
        if (texture->colormode == REPLACE_ALL) {
            ret[0] = 0;
            ret[1] = 0;
            ret[2] = 0;
            return ret;
        }
        if (texture->colormode == REPLACE_KD) {
            // Do nothing we already use ret as diffuse coef
        } else if (texture->colormode == BLEND_KD) {
            ret[0] = (ret[0] + scene.materials[hit.materialID - 1].diffuse.x) / 2;
            ret[1] = (ret[1] + scene.materials[hit.materialID - 1].diffuse.y) / 2;
            ret[2] = (ret[2] + scene.materials[hit.materialID - 1].diffuse.z) / 2;
        } else {
            throw - 1;
        }
    }
    if (light == NULL) {
        if (!ret)
            ret = new double[3];
        ret[0] = 0;
        ret[1] = 0;
        ret[2] = 0;
        return ret;
    }
    if (!ret) {
        ret = new double[3];
        ret[0] = scene.materials[hit.materialID - 1].diffuse.x;
        ret[1] = scene.materials[hit.materialID - 1].diffuse.y;
        ret[2] = scene.materials[hit.materialID - 1].diffuse.z;
    }

    toLight = (light->position - hit.intersectPoint);
    dSquare = toLight.dot(toLight);
    toLight = toLight.normalize();
    temp = MAX(toLight.dot(hit.normal), 0);

    ret[0] = ret[0] * temp * (light->intensity.x / dSquare);
    ret[1] = ret[1] * temp * (light->intensity.y / dSquare);
    ret[2] = ret[2] * temp * (light->intensity.z / dSquare);

    return ret;
}

bool isShadow(Hit& hit, PointLight& light, Scene& scene)
{
    Vec3f toLight;
    Vec3f toShadow;
    Ray newRay;
    toLight = (light.position - hit.intersectPoint);
    newRay.dir = toLight;
    newRay.start = hit.intersectPoint + hit.normal * scene.shadow_ray_epsilon;
    double d = toLight.dot(toLight);
    Hit hitsh = ClosestHit(newRay, scene);
    if (hitsh.hitOccur) {
        toShadow = (hitsh.intersectPoint - hit.intersectPoint);
        double ds = toShadow.dot(toShadow);
        if (ds < d)
            return true;
        else
            return false;
    } else
        return false;
}

unsigned char* CalculateColor(Ray& ray, int iterationCount, Scene& scene)
{
    Vec3f color = { 0, 0, 0 };
    unsigned char* ret = new unsigned char[3];
    ret[0] = ret[1] = ret[2] = 0;
    if (iterationCount < 0)
        return ret;
    Hit hit = ClosestHit(ray, scene);
    if (!hit.hitOccur) {
        color.x = clip(scene.background_color.x);
        color.y = clip(scene.background_color.y);
        color.z = clip(scene.background_color.z);
        ret[0] = color.x;
        ret[1] = color.y;
        ret[2] = color.z;
        return ret;
    }
    // Ambient color
    color.x = color.x + scene.materials[hit.materialID - 1].ambient.x * scene.ambient_light.x;
    color.y = color.y + scene.materials[hit.materialID - 1].ambient.y * scene.ambient_light.y;
    color.z = color.z + scene.materials[hit.materialID - 1].ambient.z * scene.ambient_light.z;

    // Calculate shadow for all light
    double* diffuse = Diffuse(ray, hit, NULL, scene);
    color.x = (color.x + diffuse[0]);
    color.y = (color.y + diffuse[1]);
    color.z = (color.z + diffuse[2]);
    for (int lightNo = 0; lightNo < scene.point_lights.size(); lightNo++) {
        PointLight& currentLight = scene.point_lights[lightNo];

        if (isShadow(hit, currentLight, scene)) {

            continue;
        }

        // Diffuse and Specular if not in shadow

        double* specular = Specular(ray, hit, currentLight, scene);
        color.x = (color.x + specular[0]);
        color.y = (color.y + specular[1]);
        color.z = (color.z + specular[2]);
        delete[] specular;

        double* diffuse = Diffuse(ray, hit, &currentLight, scene);
        color.x = (color.x + diffuse[0]);
        color.y = (color.y + diffuse[1]);
        color.z = (color.z + diffuse[2]);
        delete[] diffuse;
    }

    // Reflected component
    unsigned char* mirrorness;
    if (scene.materials[hit.materialID - 1].mirror.x || scene.materials[hit.materialID - 1].mirror.y || scene.materials[hit.materialID - 1].mirror.z) {
        Ray newRay, toSource;
        toSource.dir = (ray.start - hit.intersectPoint).normalize();
        newRay.dir = hit.normal * 2 * hit.normal.dot(toSource.dir) - toSource.dir;
        newRay.start = hit.intersectPoint + hit.normal * (scene.shadow_ray_epsilon);
        mirrorness = CalculateColor(newRay, iterationCount - 1, scene);

        color.x = (color.x + mirrorness[0] * scene.materials[hit.materialID - 1].mirror.x);
        color.y = (color.y + mirrorness[1] * scene.materials[hit.materialID - 1].mirror.y);
        color.z = (color.z + mirrorness[2] * scene.materials[hit.materialID - 1].mirror.z);
        delete[] mirrorness;
    }

    // Rounding and clipping
    ret[0] = clip(color.x);
    ret[1] = clip(color.y);
    ret[2] = clip(color.z);
    return ret;
}
void worker(Camera& camera, unsigned char*(&image), Scene& scene, int i, int j)
{

    Ray currentRay;
    for (int t = i; t < j; t++) {
        for (int k = 0; k < camera.image_width; k++) {
            currentRay = Generate(camera, t, k);
            unsigned char* color = CalculateColor(currentRay, scene.max_recursion_depth, scene);
            image[3 * (t * (camera.image_width) + k)] = color[0];
            image[3 * (t * (camera.image_width) + k) + 1] = color[1];
            image[3 * (t * (camera.image_width) + k) + 2] = color[2];
            delete[] color;
        }
    }
}

int main(int argc, char* argv[])
{
    // Sample usage for reading an XML scene file

    for (int inID = 1; inID < argc; inID++) {

        Scene scene;
        scene.loadFromXml(argv[inID]);

        // test values

        auto start = std::chrono::high_resolution_clock::now();

        for (int cam = 0; cam < scene.cameras.size(); cam++) {
            Camera& camera = scene.cameras[cam];
            unsigned char* image = new unsigned char[camera.image_width * camera.image_height * 3];
            int index = 0;
            //worker(camera, image, scene, 0, camera.image_height);

            int i = camera.image_height / 10;
            std::thread t1(&worker, std::ref(camera), std::ref(image), std::ref(scene), 0, i);
            std::thread t2(&worker, std::ref(camera), std::ref(image), std::ref(scene), i, 2 * i);
            std::thread t3(&worker, std::ref(camera), std::ref(image), std::ref(scene), 2 * i, 3 * i);
            std::thread t4(&worker, std::ref(camera), std::ref(image), std::ref(scene), 3 * i, 4 * i);
            std::thread t5(&worker, std::ref(camera), std::ref(image), std::ref(scene), 4 * i, 5 * i);
            std::thread t6(&worker, std::ref(camera), std::ref(image), std::ref(scene), 5 * i, 6 * i);
            std::thread t7(&worker, std::ref(camera), std::ref(image), std::ref(scene), 6 * i, 7 * i);
            std::thread t8(&worker, std::ref(camera), std::ref(image), std::ref(scene), 7 * i, 8 * i);
            std::thread t9(&worker, std::ref(camera), std::ref(image), std::ref(scene), 8 * i, 9 * i);
            std::thread t10(&worker, std::ref(camera), std::ref(image), std::ref(scene), 9 * i, 10 * i);
            if (camera.image_height % 10 != 0) {
                std::thread t11(&worker, std::ref(camera), std::ref(image), std::ref(scene), 10 * i, camera.image_height);
                t11.join();
            }
            t1.join();
            t2.join();
            t3.join();
            t4.join();
            t5.join();
            t6.join();
            t7.join();
            t8.join();
            t9.join();
            t10.join();

            write_ppm(camera.image_name.c_str(), image, camera.image_width, camera.image_height);
            delete[] image;
        }
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
        std::cout << argv[inID] << std::endl;
        std::cout << duration.count() << std::endl;
    }
    return 0;
}
