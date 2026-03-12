#include "UsdViewportItem.h"
#include "UsdDocument.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSet>
#include <QtMath>
#include <rhi/qrhi.h>
#include <cfloat>

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
#include <pxr/usd/usdGeom/metrics.h>
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
    };

    void destroyMeshes();
    void uploadMesh(RhiMesh &dst, const MeshData &src, QRhiResourceUpdateBatch *batch);

    QRhiGraphicsPipeline *m_pipeline       = nullptr;
    QRhiGraphicsPipeline *m_wirePipeline   = nullptr;
    QRhiGraphicsPipeline *m_stencilPipeline = nullptr;
    QShader m_vs, m_fs;
    bool m_initialized = false;

    QVector<RhiMesh>  m_meshes;
    QVector<MeshData> m_pending;
    bool m_rebuild = true;
    QSet<int> m_selectedIndices;

    QMatrix4x4 m_view, m_proj;
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

static void genCube(float size, QVector<float> &v, QVector<quint32> &idx)
{
    float h = size * 0.5f;
    struct Face { float nx,ny,nz; float p[4][3]; };
    static const Face faces[6] = {
        { 0, 0, 1, {{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}}},
        { 0, 0,-1, {{ h,-h,-h},{-h,-h,-h},{-h,h,-h},{h,h,-h}}},
        {-1, 0, 0, {{-h,-h,-h},{-h,-h,h},{-h,h,h},{-h,h,-h}}},
        { 1, 0, 0, {{ h,-h,h},{h,-h,-h},{h,h,-h},{h,h,h}}},
        { 0, 1, 0, {{-h,h,h},{h,h,h},{h,h,-h},{-h,h,-h}}},
        { 0,-1, 0, {{-h,-h,-h},{h,-h,-h},{h,-h,h},{-h,-h,h}}}
    };
    v.clear(); idx.clear();
    quint32 base = 0;
    for (auto &f : faces) {
        for (int i = 0; i < 4; i++)
            v << f.p[i][0] << f.p[i][1] << f.p[i][2] << f.nx << f.ny << f.nz;
        idx << base << base+1 << base+2 << base << base+2 << base+3;
        base += 4;
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

    int fviOffset = 0;
    int faceIdx   = 0;
    for (int fvc : fvcArray) {
        if (fvc < 3) { fviOffset += fvc; ++faceIdx; continue; }

        // Compute face normal (used when no authored normals)
        GfVec3f fn(0, 1, 0);
        if (!hasNormals) {
            GfVec3f p0 = points[fviArray[fviOffset]];
            GfVec3f p1 = points[fviArray[fviOffset + 1]];
            GfVec3f p2 = points[fviArray[fviOffset + 2]];
            fn = GfCross(p1 - p0, p2 - p0);
            if (fn.GetLengthSq() > 1e-10f) fn.Normalize();
        }

        // Fan triangulation from first vertex
        for (int tri = 1; tri < fvc - 1; tri++) {
            for (int k : {0, tri, tri + 1}) {
                int vi = fviArray[fviOffset + k];
                GfVec3f p = points[vi];
                GfVec3f n = fn;
                if (hasNormals) {
                    if (normInterp == UsdGeomTokens->faceVarying)
                        n = normals[fviOffset + k];
                    else if (normInterp == UsdGeomTokens->vertex)
                        n = (vi < (int)normals.size()) ? normals[vi] : fn;
                    else // uniform / constant
                        n = (faceIdx < (int)normals.size()) ? normals[faceIdx] : fn;
                    if (n.GetLengthSq() > 1e-10f) n.Normalize();
                }
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
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
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
                this, &UsdViewportItem::onDocumentChanged);
    }
    m_cameraInitialized = false;
    onDocumentChanged();
    emit documentChanged();
}

void UsdViewportItem::onDocumentChanged()
{
    buildMeshes();
    update();
}

void UsdViewportItem::buildMeshes()
{
    m_meshes.clear();
    if (!m_doc || !m_doc->isOpen()) { m_meshDirty = true; return; }

    auto *stageRef = static_cast<UsdStageRefPtr *>(m_doc->stagePtr());
    if (!stageRef || !*stageRef) { m_meshDirty = true; return; }
    const UsdStageRefPtr &stage = *stageRef;

    // Detect stage up-axis
    m_zUp = (UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->z);

    // XformCache for world transforms (time = default)
    UsdGeomXformCache xfCache;

    int meshColorIndex = 0;
    for (const UsdPrim &prim : stage->Traverse()) {
        const TfToken type = prim.GetTypeName();

        QVector<float>   verts;
        QVector<quint32> indices;

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
        } else {
            continue;
        }

        if (indices.isEmpty()) continue;

        MeshData md;
        md.vertices = std::move(verts);
        md.indices  = std::move(indices);

        // Display color — use palette fallback when not authored
        UsdGeomGprim gprim(prim);
        VtArray<GfVec3f> colors;
        gprim.GetDisplayColorAttr().Get(&colors);
        md.color = colors.empty()
                   ? colorForMesh(meshColorIndex)
                   : QVector3D(colors[0][0], colors[0][1], colors[0][2]);
        meshColorIndex++;

        // World transform via XformCache
        bool resetXform = false;
        GfMatrix4d xf = xfCache.GetLocalToWorldTransform(prim);
        float mf[16];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                mf[c*4+r] = float(xf[r][c]);
        md.transform = QMatrix4x4(mf);

        // Extract unique edges from triangle indices
        {
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

        computeSmoothNormals(md);
        m_meshes << md;
    }

    // Compute scene center on first load
    if (!m_cameraInitialized && !m_meshes.isEmpty()) {
        QVector3D center(0, 0, 0);
        for (const auto &md : m_meshes)
            center += QVector3D(md.transform(0, 3), md.transform(1, 3), md.transform(2, 3));
        center /= float(m_meshes.size());
        m_target = center;
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

    m_view.setToIdentity();
    m_view.lookAt(eye, m_target, up);

    const float aspect = (width() > 0 && height() > 0)
                         ? float(width()) / float(height()) : 1.f;
    m_proj.setToIdentity();
    m_proj.perspective(45.f, aspect, 0.1f, 500.f);
}

void UsdViewportItem::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse = e->position();
    m_pressPos  = e->position();
    m_panning = (e->modifiers() & Qt::AltModifier) || (e->button() == Qt::MiddleButton);
    m_dragging = true;
    e->accept();
}
void UsdViewportItem::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) return;
    QPointF d = e->position() - m_lastMouse; m_lastMouse = e->position();

    if (m_panning) {
        // Pan: move target along camera right/up vectors
        float scale = m_dist * 0.002f;
        QVector3D right(m_view(0, 0), m_view(0, 1), m_view(0, 2));
        QVector3D camUp(m_view(1, 0), m_view(1, 1), m_view(1, 2));
        m_target -= right * float(d.x()) * scale;
        m_target += camUp * float(d.y()) * scale;
    } else {
        m_yaw   += float(d.x()) * 0.5f;
        m_pitch += float(d.y()) * 0.5f;
    }
    updateCamera(); update(); e->accept();
}
void UsdViewportItem::mouseReleaseEvent(QMouseEvent *e)
{
    // Detect click (no significant drag)
    if (e->button() == Qt::LeftButton) {
        QPointF delta = e->position() - m_pressPos;
        if (delta.manhattanLength() < 3.0) {
            int hit = pickMesh(e->position());
            bool changed = false;
            if (e->modifiers() & Qt::ControlModifier) {
                // Ctrl+click: toggle hit in selection set
                if (hit >= 0) {
                    if (m_selectedMeshes.contains(hit))
                        m_selectedMeshes.remove(hit);
                    else
                        m_selectedMeshes.insert(hit);
                    changed = true;
                }
            } else {
                // Normal click: single select or clear
                QSet<int> newSel;
                if (hit >= 0) newSel.insert(hit);
                if (newSel != m_selectedMeshes) {
                    m_selectedMeshes = newSel;
                    changed = true;
                }
            }
            if (changed) {
                m_meshDirty = true;
                update();
                emit selectedPrimPathsChanged();
            }
        }
    }
    m_dragging = false; m_panning = false; e->accept();
}
void UsdViewportItem::wheelEvent(QWheelEvent *e)
{
    m_dist -= e->angleDelta().y() * 0.02f;
    if (m_dist < 0.01f) m_dist = 0.01f;
    updateCamera(); update(); e->accept();
}
void UsdViewportItem::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickRhiItem::geometryChange(n, o);
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
    if (!path.isEmpty()) {
        for (int i = 0; i < m_meshes.size(); ++i) {
            if (m_meshes[i].primPath == path) {
                newSel.insert(i);
                break;
            }
        }
    }
    if (newSel != m_selectedMeshes) {
        m_selectedMeshes = newSel;
        m_meshDirty = true;
        update();
        emit selectedPrimPathsChanged();
    }
}

void UsdViewportItem::togglePrimPath(const QString &path)
{
    if (path.isEmpty()) return;
    int idx = -1;
    for (int i = 0; i < m_meshes.size(); ++i) {
        if (m_meshes[i].primPath == path) {
            idx = i;
            break;
        }
    }
    if (idx < 0) return;
    if (m_selectedMeshes.contains(idx))
        m_selectedMeshes.remove(idx);
    else
        m_selectedMeshes.insert(idx);
    m_meshDirty = true;
    update();
    emit selectedPrimPathsChanged();
}

// ================================================================
//  CPU Ray Picking (Möller–Trumbore)
// ================================================================
int UsdViewportItem::pickMesh(const QPointF &pos) const
{
    // Build ray from camera
    QMatrix4x4 invVP = (m_proj * m_view).inverted();
    float nx = 2.f * float(pos.x()) / float(width())  - 1.f;
    float ny = 1.f - 2.f * float(pos.y()) / float(height());

    QVector4D nearH = invVP * QVector4D(nx, ny, -1.f, 1.f);
    QVector4D farH  = invVP * QVector4D(nx, ny,  1.f, 1.f);
    QVector3D nearP = nearH.toVector3DAffine();
    QVector3D farP  = farH.toVector3DAffine();
    QVector3D rayOrig = nearP;
    QVector3D rayDir  = (farP - nearP).normalized();

    float bestT = FLT_MAX;
    int   bestIdx = -1;

    for (int mi = 0; mi < m_meshes.size(); ++mi) {
        const MeshData &md = m_meshes[mi];
        // Transform ray to mesh local space
        QMatrix4x4 invModel = md.transform.inverted();
        QVector3D orig = invModel.map(rayOrig);
        QVector3D dir  = invModel.mapVector(rayDir).normalized();

        const float *vd = md.vertices.constData();
        const int stride = 6; // x,y,z,nx,ny,nz

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
            if (t > 1e-5f && t < bestT) {
                bestT = t;
                bestIdx = mi;
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
    delete m_pipeline;
    delete m_wirePipeline;
    delete m_stencilPipeline;
}

void UsdViewportRenderer::destroyMeshes()
{
    for (auto &m : m_meshes) {
        delete m.vbuf; delete m.vbufSmooth;
        delete m.ibuf;
        delete m.ubuf; delete m.srb;
        delete m.ibufEdge;
        delete m.wireUbuf; delete m.wireSrb;
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
    dst.vbufSmooth = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vsize);
    dst.ibuf = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,  isize);
    // UBO layout: mvp(64) + model(64) + color(16) + lightDir(16) = 160
    dst.ubuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 160);
    dst.vbuf->create(); dst.vbufSmooth->create(); dst.ibuf->create(); dst.ubuf->create();

    dst.srb = rhi->newShaderResourceBindings();
    dst.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            dst.ubuf)
    });
    dst.srb->create();

    batch->uploadStaticBuffer(dst.vbuf, src.vertices.constData());
    batch->uploadStaticBuffer(dst.vbufSmooth, src.smoothVertices.constData());
    batch->uploadStaticBuffer(dst.ibuf, src.indices.constData());

    // Per-mesh wire UBO + SRB for outline rendering
    dst.wireUbuf = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 160);
    dst.wireUbuf->create();
    dst.wireSrb = rhi->newShaderResourceBindings();
    dst.wireSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            dst.wireUbuf)
    });
    dst.wireSrb->create();

    dst.indexCount = src.indices.size();
    dst.transform  = src.transform;
    dst.color      = src.color;

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

    // Edge index buffer
    if (!src.edges.isEmpty()) {
        const int esize = src.edges.size() * sizeof(quint32);
        dst.ibufEdge = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, esize);
        dst.ibufEdge->create();
        batch->uploadStaticBuffer(dst.ibufEdge, src.edges.constData());
        dst.edgeCount = src.edges.size();
    }
}

void UsdViewportRenderer::initialize(QRhiCommandBuffer *)
{
    if (m_initialized) return;
    m_initialized = true;
    m_vs = loadShader(":/shaders/viewport.vert.qsb");
    m_fs = loadShader(":/shaders/viewport.frag.qsb");
}

void UsdViewportRenderer::synchronize(QQuickRhiItem *item)
{
    auto *vp = static_cast<UsdViewportItem *>(item);
    m_view = vp->viewMatrix();
    m_proj = vp->projMatrix();
    m_selectedIndices = vp->selectedMeshes();
    if (vp->meshDirty()) {
        m_pending = vp->meshes();
        vp->clearMeshDirty();
        m_rebuild = true;
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
        m_meshes.resize(m_pending.size());
        for (int i = 0; i < m_pending.size(); i++)
            uploadMesh(m_meshes[i], m_pending[i], upd);
        m_rebuild = false;
    }

    if (m_meshes.isEmpty()) {
        cb->beginPass(renderTarget(), QColor(30, 30, 30), {1.f, 0}, upd);
        cb->endPass();
        return;
    }

    if (!m_pipeline) {
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });

        // Solid pipeline
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
        m_pipeline->create();

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
        m_stencilPipeline->setShaderResourceBindings(m_meshes[0].wireSrb);
        m_stencilPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
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
        m_wirePipeline->setShaderResourceBindings(m_meshes[0].wireSrb);
        m_wirePipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_wirePipeline->create();
    }

    const QSize sz = renderTarget()->pixelSize();
    const QVector3D lightDir = QVector3D(0.6f, 1.f, 0.8f).normalized();

    struct alignas(16) UBuf {
        float mvp[16], model[16], color[4], lightDir[4];
    };

    for (int i = 0; i < m_meshes.size(); ++i) {
        auto &m = m_meshes[i];
        UBuf ub{};
        QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix() * m_proj * m_view * m.transform;
        memcpy(ub.mvp,   mvp.constData(),          64);
        memcpy(ub.model, m.transform.constData(),   64);
        ub.color[0] = m.color.x(); ub.color[1] = m.color.y();
        ub.color[2] = m.color.z(); ub.color[3] = 1.f;
        ub.lightDir[0] = lightDir.x(); ub.lightDir[1] = lightDir.y();
        ub.lightDir[2] = lightDir.z(); ub.lightDir[3] = 0.f;
        upd->updateDynamicBuffer(m.ubuf, 0, sizeof(UBuf), &ub);

        // Pre-upload outline UBO for selected meshes (avoids mid-pass updates)
        if (m_selectedIndices.contains(i)) {
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
    }

    cb->beginPass(renderTarget(), QColor(30, 30, 30), {1.f, 0}, upd);
    cb->setGraphicsPipeline(m_pipeline);
    cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));

    for (auto &m : m_meshes) {
        cb->setShaderResources(m.srb);
        QRhiCommandBuffer::VertexInput vi(m.vbuf, 0);
        cb->setVertexInput(0, 1, &vi, m.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(m.indexCount);
    }

    // Selection outline for each selected mesh (stencil mask + inverted hull)
    for (int selIdx : m_selectedIndices) {
        if (selIdx < 0 || selIdx >= m_meshes.size()) continue;
        auto &sel = m_meshes[selIdx];

        // Pass 1: Write stencil=1 for this mesh (use original vbuf for exact shape)
        cb->setGraphicsPipeline(m_stencilPipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        cb->setStencilRef(1);
        cb->setShaderResources(sel.srb);
        QRhiCommandBuffer::VertexInput vi1(sel.vbuf, 0);
        cb->setVertexInput(0, 1, &vi1, sel.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(sel.indexCount);

        // Pass 2: Draw outline where stencil != 1 (use smooth normals for closed outline)
        cb->setGraphicsPipeline(m_wirePipeline);
        cb->setViewport(QRhiViewport(0, 0, sz.width(), sz.height()));
        cb->setStencilRef(1);
        cb->setShaderResources(sel.wireSrb);
        QRhiCommandBuffer::VertexInput vi2(sel.vbufSmooth, 0);
        cb->setVertexInput(0, 1, &vi2, sel.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(sel.indexCount);

        // Pass 3: Reset stencil to 0 so it doesn't interfere with next mesh
        cb->setGraphicsPipeline(m_stencilPipeline);
        cb->setStencilRef(0);
        cb->setShaderResources(sel.srb);
        QRhiCommandBuffer::VertexInput vi3(sel.vbuf, 0);
        cb->setVertexInput(0, 1, &vi3, sel.ibuf, 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(sel.indexCount);
    }

    cb->endPass();
}
