//
//  TGAImage.hpp
//  FastTGA
//
//  Created by Evgenij Lutz on 26.01.26.
//

#pragma once

#include <FastTGAC/Common.hpp>


enum class TGAErrorCode: int {
    notImplemented,
    notTGA,
    other
};

#define TGA_ERROR_OTHER_MESSAGE_SIZE 64

struct TGAError final {
    bool error;
    TGAErrorCode code;
    union {
        struct {
            char message[TGA_ERROR_OTHER_MESSAGE_SIZE];
        } other;
    };
};


/// TGA image.
///
/// - Note: This object is immutable, thus thread-safe. You can access this object from different threads at the same time.
class TGAImage final {
private:
    std::atomic<size_t> _referenceCounter;
    
    const long _width;
    const long _height;
    const long _numComponents;
    const char* fn_nonnull _contents;
    const long _size;
    
    FN_FRIEND_SWIFT_INTERFACE(TGAImage)
    
    TGAImage(long width, long height, long numComponents, char* fn_nonnull contents, long size);
    ~TGAImage();
    
public:
    static bool isTGA(const char* fn_nonnull filePath fn_noescape, TGAError* fn_nullable error fn_noescape);
    //static bool isTGA(const void* fn_nonnull data fn_noescape, long dataSize, long offset, TGAError* fn_nullable error fn_noescape);
    
    static TGAImage* fn_nullable load(const char* fn_nonnull filePath fn_noescape, TGAError* fn_nullable error fn_noescape) SWIFT_RETURNS_RETAINED;
    
    long getWidth() SWIFT_COMPUTED_PROPERTY { return _width; }
    long getHeight() SWIFT_COMPUTED_PROPERTY { return _height; }
    long getNumComponents() SWIFT_COMPUTED_PROPERTY { return _numComponents; }
    const char* fn_nonnull getContents() SWIFT_COMPUTED_PROPERTY { return _contents; }
    long getSize() SWIFT_COMPUTED_PROPERTY { return _size; }
}
FN_SWIFT_INTERFACE(TGAImage)
SWIFT_UNCHECKED_SENDABLE;


FN_DEFINE_SWIFT_INTERFACE(TGAImage)
