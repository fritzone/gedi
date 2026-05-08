#include "BufferManager.h"
#include <stdexcept>

BufferManager::BufferManager() : m_currentIndex(-1) {}

int BufferManager::addBuffer() {
    m_buffers.emplace_back(-1);
    m_currentIndex = (int)m_buffers.size() - 1;
    m_buffers.back().bufferNr = m_currentIndex + 1;
    return m_currentIndex;
}

void BufferManager::removeBuffer(int index) {
    if (index < 0 || index >= (int)m_buffers.size()) return;
    
    m_buffers.erase(m_buffers.begin() + index);
    
    if (m_buffers.empty()) {
        m_currentIndex = -1;
    } else if (m_currentIndex >= (int)m_buffers.size()) {
        m_currentIndex = (int)m_buffers.size() - 1;
    }
}

EditorBuffer& BufferManager::getBuffer(int index) {
    if (index < 0 || index >= (int)m_buffers.size()) {
        throw std::out_of_range("Buffer index out of range");
    }
    return m_buffers[index];
}

EditorBuffer& BufferManager::currentBuffer() {
    if (m_currentIndex < 0 || m_currentIndex >= (int)m_buffers.size()) {
        throw std::runtime_error("No current buffer");
    }
    return m_buffers[m_currentIndex];
}

void BufferManager::setCurrentBufferIndex(int index) {
    if (index >= -1 && index < (int)m_buffers.size()) {
        m_currentIndex = index;
    }
}
