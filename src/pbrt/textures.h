// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_TEXTURES_H
#define PBRT_TEXTURES_H

#include <pbrt/pbrt.h>

#include <pbrt/base/texture.h>
#include <pbrt/interaction.h>
#include <pbrt/paramdict.h>
#include <pbrt/util/math.h>
#include <pbrt/util/mipmap.h>
#include <pbrt/util/noise.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/taggedptr.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>

#include <initializer_list>
#include <map>
#include <mutex>
#include <string>

namespace pbrt {

// TextureEvalContext Definition
struct TextureEvalContext {
    TextureEvalContext() = default;
    PBRT_CPU_GPU
    TextureEvalContext(const SurfaceInteraction &si)
        : p(si.p()),
          dpdx(si.dpdx),
          dpdy(si.dpdy),
          uv(si.uv),
          dudx(si.dudx),
          dudy(si.dudy),
          dvdx(si.dvdx),
          dvdy(si.dvdy),
          faceIndex(si.faceIndex) {}
    PBRT_CPU_GPU
    TextureEvalContext(const Point3f &p, const Vector3f &dpdx, const Vector3f &dpdy,
                       const Point2f &uv, Float dudx, Float dudy, Float dvdx, Float dvdy,
                       int faceIndex)
        : p(p),
          dpdx(dpdx),
          dpdy(dpdy),
          uv(uv),
          dudx(dudx),
          dudy(dudy),
          dvdx(dvdx),
          dvdy(dvdy),
          faceIndex(faceIndex) {}

    Point3f p;
    Vector3f dpdx, dpdy;
    Point2f uv;
    Float dudx = 0, dudy = 0, dvdx = 0, dvdy = 0;
    int faceIndex = 0;
};

// UVMapping2D Definition
class UVMapping2D {
  public:
    // UVMapping2D Public Methods
    UVMapping2D(Float su = 1, Float sv = 1, Float du = 0, Float dv = 0)
        : su(su), sv(sv), du(du), dv(dv) {}

    std::string ToString() const;

    PBRT_CPU_GPU
    Point2f Map(TextureEvalContext ctx, Vector2f *dstdx, Vector2f *dstdy) const {
        // Compute texture differentials for 2D $(u,v)$ mapping
        *dstdx = Vector2f(su * ctx.dudx, sv * ctx.dvdx);
        *dstdy = Vector2f(su * ctx.dudy, sv * ctx.dvdy);

        return {su * ctx.uv[0] + du, sv * ctx.uv[1] + dv};
    }

  private:
    Float su, sv, du, dv;
};

// SphericalMapping2D Definition
class SphericalMapping2D {
  public:
    // SphericalMapping2D Public Methods
    SphericalMapping2D(const Transform &textureFromRender)
        : textureFromRender(textureFromRender) {}

    std::string ToString() const;

    PBRT_CPU_GPU
    Point2f Map(TextureEvalContext ctx, Vector2f *dstdx, Vector2f *dstdy) const {
        Point2f st = sphere(ctx.p);
        // Compute texture coordinate differentials for sphere $(u,v)$ mapping
        const Float delta = .1f;
        Point2f stDeltaX = sphere(ctx.p + delta * ctx.dpdx);
        *dstdx = (stDeltaX - st) / delta;
        Point2f stDeltaY = sphere(ctx.p + delta * ctx.dpdy);
        *dstdy = (stDeltaY - st) / delta;

        // Handle sphere mapping discontinuity for coordinate differentials
        if ((*dstdx)[1] > .5)
            (*dstdx)[1] = 1 - (*dstdx)[1];
        else if ((*dstdx)[1] < -.5f)
            (*dstdx)[1] = -((*dstdx)[1] + 1);
        if ((*dstdy)[1] > .5)
            (*dstdy)[1] = 1 - (*dstdy)[1];
        else if ((*dstdy)[1] < -.5f)
            (*dstdy)[1] = -((*dstdy)[1] + 1);

        return st;
    }

  private:
    // SphericalMapping2D Private Methods
    PBRT_CPU_GPU
    Point2f sphere(const Point3f &p) const {
        Vector3f vec = Normalize(textureFromRender(p) - Point3f(0, 0, 0));
        Float theta = SphericalTheta(vec), phi = SphericalPhi(vec);
        return {theta * InvPi, phi * Inv2Pi};
    }

    // SphericalMapping2D Private Members
    Transform textureFromRender;
};

// CylindricalMapping2D Definition
class CylindricalMapping2D {
  public:
    // CylindricalMapping2D Public Methods
    CylindricalMapping2D(const Transform &textureFromRender)
        : textureFromRender(textureFromRender) {}
    std::string ToString() const;

    PBRT_CPU_GPU
    Point2f Map(TextureEvalContext ctx, Vector2f *dstdx, Vector2f *dstdy) const {
        Point2f st = cylinder(ctx.p);
        // Compute texture coordinate differentials for cylinder $(u,v)$ mapping
        const Float delta = .01f;
        Point2f stDeltaX = cylinder(ctx.p + delta * ctx.dpdx);
        *dstdx = (stDeltaX - st) / delta;
        if ((*dstdx)[1] > .5)
            (*dstdx)[1] = 1.f - (*dstdx)[1];
        else if ((*dstdx)[1] < -.5f)
            (*dstdx)[1] = -((*dstdx)[1] + 1);
        Point2f stDeltaY = cylinder(ctx.p + delta * ctx.dpdy);
        *dstdy = (stDeltaY - st) / delta;
        if ((*dstdy)[1] > .5)
            (*dstdy)[1] = 1.f - (*dstdy)[1];
        else if ((*dstdy)[1] < -.5f)
            (*dstdy)[1] = -((*dstdy)[1] + 1);

        return st;
    }

  private:
    // CylindricalMapping2D Private Methods
    PBRT_CPU_GPU
    Point2f cylinder(const Point3f &p) const {
        Vector3f vec = Normalize(textureFromRender(p) - Point3f(0, 0, 0));
        return Point2f((Pi + std::atan2(vec.y, vec.x)) * Inv2Pi, vec.z);
    }

    // CylindricalMapping2D Private Members
    Transform textureFromRender;
};

// PlanarMapping2D Definition
class PlanarMapping2D {
  public:
    // PlanarMapping2D Public Methods
    PlanarMapping2D(const Vector3f &vs, const Vector3f &vt, Float ds = 0, Float dt = 0)
        : vs(vs), vt(vt), ds(ds), dt(dt) {}

    PBRT_CPU_GPU
    Point2f Map(TextureEvalContext ctx, Vector2f *dstdx, Vector2f *dstdy) const {
        Vector3f vec(ctx.p);
        *dstdx = Vector2f(Dot(ctx.dpdx, vs), Dot(ctx.dpdx, vt));
        *dstdy = Vector2f(Dot(ctx.dpdy, vs), Dot(ctx.dpdy, vt));
        return {ds + Dot(vec, vs), dt + Dot(vec, vt)};
    }

    std::string ToString() const;

  private:
    Vector3f vs, vt;
    Float ds, dt;
};

// TextureMapping2DHandle Definition
class TextureMapping2DHandle
    : public TaggedPointer<UVMapping2D, SphericalMapping2D, CylindricalMapping2D,
                           PlanarMapping2D> {
  public:
    // TextureMapping2D Interface
    using TaggedPointer::TaggedPointer;
    PBRT_CPU_GPU
    TextureMapping2DHandle(TaggedPointer<UVMapping2D, SphericalMapping2D,
                                         CylindricalMapping2D, PlanarMapping2D>
                               tp)
        : TaggedPointer(tp) {}

    static TextureMapping2DHandle Create(const ParameterDictionary &parameters,
                                         const Transform &renderFromTexture,
                                         const FileLoc *loc, Allocator alloc);

    PBRT_CPU_GPU inline Point2f Map(TextureEvalContext ctx, Vector2f *dstdx,
                                    Vector2f *dstdy) const;
};

// TextureMapping2DHandle Inline Functions
inline Point2f TextureMapping2DHandle::Map(TextureEvalContext ctx, Vector2f *dstdx,
                                           Vector2f *dstdy) const {
    auto map = [&](auto ptr) { return ptr->Map(ctx, dstdx, dstdy); };
    return Dispatch(map);
}

// TransformMapping3D Definition
class TransformMapping3D {
  public:
    // TransformMapping3D Public Methods
    TransformMapping3D(const Transform &textureFromRender)
        : textureFromRender(textureFromRender) {}

    std::string ToString() const;

    PBRT_CPU_GPU
    Point3f Map(TextureEvalContext ctx, Vector3f *dpdx, Vector3f *dpdy) const {
        *dpdx = textureFromRender(ctx.dpdx);
        *dpdy = textureFromRender(ctx.dpdy);
        return textureFromRender(ctx.p);
    }

  private:
    Transform textureFromRender;
};

// TextureMapping3DHandle Definition
class TextureMapping3DHandle : public TaggedPointer<TransformMapping3D> {
  public:
    // TextureMapping3D Interface
    using TaggedPointer::TaggedPointer;
    PBRT_CPU_GPU
    TextureMapping3DHandle(TaggedPointer<TransformMapping3D> tp) : TaggedPointer(tp) {}

    static TextureMapping3DHandle Create(const ParameterDictionary &parameters,
                                         const Transform &renderFromTexture,
                                         const FileLoc *loc, Allocator alloc);

    PBRT_CPU_GPU
    Point3f Map(TextureEvalContext ctx, Vector3f *dpdx, Vector3f *dpdy) const;
};

inline Point3f TextureMapping3DHandle::Map(TextureEvalContext ctx, Vector3f *dpdx,
                                           Vector3f *dpdy) const {
    auto map = [&](auto ptr) { return ptr->Map(ctx, dpdx, dpdy); };
    return Dispatch(map);
}

// FloatConstantTexture Definition
class FloatConstantTexture {
  public:
    FloatConstantTexture(Float value) : value(value) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const { return value; }

    static FloatConstantTexture *Create(const Transform &renderFromTexture,
                                        const TextureParameterDictionary &parameters,
                                        const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    Float value;
};

// RGBConstantTexture Definition
class RGBConstantTexture {
  public:
    RGBConstantTexture(const RGBColorSpace &cs, const RGB &rgb) : value(cs, rgb) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        return value.Sample(lambda);
    }

    std::string ToString() const;

  private:
    RGBSpectrum value;
};

// RGBReflectanceConstantTexture Definition
class RGBReflectanceConstantTexture {
  public:
    RGBReflectanceConstantTexture(const RGBColorSpace &cs, const RGB &rgb)
        : value(cs, rgb) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        return value.Sample(lambda);
    }

    std::string ToString() const;

  private:
    RGBReflectanceSpectrum value;
};

// SpectrumConstantTexture Definition
class SpectrumConstantTexture {
  public:
    SpectrumConstantTexture(SpectrumHandle value) : value(value) { DCHECK(value); }

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        return value.Sample(lambda);
    }

    static SpectrumConstantTexture *Create(const Transform &renderFromTexture,
                                           const TextureParameterDictionary &parameters,
                                           const FileLoc *loc, Allocator alloc);
    std::string ToString() const;

  private:
    SpectrumHandle value;
};

// FloatBilerpTexture Definition
class FloatBilerpTexture {
  public:
    FloatBilerpTexture(TextureMapping2DHandle mapping, Float v00, Float v01, Float v10,
                       Float v11)
        : mapping(mapping), v00(v00), v01(v01), v10(v10), v11(v11) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        return Bilerp({st[0], st[1]}, {v00, v10, v01, v11});
    }

    static FloatBilerpTexture *Create(const Transform &renderFromTexture,
                                      const TextureParameterDictionary &parameters,
                                      const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // BilerpTexture Private Data
    TextureMapping2DHandle mapping;
    Float v00, v01, v10, v11;
};

// SpectrumBilerpTexture Definition
class SpectrumBilerpTexture {
  public:
    SpectrumBilerpTexture(TextureMapping2DHandle mapping, SpectrumHandle v00,
                          SpectrumHandle v01, SpectrumHandle v10, SpectrumHandle v11)
        : mapping(mapping), v00(v00), v01(v01), v10(v10), v11(v11) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        return Bilerp({st[0], st[1]}, {v00.Sample(lambda), v10.Sample(lambda),
                                       v01.Sample(lambda), v11.Sample(lambda)});
    }

    static SpectrumBilerpTexture *Create(const Transform &renderFromTexture,
                                         const TextureParameterDictionary &parameters,
                                         const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // BilerpTexture Private Data
    TextureMapping2DHandle mapping;
    SpectrumHandle v00, v01, v10, v11;
};

// AAMethod Definition
enum class AAMethod { None, ClosedForm };

PBRT_CPU_GPU
pstd::array<Float, 2> Checkerboard(AAMethod aaMethod, TextureEvalContext ctx,
                                   const TextureMapping2DHandle map2D,
                                   const TextureMapping3DHandle map3D);

class FloatCheckerboardTexture {
  public:
    FloatCheckerboardTexture(TextureMapping2DHandle map2D, TextureMapping3DHandle map3D,
                             FloatTextureHandle tex1, FloatTextureHandle tex2,
                             AAMethod aaMethod)
        : map2D(map2D), map3D(map3D), tex{tex1, tex2}, aaMethod(aaMethod) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        pstd::array<Float, 2> wt = Checkerboard(aaMethod, ctx, map2D, map3D);
        if (wt[0] == 0)
            return wt[1] * tex[1].Evaluate(ctx);
        else if (wt[1] == 0)
            return wt[0] * tex[0].Evaluate(ctx);
        else
            return wt[0] * tex[0].Evaluate(ctx) + wt[1] * tex[1].Evaluate(ctx);
    }

    static FloatCheckerboardTexture *Create(const Transform &renderFromTexture,
                                            const TextureParameterDictionary &parameters,
                                            const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    TextureMapping2DHandle map2D;
    TextureMapping3DHandle map3D;
    FloatTextureHandle tex[2];
    AAMethod aaMethod;
};

// SpectrumCheckerboardTexture Definition
class SpectrumCheckerboardTexture {
  public:
    SpectrumCheckerboardTexture(TextureMapping2DHandle map2D,
                                TextureMapping3DHandle map3D, SpectrumTextureHandle tex1,
                                SpectrumTextureHandle tex2, AAMethod aaMethod)
        : map2D(map2D), map3D(map3D), tex{tex1, tex2}, aaMethod(aaMethod) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        pstd::array<Float, 2> wt = Checkerboard(aaMethod, ctx, map2D, map3D);
        if (wt[0] == 0)
            return wt[1] * tex[1].Evaluate(ctx, lambda);
        else if (wt[1] == 0)
            return wt[0] * tex[0].Evaluate(ctx, lambda);
        else
            return wt[0] * tex[0].Evaluate(ctx, lambda) +
                   wt[1] * tex[1].Evaluate(ctx, lambda);
    }

    static SpectrumCheckerboardTexture *Create(
        const Transform &renderFromTexture, const TextureParameterDictionary &parameters,
        const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    TextureMapping2DHandle map2D;
    TextureMapping3DHandle map3D;
    SpectrumTextureHandle tex[2];
    AAMethod aaMethod;
};

// DotsBase Definition
class DotsBase {
  protected:
    PBRT_CPU_GPU
    static bool Inside(const Point2f &st) {
        int sCell = std::floor(st[0] + .5f), tCell = std::floor(st[1] + .5f);

        // Return _insideDot_ result if point is inside dot
        if (Noise(sCell + .5f, tCell + .5f) > 0) {
            Float radius = .35f;
            Float maxShift = 0.5f - radius;
            Float sCenter = sCell + maxShift * Noise(sCell + 1.5f, tCell + 2.8f);
            Float tCenter = tCell + maxShift * Noise(sCell + 4.5f, tCell + 9.8f);
            Vector2f dst = st - Point2f(sCenter, tCenter);
            if (LengthSquared(dst) < radius * radius)
                return true;
        }
        return false;
    }
};

class FloatDotsTexture : public DotsBase {
  public:
    FloatDotsTexture(TextureMapping2DHandle mapping, FloatTextureHandle outsideDot,
                     FloatTextureHandle insideDot)
        : mapping(mapping), outsideDot(outsideDot), insideDot(insideDot) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        return Inside(st) ? insideDot.Evaluate(ctx) : outsideDot.Evaluate(ctx);
    }

    static FloatDotsTexture *Create(const Transform &renderFromTexture,
                                    const TextureParameterDictionary &parameters,
                                    const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // DotsTexture Private Data
    TextureMapping2DHandle mapping;
    FloatTextureHandle outsideDot, insideDot;
};

// SpectrumDotsTexture Definition
class SpectrumDotsTexture : public DotsBase {
  public:
    SpectrumDotsTexture(TextureMapping2DHandle mapping, SpectrumTextureHandle outsideDot,
                        SpectrumTextureHandle insideDot)
        : mapping(mapping), outsideDot(outsideDot), insideDot(insideDot) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        return Inside(st) ? insideDot.Evaluate(ctx, lambda)
                          : outsideDot.Evaluate(ctx, lambda);
    }

    static SpectrumDotsTexture *Create(const Transform &renderFromTexture,
                                       const TextureParameterDictionary &parameters,
                                       const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // DotsTexture Private Data
    TextureMapping2DHandle mapping;
    SpectrumTextureHandle outsideDot, insideDot;
};

// FBmTexture Definition
class FBmTexture {
  public:
    // FBmTexture Public Methods
    FBmTexture(TextureMapping3DHandle mapping, int octaves, Float omega)
        : mapping(mapping), omega(omega), octaves(octaves) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Vector3f dpdx, dpdy;
        Point3f P = mapping.Map(ctx, &dpdx, &dpdy);
        return FBm(P, dpdx, dpdy, omega, octaves);
    }

    static FBmTexture *Create(const Transform &renderFromTexture,
                              const TextureParameterDictionary &parameters,
                              const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    TextureMapping3DHandle mapping;
    Float omega;
    int octaves;
};

// TexInfo Declarations
struct TexInfo {
    TexInfo(const std::string &f, const std::string &filt, Float ma, WrapMode wm,
            ColorEncodingHandle encoding)
        : filename(f), filter(filt), maxAniso(ma), wrapMode(wm), encoding(encoding) {}
    std::string filename;
    std::string filter;
    Float maxAniso;
    WrapMode wrapMode;
    ColorEncodingHandle encoding;
    bool operator<(const TexInfo &t2) const {
        return std::tie(filename, filter, maxAniso, encoding, wrapMode) <
               std::tie(t2.filename, t2.filter, t2.maxAniso, t2.encoding, t2.wrapMode);
    }

    std::string ToString() const;
};

// ImageTextureBase Definition
class ImageTextureBase {
  public:
    ImageTextureBase(TextureMapping2DHandle m, const std::string &filename,
                     const std::string &filter, Float maxAniso, WrapMode wm, Float scale,
                     ColorEncodingHandle encoding, Allocator alloc);

    static void ClearCache() { textureCache.clear(); }

    TextureMapping2DHandle mapping;
    Float scale;
    MIPMap *mipmap;

  private:
    static MIPMap *GetTexture(const std::string &filename, const std::string &filter,
                              Float maxAniso, WrapMode wm, ColorEncodingHandle encoding,
                              Allocator alloc);

    // ImageTextureBase Private Data
    static std::mutex textureCacheMutex;
    static std::map<TexInfo, std::unique_ptr<MIPMap>> textureCache;
};

// FloatImageTexture Definition
class FloatImageTexture : public ImageTextureBase {
  public:
    FloatImageTexture(TextureMapping2DHandle m, const std::string &filename,
                      const std::string &filter, Float maxAniso, WrapMode wm, Float scale,
                      ColorEncodingHandle encoding, Allocator alloc)
        : ImageTextureBase(m, filename, filter, maxAniso, wm, scale, encoding, alloc) {}
    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
#ifdef PBRT_IS_GPU_CODE
        assert(!"Should not be called in GPU code");
        return 0;
#else
        if (!mipmap)
            return scale;
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        // Texture coordinates are (0,0) in the lower left corner, but
        // image coordinates are (0,0) in the upper left.
        st[1] = 1 - st[1];
        return scale * mipmap->Lookup<Float>(st, dstdx, dstdy);
#endif
    }

    static FloatImageTexture *Create(const Transform &renderFromTexture,
                                     const TextureParameterDictionary &parameters,
                                     const FileLoc *loc, Allocator alloc);

    std::string ToString() const;
};

// SpectrumImageTexture Definition
class SpectrumImageTexture : public ImageTextureBase {
  public:
    SpectrumImageTexture(TextureMapping2DHandle m, const std::string &filename,
                         const std::string &filter, Float maxAniso, WrapMode wm,
                         Float scale, ColorEncodingHandle encoding, Allocator alloc)
        : ImageTextureBase(m, filename, filter, maxAniso, wm, scale, encoding, alloc) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const;

    static SpectrumImageTexture *Create(const Transform &renderFromTexture,
                                        const TextureParameterDictionary &parameters,
                                        const FileLoc *loc, Allocator alloc);

    std::string ToString() const;
};

#if defined(PBRT_BUILD_GPU_RENDERER) && defined(__NVCC__)
class GPUSpectrumImageTexture {
  public:
    GPUSpectrumImageTexture(TextureMapping2DHandle mapping, cudaTextureObject_t texObj,
                            Float scale, bool isSingleChannel,
                            const RGBColorSpace *colorSpace)
        : mapping(mapping),
          texObj(texObj),
          scale(scale),
          isSingleChannel(isSingleChannel),
          colorSpace(colorSpace) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
#ifndef PBRT_IS_GPU_CODE
        LOG_FATAL("GPUSpectrumImageTexture::Evaluate called from CPU");
        return SampledSpectrum(0);
#else
        // flip y coord since image has (0,0) at upper left, texture at lower
        // left
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        RGB rgb;
        if (isSingleChannel) {
            float tex = scale * tex2D<float>(texObj, st[0], 1 - st[1]);
            rgb = RGB(tex, tex, tex);
        } else {
            float4 tex = tex2D<float4>(texObj, st[0], 1 - st[1]);
            rgb = scale * RGB(tex.x, tex.y, tex.z);
        }
        if (std::max({rgb.r, rgb.g, rgb.b}) > 1)
            return RGBSpectrum(*colorSpace, rgb).Sample(lambda);
        return RGBReflectanceSpectrum(*colorSpace, rgb).Sample(lambda);
#endif
    }

    static GPUSpectrumImageTexture *Create(const Transform &renderFromTexture,
                                           const TextureParameterDictionary &parameters,
                                           const FileLoc *loc, Allocator alloc);

    std::string ToString() const { return "GPUSpectrumImageTexture"; }

    TextureMapping2DHandle mapping;
    cudaTextureObject_t texObj;
    Float scale;
    bool isSingleChannel;
    const RGBColorSpace *colorSpace;
};

class GPUFloatImageTexture {
  public:
    GPUFloatImageTexture(TextureMapping2DHandle mapping, cudaTextureObject_t texObj,
                         Float scale)
        : mapping(mapping), texObj(texObj), scale(scale) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
#ifndef PBRT_IS_GPU_CODE
        LOG_FATAL("GPUSpectrumImageTexture::Evaluate called from CPU");
        return 0;
#else
        Vector2f dstdx, dstdy;
        Point2f st = mapping.Map(ctx, &dstdx, &dstdy);
        // flip y coord since image has (0,0) at upper left, texture at lower
        // left
        return scale * tex2D<float>(texObj, st[0], 1 - st[1]);
#endif
    }

    static GPUFloatImageTexture *Create(const Transform &renderFromTexture,
                                        const TextureParameterDictionary &parameters,
                                        const FileLoc *loc, Allocator alloc);

    std::string ToString() const { return "GPUFloatImageTexture"; }

    TextureMapping2DHandle mapping;
    cudaTextureObject_t texObj;
    Float scale;
};

#else  // PBRT_BUILD_GPU_RENDERER && __NVCC__

class GPUSpectrumImageTexture {
  public:
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        LOG_FATAL("GPUSpectrumImageTexture::Evaluate called from CPU");
        return SampledSpectrum(0);
    }

    static GPUSpectrumImageTexture *Create(const Transform &renderFromTexture,
                                           const TextureParameterDictionary &parameters,
                                           const FileLoc *loc, Allocator alloc) {
        LOG_FATAL("GPUSpectrumImageTexture::Create called in non-GPU configuration.");
        return nullptr;
    }

    std::string ToString() const { return "GPUSpectrumImageTexture"; }
};

class GPUFloatImageTexture {
  public:
    Float Evaluate(const TextureEvalContext &) const {
        LOG_FATAL("GPUFloatImageTexture::Evaluate called from CPU");
        return 0;
    }

    static GPUFloatImageTexture *Create(const Transform &renderFromTexture,
                                        const TextureParameterDictionary &parameters,
                                        const FileLoc *loc, Allocator alloc) {
        LOG_FATAL("GPUFloatImageTexture::Create called in non-GPU configuration.");
        return nullptr;
    }

    std::string ToString() const { return "GPUFloatImageTexture"; }
};

#endif  // PBRT_BUILD_GPU_RENDERER && __NVCC__

// MarbleTexture Definition
class MarbleTexture {
  public:
    // MarbleTexture Public Methods
    MarbleTexture(TextureMapping3DHandle mapping, int octaves, Float omega, Float scale,
                  Float variation)
        : mapping(mapping),
          octaves(octaves),
          omega(omega),
          scale(scale),
          variation(variation) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const;

    static MarbleTexture *Create(const Transform &renderFromTexture,
                                 const TextureParameterDictionary &parameters,
                                 const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // MarbleTexture Private Data
    TextureMapping3DHandle mapping;
    int octaves;
    Float omega, scale, variation;
};

// FloatMixTexture Definition
class FloatMixTexture {
  public:
    // MixTexture Public Methods
    FloatMixTexture(FloatTextureHandle tex1, FloatTextureHandle tex2,
                    FloatTextureHandle amount)
        : tex1(tex1), tex2(tex2), amount(amount) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Float t1 = tex1.Evaluate(ctx), t2 = tex2.Evaluate(ctx);
        Float amt = amount.Evaluate(ctx);
        return (1 - amt) * t1 + amt * t2;
    }

    static FloatMixTexture *Create(const Transform &renderFromTexture,
                                   const TextureParameterDictionary &parameters,
                                   const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    FloatTextureHandle tex1, tex2;
    FloatTextureHandle amount;
};

// SpectrumMixTexture Definition
class SpectrumMixTexture {
  public:
    // MixTexture Public Methods
    SpectrumMixTexture(SpectrumTextureHandle tex1, SpectrumTextureHandle tex2,
                       FloatTextureHandle amount)
        : tex1(tex1), tex2(tex2), amount(amount) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        SampledSpectrum t1 = tex1.Evaluate(ctx, lambda);
        SampledSpectrum t2 = tex2.Evaluate(ctx, lambda);
        Float amt = amount.Evaluate(ctx);
        return (1 - amt) * t1 + amt * t2;
    }

    static SpectrumMixTexture *Create(const Transform &renderFromTexture,
                                      const TextureParameterDictionary &parameters,
                                      const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    SpectrumTextureHandle tex1, tex2;
    FloatTextureHandle amount;
};

// PtexTexture Declarations
class PtexTextureBase {
  public:
    PtexTextureBase(const std::string &filename, ColorEncodingHandle encoding);

    static void ReportStats();

  protected:
    int SampleTexture(TextureEvalContext ctx, float *result) const;
    std::string BaseToString() const;

  private:
    bool valid;
    std::string filename;
    ColorEncodingHandle encoding;
};

class FloatPtexTexture : public PtexTextureBase {
  public:
    FloatPtexTexture(const std::string &filename, ColorEncodingHandle encoding)
        : PtexTextureBase(filename, encoding) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const;
    static FloatPtexTexture *Create(const Transform &renderFromTexture,
                                    const TextureParameterDictionary &parameters,
                                    const FileLoc *loc, Allocator alloc);
    std::string ToString() const;
};

class SpectrumPtexTexture : public PtexTextureBase {
  public:
    SpectrumPtexTexture(const std::string &filename, ColorEncodingHandle encoding)
        : PtexTextureBase(filename, encoding) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const;

    static SpectrumPtexTexture *Create(const Transform &renderFromTexture,
                                       const TextureParameterDictionary &parameters,
                                       const FileLoc *loc, Allocator alloc);

    std::string ToString() const;
};

// FloatScaledTexture Definition
class FloatScaledTexture {
  public:
    FloatScaledTexture(FloatTextureHandle tex, FloatTextureHandle scale)
        : tex(tex), scale(scale) {}

    static FloatTextureHandle Create(const Transform &renderFromTexture,
                                     const TextureParameterDictionary &parameters,
                                     const FileLoc *loc, Allocator alloc);

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        return tex.Evaluate(ctx) * scale.Evaluate(ctx);
    }

    std::string ToString() const;

  private:
    FloatTextureHandle tex, scale;
};

// SpectrumScaledTexture Definition
class SpectrumScaledTexture {
  public:
    SpectrumScaledTexture(SpectrumTextureHandle tex, FloatTextureHandle scale)
        : tex(tex), scale(scale) {}

    PBRT_CPU_GPU
    SampledSpectrum Evaluate(TextureEvalContext ctx, SampledWavelengths lambda) const {
        return tex.Evaluate(ctx, lambda) * scale.Evaluate(ctx);
    }

    static SpectrumTextureHandle Create(const Transform &renderFromTexture,
                                        const TextureParameterDictionary &parameters,
                                        const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    SpectrumTextureHandle tex;
    FloatTextureHandle scale;
};

// WindyTexture Definition
class WindyTexture {
  public:
    // WindyTexture Public Methods
    WindyTexture(TextureMapping3DHandle mapping) : mapping(mapping) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Vector3f dpdx, dpdy;
        Point3f P = mapping.Map(ctx, &dpdx, &dpdy);
        Float windStrength = FBm(.1f * P, .1f * dpdx, .1f * dpdy, .5, 3);
        Float waveHeight = FBm(P, dpdx, dpdy, .5, 6);
        return std::abs(windStrength) * waveHeight;
    }

    static WindyTexture *Create(const Transform &renderFromTexture,
                                const TextureParameterDictionary &parameters,
                                const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    TextureMapping3DHandle mapping;
};

// WrinkledTexture Definition
class WrinkledTexture {
  public:
    // WrinkledTexture Public Methods
    WrinkledTexture(TextureMapping3DHandle mapping, int octaves, Float omega)
        : mapping(mapping), octaves(octaves), omega(omega) {}

    PBRT_CPU_GPU
    Float Evaluate(TextureEvalContext ctx) const {
        Vector3f dpdx, dpdy;
        Point3f p = mapping.Map(ctx, &dpdx, &dpdy);
        return Turbulence(p, dpdx, dpdy, omega, octaves);
    }

    static WrinkledTexture *Create(const Transform &renderFromTexture,
                                   const TextureParameterDictionary &parameters,
                                   const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

  private:
    // WrinkledTexture Private Data
    TextureMapping3DHandle mapping;
    int octaves;
    Float omega;
};

inline Float FloatTextureHandle::Evaluate(TextureEvalContext ctx) const {
    auto eval = [&](auto ptr) { return ptr->Evaluate(ctx); };
    return Dispatch(eval);
}

inline SampledSpectrum SpectrumTextureHandle::Evaluate(TextureEvalContext ctx,
                                                       SampledWavelengths lambda) const {
    auto eval = [&](auto ptr) { return ptr->Evaluate(ctx, lambda); };
    return Dispatch(eval);
}

class UniversalTextureEvaluator {
  public:
    PBRT_CPU_GPU
    bool CanEvaluate(std::initializer_list<FloatTextureHandle>,
                     std::initializer_list<SpectrumTextureHandle>) const {
        return true;
    }
    PBRT_CPU_GPU
    Float operator()(FloatTextureHandle tex, TextureEvalContext ctx);

    PBRT_CPU_GPU
    SampledSpectrum operator()(SpectrumTextureHandle tex, TextureEvalContext ctx,
                               SampledWavelengths lambda);
};

class BasicTextureEvaluator {
  public:
    PBRT_CPU_GPU
    bool CanEvaluate(std::initializer_list<FloatTextureHandle> ftex,
                     std::initializer_list<SpectrumTextureHandle> stex) const {
        for (auto f : ftex)
            if (f && (!f.Is<FloatConstantTexture>() && !f.Is<GPUFloatImageTexture>())) {
                return false;
            }
        for (auto s : stex)
            if (s && (!s.Is<SpectrumConstantTexture>() && !s.Is<RGBConstantTexture>() &&
                      !s.Is<RGBReflectanceConstantTexture>() &&
                      !s.Is<GPUSpectrumImageTexture>())) {
                return false;
            }
        return true;
    }

    PBRT_CPU_GPU
    Float operator()(FloatTextureHandle tex, TextureEvalContext ctx) {
        if (FloatConstantTexture *fc = tex.CastOrNullptr<FloatConstantTexture>())
            return fc->Evaluate(ctx);
        else if (GPUFloatImageTexture *fg = tex.CastOrNullptr<GPUFloatImageTexture>())
            return fg->Evaluate(ctx);
        else
            return 0.f;
    }

    PBRT_CPU_GPU
    SampledSpectrum operator()(SpectrumTextureHandle tex, TextureEvalContext ctx,
                               SampledWavelengths lambda) {
        if (SpectrumConstantTexture *sc = tex.CastOrNullptr<SpectrumConstantTexture>())
            return sc->Evaluate(ctx, lambda);
        else if (RGBConstantTexture *rgbc = tex.CastOrNullptr<RGBConstantTexture>())
            return rgbc->Evaluate(ctx, lambda);
        else if (RGBReflectanceConstantTexture *rgbc =
                     tex.CastOrNullptr<RGBReflectanceConstantTexture>())
            return rgbc->Evaluate(ctx, lambda);
        else if (GPUSpectrumImageTexture *sg =
                     tex.CastOrNullptr<GPUSpectrumImageTexture>())
            return sg->Evaluate(ctx, lambda);
        else
            return SampledSpectrum(0.f);
    }
};

}  // namespace pbrt

#endif  // PBRT_TEXTURES_H
