#ifndef BUFFERMANAGER_H
#define BUFFERMANAGER_H

#include "EditorBuffer.h"
#include <vector>
#include <memory>

class BufferManager {
public:
    BufferManager();
    
    int addBuffer();
    void removeBuffer(int index);
    
    EditorBuffer& getBuffer(int index);
    EditorBuffer& currentBuffer();
    
    int currentBufferIndex() const { return m_currentIndex; }
    void setCurrentBufferIndex(int index);
    
    size_t bufferCount() const { return m_buffers.size(); }
    
    bool hasBuffers() const { return !m_buffers.empty(); }

private:
    std::vector<EditorBuffer> m_buffers;
    int m_currentIndex = -1;
};

#endif // BUFFERMANAGER_H
