#include "UsdViewportItem.h"
#include "UsdDocument.h"
#include "UndoStack.h"
#include "UndoCommands.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSet>
#include <QtMath>
#include <rhi/qrhi.h>
#include <cfloat>
#include <algorithm>

// USD
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/domeLight.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/lightAPI.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ================================================================
//  Renderer declaration
// ================================================================
class UsdViewportRenderer : public QQuickRhiItemRenderer
{
public:
    ~UsdViewportRenderer() override;
    void initialize(QRhiCommandBuffer *cb) override;
    void synchronize(QQuickRhiItem *item) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    struct RhiMesh {
        QRhiBuffer *vbuf = nullptr;
        QRhiBuffer *vbufSmooth = nullptr; // smooth normals for outline
        QRhiBuffer *ibuf = nullptr;
        QRhiBuffer *ubuf = nullptr;
        QRhiShaderResourceBindings *srb = nullptr;
        QRhiBuffer *ibufEdge = nullptr;
        QRhiBuffer *wireUbuf = nullptr;
        QRhiShaderResourceBindings *wireSrb = nullptr;
        int indexCount = 0;
        int edgeCount  = 0;
        QMatrix4x4 transform;
        QVector3D  color;
        QVector3D  centroid;
        bool lineOnly = false;
        bool isLightGizmo = false;
        bool isCollision = false;
        bool hasCollisionAPI = false;
        QRhiBuffer *colUbuf = nullptr;              // collision wireframe UBO
        QRhiShaderResourceBindings *colSrb = nullptr;
        QRhiBuffer *colVbuf = nullptr;              // sparse wireframe vertices
        QRhiBuffer *colIbuf = nullptr;              // sparse wireframe indices
        int colWireCount = 0;
    };

    void destroyMeshes();
    void uploadMesh(RhiMesh &dst, const MeshData &src, QRhiResourceUpdateBatch *batch);

    struct RhiGizmoMesh {
        QRhiBuffer *vbuf = nullptr, *ibuf = nullptr, *ubuf = nullptr;
        QRhiShaderResourceBindings *srb = nullptr;
        int indexCount = 0;
        QVector3D color;
        QVector3D highlightColor;
    };
    void destroyGizmoMeshes();
    void uploadGizmoMesh(RhiGizmoMesh &dst, const GizmoMeshData &src, QRhiResourceUpdateBatch *batch);

    QRhiGraphicsPipeline *m_pipeline       = nullptr;
    QRhiGraphicsPipeline *m_wirePipeline   = nullptr;
    QRhiGraphicsPipeline *m_stencilPipeline = nullptr;
    QRhiGraphicsPipeline *m_gizmoPipeline  = nullptr;
    QRhiGraphicsPipeline *m_lineMeshPipeline = nullptr;
    QShader m_vs, m_fs;
    bool m_initialized = false;

    QVector<RhiMesh>  m_meshes;
    QVector<MeshData> m_pending;
    bool m_rebuild = true;
    QSet<int> m_selectedIndices;

    QMatrix4x4 m_view, m_proj;

    // Gizmo render state
    QVector<RhiGizmoMesh> m_rhiGizmo;
    QVector<GizmoMeshData> m_gizmoPending;
    bool m_gizmoRebuild = false;
    bool m_gizmoVisible = false;
    int  m_gizmoMode = GizmoModeNone;
    QMatrix4x4 m_gizmoTransform;
    int m_gizmoHoveredPart = -1;
    QVector3D m_scaleCubeFactors{1.f, 1.f, 1.f};

    // Orientation indicator
    QVector<RhiGizmoMesh> m_rhiOrientAxes;
    QVector<GizmoMeshData> m_orientPending;
    bool m_orientRebuild = false;
    QMatrix4x4 m_orientView, m_orientProj;
    QSizeF m_logicalSize;

    // Grid
    QRhiGraphicsPipeline *m_gridPipeline = nullptr;
    QRhiGraphicsPipeline *m_collisionPipeline = nullptr;
    QVector<RhiGizmoMesh> m_rhiGrid;
    QVector<GizmoMeshData> m_gridPending;
    bool m_gridRebuild = false;
    bool m_showGrid = false;
    int  m_collisionDisplayMode = 0;

    float m_unitScale = 1.f;

    // Scene lights
    QVector<LightData> m_sceneLights;
    QRhiBuffer *m_lightUbuf = nullptr;
    QVector3D m_cameraEye;

    // Post-process outline
    QRhiTexture *m_maskTex = nullptr;
    QRhiRenderBuffer *m_maskDS = nullptr;
    QRhiTextureRenderTarget *m_maskRT = nullptr;
    QRhiRenderPassDescriptor *m_maskRpDesc = nullptr;
    QRhiGraphicsPipeline *m_maskPipeline = nullptr;
    QRhiGraphicsPipeline *m_outlinePipeline = nullptr;
    QRhiSampler *m_maskSampler = nullptr;
    QRhiBuffer *m_outlineUbuf = nullptr;
    QRhiBuffer *m_fsTriVbuf = nullptr;
    QRhiShaderResourceBindings *m_outlineSrb = nullptr;
    QShader m_outlineVs, m_outlineFs;
    QSize m_maskSize;
};

// ================================================================
//  Geometry helpers
// ================================================================

static void addTriangle(QVector<float> &v, QVector<quint32> &idx,
                        GfVec3f p0, GfVec3f p1, GfVec3f p2, GfVec3f n)
{
    quint32 base = v.size() / 6;
    for (auto &p : {p0, p1, p2})
        v << p[0] << p[1] << p[2] << n[0] << n[1] << n[2];
    idx << base << base+1 << base+2;
}

static void genSphere(float r, QVector<float> &v, QVector<quint32> &idx)
{
    const int rings = 20, sectors = 36;
    v.clear(); idx.clear();
    for (int ri = 0; ri <= rings; ri++) {
        float phi = float(M_PI) * ri / rings;
        float sp = sinf(phi), cp = cosf(phi);
        for (int si = 0; si <= sectors; si++) {
            float theta = 2.f * float(M_PI) * si / sectors;
            float nx = sp * cosf(theta), ny = cp, nz = sp * sinf(theta);
            v << nx*r << ny*r << nz*r << nx << ny << nz;
        }
    }
    for (int ri = 0; ri < rings; ri++)
        for (int si = 0; si < sectors; si++) {
            quint32 a = ri*(sectors+1)+si, b = a+sectors+1;
            idx << a << b << a+1 << a+1 << b << b+1;
        }
}

static void genCylinder(float radius, float height,
                         QVector<float> &v, QVector<quint32> &idx,
                         bool caps = true)
{
    const int segs = 36;
    float h = height * 0.5f;

    // side
    quint32 base = v.size() / 6;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        float nx = cosf(t), nz = sinf(t);
        v << nx*radius << -h << nz*radius << nx << 0.f << nz;
        v << nx*radius <<  h << nz*radius << nx << 0.f << nz;
    }
    for (int i = 0; i < segs; i++) {
        quint32 b = base + i*2;
        idx << b << b+2 << b+1 << b+1 << b+2 << b+3;
    }

    if (!caps) return;

    // top cap
    base = v.size() / 6;
    v << 0.f << h << 0.f << 0.f << 1.f << 0.f;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        v << cosf(t)*radius << h << sinf(t)*radius << 0.f << 1.f << 0.f;
    }
    for (int i = 0; i < segs; i++)
        idx << base << base+i+2 << base+i+1;

    // bottom cap
    base = v.size() / 6;
    v << 0.f << -h << 0.f << 0.f << -1.f << 0.f;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        v << cosf(t)*radius << -h << sinf(t)*radius << 0.f << -1.f << 0.f;
    }
    for (int i = 0; i < segs; i++)
        idx << base << base+i+1 << base+i+2;
}

static void genCone(float radius, float height,
                     QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    float h = height * 0.5f;
    float sl = sqrtf(radius*radius + height*height); // slant length
    float nx_s = height / sl, ny_s = radius / sl;   // side normal components

    // side
    quint32 base = v.size() / 6;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        float cx = cosf(t), cz = sinf(t);
        // bottom ring
        v << cx*radius << -h << cz*radius << cx*nx_s << ny_s << cz*nx_s;
        // apex (share position, vary normal direction around ring)
        v << 0.f << h << 0.f << cx*nx_s << ny_s << cz*nx_s;
    }
    for (int i = 0; i < segs; i++) {
        quint32 b = base + i*2;
        idx << b << b+2 << b+1;   // side triangle
    }

    // bottom cap
    base = v.size() / 6;
    v << 0.f << -h << 0.f << 0.f << -1.f << 0.f;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        v << cosf(t)*radius << -h << sinf(t)*radius << 0.f << -1.f << 0.f;
    }
    for (int i = 0; i < segs; i++)
        idx << base << base+i+1 << base+i+2;
}

static void genCapsule(float radius, float height,
                        QVector<float> &v, QVector<quint32> &idx)
{
    // cylinder body + hemisphere caps
    const int rings = 10, sectors = 36;
    float h = height * 0.5f; // half-height of the cylindrical part

    // cylinder side (no caps)
    genCylinder(radius, height, v, idx, false);

    // top hemisphere
    quint32 base = v.size() / 6;
    for (int ri = 0; ri <= rings; ri++) {
        float phi = float(M_PI_2) * ri / rings; // 0..π/2
        float sp = sinf(phi), cp = cosf(phi);
        for (int si = 0; si <= sectors; si++) {
            float theta = 2.f * float(M_PI) * si / sectors;
            float nx = cp * cosf(theta), ny = sp, nz = cp * sinf(theta);
            v << nx*radius << ny*radius + h << nz*radius << nx << ny << nz;
        }
    }
    for (int ri = 0; ri < rings; ri++)
        for (int si = 0; si < sectors; si++) {
            quint32 a = base + ri*(sectors+1)+si, b = a+sectors+1;
            idx << a << b << a+1 << a+1 << b << b+1;
        }

    // bottom hemisphere (flipped)
    base = v.size() / 6;
    for (int ri = 0; ri <= rings; ri++) {
        float phi = float(M_PI_2) * ri / rings;
        float sp = sinf(phi), cp = cosf(phi);
        for (int si = 0; si <= sectors; si++) {
            float theta = 2.f * float(M_PI) * si / sectors;
            float nx = cp * cosf(theta), ny = -sp, nz = cp * sinf(theta);
            v << nx*radius << ny*radius - h << nz*radius << nx << ny << nz;
        }
    }
    for (int ri = 0; ri < rings; ri++)
        for (int si = 0; si < sectors; si++) {
            quint32 a = base + ri*(sectors+1)+si, b = a+sectors+1;
            idx << a << a+1 << b << a+1 << b+1 << b;
        }
}

static void genCube(float size, QVector<float> &v, QVector<quint32> &idx,
                    int subdivisions = 8)
{
    float h = size * 0.5f;
    struct Face { float nx,ny,nz; float p[4][3]; };
    const Face faces[6] = {
        { 0, 0, 1, {{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}}},
        { 0, 0,-1, {{ h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}}},
        {-1, 0, 0, {{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}}},
        { 1, 0, 0, {{ h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}}},
        { 0, 1, 0, {{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}}},
        { 0,-1, 0, {{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}}}
    };
    v.clear(); idx.clear();
    const int N = subdivisions;
    for (auto &f : faces) {
        quint32 base = quint32(v.size() / 6);
        // Generate (N+1)x(N+1) grid of vertices per face
        for (int j = 0; j <= N; ++j) {
            float tv = float(j) / float(N);
            for (int i = 0; i <= N; ++i) {
                float tu = float(i) / float(N);
                // Bilinear interpolation of face corners: p0,p1,p2,p3
                float px = (1-tu)*(1-tv)*f.p[0][0] + tu*(1-tv)*f.p[1][0]
                         + tu*tv*f.p[2][0] + (1-tu)*tv*f.p[3][0];
                float py = (1-tu)*(1-tv)*f.p[0][1] + tu*(1-tv)*f.p[1][1]
                         + tu*tv*f.p[2][1] + (1-tu)*tv*f.p[3][1];
                float pz = (1-tu)*(1-tv)*f.p[0][2] + tu*(1-tv)*f.p[1][2]
                         + tu*tv*f.p[2][2] + (1-tu)*tv*f.p[3][2];
                v << px << py << pz << f.nx << f.ny << f.nz;
            }
        }
        // Generate NxN quads (2 triangles each)
        for (int j = 0; j < N; ++j) {
            for (int i = 0; i < N; ++i) {
                quint32 r0 = base + quint32(j * (N + 1) + i);
                quint32 r1 = base + quint32((j + 1) * (N + 1) + i);
                idx << r0 << (r0 + 1) << (r1 + 1)
                    << r0 << (r1 + 1) << r1;
            }
        }
    }
}

static void genPlane(float width, float length,
                      QVector<float> &v, QVector<quint32> &idx)
{
    float hw = width * 0.5f, hl = length * 0.5f;
    v.clear(); idx.clear();
    // single face (Y-up normal) — two-sided lighting handles the back
    v << -hw << 0 <<  hl << 0 << 1 << 0;
    v <<  hw << 0 <<  hl << 0 << 1 << 0;
    v <<  hw << 0 << -hl << 0 << 1 << 0;
    v << -hw << 0 << -hl << 0 << 1 << 0;
    idx << 0 << 1 << 2 << 0 << 2 << 3;
}

static void genDisk(float radius, QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    v.clear(); idx.clear();
    // center vertex
    v << 0.f << 0.f << 0.f << 0.f << 1.f << 0.f;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        v << cosf(t)*radius << 0.f << sinf(t)*radius << 0.f << 1.f << 0.f;
    }
    for (int i = 0; i < segs; i++)
        idx << 0 << i+1 << i+2;
}

// Line-only gizmo for RectLight (Isaac Sim style):
// - Square frame in XY plane at Z=0 (the light face)
// - 5 ray lines going -Z (4 corners + center) — USD emission direction is -Z
// Indices are line pairs (for GL_LINES topology).
static void genRectLightGizmo(float size, QVector<float> &v, QVector<quint32> &idx)
{
    float hs = size * 0.5f;
    float rayLen = size * 0.6f;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 0.f << -1.f; // normal along -Z (emission dir)
        return i;
    };

    // 4 corners of the square face at Z=0 (in XY plane)
    quint32 c0 = addVert(-hs, -hs, 0);
    quint32 c1 = addVert( hs, -hs, 0);
    quint32 c2 = addVert( hs,  hs, 0);
    quint32 c3 = addVert(-hs,  hs, 0);

    // Square frame edges
    idx << c0 << c1 << c1 << c2 << c2 << c3 << c3 << c0;

    // 5 ray lines going -Z from face (4 corners + center) — emission direction
    quint32 r0 = addVert(-hs, -hs, -rayLen);
    quint32 r1 = addVert( hs, -hs, -rayLen);
    quint32 r2 = addVert( hs,  hs, -rayLen);
    quint32 r3 = addVert(-hs,  hs, -rayLen);
    quint32 rc = addVert(0,    0,   -rayLen);
    quint32 cc = addVert(0,    0,    0);       // center of face
    idx << c0 << r0 << c1 << r1 << c2 << r2 << c3 << r3 << cc << rc;
}

// Line-only gizmo for DiskLight (Isaac Sim style):
// - Circle in XY plane at Z=0 (the disk face)
// - 4 ray lines going -Z from cardinal points — USD emission direction is -Z
// Indices are line pairs (for GL_LINES topology).
static void genDiskLightGizmo(float radius, QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    float rayLen = radius * 1.2f;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 0.f << -1.f; // normal along -Z (emission dir)
        return i;
    };

    // Circle vertices at Z=0 (in XY plane)
    QVector<quint32> circleVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        circleVerts << addVert(cosf(t) * radius, sinf(t) * radius, 0.f);
    }
    // Circle edges
    for (int i = 0; i < segs; i++)
        idx << circleVerts[i] << circleVerts[(i + 1) % segs];

    // 4 ray lines from cardinal points going -Z — emission direction
    for (int i = 0; i < 4; i++) {
        int ci = i * (segs / 4);
        float t = 2.f * float(M_PI) * ci / segs;
        quint32 rv = addVert(cosf(t) * radius, sinf(t) * radius, -rayLen);
        idx << circleVerts[ci] << rv;
    }
}

static void genHemisphere(float r, QVector<float> &v, QVector<quint32> &idx)
{
    const int rings = 10, sectors = 36;
    v.clear(); idx.clear();
    for (int ri = 0; ri <= rings; ri++) {
        float phi = float(M_PI_2) * ri / rings; // 0..π/2
        float sp = sinf(phi), cp = cosf(phi);
        for (int si = 0; si <= sectors; si++) {
            float theta = 2.f * float(M_PI) * si / sectors;
            float nx = cp * cosf(theta), ny = sp, nz = cp * sinf(theta);
            v << nx*r << ny*r << nz*r << nx << ny << nz;
        }
    }
    for (int ri = 0; ri < rings; ri++)
        for (int si = 0; si < sectors; si++) {
            quint32 a = ri*(sectors+1)+si, b = a+sectors+1;
            idx << a << b << a+1 << a+1 << b << b+1;
        }
    // bottom cap (seal the hemisphere)
    quint32 base = v.size() / 6;
    v << 0.f << 0.f << 0.f << 0.f << -1.f << 0.f;
    for (int i = 0; i <= sectors; i++) {
        float t = 2.f * float(M_PI) * i / sectors;
        v << cosf(t)*r << 0.f << sinf(t)*r << 0.f << -1.f << 0.f;
    }
    for (int i = 0; i < sectors; i++)
        idx << base << base+i+2 << base+i+1;
}

// Line-only gizmo for SphereLight:
// 3 great circles in XY, XZ, YZ planes.
// Indices are line pairs (for GL_LINES topology).
static void genSphereLightGizmo(float radius, QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 0.f << 0.f;
        return i;
    };

    // XY plane circle (horizontal at equator)
    QVector<quint32> xyVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        xyVerts << addVert(cosf(t) * radius, sinf(t) * radius, 0.f);
    }
    for (int i = 0; i < segs; i++)
        idx << xyVerts[i] << xyVerts[(i + 1) % segs];

    // XZ plane circle (vertical, front-back)
    QVector<quint32> xzVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        xzVerts << addVert(cosf(t) * radius, 0.f, sinf(t) * radius);
    }
    for (int i = 0; i < segs; i++)
        idx << xzVerts[i] << xzVerts[(i + 1) % segs];

    // YZ plane circle (vertical, left-right)
    QVector<quint32> yzVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        yzVerts << addVert(0.f, cosf(t) * radius, sinf(t) * radius);
    }
    for (int i = 0; i < segs; i++)
        idx << yzVerts[i] << yzVerts[(i + 1) % segs];
}

// Line-only gizmo for DistantLight:
// Wireframe cone pointing along -Z (USD light emission axis) with rays.
// Indices are line pairs (for GL_LINES topology).
static void genDistantLightGizmo(float radius, float height,
                                  QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    const int rays = 8;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 0.f << -1.f;
        return i;
    };

    // Base circle at Z=0 (in XY plane)
    QVector<quint32> circleVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        circleVerts << addVert(cosf(t) * radius, sinf(t) * radius, 0.f);
    }
    // Circle edges
    for (int i = 0; i < segs; i++)
        idx << circleVerts[i] << circleVerts[(i + 1) % segs];

    // Apex at -Z (emission direction)
    quint32 apex = addVert(0.f, 0.f, -height);

    // Lines from circle to apex
    for (int i = 0; i < rays; i++) {
        int ci = i * (segs / rays);
        idx << circleVerts[ci] << apex;
    }
}

// Line-only gizmo for DomeLight:
// Wireframe hemisphere: 3 parallels + 4 meridians + base circle.
// Indices are line pairs (for GL_LINES topology).
static void genDomeLightGizmo(float radius, QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 1.f << 0.f;
        return i;
    };

    // Base circle at Y=0 (in XZ plane)
    QVector<quint32> baseVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        baseVerts << addVert(cosf(t) * radius, 0.f, sinf(t) * radius);
    }
    for (int i = 0; i < segs; i++)
        idx << baseVerts[i] << baseVerts[(i + 1) % segs];

    // 3 parallel circles at phi = 22.5°, 45°, 67.5°
    const float phiAngles[] = { float(M_PI) * 0.125f, float(M_PI) * 0.25f, float(M_PI) * 0.375f };
    for (float phi : phiAngles) {
        float rr = cosf(phi) * radius;
        float yy = sinf(phi) * radius;
        QVector<quint32> pVerts;
        for (int i = 0; i < segs; i++) {
            float t = 2.f * float(M_PI) * i / segs;
            pVerts << addVert(cosf(t) * rr, yy, sinf(t) * rr);
        }
        for (int i = 0; i < segs; i++)
            idx << pVerts[i] << pVerts[(i + 1) % segs];
    }

    // 4 meridian semicircles: each goes from base point at θ, over the pole, to
    // the opposite base point at θ+π.  Distributed every 45° around Y axis.
    const int halfSegs = segs / 2;
    const float meridianAngles[] = { 0.f, float(M_PI) * 0.25f, float(M_PI) * 0.5f, float(M_PI) * 0.75f };
    for (float theta : meridianAngles) {
        float cx = cosf(theta), cz = sinf(theta);
        QVector<quint32> mVerts;
        for (int i = 0; i <= halfSegs; i++) {
            float phi = float(M_PI) * i / halfSegs; // 0..π (full semicircle)
            float rr = cosf(phi) * radius;
            float yy = sinf(phi) * radius;
            mVerts << addVert(cx * rr, yy, cz * rr);
        }
        for (int i = 0; i < halfSegs; i++)
            idx << mVerts[i] << mVerts[i + 1];
    }
}

// Line-only gizmo for CylinderLight:
// Two circles (top/bottom caps) + vertical lines connecting them.
// Indices are line pairs (for GL_LINES topology).
static void genCylinderLightGizmo(float radius, float length,
                                   QVector<float> &v, QVector<quint32> &idx)
{
    const int segs = 36;
    const int vertLines = 8;
    float h = length * 0.5f;
    v.clear(); idx.clear();

    auto addVert = [&](float x, float y, float z) -> quint32 {
        quint32 i = v.size() / 6;
        v << x << y << z << 0.f << 0.f << 0.f;
        return i;
    };

    // Top circle at Y=+h
    QVector<quint32> topVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        topVerts << addVert(cosf(t) * radius, h, sinf(t) * radius);
    }
    for (int i = 0; i < segs; i++)
        idx << topVerts[i] << topVerts[(i + 1) % segs];

    // Bottom circle at Y=-h
    QVector<quint32> botVerts;
    for (int i = 0; i < segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        botVerts << addVert(cosf(t) * radius, -h, sinf(t) * radius);
    }
    for (int i = 0; i < segs; i++)
        idx << botVerts[i] << botVerts[(i + 1) % segs];

    // Vertical lines connecting top and bottom
    for (int i = 0; i < vertLines; i++) {
        int ci = i * (segs / vertLines);
        idx << topVerts[ci] << botVerts[ci];
    }
}

static void genCameraFrustum(QVector<float> &v, QVector<quint32> &idx)
{
    v.clear(); idx.clear();
    // small camera body box + lens cone
    // body: 0.4 x 0.3 x 0.25 centered at origin
    float bw = 0.2f, bh = 0.15f, bd = 0.125f;
    struct Face { float nx,ny,nz; float p[4][3]; };
    const Face faces[6] = {
        { 0, 0, 1, {{-bw,-bh,bd},{bw,-bh,bd},{bw,bh,bd},{-bw,bh,bd}}},
        { 0, 0,-1, {{ bw,-bh,-bd},{-bw,-bh,-bd},{-bw,bh,-bd},{bw,bh,-bd}}},
        {-1, 0, 0, {{-bw,-bh,-bd},{-bw,-bh,bd},{-bw,bh,bd},{-bw,bh,-bd}}},
        { 1, 0, 0, {{ bw,-bh,bd},{bw,-bh,-bd},{bw,bh,-bd},{bw,bh,bd}}},
        { 0, 1, 0, {{-bw,bh,bd},{bw,bh,bd},{bw,bh,-bd},{-bw,bh,-bd}}},
        { 0,-1, 0, {{-bw,-bh,-bd},{bw,-bh,-bd},{bw,-bh,bd},{-bw,-bh,bd}}}
    };
    quint32 base = 0;
    for (auto &f : faces) {
        for (int i = 0; i < 4; i++)
            v << f.p[i][0] << f.p[i][1] << f.p[i][2] << f.nx << f.ny << f.nz;
        idx << base << base+1 << base+2 << base << base+2 << base+3;
        base += 4;
    }
    // lens: small cone in front (along -Z)
    const int segs = 18;
    float lr = 0.12f, ll = 0.15f;
    float sl = sqrtf(lr*lr + ll*ll);
    float nx_s = ll / sl, nz_s = lr / sl;
    base = v.size() / 6;
    for (int i = 0; i <= segs; i++) {
        float t = 2.f * float(M_PI) * i / segs;
        float cx = cosf(t), cy = sinf(t);
        v << cx*lr << cy*lr << -bd << cx*nz_s << cy*nz_s << -nx_s;
        v << 0.f << 0.f << -bd-ll << 0.f << 0.f << -1.f;
    }
    for (int i = 0; i < segs; i++) {
        quint32 b = base + i*2;
        idx << b << b+2 << b+1;
    }
}

// ================================================================
//  Gizmo geometry generation
// ================================================================
static void shiftVerticesY(QVector<float> &v, float dy)
{
    const int stride = 6;
    float *d = v.data();
    for (int i = 0; i < v.size(); i += stride)
        d[i + 1] += dy;
}

static void mergeGeometry(QVector<float> &dstV, QVector<quint32> &dstI,
                           const QVector<float> &srcV, const QVector<quint32> &srcI)
{
    quint32 base = dstV.size() / 6;
    dstV.append(srcV);
    for (quint32 idx : srcI)
        dstI.append(base + idx);
}

void UsdViewportItem::buildGizmoMeshes(QVector<GizmoMeshData> &out)
{
    out.resize(GizmoCount);

    // Generate Y-axis base geometry (shaft + cone)
    QVector<float> shaftV; QVector<quint32> shaftI;
    genCylinder(0.015f, 0.8f, shaftV, shaftI, false);
    shiftVerticesY(shaftV, 0.4f); // 0..0.8

    QVector<float> coneV; QVector<quint32> coneI;
    genCone(0.05f, 0.2f, coneV, coneI);
    shiftVerticesY(coneV, 0.9f); // 0.8..1.0

    // Y-axis
    out[AxisY].vertices = shaftV;
    out[AxisY].indices  = shaftI;
    mergeGeometry(out[AxisY].vertices, out[AxisY].indices, coneV, coneI);
    out[AxisY].color          = QVector3D(0.2f, 1.0f, 0.2f);
    out[AxisY].highlightColor = QVector3D(0.6f, 1.0f, 0.6f);

    // X-axis: Y→X swap (x,y,z) → (y,x,z)
    out[AxisX].vertices = out[AxisY].vertices;
    out[AxisX].indices  = out[AxisY].indices;
    {
        float *d = out[AxisX].vertices.data();
        int count = out[AxisX].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            std::swap(p[0], p[1]); // swap x,y positions
            std::swap(p[3], p[4]); // swap x,y normals
        }
    }
    out[AxisX].color          = QVector3D(1.0f, 0.2f, 0.2f);
    out[AxisX].highlightColor = QVector3D(1.0f, 0.6f, 0.6f);

    // Z-axis: Y→Z (x,y,z) → (x,-z,y)
    out[AxisZ].vertices = out[AxisY].vertices;
    out[AxisZ].indices  = out[AxisY].indices;
    {
        float *d = out[AxisZ].vertices.data();
        int count = out[AxisZ].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    }
    out[AxisZ].color          = QVector3D(0.3f, 0.5f, 1.0f);
    out[AxisZ].highlightColor = QVector3D(0.6f, 0.8f, 1.0f);

    // XY plane (normal Z): quad centered in XY square, range [0.5,0.65]
    {
        auto &m = out[PlaneXY];
        m.vertices << 0.5f << 0.5f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.65f << 0.5f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.65f << 0.65f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.5f << 0.65f << 0.f << 0.f << 0.f << 1.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.9f, 0.9f, 0.2f);
        m.highlightColor = QVector3D(1.0f, 1.0f, 0.6f);
    }

    // XZ plane (normal Y): quad centered in XZ square, range [0.5,0.65]
    {
        auto &m = out[PlaneXZ];
        m.vertices << 0.5f << 0.f << 0.5f << 0.f << 1.f << 0.f;
        m.vertices << 0.65f << 0.f << 0.5f << 0.f << 1.f << 0.f;
        m.vertices << 0.65f << 0.f << 0.65f << 0.f << 1.f << 0.f;
        m.vertices << 0.5f << 0.f << 0.65f << 0.f << 1.f << 0.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.9f, 0.5f, 0.9f);
        m.highlightColor = QVector3D(1.0f, 0.8f, 1.0f);
    }

    // YZ plane (normal X): quad centered in YZ square, range [0.5,0.65]
    {
        auto &m = out[PlaneYZ];
        m.vertices << 0.f << 0.5f << 0.5f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.65f << 0.5f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.65f << 0.65f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.5f << 0.65f << 1.f << 0.f << 0.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.2f, 0.9f, 0.9f);
        m.highlightColor = QVector3D(0.6f, 1.0f, 1.0f);
    }

    // Origin sphere
    {
        auto &m = out[Origin];
        genSphere(0.08f, m.vertices, m.indices);
        m.color          = QVector3D(0.9f, 0.9f, 0.9f);
        m.highlightColor = QVector3D(1.0f, 1.0f, 1.0f);
    }
}

// ================================================================
//  Torus geometry generator (ring in XZ plane around Y axis)
// ================================================================
static void genTorus(float majorR, float minorR,
                      QVector<float> &v, QVector<quint32> &idx)
{
    const int majorSegs = 64, minorSegs = 16;
    v.clear(); idx.clear();
    for (int i = 0; i <= majorSegs; i++) {
        float theta = 2.f * float(M_PI) * i / majorSegs;
        float ct = cosf(theta), st = sinf(theta);
        for (int j = 0; j <= minorSegs; j++) {
            float phi = 2.f * float(M_PI) * j / minorSegs;
            float cp = cosf(phi), sp = sinf(phi);
            float x = (majorR + minorR * cp) * ct;
            float y = minorR * sp;
            float z = (majorR + minorR * cp) * st;
            float nx = cp * ct;
            float ny = sp;
            float nz = cp * st;
            v << x << y << z << nx << ny << nz;
        }
    }
    for (int i = 0; i < majorSegs; i++) {
        for (int j = 0; j < minorSegs; j++) {
            quint32 a = i * (minorSegs + 1) + j;
            quint32 b = a + minorSegs + 1;
            idx << a << b << a + 1 << a + 1 << b << b + 1;
        }
    }
}

// ================================================================
//  Rotation gizmo: 3 torus rings
// ================================================================
void UsdViewportItem::buildRotateGizmoMeshes(QVector<GizmoMeshData> &out)
{
    out.resize(RotatePartCount);

    // Y-ring (green): torus in XZ plane around Y axis
    genTorus(0.9f, 0.02f, out[RotateRingY].vertices, out[RotateRingY].indices);
    out[RotateRingY].color          = QVector3D(0.2f, 1.0f, 0.2f);
    out[RotateRingY].highlightColor = QVector3D(0.6f, 1.0f, 0.6f);

    // X-ring (red): swap Y↔X
    out[RotateRingX].vertices = out[RotateRingY].vertices;
    out[RotateRingX].indices  = out[RotateRingY].indices;
    {
        float *d = out[RotateRingX].vertices.data();
        int count = out[RotateRingX].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            std::swap(p[0], p[1]); // swap x,y positions
            std::swap(p[3], p[4]); // swap x,y normals
        }
    }
    out[RotateRingX].color          = QVector3D(1.0f, 0.2f, 0.2f);
    out[RotateRingX].highlightColor = QVector3D(1.0f, 0.6f, 0.6f);

    // Z-ring (blue): Y→Z (x,y,z) → (x,-z,y)
    out[RotateRingZ].vertices = out[RotateRingY].vertices;
    out[RotateRingZ].indices  = out[RotateRingY].indices;
    {
        float *d = out[RotateRingZ].vertices.data();
        int count = out[RotateRingZ].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    }
    out[RotateRingZ].color          = QVector3D(0.3f, 0.5f, 1.0f);
    out[RotateRingZ].highlightColor = QVector3D(0.6f, 0.8f, 1.0f);
}

// ================================================================
//  Scale gizmo: 3 axes (shaft + cube tip), 3 planes, 1 origin cube
// ================================================================
void UsdViewportItem::buildScaleGizmoMeshes(QVector<GizmoMeshData> &out)
{
    out.resize(ScaleGizmoPartCount);

    // Generate Y-axis shaft only (no cube tip merged)
    QVector<float> shaftV; QVector<quint32> shaftI;
    genCylinder(0.015f, 0.8f, shaftV, shaftI, false);
    shiftVerticesY(shaftV, 0.4f); // 0..0.8

    // Generate cube tip geometry (separate)
    QVector<float> cubeV; QVector<quint32> cubeI;
    genCube(0.08f, cubeV, cubeI);
    shiftVerticesY(cubeV, 0.84f); // cube bottom at 0.8 = shaft top

    // Y-axis shaft
    out[AxisY].vertices = shaftV;
    out[AxisY].indices  = shaftI;
    out[AxisY].color          = QVector3D(0.2f, 1.0f, 0.2f);
    out[AxisY].highlightColor = QVector3D(0.6f, 1.0f, 0.6f);

    // X-axis shaft: Y→X swap
    out[AxisX].vertices = shaftV;
    out[AxisX].indices  = shaftI;
    {
        float *d = out[AxisX].vertices.data();
        int count = out[AxisX].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            std::swap(p[0], p[1]);
            std::swap(p[3], p[4]);
        }
    }
    out[AxisX].color          = QVector3D(1.0f, 0.2f, 0.2f);
    out[AxisX].highlightColor = QVector3D(1.0f, 0.6f, 0.6f);

    // Z-axis shaft: Y→Z
    out[AxisZ].vertices = shaftV;
    out[AxisZ].indices  = shaftI;
    {
        float *d = out[AxisZ].vertices.data();
        int count = out[AxisZ].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    }
    out[AxisZ].color          = QVector3D(0.3f, 0.5f, 1.0f);
    out[AxisZ].highlightColor = QVector3D(0.6f, 0.8f, 1.0f);

    // Planes (same as translate gizmo)
    // XY plane
    {
        auto &m = out[PlaneXY];
        m.vertices << 0.5f << 0.5f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.65f << 0.5f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.65f << 0.65f << 0.f << 0.f << 0.f << 1.f;
        m.vertices << 0.5f << 0.65f << 0.f << 0.f << 0.f << 1.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.9f, 0.9f, 0.2f);
        m.highlightColor = QVector3D(1.0f, 1.0f, 0.6f);
    }
    // XZ plane
    {
        auto &m = out[PlaneXZ];
        m.vertices << 0.5f << 0.f << 0.5f << 0.f << 1.f << 0.f;
        m.vertices << 0.65f << 0.f << 0.5f << 0.f << 1.f << 0.f;
        m.vertices << 0.65f << 0.f << 0.65f << 0.f << 1.f << 0.f;
        m.vertices << 0.5f << 0.f << 0.65f << 0.f << 1.f << 0.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.9f, 0.5f, 0.9f);
        m.highlightColor = QVector3D(1.0f, 0.8f, 1.0f);
    }
    // YZ plane
    {
        auto &m = out[PlaneYZ];
        m.vertices << 0.f << 0.5f << 0.5f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.65f << 0.5f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.65f << 0.65f << 1.f << 0.f << 0.f;
        m.vertices << 0.f << 0.5f << 0.65f << 1.f << 0.f << 0.f;
        m.indices << 0 << 1 << 2 << 0 << 2 << 3;
        m.color          = QVector3D(0.2f, 0.9f, 0.9f);
        m.highlightColor = QVector3D(0.6f, 1.0f, 1.0f);
    }
    // Origin cube (uniform scale)
    {
        auto &m = out[Origin];
        genCube(0.12f, m.vertices, m.indices);
        m.color          = QVector3D(0.9f, 0.9f, 0.9f);
        m.highlightColor = QVector3D(1.0f, 1.0f, 1.0f);
    }

    // Cube tips as separate parts (for sliding during drag)
    // Y-axis cube tip
    out[ScaleCubeTipY].vertices = cubeV;
    out[ScaleCubeTipY].indices  = cubeI;
    out[ScaleCubeTipY].color          = QVector3D(0.2f, 1.0f, 0.2f);
    out[ScaleCubeTipY].highlightColor = QVector3D(0.6f, 1.0f, 0.6f);

    // X-axis cube tip: Y→X swap
    out[ScaleCubeTipX].vertices = cubeV;
    out[ScaleCubeTipX].indices  = cubeI;
    {
        float *d = out[ScaleCubeTipX].vertices.data();
        int count = out[ScaleCubeTipX].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            std::swap(p[0], p[1]);
            std::swap(p[3], p[4]);
        }
    }
    out[ScaleCubeTipX].color          = QVector3D(1.0f, 0.2f, 0.2f);
    out[ScaleCubeTipX].highlightColor = QVector3D(1.0f, 0.6f, 0.6f);

    // Z-axis cube tip: Y→Z
    out[ScaleCubeTipZ].vertices = cubeV;
    out[ScaleCubeTipZ].indices  = cubeI;
    {
        float *d = out[ScaleCubeTipZ].vertices.data();
        int count = out[ScaleCubeTipZ].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    }
    out[ScaleCubeTipZ].color          = QVector3D(0.3f, 0.5f, 1.0f);
    out[ScaleCubeTipZ].highlightColor = QVector3D(0.6f, 0.8f, 1.0f);
}

// ================================================================
//  Orientation indicator geometry (3 axes: X, Y, Z)
// ================================================================
void UsdViewportItem::buildOrientAxesMeshes(QVector<GizmoMeshData> &out)
{
    out.resize(3); // X=0, Y=1, Z=2

    // Generate Y-axis base geometry (shaft + cone), same proportions as gizmo
    QVector<float> shaftV; QVector<quint32> shaftI;
    genCylinder(0.03f, 0.7f, shaftV, shaftI, false);
    shiftVerticesY(shaftV, 0.35f); // 0..0.7

    QVector<float> coneV; QVector<quint32> coneI;
    genCone(0.08f, 0.2f, coneV, coneI);
    shiftVerticesY(coneV, 0.8f); // 0.7..0.9

    // Y-axis (green)
    out[1].vertices = shaftV;
    out[1].indices  = shaftI;
    mergeGeometry(out[1].vertices, out[1].indices, coneV, coneI);
    out[1].color          = QVector3D(0.2f, 1.0f, 0.2f);
    out[1].highlightColor = out[1].color;

    // X-axis (red): swap Y↔X
    out[0].vertices = out[1].vertices;
    out[0].indices  = out[1].indices;
    {
        float *d = out[0].vertices.data();
        int count = out[0].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            std::swap(p[0], p[1]);
            std::swap(p[3], p[4]);
        }
    }
    out[0].color          = QVector3D(1.0f, 0.2f, 0.2f);
    out[0].highlightColor = out[0].color;

    // Z-axis (blue): Y→Z  (x,y,z) → (x,-z,y)
    out[2].vertices = out[1].vertices;
    out[2].indices  = out[1].indices;
    {
        float *d = out[2].vertices.data();
        int count = out[2].vertices.size() / 6;
        for (int i = 0; i < count; ++i) {
            float *p = d + i * 6;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    }
    out[2].color          = QVector3D(0.3f, 0.5f, 1.0f);
    out[2].highlightColor = out[2].color;
}

// ================================================================
//  Grid mesh generation (5 groups: micro, small, large lines, axis1, axis2)
// ================================================================
void UsdViewportItem::buildGridMeshes()
{
    m_gridMeshes.clear();
    m_gridMeshes.resize(5); // 0=micro(1u), 1=small(10u), 2=large(100u), 3=axis1, 4=axis2

    // Scale grid to scene units: base is ±10m in internal units (cm)
    const float extent = 1000.f * qMax(1.f, m_unitScale);
    const int microStep = qMax(1, int(1.f * m_unitScale));     // micro grid (1 unit)
    const int smallStep = qMax(1, int(10.f * m_unitScale));    // small grid (10 units)
    const int largeStep = qMax(10, int(100.f * m_unitScale));  // large grid (100 units)
    const int stride = 6;

    auto addLine = [&](GizmoMeshData &gm, float x0, float y0, float z0,
                       float x1, float y1, float z1) {
        quint32 base = gm.vertices.size() / stride;
        gm.vertices << x0 << y0 << z0 << 0.f << 0.f << 0.f;
        gm.vertices << x1 << y1 << z1 << 0.f << 0.f << 0.f;
        gm.indices << base << base + 1;
    };

    for (int i = -int(extent); i <= int(extent); i += microStep) {
        if (i == 0) continue; // axis lines handled separately

        // Classify into micro / small / large
        int groupIdx;
        if (i % largeStep == 0)
            groupIdx = 2; // large
        else if (i % smallStep == 0)
            groupIdx = 1; // small
        else
            groupIdx = 0; // micro

        auto &group = m_gridMeshes[groupIdx];
        float fi = float(i);
        if (m_zUp) {
            addLine(group, -extent, fi, 0.f, extent, fi, 0.f);
            addLine(group, fi, -extent, 0.f, fi, extent, 0.f);
        } else {
            addLine(group, -extent, 0.f, fi, extent, 0.f, fi);
            addLine(group, fi, 0.f, -extent, fi, 0.f, extent);
        }
    }

    // Micro grid color (very faint)
    m_gridMeshes[0].color = QVector3D(0.10f, 0.10f, 0.10f);
    m_gridMeshes[0].highlightColor = m_gridMeshes[0].color;
    // Small grid color
    m_gridMeshes[1].color = QVector3D(0.18f, 0.18f, 0.18f);
    m_gridMeshes[1].highlightColor = m_gridMeshes[1].color;
    // Large grid color
    m_gridMeshes[2].color = QVector3D(0.30f, 0.30f, 0.30f);
    m_gridMeshes[2].highlightColor = m_gridMeshes[2].color;

    // Axis lines through origin
    if (m_zUp) {
        // X axis (red)
        addLine(m_gridMeshes[3], -extent, 0.f, 0.f, extent, 0.f, 0.f);
        m_gridMeshes[3].color = QVector3D(1.0f, 0.2f, 0.2f);
        m_gridMeshes[3].highlightColor = m_gridMeshes[3].color;
        // Y axis (green)
        addLine(m_gridMeshes[4], 0.f, -extent, 0.f, 0.f, extent, 0.f);
        m_gridMeshes[4].color = QVector3D(0.2f, 1.0f, 0.2f);
        m_gridMeshes[4].highlightColor = m_gridMeshes[4].color;
    } else {
        // X axis (red)
        addLine(m_gridMeshes[3], -extent, 0.f, 0.f, extent, 0.f, 0.f);
        m_gridMeshes[3].color = QVector3D(1.0f, 0.2f, 0.2f);
        m_gridMeshes[3].highlightColor = m_gridMeshes[3].color;
        // Z axis (blue)
        addLine(m_gridMeshes[4], 0.f, 0.f, -extent, 0.f, 0.f, extent);
        m_gridMeshes[4].color = QVector3D(0.3f, 0.5f, 1.0f);
        m_gridMeshes[4].highlightColor = m_gridMeshes[4].color;
    }

    m_gridDirty = true;
}

void UsdViewportItem::setShowGrid(bool on)
{
    if (m_showGrid == on) return;
    m_showGrid = on;
    if (on) buildGridMeshes();
    m_meshDirty = true;
    update();
    emit showGridChanged();
}

void UsdViewportItem::setSnapEnabled(bool on)
{
    if (m_snapEnabled == on) return;
    m_snapEnabled = on;
    emit snapEnabledChanged();
}

void UsdViewportItem::setCollisionDisplayMode(int mode)
{
    mode = qBound(0, mode, 2);
    if (m_collisionDisplayMode == mode) return;
    m_collisionDisplayMode = mode;
    buildMeshes();          // rebuilds m_meshes, sets m_meshDirty = true
    update();               // schedule a re-render
    emit collisionDisplayModeChanged();
}

// Rotate vertices generated with Y-up to match the USD prim's axis attribute.
// Generators produce geometry with height along Y; this swizzles to X or Z when needed.
static void rotateVertsForAxis(QVector<float> &v, const TfToken &axis)
{
    if (axis == UsdGeomTokens->y) return; // already Y-up
    const int stride = 6; // x,y,z,nx,ny,nz
    float *d = v.data();
    const int count = v.size() / stride;
    if (axis == UsdGeomTokens->z) {
        // Y-up → Z-up: (x,y,z) → (x,-z,y)
        for (int i = 0; i < count; ++i) {
            float *p = d + i * stride;
            float oy = p[1], oz = p[2];
            p[1] = -oz; p[2] = oy;
            float ny = p[4], nz = p[5];
            p[4] = -nz; p[5] = ny;
        }
    } else { // "X"
        // Y-up → X-up: (x,y,z) → (y,x,z) — swap x and y
        for (int i = 0; i < count; ++i) {
            float *p = d + i * stride;
            float ox = p[0], oy = p[1];
            p[0] = oy; p[1] = ox;
            float nx = p[3], ny = p[4];
            p[3] = ny; p[4] = nx;
        }
    }
}

// Triangulate a USD polygon mesh.
// Normals: per-face flat if not authored, otherwise use authored normals.
static void genMesh(const UsdGeomMesh &mesh,
                     QVector<float> &v, QVector<quint32> &idx)
{
    v.clear(); idx.clear();

    VtArray<GfVec3f> points;
    mesh.GetPointsAttr().Get(&points);
    if (points.empty()) return;

    VtArray<int> fvcArray, fviArray;
    mesh.GetFaceVertexCountsAttr().Get(&fvcArray);
    mesh.GetFaceVertexIndicesAttr().Get(&fviArray);
    if (fvcArray.empty() || fviArray.empty()) return;

    // Authored normals (optional)
    VtArray<GfVec3f> normals;
    TfToken normInterp;
    mesh.GetNormalsAttr().Get(&normals);
    normInterp = mesh.GetNormalsInterpolation();
    bool hasNormals = !normals.empty();

    // Detect degenerate normals: all identical (e.g. cloth sim rest-pose all (0,0,1))
    if (hasNormals && normals.size() > 1) {
        const GfVec3f &first = normals[0];
        bool allSame = true;
        for (size_t i = 1; i < normals.size(); ++i) {
            if ((normals[i] - first).GetLengthSq() > 1e-6f) {
                allSame = false;
                break;
            }
        }
        if (allSame)
            hasNormals = false; // discard uniform normals
    }

    // Compute area-weighted smooth vertex normals when authored normals are
    // missing or degenerate. This gives correct shading for cloth meshes
    // and any other mesh without reliable normal data.
    if (!hasNormals) {
        normals = VtArray<GfVec3f>(points.size(), GfVec3f(0));
        int offset = 0;
        for (int fvc : fvcArray) {
            if (fvc < 3) { offset += fvc; continue; }
            const GfVec3f &p0 = points[fviArray[offset]];
            const GfVec3f &p1 = points[fviArray[offset + 1]];
            const GfVec3f &p2 = points[fviArray[offset + 2]];
            GfVec3f fn = GfCross(p1 - p0, p2 - p0); // length = 2*area → area-weighted
            for (int k = 0; k < fvc; ++k) {
                int vi = fviArray[offset + k];
                if (vi >= 0 && vi < (int)normals.size())
                    normals[vi] += fn;
            }
            offset += fvc;
        }
        for (auto &n : normals) {
            if (n.GetLengthSq() > 1e-10f) n.Normalize();
            else n = GfVec3f(0, 1, 0);
        }

        // Fix inverted normals: if majority of normals point toward the mesh
        // centroid they are inside-out (CW winding under rightHanded convention).
        GfVec3f centroid(0);
        for (const auto &p : points) centroid += p;
        centroid /= float(points.size());
        int inwardCount = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            if (GfDot(normals[i], centroid - points[i]) > 0)
                ++inwardCount;
        }
        if (inwardCount > int(points.size()) / 2) {
            for (auto &n : normals) n = -n;
        }

        normInterp = UsdGeomTokens->vertex;
        hasNormals = true;
    }

    int fviOffset = 0;
    int faceIdx   = 0;
    for (int fvc : fvcArray) {
        if (fvc < 3) { fviOffset += fvc; ++faceIdx; continue; }

        // Fan triangulation from first vertex
        for (int tri = 1; tri < fvc - 1; tri++) {
            for (int k : {0, tri, tri + 1}) {
                int vi = fviArray[fviOffset + k];
                GfVec3f p = points[vi];
                GfVec3f n(0, 1, 0);
                if (normInterp == UsdGeomTokens->faceVarying)
                    n = normals[fviOffset + k];
                else if (normInterp == UsdGeomTokens->vertex)
                    n = (vi < (int)normals.size()) ? normals[vi] : n;
                else // uniform / constant
                    n = (faceIdx < (int)normals.size()) ? normals[faceIdx] : n;
                if (n.GetLengthSq() > 1e-10f) n.Normalize();
                quint32 base = v.size() / 6;
                v << p[0] << p[1] << p[2] << n[0] << n[1] << n[2];
                idx << base;
            }
        }
        fviOffset += fvc;
        ++faceIdx;
    }
}

// ================================================================
//  Color palette for meshes without displayColor
// ================================================================
static QVector3D colorForMesh(int index)
{
    static const QVector3D palette[] = {
        {0.90f, 0.35f, 0.25f},  // red
        {0.25f, 0.65f, 0.90f},  // blue
        {0.35f, 0.80f, 0.40f},  // green
        {0.95f, 0.75f, 0.20f},  // yellow
        {0.70f, 0.40f, 0.85f},  // purple
        {0.95f, 0.55f, 0.25f},  // orange
        {0.30f, 0.80f, 0.78f},  // teal
        {0.85f, 0.45f, 0.65f},  // pink
        {0.55f, 0.70f, 0.30f},  // olive
        {0.50f, 0.55f, 0.85f},  // lavender
    };
    constexpr int N = sizeof(palette) / sizeof(palette[0]);
    return palette[index % N];
}

// ================================================================
//  Smooth normals: average normals at coincident vertex positions
// ================================================================
#include <QHash>
#include <unordered_map>

struct QuantizedPos {
    qint32 x, y, z;
    bool operator==(const QuantizedPos &o) const { return x == o.x && y == o.y && z == o.z; }
};

namespace std {
template<> struct hash<QuantizedPos> {
    size_t operator()(const QuantizedPos &p) const {
        size_t h = std::hash<qint32>()(p.x);
        h ^= std::hash<qint32>()(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<qint32>()(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
}

static QuantizedPos quantize(float x, float y, float z)
{
    // Quantize to ~1e-4 resolution to merge coincident vertices
    return { qint32(qRound64(double(x) * 10000.0)),
             qint32(qRound64(double(y) * 10000.0)),
             qint32(qRound64(double(z) * 10000.0)) };
}

static void computeSmoothNormals(MeshData &md)
{
    const int stride = 6;
    const int vertCount = md.vertices.size() / stride;
    const float *src = md.vertices.constData();

    // Pass 1: accumulate normals per unique position (collision-free)
    struct AccumNorm { float nx = 0, ny = 0, nz = 0; };
    std::unordered_map<QuantizedPos, AccumNorm> accum;
    accum.reserve(vertCount);

    for (int i = 0; i < vertCount; ++i) {
        float px = src[i * stride + 0];
        float py = src[i * stride + 1];
        float pz = src[i * stride + 2];
        float nx = src[i * stride + 3];
        float ny = src[i * stride + 4];
        float nz = src[i * stride + 5];
        QuantizedPos key = quantize(px, py, pz);
        auto &a = accum[key];
        a.nx += nx; a.ny += ny; a.nz += nz;
    }

    // Pass 2: build smoothVertices with averaged normals
    md.smoothVertices.resize(md.vertices.size());
    float *dst = md.smoothVertices.data();
    for (int i = 0; i < vertCount; ++i) {
        float px = src[i * stride + 0];
        float py = src[i * stride + 1];
        float pz = src[i * stride + 2];
        dst[i * stride + 0] = px;
        dst[i * stride + 1] = py;
        dst[i * stride + 2] = pz;

        QuantizedPos key = quantize(px, py, pz);
        auto &a = accum[key];
        float nx = a.nx, ny = a.ny, nz = a.nz;
        float len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (len > 1e-7f) { nx /= len; ny /= len; nz /= len; }
        dst[i * stride + 3] = nx;
        dst[i * stride + 4] = ny;
        dst[i * stride + 5] = nz;
    }
}

// ================================================================
//  UsdViewportItem (GUI thread)
// ================================================================
UsdViewportItem::UsdViewportItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
{
    setSampleCount(4);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    buildGizmoMeshes(m_translateGizmoMeshes);
    buildRotateGizmoMeshes(m_rotateGizmoMeshes);
    buildScaleGizmoMeshes(m_scaleGizmoMeshes);
    buildOrientAxesMeshes(m_orientAxesMeshes);
    buildGridMeshes();
    updateCamera();
}

QQuickRhiItemRenderer *UsdViewportItem::createRenderer()
{
    return new UsdViewportRenderer;
}

void UsdViewportItem::setDocument(UsdDocument *doc)
{
    if (m_doc == doc) return;
    if (m_doc) disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (m_doc) {
        connect(m_doc, &UsdDocument::primPathsChanged,
                this, &UsdViewportItem::onDocumentChanged);
        connect(m_doc, &UsdDocument::stageModified,
                this, [this]() { onDocumentChanged(); });
        connect(m_doc, &UsdDocument::filePathChanged,
                this, [this]{ m_cameraInitialized = false; });
    }
    m_cameraInitialized = false;
    onDocumentChanged();
    emit documentChanged();
}

void UsdViewportItem::onDocumentChanged()
{
    // Skip rebuild during gizmo drag — we update transforms directly
    if (m_gizmoDragPart >= 0) return;
    buildMeshes();
    // Reset gizmo when file closed
    if (!m_doc || !m_doc->isOpen()) {
        setGizmoMode(GizmoModeNone);
        m_selectedMeshes.clear();
        m_anchorMeshIdx = -1;
        emit selectedPrimPathsChanged();
    }
    // Recompute gizmo position from updated mesh transforms
    updateGizmoPosition();
    update();
}

// ================================================================
//  Sparse collision wireframe generators (characteristic lines only)
// ================================================================
static void addCircle(QVector<float> &v, QVector<quint32> &idx,
                      float r, float cx, float cy, float cz,
                      int axis, int N = 24) // axis: 0=X,1=Y,2=Z normal
{
    quint32 base = v.size() / 6;
    for (int i = 0; i < N; ++i) {
        float a = 2.f * float(M_PI) * i / N;
        float c = r * cosf(a), s = r * sinf(a);
        float x = cx, y = cy, z = cz;
        if (axis == 0)      { y += c; z += s; }
        else if (axis == 1) { x += c; z += s; }
        else                { x += c; y += s; }
        v << x << y << z << 0 << 0 << 0;
    }
    for (int i = 0; i < N; ++i)
        idx << base + i << base + (i + 1) % N;
}

static void genCollisionWireSphere(float r, QVector<float> &v, QVector<quint32> &idx)
{
    addCircle(v, idx, r, 0, 0, 0, 0);   // YZ plane
    addCircle(v, idx, r, 0, 0, 0, 1);   // XZ plane
    addCircle(v, idx, r, 0, 0, 0, 2);   // XY plane
}

static void genCollisionWireCube(float size, QVector<float> &v, QVector<quint32> &idx)
{
    float h = size * 0.5f;
    // 8 corners
    quint32 base = v.size() / 6;
    float corners[][3] = {
        {-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},
        {-h,-h, h},{h,-h, h},{h,h, h},{-h,h, h}
    };
    for (auto &c : corners)
        v << c[0] << c[1] << c[2] << 0 << 0 << 0;
    // 12 edges
    int edges[][2] = {
        {0,1},{1,2},{2,3},{3,0},  // bottom
        {4,5},{5,6},{6,7},{7,4},  // top
        {0,4},{1,5},{2,6},{3,7}   // verticals
    };
    for (auto &e : edges)
        idx << base + e[0] << base + e[1];
}

static void genCollisionWireCylinder(float r, float h, int axisUp,
                                     QVector<float> &v, QVector<quint32> &idx)
{
    // top & bottom circles + 4 vertical lines
    float halfH = h * 0.5f;
    float lo = -halfH, hi = halfH;
    int circleAxis = (axisUp == 0) ? 0 : (axisUp == 2) ? 2 : 1; // normal = up axis
    float cx0 = 0, cy0 = 0, cz0 = 0;
    float cx1 = 0, cy1 = 0, cz1 = 0;
    if (axisUp == 0)      { cx0 = lo; cx1 = hi; }
    else if (axisUp == 2) { cz0 = lo; cz1 = hi; }
    else                  { cy0 = lo; cy1 = hi; }
    addCircle(v, idx, r, cx0, cy0, cz0, circleAxis);
    addCircle(v, idx, r, cx1, cy1, cz1, circleAxis);
    // 4 vertical lines
    const int N = 4;
    for (int i = 0; i < N; ++i) {
        float a = 2.f * float(M_PI) * i / N;
        float c = r * cosf(a), s = r * sinf(a);
        quint32 base = v.size() / 6;
        float p0[3] = {0,0,0}, p1[3] = {0,0,0};
        if (axisUp == 0)      { p0[0]=lo; p0[1]=c; p0[2]=s; p1[0]=hi; p1[1]=c; p1[2]=s; }
        else if (axisUp == 2) { p0[0]=c; p0[1]=s; p0[2]=lo; p1[0]=c; p1[1]=s; p1[2]=hi; }
        else                  { p0[0]=c; p0[1]=lo; p0[2]=s; p1[0]=c; p1[1]=hi; p1[2]=s; }
        v << p0[0] << p0[1] << p0[2] << 0 << 0 << 0;
        v << p1[0] << p1[1] << p1[2] << 0 << 0 << 0;
        idx << base << base + 1;
    }
}

static void genCollisionWireCone(float r, float h, int axisUp,
                                 QVector<float> &v, QVector<quint32> &idx)
{
    float halfH = h * 0.5f;
    float baseCtr[3] = {0,0,0}, apex[3] = {0,0,0};
    int circleAxis = 1;
    if (axisUp == 0)      { baseCtr[0]=-halfH; apex[0]=halfH; circleAxis=0; }
    else if (axisUp == 2) { baseCtr[2]=-halfH; apex[2]=halfH; circleAxis=2; }
    else                  { baseCtr[1]=-halfH; apex[1]=halfH; circleAxis=1; }
    addCircle(v, idx, r, baseCtr[0], baseCtr[1], baseCtr[2], circleAxis);
    // 4 lines from base to apex
    quint32 apexIdx = v.size() / 6;
    v << apex[0] << apex[1] << apex[2] << 0 << 0 << 0;
    const int N = 4;
    for (int i = 0; i < N; ++i) {
        float a = 2.f * float(M_PI) * i / N;
        float c = r * cosf(a), s = r * sinf(a);
        quint32 bi = v.size() / 6;
        float p[3] = {baseCtr[0], baseCtr[1], baseCtr[2]};
        if (circleAxis == 0)      { p[1]+=c; p[2]+=s; }
        else if (circleAxis == 2) { p[0]+=c; p[1]+=s; }
        else                      { p[0]+=c; p[2]+=s; }
        v << p[0] << p[1] << p[2] << 0 << 0 << 0;
        idx << bi << apexIdx;
    }
}

static void genCollisionWireCapsule(float r, float h, int axisUp,
                                    QVector<float> &v, QVector<quint32> &idx)
{
    // Cylinder body + 2 hemisphere arcs
    genCollisionWireCylinder(r, h, axisUp, v, idx);
    float halfH = h * 0.5f;
    // hemisphere arcs at each end (2 arcs per end)
    for (int end = 0; end < 2; ++end) {
        float sign = end == 0 ? -1.f : 1.f;
        for (int arc = 0; arc < 2; ++arc) {
            quint32 base = v.size() / 6;
            const int N = 12;
            for (int i = 0; i <= N; ++i) {
                float a = float(M_PI) * 0.5f * i / N; // 0 to pi/2
                float ra = r * cosf(a);
                float ha = sign * (halfH + r * sinf(a));
                float p[3] = {0,0,0};
                if (axisUp == 1) {
                    p[1] = ha;
                    if (arc == 0) { p[0] = ra; } else { p[2] = ra; }
                } else if (axisUp == 2) {
                    p[2] = ha;
                    if (arc == 0) { p[0] = ra; } else { p[1] = ra; }
                } else {
                    p[0] = ha;
                    if (arc == 0) { p[1] = ra; } else { p[2] = ra; }
                }
                v << p[0] << p[1] << p[2] << 0 << 0 << 0;
                if (i > 0) idx << base + i - 1 << base + i;
            }
        }
    }
}

static void genCollisionWirePlane(float w, float l, int axisUp,
                                  QVector<float> &v, QVector<quint32> &idx)
{
    float hw = w * 0.5f, hl = l * 0.5f;
    quint32 base = v.size() / 6;
    float corners[4][3];
    if (axisUp == 1)      { float c[][3]={{-hw,0,-hl},{hw,0,-hl},{hw,0,hl},{-hw,0,hl}}; memcpy(corners,c,sizeof(c)); }
    else if (axisUp == 2) { float c[][3]={{-hw,-hl,0},{hw,-hl,0},{hw,hl,0},{-hw,hl,0}}; memcpy(corners,c,sizeof(c)); }
    else                  { float c[][3]={{0,-hw,-hl},{0,hw,-hl},{0,hw,hl},{0,-hw,hl}}; memcpy(corners,c,sizeof(c)); }
    for (auto &c : corners)
        v << c[0] << c[1] << c[2] << 0 << 0 << 0;
    idx << base+0 << base+1 << base+1 << base+2 << base+2 << base+3 << base+3 << base+0;
}

void UsdViewportItem::buildMeshes()
{
    m_meshes.clear();
    if (!m_doc || !m_doc->isOpen()) { m_meshDirty = true; return; }

    auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
    if (!stageRef || !*stageRef) { m_meshDirty = true; return; }
    const UsdStageRefPtr &stage = *stageRef;

    // Detect stage up-axis
    bool oldZUp = m_zUp;
    m_zUp = (UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z);

    // Unit scale: convert stage units to centimeters
    double metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
    float oldUnitScale = m_unitScale;
    m_unitScale = float(metersPerUnit / 0.01);

    // Rebuild grid if up-axis or unit scale changed
    if (m_showGrid && (m_zUp != oldZUp || m_unitScale != oldUnitScale))
        buildGridMeshes();

    // Derive unit label from metersPerUnit
    auto unitLabel = [](double mpu) -> QString {
        constexpr struct { double mpu; const char *label; } known[] = {
            { 0.001,    "mm" },
            { 0.01,     "cm" },
            { 0.0254,   "in" },
            { 0.1,      "dm" },
            { 0.3048,   "ft" },
            { 0.9144,   "yd" },
            { 1.0,      "m"  },
            { 1000.0,   "km" },
            { 1609.344, "mi" },
        };
        for (auto &u : known) {
            if (std::abs(mpu - u.mpu) / std::max(mpu, u.mpu) < 1e-6)
                return QLatin1String(u.label);
        }
        return QStringLiteral("%1 m").arg(mpu);
    };
    QString newLabel = unitLabel(metersPerUnit);
    if (m_stageUnitLabel != newLabel) {
        m_stageUnitLabel = newLabel;
        emit stageUnitLabelChanged();
    }
    QMatrix4x4 unitScaleMat;
    unitScaleMat.scale(m_unitScale);

    // XformCache for world transforms (time = default)
    UsdGeomXformCache xfCache;

    int meshColorIndex = 0;
    for (const UsdPrim &prim : stage->Traverse(UsdTraverseInstanceProxies())) {
        const TfToken type = prim.GetTypeName();
        bool isLightType = (type == "SphereLight" || type == "RectLight" ||
                            type == "DiskLight"   || type == "CylinderLight" ||
                            type == "DistantLight" || type == "DomeLight");

        // Check visibility — light prims are still built (for selection/gizmo)
        // even when invisible, but non-light prims are skipped.
        UsdGeomImageable img(prim);
        bool primInvisible = img && img.ComputeVisibility() == UsdGeomTokens->invisible;
        if (primInvisible && !isLightType)
            continue;

        // Detect collision prims via PhysicsCollisionAPI.
        // PhysicsMeshCollisionAPI = convex hull source mesh → skip (not the actual shape).
        bool isCollision = false;
        bool hasCollisionAPI = false;
        if (!isLightType) {
            bool hasMeshCollisionAPI = false;
            for (const TfToken &schema : prim.GetAppliedSchemas()) {
                const auto &s = schema.GetString();
                if (s.find("PhysicsCollisionAPI") != std::string::npos)
                    hasCollisionAPI = true;
                if (s.find("PhysicsMeshCollisionAPI") != std::string::npos)
                    hasMeshCollisionAPI = true;
            }
            if (hasMeshCollisionAPI)
                hasCollisionAPI = false;
        }

        // Skip prims with purpose != "default" (guide/proxy/render),
        // unless it's a collision prim (PhysicsCollisionAPI) with display enabled.
        if (img && !isLightType) {
            TfToken purpose = img.ComputePurpose();
            if (!purpose.IsEmpty() && purpose != UsdGeomTokens->default_) {
                if (hasCollisionAPI && m_collisionDisplayMode > 0)
                    isCollision = true;
                else
                    continue;
            }
        }

        QVector<float>   verts;
        QVector<quint32> indices;
        bool isLineOnly = false;

        if (type == "Sphere") {
            double r = 1.0; UsdGeomSphere(prim).GetRadiusAttr().Get(&r);
            genSphere(float(r), verts, indices);
        } else if (type == "Cube") {
            double s = 2.0; UsdGeomCube(prim).GetSizeAttr().Get(&s);
            genCube(float(s), verts, indices);
        } else if (type == "Cylinder") {
            double h = 2.0, r = 0.5; TfToken axis;
            UsdGeomCylinder cyl(prim);
            cyl.GetHeightAttr().Get(&h);
            cyl.GetRadiusAttr().Get(&r);
            cyl.GetAxisAttr().Get(&axis);
            genCylinder(float(r), float(h), verts, indices);
            rotateVertsForAxis(verts, axis);
        } else if (type == "Cone") {
            double h = 2.0, r = 0.5; TfToken axis;
            UsdGeomCone cone(prim);
            cone.GetHeightAttr().Get(&h);
            cone.GetRadiusAttr().Get(&r);
            cone.GetAxisAttr().Get(&axis);
            genCone(float(r), float(h), verts, indices);
            rotateVertsForAxis(verts, axis);
        } else if (type == "Capsule") {
            double h = 1.0, r = 0.5; TfToken axis;
            UsdGeomCapsule cap(prim);
            cap.GetHeightAttr().Get(&h);
            cap.GetRadiusAttr().Get(&r);
            cap.GetAxisAttr().Get(&axis);
            genCapsule(float(r), float(h), verts, indices);
            rotateVertsForAxis(verts, axis);
        } else if (type == "Plane") {
            double w = 2.0, l = 2.0; TfToken axis;
            UsdGeomPlane plane(prim);
            plane.GetWidthAttr().Get(&w);
            plane.GetLengthAttr().Get(&l);
            plane.GetAxisAttr().Get(&axis);
            genPlane(float(w), float(l), verts, indices);
            rotateVertsForAxis(verts, axis);
        } else if (type == "Mesh") {
            genMesh(UsdGeomMesh(prim), verts, indices);

        // ── Light types ──
        // Light gizmo sizes reflect actual USD light attributes (width, height, radius).
        } else if (type == "SphereLight") {
            double r = 0.5;
            UsdLuxSphereLight(prim).GetRadiusAttr().Get(&r);
            genSphereLightGizmo(float(r), verts, indices);
            isLineOnly = true;
        } else if (type == "RectLight") {
            float w = 1.0f, h = 1.0f;
            UsdLuxRectLight rectLight(prim);
            rectLight.GetWidthAttr().Get(&w);
            rectLight.GetHeightAttr().Get(&h);
            qDebug() << "RectLight" << prim.GetPath().GetText() << "w=" << w << "h=" << h;
            // Generate unit gizmo then scale: X by width, Y by height, Z by avg
            genRectLightGizmo(1.0f, verts, indices);
            {
                float *d = verts.data();
                int count = verts.size() / 6;
                for (int vi = 0; vi < count; ++vi) {
                    float *p = d + vi * 6;
                    p[0] *= w;   // X by width
                    p[1] *= h;   // Y by height
                    p[2] *= (w + h) * 0.5f; // Z (rays) by average
                }
            }
            isLineOnly = true;
        } else if (type == "DiskLight") {
            float r = 0.5f;
            UsdLuxDiskLight diskLight(prim);
            diskLight.GetRadiusAttr().Get(&r);
            qDebug() << "DiskLight" << prim.GetPath().GetText() << "r=" << r;
            genDiskLightGizmo(r, verts, indices);
            isLineOnly = true;
        } else if (type == "CylinderLight") {
            float r = 0.5f, len = 1.0f;
            UsdLuxCylinderLight cylLight(prim);
            cylLight.GetRadiusAttr().Get(&r);
            cylLight.GetLengthAttr().Get(&len);
            genCylinderLightGizmo(r, len, verts, indices);
            isLineOnly = true;
        } else if (type == "DistantLight") {
            float ps = 1.f / m_unitScale;
            genDistantLightGizmo(0.3f * ps, 0.6f * ps, verts, indices);
            isLineOnly = true;
        } else if (type == "DomeLight") {
            float ps = 1.f / m_unitScale;
            genDomeLightGizmo(1.0f * ps, verts, indices);
            isLineOnly = true;

        // ── Camera ──
        } else if (type == "Camera") {
            genCameraFrustum(verts, indices);
        } else {
            continue;
        }

        if (indices.isEmpty()) continue;

        MeshData md;
        md.vertices = std::move(verts);
        md.indices  = std::move(indices);
        md.lineOnly = isLineOnly;

        // Display color — lights/camera use fixed colors, geometry uses displayColor
        static const QVector3D lightColor(1.0f, 0.84f, 0.0f);  // warm yellow
        static const QVector3D cameraColor(0.5f, 0.6f, 0.7f);  // blue-gray
        bool isLight = (type == "SphereLight" || type == "RectLight" ||
                        type == "DiskLight"   || type == "CylinderLight" ||
                        type == "DistantLight" || type == "DomeLight");
        md.isLightGizmo = isLight;
        md.isCollision = isCollision;
        md.hasCollisionAPI = hasCollisionAPI || isCollision;
        if (isCollision) {
            md.color = QVector3D(0.0f, 0.8f, 0.0f); // green wireframe
        } else if (isLight) {
            md.color = lightColor;
        } else if (type == "Camera") {
            md.color = cameraColor;
        } else {
            UsdGeomGprim gprim(prim);
            VtArray<GfVec3f> colors;
            gprim.GetDisplayColorAttr().Get(&colors);
            md.color = colors.empty()
                       ? colorForMesh(meshColorIndex)
                       : QVector3D(colors[0][0], colors[0][1], colors[0][2]);
        }
        meshColorIndex++;

        // World transform via XformCache
        bool resetXform = false;
        GfMatrix4d xf = xfCache.GetLocalToWorldTransform(prim);
        float mf[16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                mf[c*4+r] = float(xf[r][c]);
        md.transform = unitScaleMat * QMatrix4x4(mf);

        // Extract unique edges from triangle indices (skip for line-only meshes)
        if (!md.lineOnly) {
            QSet<quint64> edgeSet;
            for (int i = 0; i + 2 < md.indices.size(); i += 3) {
                quint32 i0 = md.indices[i], i1 = md.indices[i+1], i2 = md.indices[i+2];
                auto addEdge = [&](quint32 a, quint32 b) {
                    quint32 lo = qMin(a, b), hi = qMax(a, b);
                    quint64 key = (quint64(lo) << 32) | hi;
                    if (!edgeSet.contains(key)) {
                        edgeSet.insert(key);
                        md.edges << lo << hi;
                    }
                };
                addEdge(i0, i1);
                addEdge(i1, i2);
                addEdge(i2, i0);
            }
        }

        md.primPath = QString::fromStdString(prim.GetPath().GetString());

        // Generate sparse collision wireframe for simple types
        if (md.hasCollisionAPI && !md.lineOnly) {
            int axisUp = 1; // default Y-up for generated shapes
            if (type == "Cylinder" || type == "Cone" || type == "Capsule") {
                TfToken axis;
                if (type == "Cylinder") UsdGeomCylinder(prim).GetAxisAttr().Get(&axis);
                else if (type == "Cone") UsdGeomCone(prim).GetAxisAttr().Get(&axis);
                else UsdGeomCapsule(prim).GetAxisAttr().Get(&axis);
                if (axis == TfToken("X")) axisUp = 0;
                else if (axis == TfToken("Z")) axisUp = 2;
            }

            if (type == "Sphere") {
                double r = 1.0; UsdGeomSphere(prim).GetRadiusAttr().Get(&r);
                genCollisionWireSphere(float(r), md.collisionWireVerts, md.collisionWireIndices);
            } else if (type == "Cube") {
                double s = 2.0; UsdGeomCube(prim).GetSizeAttr().Get(&s);
                genCollisionWireCube(float(s), md.collisionWireVerts, md.collisionWireIndices);
            } else if (type == "Cylinder") {
                double h = 2.0, r = 0.5;
                UsdGeomCylinder cyl(prim); cyl.GetHeightAttr().Get(&h); cyl.GetRadiusAttr().Get(&r);
                genCollisionWireCylinder(float(r), float(h), axisUp, md.collisionWireVerts, md.collisionWireIndices);
            } else if (type == "Cone") {
                double h = 2.0, r = 0.5;
                UsdGeomCone cone(prim); cone.GetHeightAttr().Get(&h); cone.GetRadiusAttr().Get(&r);
                genCollisionWireCone(float(r), float(h), axisUp, md.collisionWireVerts, md.collisionWireIndices);
            } else if (type == "Capsule") {
                double h = 1.0, r = 0.5;
                UsdGeomCapsule cap(prim); cap.GetHeightAttr().Get(&h); cap.GetRadiusAttr().Get(&r);
                genCollisionWireCapsule(float(r), float(h), axisUp, md.collisionWireVerts, md.collisionWireIndices);
            } else if (type == "Plane") {
                double w = 2.0, l = 2.0; TfToken axis;
                UsdGeomPlane plane(prim); plane.GetWidthAttr().Get(&w); plane.GetLengthAttr().Get(&l);
                plane.GetAxisAttr().Get(&axis);
                int upAxis = 1;
                if (axis == TfToken("X")) upAxis = 0;
                else if (axis == TfToken("Z")) upAxis = 2;
                genCollisionWirePlane(float(w), float(l), upAxis, md.collisionWireVerts, md.collisionWireIndices);
            }
            // Mesh type: no sparse wireframe, uses full triangle edges (md.edges)
        }

        if (!md.lineOnly)
            computeSmoothNormals(md);
        m_meshes << md;
    }

    // ── Extract light data from USD light prims ──
    m_lights.clear();
    for (const UsdPrim &prim : stage->Traverse(UsdTraverseInstanceProxies())) {
        const TfToken type = prim.GetTypeName();
        bool isLight = (type == "SphereLight" || type == "RectLight" ||
                        type == "DiskLight"   || type == "CylinderLight" ||
                        type == "DistantLight" || type == "DomeLight");
        if (!isLight) continue;
        if (m_lights.size() >= 16) break;

        UsdGeomImageable img(prim);
        if (img && img.ComputeVisibility() == UsdGeomTokens->invisible)
            continue;

        UsdLuxLightAPI lightApi(prim);
        GfVec3f col(1, 1, 1);
        float intensity = 1.f, exposure = 0.f;
        lightApi.GetColorAttr().Get(&col);
        lightApi.GetIntensityAttr().Get(&intensity);
        lightApi.GetExposureAttr().Get(&exposure);

        float scale = intensity * std::pow(2.f, exposure);
        QVector3D effColor(col[0] * scale, col[1] * scale, col[2] * scale);

        GfMatrix4d xf = xfCache.GetLocalToWorldTransform(prim);
        auto pos3 = xf.GetRow(3);
        QVector3D worldPos(pos3[0] * m_unitScale, pos3[1] * m_unitScale, pos3[2] * m_unitScale);
        // +Z axis = direction towards the light (for shader's L vector)
        auto zAxis = xf.GetRow(2);
        QVector3D worldDir(zAxis[0], zAxis[1], zAxis[2]);
        worldDir.normalize();

        LightData ld;
        if (type == "DistantLight") {
            ld.type = 0;
            ld.direction = worldDir;
            ld.radius = 0.f;
            // Distant/dome: no falloff, normalize to max 1.0
            float mc = std::max({effColor.x(), effColor.y(), effColor.z(), 1.f});
            effColor /= mc;
        } else if (type == "DomeLight") {
            ld.type = 2;
            ld.radius = 0.f;
            float mc = std::max({effColor.x(), effColor.y(), effColor.z(), 1.f});
            effColor /= mc;
        } else {
            ld.type = 1;
            ld.position = worldPos;
            // Match Hydra Storm: for area lights with normalize=false (default),
            // multiply intensity by projected area.
            // The shader then uses 1/(π*d²) attenuation.
            float radiusScene = 0.5f;  // in scene units
            float areaScene = 0.f;     // projected area for intensity scaling
            if (type == "SphereLight") {
                double r = 0.5;
                UsdLuxSphereLight(prim).GetRadiusAttr().Get(&r);
                radiusScene = float(r);
                areaScene = float(M_PI) * radiusScene * radiusScene;
            } else if (type == "RectLight") {
                float w = 1.0f, h = 1.0f;
                UsdLuxRectLight(prim).GetWidthAttr().Get(&w);
                UsdLuxRectLight(prim).GetHeightAttr().Get(&h);
                radiusScene = std::sqrt(w * h / float(M_PI)); // equivalent radius
                areaScene = w * h;
            } else if (type == "DiskLight") {
                float r = 0.5f;
                UsdLuxDiskLight(prim).GetRadiusAttr().Get(&r);
                radiusScene = r;
                areaScene = float(M_PI) * radiusScene * radiusScene;
            } else if (type == "CylinderLight") {
                float r = 0.5f, len = 1.0f;
                UsdLuxCylinderLight(prim).GetRadiusAttr().Get(&r);
                UsdLuxCylinderLight(prim).GetLengthAttr().Get(&len);
                radiusScene = r;
                areaScene = 2.f * float(M_PI) * r * len; // lateral surface area
            } else {
                areaScene = float(M_PI) * radiusScene * radiusScene;
            }
            ld.radius = radiusScene * m_unitScale;
            // Directional area lights (rect/disk): emission along -Z in local space
            if (type == "RectLight" || type == "DiskLight") {
                ld.direction = -worldDir; // -Z world = emission direction
            }
            bool normalize = false;
            lightApi.GetNormalizeAttr().Get(&normalize);
            if (!normalize) {
                effColor *= areaScene;
            }
        }
        ld.color = effColor;
        ld.primPath = QString::fromStdString(prim.GetPath().GetString());
        m_lights << ld;
    }

    // Default fallback: directional light when no lights in scene
    if (m_lights.isEmpty()) {
        LightData fallback;
        fallback.type = 0;
        fallback.direction = QVector3D(0.6f, 1.f, 0.8f).normalized();
        fallback.color = QVector3D(1.f, 1.f, 1.f);
        fallback.radius = 0.f;
        m_lights << fallback;
    }

    // Compute scene bounding sphere and initialize camera on first load
    if (!m_cameraInitialized && !m_meshes.isEmpty()) {
        // Bounding box of all mesh origins
        QVector3D bmin(FLT_MAX, FLT_MAX, FLT_MAX);
        QVector3D bmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (const auto &md : m_meshes) {
            if (md.isCollision) continue; // exclude collision from camera fitting
            // Transform each vertex's bounding contribution
            const float *v = md.vertices.constData();
            int vertCount = md.vertices.size() / 6;
            for (int vi = 0; vi < vertCount; ++vi) {
                QVector3D local(v[vi*6], v[vi*6+1], v[vi*6+2]);
                QVector3D world = md.transform.map(local);
                bmin.setX(qMin(bmin.x(), world.x()));
                bmin.setY(qMin(bmin.y(), world.y()));
                bmin.setZ(qMin(bmin.z(), world.z()));
                bmax.setX(qMax(bmax.x(), world.x()));
                bmax.setY(qMax(bmax.y(), world.y()));
                bmax.setZ(qMax(bmax.z(), world.z()));
            }
        }
        QVector3D center = (bmin + bmax) * 0.5f;
        m_sceneRadius = (bmax - bmin).length() * 0.5f;
        if (m_sceneRadius < 0.1f) m_sceneRadius = 0.1f;

        m_target = center;
        // Fit camera: distance so scene fills ~60% of view (fov=45°)
        m_dist = m_sceneRadius / tanf(qDegreesToRadians(22.5f)) * 1.2f;
        m_cameraInitialized = true;
    }

    // Always recalculate camera (m_zUp may have changed on file switch)
    updateCamera();

    m_meshDirty = true;
}

void UsdViewportItem::updateCamera()
{
    const float yr = qDegreesToRadians(m_yaw);
    const float pr = qDegreesToRadians(m_pitch);
    const float cp = cosf(pr);

    QVector3D eye;
    QVector3D up;
    if (m_zUp) {
        eye = m_target + QVector3D(
            m_dist * cp * cosf(yr),
            m_dist * cp * sinf(yr),
            m_dist * sinf(pr));
        up = QVector3D(0, 0, 1);
    } else {
        eye = m_target + QVector3D(
            m_dist * cp * sinf(yr),
            m_dist * sinf(pr),
            m_dist * cp * cosf(yr));
        up = QVector3D(0, 1, 0);
    }

    // Flip up vector at poles to avoid singularity
    if (cp < 0.f)
        up = -up;

    m_cameraEye = eye;

    m_view.setToIdentity();
    m_view.lookAt(eye, m_target, up);

    const float aspect = (width() > 0 && height() > 0)
                         ? float(width()) / float(height()) : 1.f;
    m_proj.setToIdentity();
    float gridExtent = 1000.f * qMax(1.f, m_unitScale);
    float distToOrigin = eye.length();
    float nearP = qMax(0.001f, m_dist * 0.0005f);
    float farP  = std::max({gridExtent * 3.f, m_dist * 50.f,
                            distToOrigin + gridExtent * 1.5f});
    m_proj.perspective(45.f, aspect, nearP, farP);

    updateOrientLabels();
}

// ================================================================
//  Orientation indicator label positions
// ================================================================
void UsdViewportItem::updateOrientLabels()
{
    const float miniSize = 80.f;
    const float margin = 10.f;

    // Rotation-only view (strip translation from camera view)
    QMatrix4x4 rotView;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            rotView(r, c) = m_view(r, c);
    rotView(2, 3) = -5.f;
    rotView(3, 3) = 1.f;

    QMatrix4x4 ortho;
    ortho.ortho(-1.4f, 1.4f, -1.4f, 1.4f, 0.1f, 100.f);

    QMatrix4x4 mvp = ortho * rotView;

    float miniX = margin;
    float miniY = height() - margin - miniSize;

    auto project = [&](QVector3D pos) -> QPointF {
        QVector4D clip = mvp * QVector4D(pos, 1.f);
        float ndcX = clip.x() / clip.w();
        float ndcY = clip.y() / clip.w();
        return QPointF(
            miniX + (ndcX + 1.f) * 0.5f * miniSize,
            miniY + (1.f - ndcY) * 0.5f * miniSize
        );
    };

    m_orientLabels[0] = project(QVector3D(1.15f, 0, 0)); // X
    m_orientLabels[1] = project(QVector3D(0, 1.15f, 0)); // Y
    m_orientLabels[2] = project(QVector3D(0, 0, 1.15f)); // Z
    emit orientLabelsChanged();
}

void UsdViewportItem::updateLightsFromMeshTransforms()
{
    // During gizmo drag, update light positions/directions from current mesh transforms
    // so lighting updates in real-time without a full buildMeshes() rebuild.
    for (auto &light : m_lights) {
        if (light.primPath.isEmpty()) continue;
        // Find the mesh with matching prim path among selected meshes
        for (int idx : m_selectedMeshes) {
            if (idx < 0 || idx >= m_meshes.size()) continue;
            if (m_meshes[idx].primPath != light.primPath) continue;
            const QMatrix4x4 &xf = m_meshes[idx].transform;
            if (light.type == 1) {
                // Point light: extract position from transform column 3
                light.position = xf.column(3).toVector3D();
                // Directional area lights (rect/disk): update emission direction (-Z axis)
                QVector3D dir = light.direction;
                if (dir.lengthSquared() > 0.001f)
                    light.direction = -xf.column(2).toVector3D().normalized();
            } else if (light.type == 0) {
                // Distant light: extract direction from transform column 2 (+Z axis)
                light.direction = xf.column(2).toVector3D().normalized();
            }
            break;
        }
    }
}

// ================================================================
//  Gizmo helpers
// ================================================================
void UsdViewportItem::setGizmoMode(int mode)
{
    if (m_gizmoMode == mode) return;
    m_gizmoMode = mode;
    m_gizmoHoveredPart = -1;
    m_gizmoDragPart = -1;
    if (mode != GizmoModeNone) updateGizmoPosition();
    m_meshDirty = true;
    update();
    emit gizmoModeChanged();
}

const QVector<GizmoMeshData> &UsdViewportItem::activeGizmoMeshes() const
{
    static const QVector<GizmoMeshData> empty;
    switch (m_gizmoMode) {
    case GizmoModeTranslate: return m_translateGizmoMeshes;
    case GizmoModeRotate:    return m_rotateGizmoMeshes;
    case GizmoModeScale:     return m_scaleGizmoMeshes;
    default:                 return empty;
    }
}

void UsdViewportItem::updateGizmoPosition()
{
    if (m_gizmoMode == GizmoModeNone || m_selectedMeshes.isEmpty()) return;
    int anchorIdx = m_anchorMeshIdx;
    // Fallback if anchor is invalid or not in current selection
    if (anchorIdx < 0 || anchorIdx >= m_meshes.size() || !m_selectedMeshes.contains(anchorIdx)) {
        anchorIdx = -1;
        for (int idx : m_selectedMeshes)
            if (idx >= 0 && idx < m_meshes.size())
                anchorIdx = idx;
    }
    if (anchorIdx >= 0) {
        const QMatrix4x4 &xf = m_meshes[anchorIdx].transform;
        m_gizmoWorldPos = QVector3D(xf(0, 3), xf(1, 3), xf(2, 3));
    }
}

int UsdViewportItem::pickGizmo(const QPointF &pos) const
{
    if (m_gizmoMode == GizmoModeNone || m_selectedMeshes.isEmpty()) return -1;

    const auto &meshes = activeGizmoMeshes();

    QMatrix4x4 invVP = (m_proj * m_view).inverted();
    float nx = 2.f * float(pos.x()) / float(width())  - 1.f;
    float ny = 1.f - 2.f * float(pos.y()) / float(height());

    QVector4D nearH = invVP * QVector4D(nx, ny, -1.f, 1.f);
    QVector4D farH  = invVP * QVector4D(nx, ny,  1.f, 1.f);
    QVector3D rayOrig = nearH.toVector3DAffine();
    QVector3D rayDir  = (farH.toVector3DAffine() - rayOrig).normalized();

    // Compute gizmo transform (constant screen size)
    float dist = (m_gizmoWorldPos - m_cameraEye).length();
    float scale = dist * 0.08f;
    QMatrix4x4 gizmoXf;
    gizmoXf.translate(m_gizmoWorldPos);
    gizmoXf.scale(scale);

    QMatrix4x4 invGizmo = gizmoXf.inverted();
    QVector3D orig = invGizmo.map(rayOrig);
    QVector3D dir  = invGizmo.mapVector(rayDir).normalized();

    float bestT = FLT_MAX;
    int bestPart = -1;

    for (int pi = 0; pi < meshes.size(); ++pi) {
        const auto &gm = meshes[pi];
        const float *vd = gm.vertices.constData();
        const int stride = 6;

        for (int i = 0; i + 2 < gm.indices.size(); i += 3) {
            int i0 = gm.indices[i], i1 = gm.indices[i+1], i2 = gm.indices[i+2];
            QVector3D v0(vd[i0*stride], vd[i0*stride+1], vd[i0*stride+2]);
            QVector3D v1(vd[i1*stride], vd[i1*stride+1], vd[i1*stride+2]);
            QVector3D v2(vd[i2*stride], vd[i2*stride+1], vd[i2*stride+2]);

            QVector3D e1 = v1 - v0, e2 = v2 - v0;
            QVector3D h = QVector3D::crossProduct(dir, e2);
            float a = QVector3D::dotProduct(e1, h);
            if (fabsf(a) < 1e-7f) continue;
            float f = 1.f / a;
            QVector3D s = orig - v0;
            float u = f * QVector3D::dotProduct(s, h);
            if (u < 0.f || u > 1.f) continue;
            QVector3D q = QVector3D::crossProduct(s, e1);
            float v = f * QVector3D::dotProduct(dir, q);
            if (v < 0.f || u + v > 1.f) continue;
            float t = f * QVector3D::dotProduct(e2, q);
            if (t > 1e-5f && t < bestT) {
                bestT = t;
                bestPart = pi;
            }
        }
    }
    return bestPart;
}

void UsdViewportItem::hoverMoveEvent(QHoverEvent *e)
{
    if (m_gizmoMode != GizmoModeNone && !m_selectedMeshes.isEmpty()) {
        int newHover = pickGizmo(e->position());
        // Map scale cube tip picks to their axis
        if (m_gizmoMode == GizmoModeScale && newHover >= GizmoCount)
            newHover = newHover - GizmoCount; // ScaleCubeTipX(7)->AxisX(0), etc.
        if (newHover != m_gizmoHoveredPart) {
            m_gizmoHoveredPart = newHover;
            m_meshDirty = true;
            update();
        }
    }
    e->accept();
}

// ================================================================
//  Mouse interaction (camera + gizmo drag)
// ================================================================
static QVector3D buildRay(const QMatrix4x4 &invVP, float px, float py, float w, float h,
                           QVector3D &outOrig)
{
    float nx = 2.f * px / w - 1.f;
    float ny = 1.f - 2.f * py / h;
    QVector4D nearH = invVP * QVector4D(nx, ny, -1.f, 1.f);
    QVector4D farH  = invVP * QVector4D(nx, ny,  1.f, 1.f);
    outOrig = nearH.toVector3DAffine();
    return (farH.toVector3DAffine() - outOrig).normalized();
}

// Closest parameter t on line P + t*D1 from ray Q + s*D2
static float closestParamOnLine(QVector3D P, QVector3D D1, QVector3D Q, QVector3D D2)
{
    QVector3D w0 = P - Q;
    float a = QVector3D::dotProduct(D1, D1);
    float b = QVector3D::dotProduct(D1, D2);
    float c = QVector3D::dotProduct(D2, D2);
    float d = QVector3D::dotProduct(D1, w0);
    float e = QVector3D::dotProduct(D2, w0);
    float denom = a * c - b * b;
    if (fabsf(denom) < 1e-10f) return 0.f;
    return (b * e - c * d) / denom;
}

// Ray-plane intersection hit point
static QVector3D rayPlaneHit(QVector3D rayO, QVector3D rayD, QVector3D planeO, QVector3D planeN)
{
    float denom = QVector3D::dotProduct(rayD, planeN);
    if (fabsf(denom) < 1e-10f) return planeO;
    float t = QVector3D::dotProduct(planeO - rayO, planeN) / denom;
    return rayO + rayD * t;
}

void UsdViewportItem::mousePressEvent(QMouseEvent *e)
{
    forceActiveFocus();
    m_lastMouse = e->position();
    m_pressPos  = e->position();

    // Gizmo pick takes priority
    if (m_gizmoMode != GizmoModeNone && !m_selectedMeshes.isEmpty() && e->button() == Qt::LeftButton) {
        int hit = pickGizmo(e->position());
        // Map scale cube tip picks to their axis
        if (m_gizmoMode == GizmoModeScale && hit >= GizmoCount)
            hit = hit - GizmoCount;
        if (hit >= 0) {
            m_gizmoDragPart = hit;
            m_gizmoHoveredPart = hit;
            m_gizmoDragStartPos = m_gizmoWorldPos;
            m_gizmoDragStartMouse = e->position();
            // Clear all drag state
            m_gizmoDragStartTranslations.clear();
            m_gizmoDragParentTransforms.clear();
            m_gizmoDragStartLocalTranslates.clear();
            m_gizmoDragStartRotations.clear();
            m_gizmoDragStartScales.clear();
            m_gizmoDragStartAngle = 0.f;
            m_gizmoDragStartDistance = 0.f;

            // Read parent transforms and local attributes from USD
            auto *stageRef = m_doc ? static_cast<UsdStageRefPtr *>(m_doc->stagePtr()) : nullptr;
            UsdGeomXformCache xfCache;

            for (int idx : m_selectedMeshes) {
                if (idx < 0 || idx >= m_meshes.size()) continue;
                const QMatrix4x4 &xf = m_meshes[idx].transform;
                m_gizmoDragStartTranslations[idx] = QVector3D(xf(0, 3), xf(1, 3), xf(2, 3));

                if (stageRef && *stageRef) {
                    SdfPath sdfPath(m_meshes[idx].primPath.toStdString());
                    UsdPrim prim = (*stageRef)->GetPrimAtPath(sdfPath);
                    if (prim.IsValid()) {
                        // Parent-to-world transform
                        GfMatrix4d parentGf = xfCache.GetParentToWorldTransform(prim);
                        float mf[16];
                        for (int r = 0; r < 4; r++)
                            for (int c = 0; c < 4; c++)
                                mf[c * 4 + r] = float(parentGf[r][c]);
                        m_gizmoDragParentTransforms[idx] = QMatrix4x4(mf);

                        // Helper to read a vec3 attribute (float3 or double3)
                        auto readVec3Attr = [&](const char *name) -> QVector3D {
                            UsdAttribute attr = prim.GetAttribute(TfToken(name));
                            QVector3D v(0, 0, 0);
                            if (attr.IsValid()) {
                                GfVec3f vf; GfVec3d vd;
                                if (attr.Get(&vf))
                                    v = QVector3D(vf[0], vf[1], vf[2]);
                                else if (attr.Get(&vd))
                                    v = QVector3D(float(vd[0]), float(vd[1]), float(vd[2]));
                            }
                            return v;
                        };

                        if (m_gizmoMode == GizmoModeTranslate) {
                            m_gizmoDragStartLocalTranslates[idx] = readVec3Attr("xformOp:translate");
                        } else if (m_gizmoMode == GizmoModeRotate) {
                            // Ensure rotateXYZ op exists
                            UsdGeomXformable xformable(prim);
                            UsdAttribute rotAttr = prim.GetAttribute(TfToken("xformOp:rotateXYZ"));
                            if (!rotAttr.IsValid()) {
                                xformable.AddRotateXYZOp();
                                // Set default value
                                rotAttr = prim.GetAttribute(TfToken("xformOp:rotateXYZ"));
                                if (rotAttr.IsValid())
                                    rotAttr.Set(GfVec3f(0, 0, 0));
                            }
                            m_gizmoDragStartRotations[idx] = readVec3Attr("xformOp:rotateXYZ");
                        } else if (m_gizmoMode == GizmoModeScale) {
                            // Ensure scale op exists
                            UsdGeomXformable xformable(prim);
                            UsdAttribute scaleAttr = prim.GetAttribute(TfToken("xformOp:scale"));
                            if (!scaleAttr.IsValid()) {
                                xformable.AddScaleOp();
                                scaleAttr = prim.GetAttribute(TfToken("xformOp:scale"));
                                if (scaleAttr.IsValid())
                                    scaleAttr.Set(GfVec3f(1, 1, 1));
                            }
                            m_gizmoDragStartScales[idx] = readVec3Attr("xformOp:scale");
                        }
                    }
                }
            }

            // Compute start angle/distance for rotate/scale
            if (m_gizmoMode == GizmoModeRotate || m_gizmoMode == GizmoModeScale) {
                QMatrix4x4 invVP = (m_proj * m_view).inverted();
                float w = float(width()), h = float(height());
                QVector3D rayO, rayD;
                rayD = buildRay(invVP, float(e->position().x()), float(e->position().y()), w, h, rayO);

                if (m_gizmoMode == GizmoModeRotate) {
                    // Project mouse onto rotation plane, compute angle
                    static const QVector3D planeNormals[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                    QVector3D planeN = planeNormals[hit]; // RotateRingX/Y/Z = 0/1/2
                    QVector3D hitPt = rayPlaneHit(rayO, rayD, m_gizmoWorldPos, planeN);
                    QVector3D fromCenter = hitPt - m_gizmoWorldPos;
                    // Compute angle in the plane using two perpendicular axes
                    // For X-ring (normal X): use Y,Z. For Y-ring (normal Y): use X,Z. For Z-ring (normal Z): use X,Y
                    float angle = 0.f;
                    if (hit == RotateRingX)
                        angle = atan2f(fromCenter.z(), fromCenter.y());
                    else if (hit == RotateRingY)
                        angle = atan2f(fromCenter.x(), fromCenter.z());
                    else // RotateRingZ
                        angle = atan2f(fromCenter.y(), fromCenter.x());
                    m_gizmoDragStartAngle = angle;
                } else {
                    // Scale
                    if (hit <= AxisZ) {
                        // Axis drag: store projection of mouse click onto the axis
                        static const QVector3D axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                        m_gizmoDragStartDistance = closestParamOnLine(m_gizmoWorldPos, axes[hit], rayO, rayD);
                    } else {
                        // Origin/plane drag: compute distance from gizmo center to mouse hit
                        QVector3D planeN = (m_cameraEye - m_gizmoWorldPos).normalized();
                        QVector3D hitPt = rayPlaneHit(rayO, rayD, m_gizmoWorldPos, planeN);
                        m_gizmoDragStartDistance = (hitPt - m_gizmoWorldPos).length();
                        if (m_gizmoDragStartDistance < 1e-5f) m_gizmoDragStartDistance = 1.f;
                    }
                }
            }

            e->accept();
            return;
        }
    }

    m_panning = (e->modifiers() & Qt::AltModifier) || (e->button() == Qt::MiddleButton);
    m_dragging = true;
    e->accept();
}

void UsdViewportItem::mouseMoveEvent(QMouseEvent *e)
{
    // Gizmo dragging
    if (m_gizmoDragPart >= 0) {
        QMatrix4x4 invVP = (m_proj * m_view).inverted();
        float w = float(width()), h = float(height());

        QVector3D curOrig, startOrig;
        QVector3D curDir  = buildRay(invVP, float(e->position().x()), float(e->position().y()), w, h, curOrig);
        QVector3D startDir = buildRay(invVP, float(m_gizmoDragStartMouse.x()), float(m_gizmoDragStartMouse.y()), w, h, startOrig);

        QVector3D origin = m_gizmoDragStartPos;

        if (m_gizmoMode == GizmoModeTranslate) {
            // --- Translate drag ---
            QVector3D delta(0, 0, 0);
            if (m_gizmoDragPart <= AxisZ) {
                static const QVector3D axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                QVector3D axis = axes[m_gizmoDragPart];
                float tCurr  = closestParamOnLine(origin, axis, curOrig, curDir);
                float tStart = closestParamOnLine(origin, axis, startOrig, startDir);
                delta = axis * (tCurr - tStart);
            } else if (m_gizmoDragPart <= PlaneYZ) {
                static const QVector3D normals[] = { {0,0,1}, {0,1,0}, {1,0,0} };
                QVector3D planeN = normals[m_gizmoDragPart - PlaneXY];
                QVector3D hitCurr  = rayPlaneHit(curOrig, curDir, origin, planeN);
                QVector3D hitStart = rayPlaneHit(startOrig, startDir, origin, planeN);
                delta = hitCurr - hitStart;
            } else {
                QVector3D planeN = (m_cameraEye - origin).normalized();
                QVector3D hitCurr  = rayPlaneHit(curOrig, curDir, origin, planeN);
                QVector3D hitStart = rayPlaneHit(startOrig, startDir, origin, planeN);
                delta = hitCurr - hitStart;
            }

            // Snap to grid
            if (m_snapEnabled) {
                QVector3D snappedWorldPos = m_gizmoDragStartPos + delta;
                float grid = 1.0f;
                snappedWorldPos.setX(qRound(snappedWorldPos.x() / grid) * grid);
                snappedWorldPos.setY(qRound(snappedWorldPos.y() / grid) * grid);
                snappedWorldPos.setZ(qRound(snappedWorldPos.z() / grid) * grid);
                delta = snappedWorldPos - m_gizmoDragStartPos;
            }

            // Apply delta to all selected meshes (visual)
            for (auto it = m_gizmoDragStartTranslations.constBegin();
                 it != m_gizmoDragStartTranslations.constEnd(); ++it) {
                int idx = it.key();
                if (idx >= 0 && idx < m_meshes.size()) {
                    QVector3D newT = it.value() + delta;
                    m_meshes[idx].transform(0, 3) = newT.x();
                    m_meshes[idx].transform(1, 3) = newT.y();
                    m_meshes[idx].transform(2, 3) = newT.z();
                }
            }
            m_gizmoWorldPos = m_gizmoDragStartPos + delta;

            // Write to USD
            if (m_doc) {
                for (auto it = m_gizmoDragStartLocalTranslates.constBegin();
                     it != m_gizmoDragStartLocalTranslates.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D stageDelta = delta / m_unitScale;
                    QVector3D localDelta = stageDelta;
                    if (m_gizmoDragParentTransforms.contains(idx)) {
                        QMatrix4x4 parentInv = m_gizmoDragParentTransforms[idx].inverted();
                        localDelta = parentInv.mapVector(stageDelta);
                    }
                    QVector3D newLocal = it.value() + localDelta;
                    QString val = QString("(%1, %2, %3)")
                        .arg(double(newLocal.x())).arg(double(newLocal.y())).arg(double(newLocal.z()));
                    m_doc->setAttributeInternal(m_meshes[idx].primPath,
                                        QStringLiteral("xformOp:translate"), val);
                }
                updateLightsFromMeshTransforms();
                emit gizmoDragUpdated();
            }

        } else if (m_gizmoMode == GizmoModeRotate) {
            // --- Rotate drag ---
            static const QVector3D planeNormals[] = { {1,0,0}, {0,1,0}, {0,0,1} };
            QVector3D planeN = planeNormals[m_gizmoDragPart]; // RotateRingX/Y/Z = 0/1/2

            QVector3D hitPt = rayPlaneHit(curOrig, curDir, origin, planeN);
            QVector3D fromCenter = hitPt - origin;

            float curAngle = 0.f;
            if (m_gizmoDragPart == RotateRingX)
                curAngle = atan2f(fromCenter.z(), fromCenter.y());
            else if (m_gizmoDragPart == RotateRingY)
                curAngle = atan2f(fromCenter.x(), fromCenter.z());
            else // RotateRingZ
                curAngle = atan2f(fromCenter.y(), fromCenter.x());

            float angleDelta = qRadiansToDegrees(curAngle - m_gizmoDragStartAngle);

            // Write to USD and update mesh transforms from xformCache
            if (m_doc) {
                auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
                for (auto it = m_gizmoDragStartRotations.constBegin();
                     it != m_gizmoDragStartRotations.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D startRot = it.value();
                    QVector3D newRot = startRot;
                    if (m_gizmoDragPart == RotateRingX) newRot.setX(startRot.x() + angleDelta);
                    else if (m_gizmoDragPart == RotateRingY) newRot.setY(startRot.y() + angleDelta);
                    else newRot.setZ(startRot.z() + angleDelta);

                    QString val = QString("(%1, %2, %3)")
                        .arg(double(newRot.x())).arg(double(newRot.y())).arg(double(newRot.z()));
                    m_doc->setAttributeInternal(m_meshes[idx].primPath,
                                        QStringLiteral("xformOp:rotateXYZ"), val);

                    // Update visual transform from USD
                    if (stageRef && *stageRef) {
                        UsdGeomXformCache xfCache;
                        SdfPath sdfPath(m_meshes[idx].primPath.toStdString());
                        UsdPrim prim = (*stageRef)->GetPrimAtPath(sdfPath);
                        if (prim.IsValid()) {
                            GfMatrix4d xf = xfCache.GetLocalToWorldTransform(prim);
                            float mf[16];
                            for (int r = 0; r < 4; r++)
                                for (int c = 0; c < 4; c++)
                                    mf[c * 4 + r] = float(xf[r][c]);
                            QMatrix4x4 us; us.scale(m_unitScale);
                            m_meshes[idx].transform = us * QMatrix4x4(mf);
                        }
                    }
                }
                updateLightsFromMeshTransforms();
                emit gizmoDragUpdated();
            }

        } else if (m_gizmoMode == GizmoModeScale) {
            // --- Scale drag ---
            float scaleFactor;
            if (m_gizmoDragPart <= AxisZ) {
                // Axis drag: project mouse onto the gizmo axis so cube tip tracks mouse
                static const QVector3D axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                QVector3D axis = axes[m_gizmoDragPart];
                float tCurr = closestParamOnLine(origin, axis, curOrig, curDir);
                // Cube tip is at 0.84 * gizmoScale along the axis in world space
                float gizmoScale = (origin - m_cameraEye).length() * 0.08f;
                float cubeTipDist = 0.84f * gizmoScale;
                if (cubeTipDist < 1e-6f) cubeTipDist = 1.f;
                scaleFactor = 1.f + (tCurr - m_gizmoDragStartDistance) / cubeTipDist;
            } else if (m_gizmoDragPart == Origin) {
                // Origin: project onto camera-facing plane, same sensitivity as axis drag
                QVector3D planeN = (m_cameraEye - origin).normalized();
                QVector3D hitCurr  = rayPlaneHit(curOrig, curDir, origin, planeN);
                QVector3D hitStart = rayPlaneHit(startOrig, startDir, origin, planeN);
                // Signed displacement along the initial drag direction
                QVector3D startVec = (hitStart - origin);
                float startLen = startVec.length();
                float displacement;
                if (startLen > 1e-6f)
                    displacement = QVector3D::dotProduct(hitCurr - hitStart, startVec / startLen);
                else
                    displacement = (hitCurr - hitStart).length();
                float gizmoScale = (origin - m_cameraEye).length() * 0.08f;
                float cubeTipDist = 0.84f * gizmoScale;
                if (cubeTipDist < 1e-6f) cubeTipDist = 1.f;
                scaleFactor = 1.f + displacement / cubeTipDist;
            } else {
                // Plane: use pixel delta
                QPointF delta = e->position() - m_gizmoDragStartMouse;
                float pixelDelta = float(delta.x() - delta.y()) * 0.5f;
                scaleFactor = 1.f + pixelDelta / 200.f;
            }

            // Clamp scale factor
            scaleFactor = qBound(-100.f, scaleFactor, 100.f);

            // Update cube tip slide factors for visual feedback
            m_scaleCubeFactors = QVector3D(1.f, 1.f, 1.f);
            if (m_gizmoDragPart == AxisX)
                m_scaleCubeFactors.setX(scaleFactor);
            else if (m_gizmoDragPart == AxisY)
                m_scaleCubeFactors.setY(scaleFactor);
            else if (m_gizmoDragPart == AxisZ)
                m_scaleCubeFactors.setZ(scaleFactor);
            else if (m_gizmoDragPart == PlaneXY) {
                m_scaleCubeFactors.setX(scaleFactor);
                m_scaleCubeFactors.setY(scaleFactor);
            } else if (m_gizmoDragPart == PlaneXZ) {
                m_scaleCubeFactors.setX(scaleFactor);
                m_scaleCubeFactors.setZ(scaleFactor);
            } else if (m_gizmoDragPart == PlaneYZ) {
                m_scaleCubeFactors.setY(scaleFactor);
                m_scaleCubeFactors.setZ(scaleFactor);
            } else {
                // Origin: uniform
                m_scaleCubeFactors = QVector3D(scaleFactor, scaleFactor, scaleFactor);
            }

            // Write to USD and update mesh transforms
            if (m_doc) {
                auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
                for (auto it = m_gizmoDragStartScales.constBegin();
                     it != m_gizmoDragStartScales.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D startScale = it.value();
                    QVector3D newScale = startScale;

                    if (m_gizmoDragPart == AxisX)
                        newScale.setX(startScale.x() * scaleFactor);
                    else if (m_gizmoDragPart == AxisY)
                        newScale.setY(startScale.y() * scaleFactor);
                    else if (m_gizmoDragPart == AxisZ)
                        newScale.setZ(startScale.z() * scaleFactor);
                    else if (m_gizmoDragPart == PlaneXY) {
                        newScale.setX(startScale.x() * scaleFactor);
                        newScale.setY(startScale.y() * scaleFactor);
                    } else if (m_gizmoDragPart == PlaneXZ) {
                        newScale.setX(startScale.x() * scaleFactor);
                        newScale.setZ(startScale.z() * scaleFactor);
                    } else if (m_gizmoDragPart == PlaneYZ) {
                        newScale.setY(startScale.y() * scaleFactor);
                        newScale.setZ(startScale.z() * scaleFactor);
                    } else {
                        // Origin: uniform
                        newScale = startScale * scaleFactor;
                    }

                    QString val = QString("(%1, %2, %3)")
                        .arg(double(newScale.x())).arg(double(newScale.y())).arg(double(newScale.z()));
                    m_doc->setAttributeInternal(m_meshes[idx].primPath,
                                        QStringLiteral("xformOp:scale"), val);

                    // Update visual transform from USD
                    if (stageRef && *stageRef) {
                        UsdGeomXformCache xfCache;
                        SdfPath sdfPath(m_meshes[idx].primPath.toStdString());
                        UsdPrim prim = (*stageRef)->GetPrimAtPath(sdfPath);
                        if (prim.IsValid()) {
                            GfMatrix4d xf = xfCache.GetLocalToWorldTransform(prim);
                            float mf[16];
                            for (int r = 0; r < 4; r++)
                                for (int c = 0; c < 4; c++)
                                    mf[c * 4 + r] = float(xf[r][c]);
                            QMatrix4x4 us; us.scale(m_unitScale);
                            m_meshes[idx].transform = us * QMatrix4x4(mf);
                        }
                    }
                }
                updateLightsFromMeshTransforms();
                emit gizmoDragUpdated();
            }
        }

        m_meshDirty = true;
        update();
        e->accept();
        return;
    }

    if (!m_dragging) return;
    m_meshDirty = true;
    QPointF d = e->position() - m_lastMouse; m_lastMouse = e->position();

    if (m_panning) {
        float scale = m_dist * 0.002f;
        QVector3D right(m_view(0, 0), m_view(0, 1), m_view(0, 2));
        QVector3D camUp(m_view(1, 0), m_view(1, 1), m_view(1, 2));
        m_target -= right * float(d.x()) * scale;
        m_target += camUp * float(d.y()) * scale;
    } else {
        m_yaw   += float(d.x()) * 0.5f;
        m_pitch  = qBound(-89.9f, m_pitch + float(d.y()) * 0.5f, 89.9f);
    }
    updateCamera(); update(); e->accept();
}

void UsdViewportItem::mouseReleaseEvent(QMouseEvent *e)
{
    // Gizmo drag finished
    if (m_gizmoDragPart >= 0) {
        // Build undo command from pre-drag state vs current values
        if (m_doc) {
            QVector<GizmoTransformCommand::Entry> entries;
            if (m_gizmoMode == GizmoModeTranslate) {
                for (auto it = m_gizmoDragStartLocalTranslates.constBegin();
                     it != m_gizmoDragStartLocalTranslates.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D old = it.value();
                    QString oldVal = QString("(%1, %2, %3)").arg(double(old.x())).arg(double(old.y())).arg(double(old.z()));
                    QString newVal = m_doc->readAttributeValue(m_meshes[idx].primPath, QStringLiteral("xformOp:translate"));
                    entries.append({m_meshes[idx].primPath, QStringLiteral("xformOp:translate"), oldVal, newVal});
                }
            } else if (m_gizmoMode == GizmoModeRotate) {
                for (auto it = m_gizmoDragStartRotations.constBegin();
                     it != m_gizmoDragStartRotations.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D old = it.value();
                    QString oldVal = QString("(%1, %2, %3)").arg(double(old.x())).arg(double(old.y())).arg(double(old.z()));
                    QString newVal = m_doc->readAttributeValue(m_meshes[idx].primPath, QStringLiteral("xformOp:rotateXYZ"));
                    entries.append({m_meshes[idx].primPath, QStringLiteral("xformOp:rotateXYZ"), oldVal, newVal});
                }
            } else if (m_gizmoMode == GizmoModeScale) {
                for (auto it = m_gizmoDragStartScales.constBegin();
                     it != m_gizmoDragStartScales.constEnd(); ++it) {
                    int idx = it.key();
                    if (idx < 0 || idx >= m_meshes.size()) continue;
                    QVector3D old = it.value();
                    QString oldVal = QString("(%1, %2, %3)").arg(double(old.x())).arg(double(old.y())).arg(double(old.z()));
                    QString newVal = m_doc->readAttributeValue(m_meshes[idx].primPath, QStringLiteral("xformOp:scale"));
                    entries.append({m_meshes[idx].primPath, QStringLiteral("xformOp:scale"), oldVal, newVal});
                }
            }
            if (!entries.isEmpty()) {
                auto cmd = std::make_unique<GizmoTransformCommand>(m_doc, entries);
                m_doc->undoStack()->pushNoRedo(std::move(cmd));
            }
        }

        // Reset drag state first so stageModified can trigger rebuild.
        m_gizmoDragPart = -1;
        m_gizmoDragStartTranslations.clear();
        m_gizmoDragParentTransforms.clear();
        m_gizmoDragStartLocalTranslates.clear();
        m_gizmoDragStartRotations.clear();
        m_gizmoDragStartScales.clear();
        m_gizmoDragStartAngle = 0.f;
        m_gizmoDragStartDistance = 0.f;
        m_scaleCubeFactors = QVector3D(1.f, 1.f, 1.f);
        // Rebuild meshes from USD to ensure everything is in sync
        buildMeshes();
        updateGizmoPosition();
        m_meshDirty = true;
        update();
        for (int idx : m_selectedMeshes)
            if (idx >= 0 && idx < m_meshes.size())
                emit gizmoDragFinished(m_meshes[idx].primPath);
        e->accept();
        return;
    }

    // Detect click (no significant drag)
    if (e->button() == Qt::LeftButton) {
        QPointF delta = e->position() - m_pressPos;
        if (delta.manhattanLength() < 3.0) {
            int hit = pickMesh(e->position());
            QString hitPath = (hit >= 0 && hit < m_meshes.size())
                ? m_meshes[hit].primPath : QString();
            emit primClicked(hitPath, e->modifiers() & Qt::ControlModifier);
        }
    }
    m_dragging = false; m_panning = false; e->accept();
}
void UsdViewportItem::wheelEvent(QWheelEvent *e)
{
    // Proportional zoom: faster when far, slower when close
    float factor = 1.f - e->angleDelta().y() / 600.f;
    m_dist *= factor;
    float minDist = m_sceneRadius * 0.01f;
    float maxDist = m_sceneRadius * 50.f;
    m_dist = qBound(minDist, m_dist, maxDist);
    m_meshDirty = true;
    updateCamera(); update(); e->accept();

}
void UsdViewportItem::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickRhiItem::geometryChange(n, o);
    m_meshDirty = true;
    updateCamera(); update();
}

// ================================================================
//  Selection by prim path
// ================================================================
QStringList UsdViewportItem::selectedPrimPaths() const
{
    QStringList result;
    for (int idx : m_selectedMeshes) {
        if (idx >= 0 && idx < m_meshes.size())
            result << m_meshes[idx].primPath;
    }
    return result;
}

void UsdViewportItem::selectPrimPath(const QString &path)
{
    QSet<int> newSel;
    int lastIdx = -1;
    if (!path.isEmpty()) {
        for (int i = 0; i < m_meshes.size(); ++i) {
            if (m_meshes[i].primPath == path
                || m_meshes[i].primPath.startsWith(path + '/')) {
                newSel.insert(i);
                lastIdx = i;
            }
        }
    }
    if (newSel != m_selectedMeshes) {
        m_selectedMeshes = newSel;
        m_anchorMeshIdx = lastIdx;
        m_meshDirty = true;
        updateGizmoPosition();
        update();
        emit selectedPrimPathsChanged();
    }
}

void UsdViewportItem::selectPrimPaths(const QStringList &paths)
{
    QSet<int> newSel;
    int lastMatchedIdx = -1;
    for (const QString &path : paths) {
        if (path.isEmpty()) continue;
        for (int i = 0; i < m_meshes.size(); ++i) {
            if (m_meshes[i].primPath == path
                || m_meshes[i].primPath.startsWith(path + '/')) {
                newSel.insert(i);
                lastMatchedIdx = i;
            }
        }
    }
    if (newSel != m_selectedMeshes) {
        m_selectedMeshes = newSel;
        m_anchorMeshIdx = lastMatchedIdx;
        m_meshDirty = true;
        updateGizmoPosition();
        update();
        emit selectedPrimPathsChanged();
    }
}

void UsdViewportItem::togglePrimPath(const QString &path)
{
    if (path.isEmpty()) return;

    // Collect all mesh indices matching this path (or children)
    QSet<int> matched;
    for (int i = 0; i < m_meshes.size(); ++i) {
        if (m_meshes[i].primPath == path
            || m_meshes[i].primPath.startsWith(path + '/'))
            matched.insert(i);
    }
    if (matched.isEmpty()) return;

    // If ALL matched are already selected, remove them all; otherwise add them all
    bool allSelected = true;
    for (int idx : matched) {
        if (!m_selectedMeshes.contains(idx)) {
            allSelected = false;
            break;
        }
    }
    if (allSelected) {
        m_selectedMeshes -= matched;
        // If anchor was removed, reset it
        if (matched.contains(m_anchorMeshIdx))
            m_anchorMeshIdx = -1;
    } else {
        m_selectedMeshes += matched;
        // Set anchor to the last matched mesh (the one just added)
        for (int idx : matched)
            m_anchorMeshIdx = idx;
    }

    m_meshDirty = true;
    updateGizmoPosition();
    update();
    emit selectedPrimPathsChanged();
}

// ================================================================
//  CPU Ray Picking (Möller–Trumbore)
// ================================================================
int UsdViewportItem::pickMesh(const QPointF &pos) const
{
    // Build ray from camera eye through the clicked pixel.
    // Use FOV + inverse view directly instead of inverse(proj*view) to avoid
    // numerical precision loss at extreme zoom levels (large near/far ratio).
    float nx = 2.f * float(pos.x()) / float(width())  - 1.f;
    float ny = 1.f - 2.f * float(pos.y()) / float(height());

    const float aspect = (width() > 0 && height() > 0)
                         ? float(width()) / float(height()) : 1.f;
    const float tanHalfFov = tanf(qDegreesToRadians(22.5f)); // half of 45° FOV
    QVector3D camDir = QVector3D(nx * tanHalfFov * aspect,
                                  ny * tanHalfFov,
                                  -1.f).normalized();
    QMatrix4x4 invView = m_view.inverted();
    QVector3D rayOrig = m_cameraEye;
    QVector3D rayDir  = invView.mapVector(camDir).normalized();

    float bestWorldT = FLT_MAX;
    int   bestIdx = -1;

    for (int mi = 0; mi < m_meshes.size(); ++mi) {
        const MeshData &md = m_meshes[mi];
        if (md.isCollision) continue; // collision meshes are not pickable via click
        // Transform ray to mesh local space
        QMatrix4x4 invModel = md.transform.inverted();
        QVector3D orig = invModel.map(rayOrig);
        QVector3D dir  = invModel.mapVector(rayDir).normalized();

        const float *vd = md.vertices.constData();
        const int stride = 6; // x,y,z,nx,ny,nz

        // Line-only meshes: use bounding sphere test for picking
        if (md.lineOnly) {
            // Compute bounding sphere from vertices
            int vertCount = md.vertices.size() / stride;
            if (vertCount == 0) continue;
            QVector3D center(0, 0, 0);
            for (int vi = 0; vi < vertCount; vi++)
                center += QVector3D(vd[vi*stride], vd[vi*stride+1], vd[vi*stride+2]);
            center /= float(vertCount);
            float maxR2 = 0;
            for (int vi = 0; vi < vertCount; vi++) {
                QVector3D p(vd[vi*stride], vd[vi*stride+1], vd[vi*stride+2]);
                maxR2 = qMax(maxR2, (p - center).lengthSquared());
            }
            // Ray-sphere intersection
            QVector3D oc = orig - center;
            float b = QVector3D::dotProduct(oc, dir);
            float c = QVector3D::dotProduct(oc, oc) - maxR2;
            float disc = b * b - c;
            if (disc >= 0) {
                float t = -b - sqrtf(disc);
                if (t < 1e-5f) t = -b + sqrtf(disc);
                if (t > 1e-5f) {
                    QVector3D localHit = orig + dir * t;
                    QVector3D worldHit = md.transform.map(localHit);
                    float worldT = QVector3D::dotProduct(worldHit - rayOrig, rayDir);
                    if (worldT > 1e-5f && worldT < bestWorldT) {
                        bestWorldT = worldT;
                        bestIdx = mi;
                    }
                }
            }
            continue;
        }

        for (int i = 0; i + 2 < md.indices.size(); i += 3) {
            int i0 = md.indices[i], i1 = md.indices[i+1], i2 = md.indices[i+2];
            QVector3D v0(vd[i0*stride], vd[i0*stride+1], vd[i0*stride+2]);
            QVector3D v1(vd[i1*stride], vd[i1*stride+1], vd[i1*stride+2]);
            QVector3D v2(vd[i2*stride], vd[i2*stride+1], vd[i2*stride+2]);

            // Möller–Trumbore intersection
            QVector3D e1 = v1 - v0, e2 = v2 - v0;
            QVector3D h = QVector3D::crossProduct(dir, e2);
            float a = QVector3D::dotProduct(e1, h);
            if (fabsf(a) < 1e-7f) continue;
            float f = 1.f / a;
            QVector3D s = orig - v0;
            float u = f * QVector3D::dotProduct(s, h);
            if (u < 0.f || u > 1.f) continue;
            QVector3D q = QVector3D::crossProduct(s, e1);
            float v = f * QVector3D::dotProduct(dir, q);
            if (v < 0.f || u + v > 1.f) continue;
            float t = f * QVector3D::dotProduct(e2, q);
            if (t > 1e-5f) {
                // Convert hit point back to world space to compare distances consistently
                QVector3D localHit = orig + dir * t;
                QVector3D worldHit = md.transform.map(localHit);
                float worldT = QVector3D::dotProduct(worldHit - rayOrig, rayDir);
                if (worldT > 1e-5f && worldT < bestWorldT) {
                    bestWorldT = worldT;
                    bestIdx = mi;
                }
            }
        }
    }
    return bestIdx;
}

// ================================================================
//  UsdViewportRenderer (render thread)
// ================================================================
static QShader loadShader(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qWarning() << "Shader not found:" << path;
    return QShader::fromSerialized(f.readAll());
}

UsdViewportRenderer::~UsdViewportRenderer()
{
    destroyMeshes();
    destroyGizmoMeshes();
    for (auto &g : m_rhiOrientAxes) {
        delete g.vbuf; delete g.ibuf;
        delete g.ubuf; delete g.srb;
    }
    for (auto &g : m_rhiGrid) {
        delete g.vbuf; delete g.ibuf;
        delete g.ubuf; delete g.srb;
    }
    delete m_lightUbuf;
    delete m_pipeline;
    delete m_wirePipeline;
    delete m_stencilPipeline;
    delete m_gizmoPipeline;
    delete m_gridPipeline;
    delete m_lineMeshPipeline;
    delete m_collisionPipeline;
    // Post-process outline
    delete m_maskPipeline;
    delete m_outlinePipeline;
    delete m_outlineSrb;
    delete m_outlineUbuf;
    delete m_fsTriVbuf;
    delete m_maskSampler;
    delete m_maskRpDesc;
    delete m_maskRT;
    delete m_maskDS;
    delete m_maskTex;
}

void UsdViewportRenderer::destroyMeshes()
{
    for (auto &m : m_meshes) {
        delete m.vbuf; delete m.vbufSmooth;
        delete m.ibuf;
        delete m.ubuf; delete m.srb;
        delete m.ibufEdge;
        delete m.wireUbuf; delete m.wireSrb;
        delete m.colUbuf; delete m.colSrb;
        delete m.colVbuf; delete m.colIbuf;
    }
    m_meshes.clear();
}

void UsdViewportRenderer::uploadMesh(RhiMesh &dst, const MeshData &src,
                                      QRhiResourceUpdateBatch *batch)
{
    QRhi *rhi = renderTarget()->rhi();
    const int vsize = src.vertices.size() * sizeof(float);
    const int isize = src.indices.size()  * sizeof(quint32);

    dst.vbuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vsize);
    dst.ibuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,  isize);
    // UBO layout: mvp(64) + model(64) + color(16) + lightDir(16) = 160
    dst.ubuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 160);
    dst.vbuf->create(); dst.ibuf->create(); dst.ubuf->create();

    if (!src.lineOnly) {
        dst.vbufSmooth = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vsize);
        dst.vbufSmooth->create();
    }

    dst.srb = rhi->newShaderResourceBindings();
    dst.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            dst.ubuf),
        QRhiShaderResourceBinding::uniformBuffer(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            m_lightUbuf)
    });
    dst.srb->create();

    batch->uploadStaticBuffer(dst.vbuf, src.vertices.constData());
    if (!src.lineOnly)
        batch->uploadStaticBuffer(dst.vbufSmooth, src.smoothVertices.constData());
    batch->uploadStaticBuffer(dst.ibuf, src.indices.constData());

    // Per-mesh wire UBO + SRB for outline rendering (not needed for lineOnly)
    if (!src.lineOnly) {
        dst.wireUbuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 160);
        dst.wireUbuf->create();
        dst.wireSrb = rhi->newShaderResourceBindings();
        dst.wireSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                dst.wireUbuf),
            QRhiShaderResourceBinding::uniformBuffer(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_lightUbuf)
        });
        dst.wireSrb->create();
    }

    dst.indexCount = src.indices.size();
    dst.transform  = src.transform;
    dst.color      = src.color;
    dst.lineOnly   = src.lineOnly;
    dst.isLightGizmo = src.isLightGizmo;
    dst.isCollision = src.isCollision;
    dst.hasCollisionAPI = src.hasCollisionAPI;

    // Collision wireframe: dedicated UBO + optional sparse wireframe buffers
    bool hasColWire = src.hasCollisionAPI &&
                      (!src.collisionWireIndices.isEmpty() || !src.edges.isEmpty());
    if (hasColWire) {
        dst.colUbuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 160);
        dst.colUbuf->create();
        dst.colSrb = rhi->newShaderResourceBindings();
        dst.colSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                dst.colUbuf),
            QRhiShaderResourceBinding::uniformBuffer(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                m_lightUbuf)
        });
        dst.colSrb->create();

        // Upload sparse wireframe geometry if available (simple types),
        // otherwise fall back to full triangle edges (Mesh type)
        if (!src.collisionWireIndices.isEmpty()) {
            const int cvsize = src.collisionWireVerts.size() * sizeof(float);
            const int cisize = src.collisionWireIndices.size() * sizeof(quint32);
            dst.colVbuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, cvsize);
            dst.colIbuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, cisize);
            dst.colVbuf->create(); dst.colIbuf->create();
            batch->uploadStaticBuffer(dst.colVbuf, src.collisionWireVerts.constData());
            batch->uploadStaticBuffer(dst.colIbuf, src.collisionWireIndices.constData());
            dst.colWireCount = src.collisionWireIndices.size();
        }
    }

    // Compute centroid from vertices (model space)
    {
        int vertCount = src.vertices.size() / 6;
        QVector3D c(0, 0, 0);
        const float *v = src.vertices.constData();
        for (int vi = 0; vi < vertCount; ++vi)
            c += QVector3D(v[vi*6], v[vi*6+1], v[vi*6+2]);
        if (vertCount > 0) c /= float(vertCount);
        dst.centroid = c;
    }

    // Edge index buffer (not needed for lineOnly meshes)
    if (!src.lineOnly && !src.edges.isEmpty()) {
        const int esize = src.edges.size() * sizeof(quint32);
        dst.ibufEdge = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, esize);
        dst.ibufEdge->create();
        batch->uploadStaticBuffer(dst.ibufEdge, src.edges.constData());
        dst.edgeCount = src.edges.size();
    }
}

void UsdViewportRenderer::destroyGizmoMeshes()
{
    for (auto &g : m_rhiGizmo) {
        delete g.vbuf; delete g.ibuf;
        delete g.ubuf; delete g.srb;
    }
    m_rhiGizmo.clear();
}

void UsdViewportRenderer::uploadGizmoMesh(RhiGizmoMesh &dst, const GizmoMeshData &src,
                                            QRhiResourceUpdateBatch *batch)
{
    QRhi *rhi = renderTarget()->rhi();
    const int vsize = src.vertices.size() * sizeof(float);
    const int isize = src.indices.size()  * sizeof(quint32);

    dst.vbuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vsize);
    dst.ibuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,  isize);
    dst.ubuf = rhi->newBuffer(QRhiBuffer::Dynamic,   QRhiBuffer::UniformBuffer, 160);
    dst.vbuf->create(); dst.ibuf->create(); dst.ubuf->create();

    dst.srb = rhi->newShaderResourceBindings();
    dst.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            dst.ubuf),
        QRhiShaderResourceBinding::uniformBuffer(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            m_lightUbuf)
    });
    dst.srb->create();

    batch->uploadStaticBuffer(dst.vbuf, src.vertices.constData());
    batch->uploadStaticBuffer(dst.ibuf, src.indices.constData());

    dst.indexCount     = src.indices.size();
    dst.color          = src.color;
    dst.highlightColor = src.highlightColor;
}

void UsdViewportRenderer::initialize(QRhiCommandBuffer *)
{
    if (m_initialized) return;
    m_initialized = true;
    m_vs = loadShader(":/shaders/viewport.vert.qsb");
    m_fs = loadShader(":/shaders/viewport.frag.qsb");
    m_outlineVs = loadShader(":/shaders/outline_post.vert.qsb");
    m_outlineFs = loadShader(":/shaders/outline_post.frag.qsb");

    // Scene light UBO (std140: 48 bytes/light * 16 + 16 = 784)
    QRhi *rhi = renderTarget()->rhi();
    m_lightUbuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 784);
    m_lightUbuf->create();
}

void UsdViewportRenderer::synchronize(QQuickRhiItem *item)
{
    auto *vp = static_cast<UsdViewportItem *>(item);
    m_view = vp->viewMatrix();
    m_proj = vp->projMatrix();
    m_selectedIndices = vp->selectedMeshes();
    m_unitScale = vp->unitScale();
    m_cameraEye = vp->cameraEye();
    if (vp->meshDirty()) {
        m_pending = vp->meshes();
        m_sceneLights = vp->lights();
        vp->clearMeshDirty();
        m_rebuild = true;
    }

    // Gizmo state
    m_gizmoVisible = vp->gizmoVisible();
    m_gizmoHoveredPart = vp->gizmoHoveredPart();
    m_scaleCubeFactors = vp->scaleCubeFactors();
    int newGizmoMode = vp->gizmoMode();
    if (m_gizmoVisible) {
        QVector3D eye = m_view.inverted().column(3).toVector3D();
        float dist = (vp->gizmoWorldPos() - eye).length();
        float scale = dist * 0.08f;
        m_gizmoTransform.setToIdentity();
        m_gizmoTransform.translate(vp->gizmoWorldPos());
        m_gizmoTransform.scale(scale);
    }
    // Rebuild gizmo meshes when mode changes or first time
    if (newGizmoMode != m_gizmoMode || (m_rhiGizmo.isEmpty() && !vp->activeGizmoMeshes().isEmpty())) {
        m_gizmoMode = newGizmoMode;
        if (!vp->activeGizmoMeshes().isEmpty()) {
            m_gizmoPending = vp->activeGizmoMeshes();
            m_gizmoRebuild = true;
        } else if (m_gizmoMode == GizmoModeNone) {
            // Mode switched to none — no need to rebuild, just clear
            m_gizmoRebuild = false;
        }
    }

    // Orientation indicator state
    m_logicalSize = QSizeF(vp->width(), vp->height());
    {
        QMatrix4x4 rotView;
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                rotView(r, c) = m_view(r, c);
        rotView(2, 3) = -5.f;
        rotView(3, 3) = 1.f;
        m_orientView = rotView;
    }
    m_orientProj.setToIdentity();
    m_orientProj.ortho(-1.4f, 1.4f, -1.4f, 1.4f, 0.1f, 100.f);
    if (m_rhiOrientAxes.isEmpty() && !vp->orientAxesMeshes().isEmpty()) {
        m_orientPending = vp->orientAxesMeshes();
        m_orientRebuild = true;
    }

    // Collision display mode
    m_collisionDisplayMode = vp->collisionDisplayMode();

    // Grid state
    m_showGrid = vp->showGrid() && vp->document() && vp->document()->isOpen();
    if (vp->gridDirty()) {
        m_gridPending = vp->gridMeshes();
        vp->clearGridDirty();
        m_gridRebuild = true;
    }
}

void UsdViewportRenderer::render(QRhiCommandBuffer *cb)
{
    QRhi *rhi = renderTarget()->rhi();
    QRhiResourceUpdateBatch *upd = rhi->nextResourceUpdateBatch();

    if (m_rebuild) {
        destroyMeshes();
        delete m_pipeline; m_pipeline = nullptr;
        delete m_wirePipeline; m_wirePipeline = nullptr;
        delete m_stencilPipeline; m_stencilPipeline = nullptr;
        delete m_lineMeshPipeline; m_lineMeshPipeline = nullptr;
        delete m_collisionPipeline; m_collisionPipeline = nullptr;
        delete m_maskPipeline; m_maskPipeline = nullptr;
        m_meshes.resize(m_pending.size());
        for (int i = 0; i < m_pending.size(); i++)
            uploadMesh(m_meshes[i], m_pending[i], upd);
        m_rebuild = false;
    }

    // Create gizmo RHI resources (once)
    if (m_gizmoRebuild && !m_gizmoPending.isEmpty()) {
        destroyGizmoMeshes();
        delete m_gizmoPipeline; m_gizmoPipeline = nullptr;
        m_rhiGizmo.resize(m_gizmoPending.size());
        for (int i = 0; i < m_gizmoPending.size(); i++)
            uploadGizmoMesh(m_rhiGizmo[i], m_gizmoPending[i], upd);
        m_gizmoPending.clear();
        m_gizmoRebuild = false;
    }

    // Create orient axes RHI resources (once)
    if (m_orientRebuild && !m_orientPending.isEmpty()) {
        for (auto &g : m_rhiOrientAxes) {
            delete g.vbuf; delete g.ibuf;
            delete g.ubuf; delete g.srb;
        }
        m_rhiOrientAxes.clear();
        delete m_gizmoPipeline; m_gizmoPipeline = nullptr;
        m_rhiOrientAxes.resize(m_orientPending.size());
        for (int i = 0; i < m_orientPending.size(); i++)
            uploadGizmoMesh(m_rhiOrientAxes[i], m_orientPending[i], upd);
        m_orientPending.clear();
        m_orientRebuild = false;
    }

    // Create grid RHI resources
    if (m_gridRebuild && !m_gridPending.isEmpty()) {
        for (auto &g : m_rhiGrid) {
            delete g.vbuf; delete g.ibuf;
            delete g.ubuf; delete g.srb;
        }
        m_rhiGrid.clear();
        delete m_gridPipeline; m_gridPipeline = nullptr;
        m_rhiGrid.resize(m_gridPending.size());
        for (int i = 0; i < m_gridPending.size(); i++)
            uploadGizmoMesh(m_rhiGrid[i], m_gridPending[i], upd);
        m_gridPending.clear();
        m_gridRebuild = false;
    }

    if (m_meshes.isEmpty() && !(m_showGrid && !m_rhiGrid.isEmpty())) {
        cb->beginPass(renderTarget(), QColor(30, 30, 30), {1.f, 0}, upd);
        cb->endPass();
        return;
    }

    if (!m_pipeline && !m_meshes.isEmpty()) {
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });

        // Find first non-lineOnly mesh for pipeline SRB layout
        int solidIdx = -1;
        for (int i = 0; i < m_meshes.size(); ++i) {
            if (!m_meshes[i].lineOnly) { solidIdx = i; break; }
        }

        // Solid pipeline (Triangles)
        m_pipeline = rhi->newGraphicsPipeline();
        QRhiGraphicsPipeline::TargetBlend blend; blend.enable = false;
        m_pipeline->setTargetBlends({blend});
        m_pipeline->setDepthTest(true);
        m_pipeline->setDepthWrite(true);
        m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_pipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_pipeline->setVertexInputLayout(il);
        m_pipeline->setShaderResourceBindings(m_meshes[0].srb);
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->setSampleCount(renderTarget()->sampleCount());
        m_pipeline->create();

        // Line mesh pipeline (Lines topology, depth test ON, for light gizmo wireframes)
        m_lineMeshPipeline = rhi->newGraphicsPipeline();
        m_lineMeshPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_lineMeshPipeline->setLineWidth(1.f);
        QRhiGraphicsPipeline::TargetBlend lineBlend; lineBlend.enable = false;
        m_lineMeshPipeline->setTargetBlends({lineBlend});
        m_lineMeshPipeline->setDepthTest(true);
        m_lineMeshPipeline->setDepthWrite(true);
        m_lineMeshPipeline->setStencilTest(false);
        m_lineMeshPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_lineMeshPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_lineMeshPipeline->setVertexInputLayout(il);
        m_lineMeshPipeline->setShaderResourceBindings(m_meshes[0].srb);
        m_lineMeshPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_lineMeshPipeline->setSampleCount(renderTarget()->sampleCount());
        m_lineMeshPipeline->create();

        // Collision wireframe pipeline (Lines topology, depth test LessOrEqual)
        // LessOrEqual so wireframe at the same depth as its solid surface shows,
        // while objects in front (e.g. shirt on table) properly occlude it.
        m_collisionPipeline = rhi->newGraphicsPipeline();
        m_collisionPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        m_collisionPipeline->setLineWidth(1.f);
        QRhiGraphicsPipeline::TargetBlend colBlend; colBlend.enable = false;
        m_collisionPipeline->setTargetBlends({colBlend});
        m_collisionPipeline->setDepthTest(true);
        m_collisionPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        m_collisionPipeline->setDepthWrite(false);
        m_collisionPipeline->setStencilTest(false);
        m_collisionPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_collisionPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_collisionPipeline->setVertexInputLayout(il);
        m_collisionPipeline->setShaderResourceBindings(m_meshes[0].srb);
        m_collisionPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_collisionPipeline->setSampleCount(renderTarget()->sampleCount());
        m_collisionPipeline->create();

        // Stencil/wire pipelines only if there are solid meshes
        if (solidIdx >= 0) {
        // Stencil-write pipeline: render selected mesh to mark stencil=1, no color output
        m_stencilPipeline = rhi->newGraphicsPipeline();
        m_stencilPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        {
            QRhiGraphicsPipeline::TargetBlend stBlend;
            stBlend.enable = false;
            stBlend.colorWrite = {};  // disable all color writes
            m_stencilPipeline->setTargetBlends({stBlend});
        }
        m_stencilPipeline->setDepthTest(false);  // ignore depth so stencil always writes
        m_stencilPipeline->setDepthWrite(false);
        m_stencilPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_stencilPipeline->setStencilTest(true);
        m_stencilPipeline->setStencilWriteMask(0xFF);
        m_stencilPipeline->setStencilReadMask(0xFF);
        {
            QRhiGraphicsPipeline::StencilOpState sop;
            sop.compareOp   = QRhiGraphicsPipeline::Always;
            sop.passOp      = QRhiGraphicsPipeline::Replace;
            sop.failOp      = QRhiGraphicsPipeline::Replace;
            sop.depthFailOp = QRhiGraphicsPipeline::Replace;
            m_stencilPipeline->setStencilFront(sop);
            m_stencilPipeline->setStencilBack(sop);
        }
        m_stencilPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_stencilPipeline->setVertexInputLayout(il);
        m_stencilPipeline->setShaderResourceBindings(m_meshes[solidIdx].wireSrb);
        m_stencilPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_stencilPipeline->setSampleCount(renderTarget()->sampleCount());
        m_stencilPipeline->create();

        // Outline pipeline: draw expanded mesh only where stencil != 1
        m_wirePipeline = rhi->newGraphicsPipeline();
        m_wirePipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        QRhiGraphicsPipeline::TargetBlend wireBlend; wireBlend.enable = false;
        m_wirePipeline->setTargetBlends({wireBlend});
        m_wirePipeline->setDepthTest(false);
        m_wirePipeline->setDepthWrite(false);
        m_wirePipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_wirePipeline->setStencilTest(true);
        m_wirePipeline->setStencilWriteMask(0x00);
        m_wirePipeline->setStencilReadMask(0xFF);
        {
            QRhiGraphicsPipeline::StencilOpState sop;
            sop.compareOp  = QRhiGraphicsPipeline::NotEqual;
            sop.passOp     = QRhiGraphicsPipeline::Keep;
            sop.failOp     = QRhiGraphicsPipeline::Keep;
            sop.depthFailOp = QRhiGraphicsPipeline::Keep;
            m_wirePipeline->setStencilFront(sop);
            m_wirePipeline->setStencilBack(sop);
        }
        m_wirePipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_wirePipeline->setVertexInputLayout(il);
        m_wirePipeline->setShaderResourceBindings(m_meshes[solidIdx].wireSrb);
        m_wirePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_wirePipeline->setSampleCount(renderTarget()->sampleCount());
        m_wirePipeline->create();
        } // solidIdx >= 0
    }

    // Gizmo / orient axes pipeline (no depth test, always on top)
    if (!m_gizmoPipeline && (!m_rhiGizmo.isEmpty() || !m_rhiOrientAxes.isEmpty())) {
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });

        m_gizmoPipeline = rhi->newGraphicsPipeline();
        QRhiGraphicsPipeline::TargetBlend gizmoBlend;
        gizmoBlend.enable = false;
        m_gizmoPipeline->setTargetBlends({gizmoBlend});
        m_gizmoPipeline->setDepthTest(false);
        m_gizmoPipeline->setDepthWrite(false);
        m_gizmoPipeline->setStencilTest(false);
        m_gizmoPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_gizmoPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_gizmoPipeline->setVertexInputLayout(il);
        QRhiShaderResourceBindings *srb = !m_rhiGizmo.isEmpty()
            ? m_rhiGizmo[0].srb : m_rhiOrientAxes[0].srb;
        m_gizmoPipeline->setShaderResourceBindings(srb);
        m_gizmoPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_gizmoPipeline->setSampleCount(renderTarget()->sampleCount());
        m_gizmoPipeline->create();
    }

    // Grid pipeline (Lines topology, depth test ON)
    if (!m_gridPipeline && !m_rhiGrid.isEmpty()) {
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });

        m_gridPipeline = rhi->newGraphicsPipeline();
        m_gridPipeline->setTopology(QRhiGraphicsPipeline::Lines);
        QRhiGraphicsPipeline::TargetBlend gridBlend;
        gridBlend.enable = false;
        m_gridPipeline->setTargetBlends({gridBlend});
        m_gridPipeline->setDepthTest(true);
        m_gridPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual); // allow grid at far plane (bias may push z to 1.0)
        m_gridPipeline->setDepthWrite(false);      // grid never occludes geometry
        m_gridPipeline->setStencilTest(false);
        m_gridPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_gridPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_gridPipeline->setVertexInputLayout(il);
        m_gridPipeline->setShaderResourceBindings(m_rhiGrid[0].srb);
        m_gridPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_gridPipeline->setSampleCount(renderTarget()->sampleCount());
        m_gridPipeline->create();
    }

    const QSize sz = renderTarget()->pixelSize();

    // === Post-process outline: mask render target + pipelines ===
    bool hasOutlineSelection = false;
    for (int i : m_selectedIndices) {
        if (i >= 0 && i < m_meshes.size() && !m_meshes[i].lineOnly) {
            hasOutlineSelection = true; break;
        }
    }

    // Resize mask texture when viewport size changes
    if (m_maskTex && m_maskSize != sz) {
        delete m_outlinePipeline; m_outlinePipeline = nullptr;
        delete m_maskPipeline; m_maskPipeline = nullptr;
        delete m_outlineSrb; m_outlineSrb = nullptr;
        delete m_maskRpDesc; m_maskRpDesc = nullptr;
        delete m_maskRT; m_maskRT = nullptr;
        delete m_maskDS; m_maskDS = nullptr;
        delete m_maskTex; m_maskTex = nullptr;
    }

    // Create mask render target (RGBA8 texture + depth renderbuffer)
    if (hasOutlineSelection && !m_maskTex) {
        m_maskTex = rhi->newTexture(QRhiTexture::RGBA8, sz, 1);
        m_maskTex->create();
        m_maskDS = rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, sz, 1);
        m_maskDS->create();
        QRhiColorAttachment maskAtt(m_maskTex);
        QRhiTextureRenderTargetDescription rtDesc(maskAtt);
        rtDesc.setDepthStencilBuffer(m_maskDS);
        m_maskRT = rhi->newTextureRenderTarget(rtDesc);
        m_maskRpDesc = m_maskRT->newCompatibleRenderPassDescriptor();
        m_maskRT->setRenderPassDescriptor(m_maskRpDesc);
        m_maskRT->create();
        m_maskSize = sz;
    }

    // Mask pipeline: render selected meshes to mask texture (no depth test, both faces)
    if (hasOutlineSelection && m_maskRT && !m_maskPipeline && !m_meshes.isEmpty()) {
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });
        int refIdx = 0;
        for (int i = 0; i < m_meshes.size(); ++i)
            if (!m_meshes[i].lineOnly) { refIdx = i; break; }

        m_maskPipeline = rhi->newGraphicsPipeline();
        QRhiGraphicsPipeline::TargetBlend blend; blend.enable = false;
        m_maskPipeline->setTargetBlends({blend});
        m_maskPipeline->setDepthTest(false);
        m_maskPipeline->setDepthWrite(false);
        m_maskPipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_maskPipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_vs },
            { QRhiShaderStage::Fragment, m_fs }
        });
        m_maskPipeline->setVertexInputLayout(il);
        m_maskPipeline->setShaderResourceBindings(m_meshes[refIdx].srb);
        m_maskPipeline->setRenderPassDescriptor(m_maskRpDesc);
        m_maskPipeline->setSampleCount(1);
        m_maskPipeline->create();
    }

    // Outline post-process pipeline
    if (hasOutlineSelection && m_maskTex && !m_outlinePipeline) {
        // Fullscreen triangle vertex buffer [pos.xy, uv.xy]
        if (!m_fsTriVbuf) {
            static const float fsTriData[] = {
                -1.f, -1.f,  0.f, 0.f,
                 3.f, -1.f,  2.f, 0.f,
                -1.f,  3.f,  0.f, 2.f,
            };
            m_fsTriVbuf = rhi->newBuffer(QRhiBuffer::Immutable,
                                          QRhiBuffer::VertexBuffer, sizeof(fsTriData));
            m_fsTriVbuf->create();
            upd->uploadStaticBuffer(m_fsTriVbuf, fsTriData);
        }
        if (!m_maskSampler) {
            m_maskSampler = rhi->newSampler(
                QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
            m_maskSampler->create();
        }
        if (!m_outlineUbuf) {
            m_outlineUbuf = rhi->newBuffer(QRhiBuffer::Dynamic,
                                            QRhiBuffer::UniformBuffer, 32);
            m_outlineUbuf->create();
        }
        if (!m_outlineSrb) {
            m_outlineSrb = rhi->newShaderResourceBindings();
            m_outlineSrb->setBindings({
                QRhiShaderResourceBinding::sampledTexture(
                    0, QRhiShaderResourceBinding::FragmentStage,
                    m_maskTex, m_maskSampler),
                QRhiShaderResourceBinding::uniformBuffer(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    m_outlineUbuf)
            });
            m_outlineSrb->create();
        }

        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(4 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float))
        });
        m_outlinePipeline = rhi->newGraphicsPipeline();
        QRhiGraphicsPipeline::TargetBlend blend; blend.enable = false;
        m_outlinePipeline->setTargetBlends({blend});
        m_outlinePipeline->setDepthTest(false);
        m_outlinePipeline->setDepthWrite(false);
        m_outlinePipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_outlinePipeline->setShaderStages({
            { QRhiShaderStage::Vertex,   m_outlineVs },
            { QRhiShaderStage::Fragment, m_outlineFs }
        });
        m_outlinePipeline->setVertexInputLayout(il);
        m_outlinePipeline->setShaderResourceBindings(m_outlineSrb);
        m_outlinePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_outlinePipeline->setSampleCount(renderTarget()->sampleCount());
        m_outlinePipeline->create();
    }

    struct alignas(16) UBuf {
        float mvp[16], model[16], color[4], lightDir[4];
    };

    // Upload scene lights UBO
    {
        struct alignas(16) LightUBuf {
            struct { float posType[4]; float dirRadius[4]; float color[4]; } lights[16];
            int numLights[4];
        };
        LightUBuf lub{};
        int count = qMin(m_sceneLights.size(), 16);
        for (int i = 0; i < count; ++i) {
            const auto &l = m_sceneLights[i];
            lub.lights[i].posType[0] = l.position.x();
            lub.lights[i].posType[1] = l.position.y();
            lub.lights[i].posType[2] = l.position.z();
            lub.lights[i].posType[3] = float(l.type);
            lub.lights[i].dirRadius[0] = l.direction.x();
            lub.lights[i].dirRadius[1] = l.direction.y();
            lub.lights[i].dirRadius[2] = l.direction.z();
            lub.lights[i].dirRadius[3] = l.radius;
            lub.lights[i].color[0] = l.color.x();
            lub.lights[i].color[1] = l.color.y();
            lub.lights[i].color[2] = l.color.z();
            lub.lights[i].color[3] = 0.f;
        }
        lub.numLights[0] = count;
        upd->updateDynamicBuffer(m_lightUbuf, 0, sizeof(LightUBuf), &lub);
    }

    for (int i = 0; i < m_meshes.size(); ++i) {
        auto &m = m_meshes[i];
        UBuf ub{};
        QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix() * m_proj * m_view * m.transform;
        memcpy(ub.mvp,   mvp.constData(),          64);
        memcpy(ub.model, m.transform.constData(),   64);
        ub.color[0] = m.color.x(); ub.color[1] = m.color.y();
        ub.color[2] = m.color.z();
        ub.color[3] = (m.lineOnly || m.isCollision) ? 0.f : 1.f;  // flat color for wireframe
        // Pass camera eye in lightDir.xyz for normal-flip in fragment shader
        ub.lightDir[0] = m_cameraEye.x(); ub.lightDir[1] = m_cameraEye.y();
        ub.lightDir[2] = m_cameraEye.z(); ub.lightDir[3] = 0.f;
        upd->updateDynamicBuffer(m.ubuf, 0, sizeof(UBuf), &ub);

        // Pre-upload outline UBO for selected meshes (skip lineOnly — no stencil outline)
        if (m_selectedIndices.contains(i) && !m.lineOnly && m.wireUbuf) {
            UBuf wub{};
            memcpy(wub.mvp,   mvp.constData(),          64);
            memcpy(wub.model, m.transform.constData(),   64);
            wub.color[0] = 1.f; wub.color[1] = 0.78f;
            wub.color[2] = 0.f; wub.color[3] = 0.f;  // a=0 → flat color mode
            // lightDir.xyz = centroid (model space), lightDir.w = push amount
            wub.lightDir[0] = m.centroid.x(); wub.lightDir[1] = m.centroid.y();
            wub.lightDir[2] = m.centroid.z(); wub.lightDir[3] = 0.004f;
            upd->updateDynamicBuffer(m.wireUbuf, 0, sizeof(UBuf), &wub);
        }

        // Pre-upload collision wireframe UBO (green flat, dedicated buffer)
        if (m.hasCollisionAPI && m.colUbuf) {
            UBuf cub{};
            memcpy(cub.mvp,   mvp.constData(),          64);
            memcpy(cub.model, m.transform.constData(),   64);
            cub.color[0] = 0.f; cub.color[1] = 0.8f;
            cub.color[2] = 0.f; cub.color[3] = 0.f;  // flat green
            memset(cub.lightDir, 0, 16);
            upd->updateDynamicBuffer(m.colUbuf, 0, sizeof(UBuf), &cub);
        }
    }

    // Gizmo UBO updates
    if (m_gizmoVisible && !m_rhiGizmo.isEmpty()) {
        QMatrix4x4 gizmoMVP = rhi->clipSpaceCorrMatrix() * m_proj * m_view * m_gizmoTransform;
        for (int i = 0; i < m_rhiGizmo.size(); ++i) {
            auto &g = m_rhiGizmo[i];
            UBuf ub{};

            // Scale gizmo cube tips: apply sliding offset
            QMatrix4x4 partMVP = gizmoMVP;
            QMatrix4x4 partModel = m_gizmoTransform;
            if (m_gizmoMode == GizmoModeScale && i >= GizmoCount) {
                // Cube tip center is at 0.84 along its axis in local space.
                // Offset = 0.84 * (factor - 1) to slide to new position.
                static const QVector3D axes[] = { {1,0,0}, {0,1,0}, {0,0,1} };
                int axisIdx = i - GizmoCount; // 0=X, 1=Y, 2=Z
                float factor = (axisIdx == 0) ? m_scaleCubeFactors.x()
                             : (axisIdx == 1) ? m_scaleCubeFactors.y()
                                              : m_scaleCubeFactors.z();
                QMatrix4x4 offset;
                offset.translate(axes[axisIdx] * 0.84f * (factor - 1.f));
                partMVP = gizmoMVP * offset;
                partModel = m_gizmoTransform * offset;
            }

            memcpy(ub.mvp,   partMVP.constData(),    64);
            memcpy(ub.model, partModel.constData(),   64);
            bool highlighted = (m_gizmoHoveredPart == i);
            if (m_gizmoMode == GizmoModeScale) {
                // Highlight cube tip when its axis is hovered (and vice versa)
                if (i >= GizmoCount)
                    highlighted = highlighted || (m_gizmoHoveredPart == i - GizmoCount);
                else if (i <= AxisZ)
                    highlighted = highlighted || (m_gizmoHoveredPart == i + GizmoCount);
                // Also highlight planes when origin hovered
                highlighted = highlighted
                    || (m_gizmoHoveredPart == Origin && i >= PlaneXY && i <= PlaneYZ);
            } else if (m_gizmoMode != GizmoModeRotate) {
                // Translate: also highlight planes when origin hovered
                highlighted = highlighted
                    || (m_gizmoHoveredPart == Origin && i >= PlaneXY && i <= PlaneYZ);
            }
            QVector3D c = highlighted ? g.highlightColor : g.color;
            ub.color[0] = c.x(); ub.color[1] = c.y(); ub.color[2] = c.z();
            ub.color[3] = 0.f; // flat color mode
            memset(ub.lightDir, 0, 16); // no push
            upd->updateDynamicBuffer(g.ubuf, 0, sizeof(UBuf), &ub);
        }
    }

    // Orient axes UBO updates
    if (!m_rhiOrientAxes.isEmpty()) {
        QMatrix4x4 orientMVP = rhi->clipSpaceCorrMatrix() * m_orientProj * m_orientView;
        QMatrix4x4 id;
        for (int i = 0; i < m_rhiOrientAxes.size(); ++i) {
            auto &g = m_rhiOrientAxes[i];
            UBuf ub{};
            memcpy(ub.mvp,   orientMVP.constData(), 64);
            memcpy(ub.model, id.constData(),         64);
            ub.color[0] = g.color.x(); ub.color[1] = g.color.y();
            ub.color[2] = g.color.z(); ub.color[3] = 0.f;
            memset(ub.lightDir, 0, 16);
            upd->updateDynamicBuffer(g.ubuf, 0, sizeof(UBuf), &ub);
        }
    }

    // Grid UBO updates
    if (m_showGrid && !m_rhiGrid.isEmpty()) {
        QMatrix4x4 gridMVP = rhi->clipSpaceCorrMatrix() * m_proj * m_view;
        QMatrix4x4 id;
        for (int i = 0; i < m_rhiGrid.size(); ++i) {
            auto &g = m_rhiGrid[i];
            UBuf ub{};
            memcpy(ub.mvp,   gridMVP.constData(), 64);
            memcpy(ub.model, id.constData(),       64);
            ub.color[0] = g.color.x(); ub.color[1] = g.color.y();
            ub.color[2] = g.color.z(); ub.color[3] = 0.f; // flat color mode
            ub.lightDir[0] = 0.f; ub.lightDir[1] = 0.f;
            ub.lightDir[2] = 0.f; ub.lightDir[3] = -1.f; // grid mode: push depth in frag shader
            upd->updateDynamicBuffer(g.ubuf, 0, sizeof(UBuf), &ub);
        }
    }

    // Outline UBO upload
    if (hasOutlineSelection && m_outlineUbuf) {
        struct alignas(16) OutUBuf { float color[4]; float params[4]; };
        OutUBuf ou{};
        ou.color[0] = 1.f; ou.color[1] = 0.78f; ou.color[2] = 0.f; ou.color[3] = 1.f;
        ou.params[0] = 1.f / float(sz.width());   // texelSize.x
        ou.params[1] = 1.f / float(sz.height());  // texelSize.y
        ou.params[2] = 3.f;                        // radius in pixels
        ou.params[3] = rhi->isYUpInFramebuffer() ? 0.f : 1.f; // flip UV.y for Metal/Vulkan
        upd->updateDynamicBuffer(m_outlineUbuf, 0, sizeof(OutUBuf), &ou);
    }

    // --- Mask pass: render selected meshes to mask texture ---
    QRhiResourceUpdateBatch *updForMain = upd;
    if (hasOutlineSelection && m_maskRT && m_maskPipeline) {
        cb->beginPass(m_maskRT, QColor(0, 0, 0, 0), {1.f, 0}, upd);
        updForMain = nullptr; // upd consumed by mask pass
        cb->setGraphicsPipeline(m_maskPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        for (int selIdx : m_selectedIndices) {
            if (selIdx < 0 || selIdx >= m_meshes.size()) continue;
            auto &sel = m_meshes[selIdx];
            if (sel.lineOnly) continue;
            cb->setShaderResources(sel.srb);
            QRhiCommandBuffer::VertexInput vi(sel.vbuf, 0);
            cb->setVertexInput(0, 1, &vi, sel.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(sel.indexCount);
        }
        cb->endPass();
    }

    cb->beginPass(renderTarget(), QColor(30, 30, 30), {1.f, 0}, updForMain);

    if (m_pipeline) {
    // Draw solid (triangle) meshes — skip light gizmos unless selected, skip collision
    cb->setGraphicsPipeline(m_pipeline);
    cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
    for (int i = 0; i < m_meshes.size(); ++i) {
        auto &m = m_meshes[i];
        if (m.lineOnly) continue;
        if (m.isCollision) continue;
        if (m.isLightGizmo && !m_selectedIndices.contains(i)) continue;
        cb->setShaderResources(m.srb);
        QRhiCommandBuffer::VertexInput vi(m.vbuf, 0);
        cb->setVertexInput(0, 1, &vi, m.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(m.indexCount);
    }

    // Grid rendering (after solid meshes so ground plane occludes grid via depth test)
    if (m_showGrid && m_gridPipeline && !m_rhiGrid.isEmpty()) {
        cb->setGraphicsPipeline(m_gridPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        for (auto &g : m_rhiGrid) {
            if (g.indexCount == 0) continue;
            cb->setShaderResources(g.srb);
            QRhiCommandBuffer::VertexInput gvi(g.vbuf, 0);
            cb->setVertexInput(0, 1, &gvi, g.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(g.indexCount);
        }
    }

    // Draw line-only meshes (light gizmo wireframes) — only when selected
    if (m_lineMeshPipeline) {
        cb->setGraphicsPipeline(m_lineMeshPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        for (int i = 0; i < m_meshes.size(); ++i) {
            auto &m = m_meshes[i];
            if (!m.lineOnly) continue;
            if (m.isLightGizmo && !m_selectedIndices.contains(i)) continue;
            cb->setShaderResources(m.srb);
            QRhiCommandBuffer::VertexInput vi(m.vbuf, 0);
            cb->setVertexInput(0, 1, &vi, m.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(m.indexCount);
        }
    }

    // Draw collision wireframe — use sparse wireframe if available, else full edges
    if (m_collisionPipeline && m_collisionDisplayMode > 0) {
        cb->setGraphicsPipeline(m_collisionPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        for (int i = 0; i < m_meshes.size(); ++i) {
            auto &m = m_meshes[i];
            if (!m.hasCollisionAPI || !m.colSrb) continue;
            if (m_collisionDisplayMode == 1 && !m_selectedIndices.contains(i)) continue;
            cb->setShaderResources(m.colSrb);
            if (m.colVbuf && m.colIbuf && m.colWireCount > 0) {
                // Sparse wireframe (simple types: Sphere, Cube, Cylinder, etc.)
                QRhiCommandBuffer::VertexInput vi(m.colVbuf, 0);
                cb->setVertexInput(0, 1, &vi, m.colIbuf, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(m.colWireCount);
            } else if (m.ibufEdge && m.edgeCount > 0) {
                // Full triangle edges (Mesh type)
                QRhiCommandBuffer::VertexInput vi(m.vbuf, 0);
                cb->setVertexInput(0, 1, &vi, m.ibufEdge, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(m.edgeCount);
            }
        }
    }

    // Post-process selection outline (fullscreen edge detection on mask texture)
    if (hasOutlineSelection && m_outlinePipeline && m_fsTriVbuf) {
        cb->setGraphicsPipeline(m_outlinePipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        cb->setShaderResources(m_outlineSrb);
        QRhiCommandBuffer::VertexInput fvi(m_fsTriVbuf, 0);
        cb->setVertexInput(0, 1, &fvi);
        cb->draw(3);
    }
    } // if (m_pipeline)

    // Gizmo rendering (always on top, no depth test)
    if (m_gizmoVisible && m_gizmoPipeline && !m_rhiGizmo.isEmpty()) {
        cb->setGraphicsPipeline(m_gizmoPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        for (auto &g : m_rhiGizmo) {
            cb->setShaderResources(g.srb);
            QRhiCommandBuffer::VertexInput gvi(g.vbuf, 0);
            cb->setVertexInput(0, 1, &gvi, g.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(g.indexCount);
        }
    }

    // Orientation axes (bottom-left mini viewport)
    if (m_gizmoPipeline && !m_rhiOrientAxes.isEmpty()) {
        cb->setGraphicsPipeline(m_gizmoPipeline);
        float dpr = m_logicalSize.width() > 0
            ? float(sz.width()) / float(m_logicalSize.width()) : 1.f;
        int miniPx = int(80.f * dpr);
        int marginPx = int(10.f * dpr);
        cb->setViewport(QRhiViewport(marginPx, marginPx,
                                      miniPx, miniPx));
        for (auto &g : m_rhiOrientAxes) {
            cb->setShaderResources(g.srb);
            QRhiCommandBuffer::VertexInput gvi(g.vbuf, 0);
            cb->setVertexInput(0, 1, &gvi, g.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(g.indexCount);
        }
    }

    cb->endPass();
}

