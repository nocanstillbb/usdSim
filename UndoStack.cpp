#include "UndoStack.h"

UndoStack::UndoStack(QObject *parent)
    : QObject(parent)
{
}

UndoStack::~UndoStack() = default;

void UndoStack::truncateFuture()
{
    // Remove all commands from m_index onward (discard redo history).
    while ((int)m_commands.size() > m_index)
        m_commands.pop_back();
}

void UndoStack::push(std::unique_ptr<UndoCommand> cmd)
{
    // Try merging with the top command
    if (m_index > 0 && cmd->id() >= 0) {
        UndoCommand *top = m_commands[m_index - 1].get();
        if (top->id() == cmd->id() && top->mergeWith(cmd.get())) {
            // Merged — no new entry needed
            emit indexChanged();
            return;
        }
    }

    truncateFuture();
    cmd->redo();
    m_commands.push_back(std::move(cmd));
    m_index = m_commands.size();
    emit indexChanged();
}

void UndoStack::pushNoRedo(std::unique_ptr<UndoCommand> cmd)
{
    // Try merging with the top command
    if (m_index > 0 && cmd->id() >= 0) {
        UndoCommand *top = m_commands[m_index - 1].get();
        if (top->id() == cmd->id() && top->mergeWith(cmd.get())) {
            emit indexChanged();
            return;
        }
    }

    truncateFuture();
    // Do NOT call cmd->redo() — the change is already applied.
    m_commands.push_back(std::move(cmd));
    m_index = m_commands.size();
    emit indexChanged();
}

void UndoStack::undo()
{
    if (!canUndo()) return;
    --m_index;
    m_commands[m_index]->undo();
    emit indexChanged();
}

void UndoStack::redo()
{
    if (!canRedo()) return;
    m_commands[m_index]->redo();
    ++m_index;
    emit indexChanged();
}

void UndoStack::clear()
{
    m_commands.clear();
    m_index = 0;
    emit indexChanged();
}

QString UndoStack::commandText(int idx) const
{
    if (idx < 0 || idx >= (int)m_commands.size())
        return QString();
    return m_commands[idx]->text();
}
