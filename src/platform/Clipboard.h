#pragma once

#include "app/Image.h"

#include <string>

namespace daveshot::clipboard
{
    // Puts an image on the system clipboard so it can be pasted straight into
    // a chat window, a document or an image editor.
    bool CopyImage(const Image& image, std::string& error);

    bool CopyText(const std::string& text, std::string& error);
}
