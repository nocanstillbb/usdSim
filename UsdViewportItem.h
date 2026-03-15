#pragma once
#include <QQuickRhiItem>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QSet>
#include <QHash>

class UsdDocument;

struct MeshData {
    QVector<float>   vertices;       // [x,y,z, nx,ny,nz] interleaved
    QVector<float>   smoothVertices; // same positions, averaged normals (for outline)
    QVector<quint32> indices;
    QVector<quint32> edges;    // pairs of vertex indices forming unique edges
    QMatrix4x4       transform;
    QVector3D        color;
    QString          primPath;
};

enum GizmoMode {
    GizmoModeNone      = 0,
    GizmoModeTranslate = 1,
    GizmoModeRotate    = 2,
    GizmoModeScale     = 3
};

enum GizmoPartId {
    GizmoNone = -1,
    AxisX = 0, AxisY, AxisZ,
    PlaneXY, PlaneXZ, PlaneYZ,
    Origin,
    GizmoCount = 7,
    // Scale gizmo extra parts: separate cube tips for visual sliding
    ScaleCubeTipX = 7, ScaleCubeTipY, ScaleCubeTipZ,
    ScaleGizmoPartCount = 10
};

enum RotatePartId {
    RotateRingX = 0,
    RotateRingY = 1,
    RotateRingZ = 2,
    RotatePartCount = 3
};

struct GizmoMeshData {
    QVector<float>   vertices;      // [x,y,z,nx,ny,nz] stride=6
    QVector<quint32> indices;
    QVector3D color;
    QVector3D highlightColor;
};

class UsdViewportItem : public QQuickRhiItem
{
    Q_OBJECT
    Q_PROPERTY(UsdDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QStringList selectedPrimPaths READ selectedPrimPaths NOTIFY selectedPrimPathsChanged)
    Q_PROPERTY(int gizmoMode READ gizmoMode WRITE setGizmoMode NOTIFY gizmoModeChanged)
    Q_PROPERTY(QPointF orientLabelX READ orientLabelX NOTIFY orientLabelsChanged)
    Q_PROPERTY(QPointF orientLabelY READ orientLabelY NOTIFY orientLabelsChanged)
    Q_PROPERTY(QPointF orientLabelZ READ orientLabelZ NOTIFY orientLabelsChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY showGridChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapEnabledChanged)
    QML_ELEMENT

public:
    explicit UsdViewportItem(QQuickItem *parent = nullptr);

    QQuickRhiItemRenderer *createRenderer() override;

    UsdDocument *document() const { return m_doc; }
    void setDocument(UsdDocument *doc);

    QStringList selectedPrimPaths() const;
    Q_INVOKABLE void selectPrimPath(const QString &path);
    Q_INVOKABLE void selectPrimPaths(const QStringList &paths);
    Q_INVOKABLE void togglePrimPath(const QString &path);

    int  gizmoMode() const { return m_gizmoMode; }
    void setGizmoMode(int mode);
    bool gizmoVisible() const { return m_gizmoMode != GizmoModeNone && !m_selectedMeshes.isEmpty(); }
    int  gizmoHoveredPart() const { return m_gizmoHoveredPart; }
    QVector3D gizmoWorldPos() const { return m_gizmoWorldPos; }
    QVector3D scaleCubeFactors() const { return m_scaleCubeFactors; }
    const QVector<GizmoMeshData> &activeGizmoMeshes() const;

    // Orientation indicator
    QPointF orientLabelX() const { return m_orientLabels[0]; }
    QPointF orientLabelY() const { return m_orientLabels[1]; }
    QPointF orientLabelZ() const { return m_orientLabels[2]; }
    const QVector<GizmoMeshData> &orientAxesMeshes() const { return m_orientAxesMeshes; }

    // Grid & Snap
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on);
    bool snapEnabled() const { return m_snapEnabled; }
    void setSnapEnabled(bool on);
    const QVector<GizmoMeshData> &gridMeshes() const { return m_gridMeshes; }
    bool gridDirty() const { return m_gridDirty; }
    void clearGridDirty() { m_gridDirty = false; }

    const QVector<MeshData> &meshes() const { return m_meshes; }
    bool      meshDirty() const  { return m_meshDirty; }
    void      clearMeshDirty()   { m_meshDirty = false; }
    QMatrix4x4 viewMatrix() const { return m_view; }
    QMatrix4x4 projMatrix() const { return m_proj; }
    QSet<int> selectedMeshes() const { return m_selectedMeshes; }

signals:
    void documentChanged();
    void selectedPrimPathsChanged();
    void primClicked(const QString &primPath, bool ctrlHeld);
    void gizmoModeChanged();
    void gizmoDragUpdated();
    void gizmoDragFinished(const QString &primPath);
    void orientLabelsChanged();
    void showGridChanged();
    void snapEnabledChanged();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void hoverMoveEvent(QHoverEvent *e) override;

private slots:
    void onDocumentChanged();

private:
    void buildMeshes();
    void updateCamera();
    int  pickMesh(const QPointF &pos) const;
    static void buildGizmoMeshes(QVector<GizmoMeshData> &out);
    static void buildRotateGizmoMeshes(QVector<GizmoMeshData> &out);
    static void buildScaleGizmoMeshes(QVector<GizmoMeshData> &out);
    static void buildOrientAxesMeshes(QVector<GizmoMeshData> &out);
    void buildGridMeshes();
    int  pickGizmo(const QPointF &pos) const;
    void updateGizmoPosition();
    void updateOrientLabels();

    UsdDocument       *m_doc = nullptr;
    QVector<MeshData>  m_meshes;
    bool               m_meshDirty = false;
    QSet<int>          m_selectedMeshes;
    int                m_anchorMeshIdx = -1;  // last-selected mesh for gizmo placement

    // Gizmo state
    int m_gizmoMode = GizmoModeNone;
    QVector<GizmoMeshData> m_translateGizmoMeshes;
    QVector<GizmoMeshData> m_rotateGizmoMeshes;
    QVector<GizmoMeshData> m_scaleGizmoMeshes;
    int m_gizmoHoveredPart = -1;
    int m_gizmoDragPart = -1;
    QVector3D m_gizmoWorldPos;
    QVector3D m_gizmoDragStartPos;
    QPointF   m_gizmoDragStartMouse;
    QHash<int, QVector3D> m_gizmoDragStartTranslations;  // world translations at drag start
    QHash<int, QMatrix4x4> m_gizmoDragParentTransforms;  // parent-to-world per mesh
    QHash<int, QVector3D> m_gizmoDragStartLocalTranslates; // xformOp:translate at drag start
    QHash<int, QVector3D> m_gizmoDragStartRotations;     // xformOp:rotateXYZ at drag start
    float m_gizmoDragStartAngle = 0.f;                    // initial angle for rotate drag
    QHash<int, QVector3D> m_gizmoDragStartScales;         // xformOp:scale at drag start
    float m_gizmoDragStartDistance = 0.f;                  // initial distance for scale drag
    QVector3D m_scaleCubeFactors{1.f, 1.f, 1.f};            // live scale factors for cube tip sliding

    float   m_yaw   = 30.f;
    float   m_pitch = 25.f;
    float   m_dist  = 20.f;
    QPointF m_lastMouse;
    QPointF m_pressPos;
    bool    m_dragging = false;
    bool    m_panning  = false;

    QVector3D  m_target{0, 0, 0};
    QVector3D  m_cameraEye;
    bool       m_zUp = true;
    bool       m_cameraInitialized = false;

    QMatrix4x4 m_view;
    QMatrix4x4 m_proj;

    // Orientation indicator
    QVector<GizmoMeshData> m_orientAxesMeshes;
    QPointF m_orientLabels[3];

    // Grid & Snap
    bool m_showGrid = true;
    bool m_snapEnabled = false;
    bool m_gridDirty = false;
    QVector<GizmoMeshData> m_gridMeshes;
};
