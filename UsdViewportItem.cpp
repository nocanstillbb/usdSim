#include "UsdViewportItem.h"
#include "UsdDocument.h"

#include <QFile>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <rhi/qrhi.h>

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
        QRhiBuffer *ibuf = nullptr;
        QRhiBuffer *ubuf = nullptr;
        QRhiShaderResourceBindings *srb = nullptr;
        int indexCount = 0;
        QMatrix4x4 transform;
        QVector3D  color;
    };

    void destroyMeshes();
    void uploadMesh(RhiMesh &dst, const MeshData &src, QRhiResourceUpdateBatch *batch);

    QRhiGraphicsPipeline *m_pipeline = nullptr;
    QShader m_vs, m_fs;
    bool m_initialized = false;

    QVector<RhiMesh>  m_meshes;
    QVector<MeshData> m_pending;
    bool m_rebuild = true;

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
            double h = 2.0, r = 0.5;
            UsdGeomCylinder(prim).GetHeightAttr().Get(&h);
            UsdGeomCylinder(prim).GetRadiusAttr().Get(&r);
            genCylinder(float(r), float(h), verts, indices);
        } else if (type == "Cone") {
            double h = 2.0, r = 0.5;
            UsdGeomCone(prim).GetHeightAttr().Get(&h);
            UsdGeomCone(prim).GetRadiusAttr().Get(&r);
            genCone(float(r), float(h), verts, indices);
        } else if (type == "Capsule") {
            double h = 1.0, r = 0.5;
            UsdGeomCapsule(prim).GetHeightAttr().Get(&h);
            UsdGeomCapsule(prim).GetRadiusAttr().Get(&r);
            genCapsule(float(r), float(h), verts, indices);
        } else if (type == "Plane") {
            double w = 2.0, l = 2.0;
            UsdGeomPlane(prim).GetWidthAttr().Get(&w);
            UsdGeomPlane(prim).GetLengthAttr().Get(&l);
            genPlane(float(w), float(l), verts, indices);
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
        updateCamera();
    }

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
}

void UsdViewportRenderer::destroyMeshes()
{
    for (auto &m : m_meshes) {
        delete m.vbuf; delete m.ibuf;
        delete m.ubuf; delete m.srb;
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

    dst.srb = rhi->newShaderResourceBindings();
    dst.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            dst.ubuf)
    });
    dst.srb->create();

    batch->uploadStaticBuffer(dst.vbuf, src.vertices.constData());
    batch->uploadStaticBuffer(dst.ibuf, src.indices.constData());

    dst.indexCount = src.indices.size();
    dst.transform  = src.transform;
    dst.color      = src.color;
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
        QRhiVertexInputLayout il;
        il.setBindings({ QRhiVertexInputBinding(6 * sizeof(float)) });
        il.setAttributes({
            QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float3, 0),
            QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float))
        });
        m_pipeline->setVertexInputLayout(il);
        m_pipeline->setShaderResourceBindings(m_meshes[0].srb);
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();
    }

    const QSize sz = renderTarget()->pixelSize();
    const QVector3D lightDir = QVector3D(0.6f, 1.f, 0.8f).normalized();

    struct alignas(16) UBuf {
        float mvp[16], model[16], color[4], lightDir[4];
    };

    for (auto &m : m_meshes) {
        UBuf ub{};
        QMatrix4x4 mvp = rhi->clipSpaceCorrMatrix() * m_proj * m_view * m.transform;
        memcpy(ub.mvp,   mvp.constData(),          64);
        memcpy(ub.model, m.transform.constData(),   64);
        ub.color[0] = m.color.x(); ub.color[1] = m.color.y();
        ub.color[2] = m.color.z(); ub.color[3] = 1.f;
        ub.lightDir[0] = lightDir.x(); ub.lightDir[1] = lightDir.y();
        ub.lightDir[2] = lightDir.z(); ub.lightDir[3] = 0.f;
        upd->updateDynamicBuffer(m.ubuf, 0, sizeof(UBuf), &ub);
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

    cb->endPass();
}
