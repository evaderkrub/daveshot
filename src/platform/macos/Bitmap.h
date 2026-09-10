#pragma once
#include "app/Image.h"
#import <Cocoa/Cocoa.h>
#include <cstring>
#include <algorithm>

namespace daveshot::macos {
inline NSBitmapImageRep* Bitmap(const Image& image) {
    if (!image.Valid()) return nil;
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
        pixelsWide:image.width pixelsHigh:image.height bitsPerSample:8 samplesPerPixel:4
        hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
        bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:image.width * 4 bitsPerPixel:32];
    if (rep) std::memcpy(rep.bitmapData, image.pixels.data(), image.pixels.size());
    return rep;
}
inline bool ReadBitmap(CGImageRef cg, int width, int height, Image& out, std::string& error) {
    if (!cg || width <= 0 || height <= 0) { error = "no image was returned"; return false; }
    Image image{width, height, std::vector<uint8_t>((size_t)width * (size_t)height * 4)};
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(image.pixels.data(), width, height, 8, width * 4,
        space, (CGBitmapInfo)kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(space);
    if (!ctx) { error = "could not allocate image bitmap"; return false; }
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), cg);
    CGContextRelease(ctx);
    // The application stores straight alpha; CoreGraphics renders premultiplied alpha.
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        unsigned a = image.pixels[i + 3];
        if (a && a < 255) for (size_t c = 0; c < 3; ++c)
            image.pixels[i + c] = (uint8_t)std::min(255u, (image.pixels[i + c] * 255u + a / 2) / a);
    }
    out = std::move(image); return true;
}
}
