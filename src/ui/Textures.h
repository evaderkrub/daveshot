#pragma once

#include "app/Capture.h"

#include "imgui.h"

#include <unordered_map>

namespace daveshot
{
    class Host;

    namespace ui
    {
        // GPU textures for the captures on screen. Owned by the drawing layer
        // -- unlike the pixels, which belong to the application state -- and
        // keyed by shot id so a texture survives the history reordering
        // around it.
        //
        // Works with no host at all, handing back 0 for everything: that is
        // what lets the end-to-end tests run the real panels without a
        // renderer, since ImGui::Image with a null texture still lays out.
        class TextureCache
        {
        public:
            TextureCache() = default;
            ~TextureCache();

            TextureCache(const TextureCache&)            = delete;
            TextureCache& operator=(const TextureCache&) = delete;

            void SetHost(Host* host) { m_host = host; }

            // Uploads on first use and remembers the result. `revision` lets
            // one shot have two entries -- full size and thumbnail -- without
            // a second map.
            ImTextureID Get(unsigned shotId, int slot, const Image& image);

            // Drops textures for shots that have fallen out of the history.
            // Called once a frame; a capture at 4K is 33 MB of VRAM, so this
            // is not an optimisation.
            void Sweep(const History& history);

            void Forget(unsigned shotId);
            void Clear();

            size_t Count() const { return m_textures.size(); }

        private:
            static uint64_t KeyFor(unsigned shotId, int slot)
            {
                return ((uint64_t)shotId << 8) | (uint64_t)(slot & 0xFF);
            }

            Host* m_host = nullptr;
            std::unordered_map<uint64_t, ImTextureID> m_textures;
        };

        inline constexpr int kSlotFull      = 0;
        inline constexpr int kSlotThumbnail = 1;
        inline constexpr int kSlotBackdrop  = 2;

        // The frozen desktop the region overlay draws is not a shot and has
        // no id, so it gets a reserved one that no capture can collide with.
        inline constexpr unsigned kBackdropShotId = 0xFFFFFFFFu;
    }
}
