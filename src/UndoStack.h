#pragma once
#include <QObject>
#include <vector>
#include <memory>

class UndoCommand
{
public:
    virtual ~UndoCommand() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;

    // For merging consecutive commands of the same type/target.
    // Return a non-negative id to enable merging; -1 = no merge.
    virtual int id() const { return -1; }
    // Return true if successfully merged `other` into this command.
    virtual bool mergeWith(const UndoCommand * /*other*/) { return false; }
    // Descriptive text for the undo history UI.
    virtual QString text() const { return QString(); }
};

class UndoStack : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY indexChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY indexChanged)
    Q_PROPERTY(int count READ count NOTIFY indexChanged)
    Q_PROPERTY(int index READ index NOTIFY indexChanged)

public:
    explicit UndoStack(QObject *parent = nullptr);
    ~UndoStack() override;

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < (int)m_commands.size(); }
    int count() const { return (int)m_commands.size(); }
    int index() const { return m_index; }

    Q_INVOKABLE QString commandText(int idx) const;

    // Push a command and execute its redo().
    void push(std::unique_ptr<UndoCommand> cmd);

    // Push a command whose effect is already applied (skip first redo).
    void pushNoRedo(std::unique_ptr<UndoCommand> cmd);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clear();

signals:
    void indexChanged();

private:
    void truncateFuture();

    std::vector<std::unique_ptr<UndoCommand>> m_commands;
    int m_index = 0; // points to the next command to redo (i.e. past the last undoable)
};
