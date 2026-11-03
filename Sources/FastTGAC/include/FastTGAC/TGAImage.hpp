//
//  TGAImage.hpp
//  FastTGA
//
//  Created by Evgenij Lutz on 26.01.26.
//

#pragma once

#include <FastTGAC/Common.hpp>
#include <atomic>


enum class TGAErrorCode: int {
    notImplemented,
    notTGA,
    other
};

#define TGA_ERROR_OTHER_MESSAGE_SIZE 128

struct TGAError final {
    bool error;
    TGAErrorCode code;
    union {
        struct {
            char message[TGA_ERROR_OTHER_MESSAGE_SIZE];
        } other;
    };
};


struct TGASource final {
    bool usePath;
    union {
        const char* fn_nonnull path;
        struct {
            const void* fn_nonnull buffer;
            long bufferSize;
        };
    };
    
    TGASource(const char* fn_nullable path):
    usePath(true),
    path(path) { }
    
    TGASource(const void* fn_nullable buffer, long bufferSize):
    usePath(false),
    buffer(buffer),
    bufferSize(bufferSize) { }
    
};


/// TGA image.
///
/// The image contents are always stored as `uint8` pixels.
///
/// - Note: This object is immutable, thus thread-safe. You can access this object from different threads at the same time.
class TGAImage final {
private:
    std::atomic<size_t> _referenceCounter;
    
    const long _width;
    const long _height;
    /// Number of components.
    ///
    /// Possible values are `1`, `3` and `4`.
    const long _numComponents;
    std::byte* fn_nonnull _contents;
    bool _ownsContents;
    const long _size;
    
    FN_FRIEND_SWIFT_INTERFACE(TGAImage)
    
    TGAImage(long width, long height, long numComponents, std::byte* fn_nonnull contents, bool ownsContents, long size);
    ~TGAImage();
    
public:
    static bool isTGA(const TGASource& source fn_noescape, TGAError* fn_nullable error fn_noescape);
    static bool isTGA(const char* fn_nonnull path fn_noescape, TGAError* fn_nullable error fn_noescape);
    static bool isTGA(const void* fn_nonnull buffer fn_noescape, long bufferSize, TGAError* fn_nullable error fn_noescape);
    
    static TGAImage* fn_nullable load(const TGASource& source fn_noescape, TGAError* fn_nullable error fn_noescape) SWIFT_RETURNS_RETAINED;
    static TGAImage* fn_nullable load(const char* fn_nonnull path fn_noescape, TGAError* fn_nullable error fn_noescape);
    static TGAImage* fn_nullable load(const void* fn_nonnull buffer fn_noescape, long bufferSize, TGAError* fn_nullable error fn_noescape);
    
    long getWidth() SWIFT_COMPUTED_PROPERTY { return _width; }
    long getHeight() SWIFT_COMPUTED_PROPERTY { return _height; }
    long getNumComponents() SWIFT_COMPUTED_PROPERTY { return _numComponents; }
    const void* fn_nonnull getContents() SWIFT_COMPUTED_PROPERTY { return _contents; }
    long getSize() SWIFT_COMPUTED_PROPERTY { return _size; }
    
    /// A very dangerous and thread-unsafe zone. Access it only when you can guarantee that the image is accessed from a single thread.
    struct Unsafe final {
        Unsafe() = delete;
        static void* fn_nonnull transferContentsOwnership(TGAImage* fn_nonnull tga);
    };
}
FN_SWIFT_INTERFACE(TGAImage)
SWIFT_UNCHECKED_SENDABLE;


FN_DEFINE_SWIFT_INTERFACE(TGAImage)
