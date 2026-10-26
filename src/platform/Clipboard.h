#pragma once

#include "app/Image.h"

#include <string>

namespace daveshot::clipboard
{
    // Puts an image on the system clipboard so it can be pasted straight into
    // a chat window, a document or an image editor.
    bool CopyImage(const Image& image, std::string& error);

    bool CopyText(const std::string& text, std::string& error);

    // Polled by the loop after a copy. Non-empty, once, when another program
    // has replaced what the last copy put there within a few seconds of it
    // -- naming the program when the OS says who. To the person pasting that
    // looks exactly like a copy that never happened, so it is worth a word
    // in the status line. Only Windows can tell today; elsewhere always empty.
    std::string TakeOverwriteNotice();
}
