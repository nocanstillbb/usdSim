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

enum GizmoPartId {
    GizmoNone = -1,
    AxisX = 0, AxisY, AxisZ,
    PlaneXY, PlaneXZ, PlaneYZ,
    Origin,
    GizmoCount = 7
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
    Q_PROPERTY(bool gizmoEnabled READ gizmoEnabled WRITE setGizmoEnabled NOTIFY gizmoEnabledChanged)
    Q_PROPERTY(QPointF orientLabelX READ orientLabelX NOTIFY orientLabelsChanged)
    Q_PROPERTY(QPointF orientLabelY READ orientLabelY NOTIFY orientLabelsChanged)
    Q_PROPERTY(QPointF orientLabelZ READ orientLabelZ NOTIFY orientLabelsChanged)
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

    bool gizmoEnabled() const { return m_gizmoEnabled; }
    void setGizmoEnabled(bool on);
    bool gizmoVisible() const { return m_gizmoEnabled && !m_selectedMeshes.isEmpty(); }
    int  gizmoHoveredPart() const { return m_gizmoHoveredPart; }
    QVector3D gizmoWorldPos() const { return m_gizmoWorldPos; }
    const QVector<GizmoMeshData> &gizmoMeshes() const { return m_gizmoMeshes; }

    // Orientation indicator
    QPointF orientLabelX() const { return m_orientLabels[0]; }
    QPointF orientLabelY() const { return m_orientLabels[1]; }
    QPointF orientLabelZ() const { return m_orientLabels[2]; }
    const QVector<GizmoMeshData> &orientAxesMeshes() const { return m_orientAxesMeshes; }

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
    void gizmoEnabledChanged();
    void gizmoDragUpdated();
    void gizmoDragFinished(const QString &primPath);
    void orientLabelsChanged();

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
    static void buildOrientAxesMeshes(QVector<GizmoMeshData> &out);
    int  pickGizmo(const QPointF &pos) const;
    void updateGizmoPosition();
    void updateOrientLabels();

    UsdDocument       *m_doc = nullptr;
    QVector<MeshData>  m_meshes;
    bool               m_meshDirty = false;
    QSet<int>          m_selectedMeshes;

    // Gizmo state
    bool m_gizmoEnabled = false;
    QVector<GizmoMeshData> m_gizmoMeshes;
    int m_gizmoHoveredPart = -1;
    int m_gizmoDragPart = -1;
    QVector3D m_gizmoWorldPos;
    QVector3D m_gizmoDragStartPos;
    QPointF   m_gizmoDragStartMouse;
    QHash<int, QVector3D> m_gizmoDragStartTranslations;  // world translations at drag start
    QHash<int, QMatrix4x4> m_gizmoDragParentTransforms;  // parent-to-world per mesh
    QHash<int, QVector3D> m_gizmoDragStartLocalTranslates; // xformOp:translate at drag start

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
};
