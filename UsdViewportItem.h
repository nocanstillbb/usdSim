#pragma once
#include <QQuickRhiItem>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPointF>
#include <QSet>

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

class UsdViewportItem : public QQuickRhiItem
{
    Q_OBJECT
    Q_PROPERTY(UsdDocument* document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QStringList selectedPrimPaths READ selectedPrimPaths NOTIFY selectedPrimPathsChanged)
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


protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;

private slots:
    void onDocumentChanged();

private:
    void buildMeshes();
    void updateCamera();
    int  pickMesh(const QPointF &pos) const;

    UsdDocument       *m_doc = nullptr;
    QVector<MeshData>  m_meshes;
    bool               m_meshDirty = false;
    QSet<int>          m_selectedMeshes;

    float   m_yaw   = 30.f;
    float   m_pitch = 25.f;
    float   m_dist  = 20.f;
    QPointF m_lastMouse;
    QPointF m_pressPos;
    bool    m_dragging = false;
    bool    m_panning  = false;

    QVector3D  m_target{0, 0, 0};
    bool       m_zUp = true;
    bool       m_cameraInitialized = false;

    QMatrix4x4 m_view;
    QMatrix4x4 m_proj;
};
