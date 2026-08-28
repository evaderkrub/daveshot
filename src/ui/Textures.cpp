#include "ui/Textures.h"

#include "platform/Host.h"

#include <vector>

namespace daveshot::ui
{
TextureCache::~TextureCache()
{
    Clear();
}

ImTextureID TextureCache::Get(unsigned shotId, int slot, const Image& image)
{
    if (m_host == nullptr || !image.Valid())
        return 0;

    const uint64_t key = KeyFor(shotId, slot);
    auto found = m_textures.find(key);
    if (found != m_textures.end())
        return found->second;

    const ImTextureID texture = m_host->CreateTexture(image);
    if (texture == 0)
        return 0;

    m_textures.emplace(key, texture);
    return texture;
}

void TextureCache::Sweep(const History& history)
{
    if (m_textures.empty())
        return;

    std::vector<uint64_t> doomed;
    for (const auto& entry : m_textures)
    {
        const unsigned shotId = (unsigned)(entry.first >> 8);
        if (shotId == kBackdropShotId)
            continue;   // the overlay owns that one; Forget() releases it
        if (history.Find(shotId) == nullptr)
            doomed.push_back(entry.first);
    }

    for (uint64_t key : doomed)
    {
        if (m_host != nullptr)
            m_host->DestroyTexture(m_textures[key]);
        m_textures.erase(key);
    }
}

void TextureCache::Forget(unsigned shotId)
{
    for (int slot = kSlotFull; slot <= kSlotBackdrop; ++slot)
    {
        const uint64_t key = KeyFor(shotId, slot);
        auto found = m_textures.find(key);
        if (found == m_textures.end())
            continue;
        if (m_host != nullptr)
            m_host->DestroyTexture(found->second);
        m_textures.erase(found);
    }
}

void TextureCache::Clear()
{
    if (m_host != nullptr)
        for (auto& entry : m_textures)
            m_host->DestroyTexture(entry.second);
    m_textures.clear();
}
}
