//
//  TGAImage.cpp
//  FastTGA
//
//  Created by Evgenij Lutz on 26.01.26.
//

#define _LIBCPP_REMOVE_TRANSITIVE_INCLUDES
#include <FastTGAC/TGAImage.hpp>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#if __APPLE__
#  include <simd/simd.h>
#  define USE_SIMD 1
#else
#  define USE_SIMD 0
#endif


// MARK: Error

static void tga_error_reset(TGAError* fn_nullable error fn_noescape) {
    if (error == nullptr) {
        return;
    }
    
    std::memset(error, 0, sizeof(TGAError));
}

//static void tga_error_notImplemented(TGAError* fn_nullable error fn_noescape) {
//    if (error == nullptr) {
//        return;
//    }
//    
//    error->error = true;
//    error->code = TGAErrorCode::notImplemented;
//}

static void tga_error_other(TGAError* fn_nullable error fn_noescape, const char* fn_nonnull __restrict format, ...) __printflike(2, 3);
static void tga_error_other(TGAError* fn_nullable error fn_noescape, const char* fn_nonnull __restrict format, ...) {
    if (error == nullptr) {
        return;
    }
    
    error->error = true;
    error->code = TGAErrorCode::other;
    
    va_list args;
    va_start(args, format);
    vsnprintf(error->other.message, TGA_ERROR_OTHER_MESSAGE_SIZE - 1, format, args);
    error->other.message[TGA_ERROR_OTHER_MESSAGE_SIZE - 1] = '\0';
    va_end(args);
    
    printf("%s\n", error->other.message);
}


// MARK: TGAImage

TGAImage::TGAImage(long width, long height, long numComponents, std::byte* fn_nonnull contents, bool ownsContents, long size):
_referenceCounter(1),
_width(width),
_height(height),
_numComponents(numComponents),
_contents(contents),
_ownsContents(ownsContents),
_size(size) {
    //
}


TGAImage::~TGAImage() {
    if (_contents && _ownsContents) {
        std::free(_contents);
    }
}


struct fn_packed TGAHeader final {
    uint8_t idLength;
    uint8_t colorMapType;
    uint8_t imageType;
    
    uint16_t colorMapFirstEntryIndex;
    uint16_t colorMapLength;
    uint8_t colorMapEntrySize;
    
    uint16_t xOrigin;
    uint16_t yOrigin;
    uint16_t width;
    uint16_t height;
    uint8_t pixelDepth;
    uint8_t imageDescriptor;
};

static_assert(sizeof(TGAHeader) == 18, "Size of TGAHeader should be 18 bytes");


struct fn_packed TGAFooter final {
    uint32_t extensionAreaOffset;
    uint32_t developerAreaOffset;
    char signature[16];
    char point;
    char zero;
};

static_assert(sizeof(TGAFooter) == 26, "Size of TGAFooter should be 26 bytes");


static FILE* fn_nullable tga_open_file(const char* fn_nonnull filePath fn_noescape, TGAError* fn_nullable error fn_noescape) {
    tga_error_reset(error);
    
    // Open file
    auto file = std::fopen(filePath, "rb");
    if (file == nullptr) {
        tga_error_other(error, "Could not open file: %s", strerror(errno));
        return nullptr;
    }
    
    return file;
}


static bool tga_close_file(FILE* fn_nullable file, TGAError* fn_nullable error fn_noescape) {
    if (file == nullptr) {
        return true;
    }
    
    std::fclose(file);
    
    return true;
}


struct TGAReaderContext {
    bool isFile;
    union {
        FILE* fn_nonnull file;
        
        struct {
            union {
                const void* fn_nonnull buffer;
                const char* fn_nonnull charBuffer;
            };
             long bufferSize;
        } data;
    };
    long position;
    
    
    TGAReaderContext(const char* fn_nonnull path, TGAError* fn_nullable error fn_noescape) {
        isFile = true;
        file = tga_open_file(path, error);
        position = 0;
    }
    
    
    TGAReaderContext(const void* fn_nonnull buffer, long bufferSize) {
        isFile = false;
        data = {
            .buffer = buffer,
            .bufferSize = bufferSize
        };
        position = 0;
    }
    
    
    static TGAReaderContext fromSource(const TGASource& source, TGAError* fn_nullable error fn_noescape) {
        if (source.usePath) {
            return TGAReaderContext(source.path, error);
        }
        
        return TGAReaderContext(source.buffer, source.bufferSize);
    }
    
    
    ~TGAReaderContext() {
        if (isFile && file) {
            fclose(file);
        }
    }
    
    
    long getPosition() {
        return position;
    }
    
    
    bool setPosition(long newPosition, TGAError* fn_nullable error fn_noescape) {
        if (isFile) {
            // Set to the desired position or to the end if negative
            long pos = newPosition < 0 ? 0 : newPosition;
            int pivot = newPosition < 0 ? SEEK_END : SEEK_SET;
            
            auto result = std::fseek(file, pos, pivot);
            if (result != 0) {
                tga_error_other(error, "Could not reach the end of the file: %s", strerror(errno));
                tga_close_file(file, error);
                return false;
            }
            
            // Update position
            position = ftell(file);
        }
        else {
            position = newPosition < 0 ? data.bufferSize : newPosition;
        }
        
        return true;
    }
    
    
    bool read(void* out, long size, TGAError* fn_nullable error fn_noescape) {
        if (isFile) {
            auto read = std::fread(out, size, 1, file);
            if (read != 1) {
                tga_error_other(error, "Could not read image data: %s", strerror(errno));
                return false;
            }
        }
        else {
            if (position + size > data.bufferSize) {
                tga_error_other(error, "Trying to read more (%ld) than available (%ld).", size, data.bufferSize - position);
                return false;
            }
            std::memcpy(out, data.charBuffer + position, size);
        }
        
        position += size;
        
        return true;
    }
    
};


static bool tga_validate_attribute_bits(uint8_t attributeBits) {
    return attributeBits == 0 // no Alpha data included
    || attributeBits == 1    // undefined data in the Alpha field, can be ignored
    || attributeBits == 2    // undefined data in the Alpha field, but should be retained
    || attributeBits == 4    // useful Alpha channel data is present
    || attributeBits == 8;   // pre-multiplied Alpha
}


static uint8_t tga_pixel_size(uint8_t pixelDepth) {
    auto rest = static_cast<uint8_t>(pixelDepth % 8 > 0);
    return pixelDepth / 8 + rest;
}


static bool tga_inspect_true_color_pixel_depth(uint8_t pixelDepth, uint8_t attributeBits, long* pixelBytes, long* numComponents, bool* useAlpha) {
    // Although the value 2 means unused alpha, we still present it
    auto hasAlpha = attributeBits == 2 || attributeBits == 4 || attributeBits == 8;
    switch (pixelDepth) {
        case 8:
            *pixelBytes = 1;
            *numComponents = 4;
            *useAlpha = hasAlpha;
            return true;
            
        case 15:
            *pixelBytes = 2;
            *numComponents = 3;
            *useAlpha = false;
            return true;
            
        case 16:
            *pixelBytes = 2;
            *numComponents = 4;
            *useAlpha = hasAlpha;
            return true;
            
        case 24:
            *pixelBytes = 3;
            *numComponents = 3;
            *useAlpha = false;
            return true;
            
        case 32:
            *pixelBytes = 4;
            *numComponents = 4;
            *useAlpha = true;
            return true;
            
        default:
            return false;
    }
}


static bool tga_validate(TGAReaderContext& context,
                         TGAError* fn_nullable error fn_noescape,
                         TGAHeader* outHeader,
                         TGAFooter* outFooter) {
    tga_error_reset(error);
    
    // 1. Read the footer
    
    // Go to the end of file
    if (context.setPosition(-1, error) == false) {
        return false;
    }
    
    // Get file size
    auto size = context.getPosition();
    
    // Read the footer to determine if it's the "New TGA Format", otherwise it may be the old TGA
    if (context.setPosition(size - sizeof(TGAFooter), error) == false) {
        return false;
    }
    
    TGAFooter footer;
    if (context.read(&footer, sizeof(TGAFooter), error) == false) {
        return false;
    }
    
#if DEBUG
    auto newTGAFormat = std::memcmp(footer.signature, "TRUEVISION-XFILE", 16) == 0;
    if (newTGAFormat) {
        printf("New TGA Format\n");
    }
#endif
    
    
    // 2. Read the header
    
    // Go to the start of file
    if (context.setPosition(0, error) == false) {
        return false;
    }
    
    TGAHeader header;
    if (context.read(&header, sizeof(TGAHeader), error) == false) {
        return false;
    }
    
    /*
     (TGAHeader) header = {
       idLength = '\0'
//       colorMapType = '\0'
//       imageType = '\x02'
//       colorMapFirstEntryIndex = 0
//       colorMapLength = 0
//       colorMapEntrySize = '\0'
//       xOrigin = 0
//       yOrigin = 0
//       width = 4096
//       height = 4096
//       pixelDepth = '\x18'
//       imageDescriptor = '\0'
     }
     */
    
    
    /*
     (TGAHeader) header = {
       idLength = '\0'
       colorMapType = '\0'
       imageType = '\x02'
       colorMapFirstEntryIndex = 0
       colorMapLength = 0
       colorMapEntrySize = '\0'
       xOrigin = 0
       yOrigin = 0
       width = 4096
       height = 4096
       pixelDepth = ' '
       imageDescriptor = '\b'
     }
     */
    
    // Validate header
    
    // Color map type
    auto colorMapType = static_cast<int>(header.colorMapType & 0x7F);
    if (colorMapType != 0 && colorMapType != 1) {
        tga_error_other(error, "Unknown color map type (%d), probably not TGA", colorMapType);
        return false;
    }
    
    // Image type
    auto imageType = static_cast<int>(header.imageType & 0x7F);
    if (imageType != 0 &&
        imageType != 1 && imageType != 2 && imageType != 3 &&
        imageType != 9 && imageType != 10 && imageType != 11) {
        tga_error_other(error, "Unknown image type (%d), probably not TGA", static_cast<int>(imageType));
        return false;
    }
    
    // Size
    if (header.width == 0 || header.height == 0) {
        tga_error_other(error, "Suspicious image size %dx%d, probably not TGA", header.width, header.height);
        return false;
    }
    if (header.width > 16384 || header.height > 16384) {
        tga_error_other(error, "Suspiciously big image size %dx%d, probably not TGA", header.width, header.height);
        return false;
    }
    
    
    // TODO: Unsupported, implement in the future.
    //if (colorMapType != 0) {
    //    tga_error_other(error, "No color map is supported");
    //    tga_close_file_if_not_equal(file, inFile, error);
    //    return false;
    //}
    if (imageType != 1 && imageType != 2 && imageType != 3) {
        tga_error_other(error, "Only uncompressed images are supported");
        return false;
    }
    if (header.colorMapFirstEntryIndex != 0) {
        tga_error_other(error, "Color map first entry index is not supported");
        return false;
    }
    if (header.colorMapEntrySize % 8 != 0) {
        tga_error_other(error, "Only color map entry size multiple of 8 bits is supported");
        return false;
    }
    if (header.colorMapEntrySize != 0 && header.colorMapEntrySize != 16 && header.colorMapEntrySize != 24 && header.colorMapEntrySize != 32) {
        tga_error_other(error, "Unsupported color map entry size: %d", header.colorMapEntrySize);
        return false;
    }
    //if (header.colorMapFirstEntryIndex != 0 || header.colorMapLength != 0 || header.colorMapEntrySize != 0) {
    //    tga_error_other(error, "Color map is not supported");
    //    tga_close_file_if_not_equal(file, inFile, error);
    //    return false;
    //}
    if (header.xOrigin != 0 || header.yOrigin != 0) {
        tga_error_other(error, "Image origin is not supported");
        return false;
    }
    if ((imageType == 1 || imageType == 9) && header.pixelDepth % 8 != 0) {
        tga_error_other(error, "Only pixel depths multiple of 8 bits are supported when image is color-mapped");
        return false;
    }
    //if (std::min(header.pixelDepth / 3, 8) != 8) {
    //    tga_error_other(error, "Only 8-bit pixel components are supported");
    //    tga_close_file_if_not_equal(file, inFile, error);
    //    return false;
    //}
    //if (header.pixelDepth != 24) {
    //    tga_error_other(error, "Only pixel depth 24 is supported");
    //    tga_close_file_if_not_equal(file, inFile, error);
    //    return false;
    //}
    if ((header.imageDescriptor & 0b00110000) != 0) {
        tga_error_other(error, "Image origin direction is not supported");
        return false;
    }
    auto attributeBits = header.imageDescriptor & 0b00001111;
    auto attributeBitsValid = tga_validate_attribute_bits(attributeBits);
    if (attributeBitsValid == false) {
        tga_error_other(error, "Unsupported attribute bits: %d", attributeBits);
        return false;
    }
    if ((imageType == 1 || imageType == 9) && (attributeBits != 0)) {
        tga_error_other(error, "Aplha channel info in the image descriptior is not supported when image is color-mapped");
        return false;
    }
    
    
    // Return header
    if (outHeader) {
        *outHeader = header;
    }
    
    // Return footer
    if (outFooter) {
        *outFooter = footer;
    }
    
    // Success
    return true;
}


bool TGAImage::isTGA(const TGASource& source fn_noescape, TGAError* fn_nullable error fn_noescape) {
    auto context = TGAReaderContext::fromSource(source, error);
    if (context.file == nullptr) {
        return false;
    }
    return tga_validate(context, error, nullptr, nullptr);
}



bool TGAImage::isTGA(const char* fn_nonnull path fn_noescape, TGAError* fn_nullable error fn_noescape) {
    return isTGA(TGASource(path), error);
}

bool TGAImage::isTGA(const void* fn_nonnull buffer fn_noescape, long bufferSize, TGAError* fn_nullable error fn_noescape) {
    return isTGA(TGASource(buffer, bufferSize), error);
}


struct helper {
    static inline std::byte expand5To8(uint16_t v) {
        auto x = static_cast<std::byte>(v);
        return (x << 3) | (x >> 2);
    }
    
    
    static inline float fromLinearToSRGB(float linear) {
        if (linear < 0.0031308f) {
            return linear * 12.92f;
        }
        
        return std::pow(linear, 1.0f / 2.4f) * 1.055f - 0.055f;
    }


    static inline float fromSRGBToLinear(float sRGB) {
        if (sRGB <= 0.04045f) {
            return sRGB / 12.92f;
        }
        
        return std::pow((sRGB + 0.055f) / 1.055f, 2.4f);
    }
    
    
    static inline float ft(uint8_t value) {
        return static_cast<float>(value) / 255.0f;
    }
    
    
    static inline uint8_t ut(float value) {
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        return static_cast<uint8_t>(roundf(value * 255.0f));
    }
    
    
    static inline float _clamp(float value, float min, float max) {
        if (value < min) {
            return min;
        }
        
        if (value > max) {
            return max;
        }
        
        return value;
    }
    
    
    static inline void unmultiply(std::byte* dst) {
#if 1
        auto bytes = reinterpret_cast<uint8_t*>(dst);
        auto alpha = bytes[3];
        if (alpha > 0 && alpha < 255) [[ likely ]] {
# if 0
            // sRGB -> linear -> unmultiply -> sRGB
            auto fa = ft(alpha);
            bytes[0] = ut(fromLinearToSRGB(fromSRGBToLinear(ft(bytes[0])) / fa));
            bytes[1] = ut(fromLinearToSRGB(fromSRGBToLinear(ft(bytes[1])) / fa));
            bytes[2] = ut(fromLinearToSRGB(fromSRGBToLinear(ft(bytes[2])) / fa));
# else
            bytes[0] = _clamp(bytes[0] * 255.0f / alpha, 0.0f, 255.0f);
            bytes[1] = _clamp(bytes[1] * 255.0f / alpha, 0.0f, 255.0f);
            bytes[2] = _clamp(bytes[2] * 255.0f / alpha, 0.0f, 255.0f);
#endif
        }
# if 0
        else if (alpha == 0) {
            bytes[0] = 0;
            bytes[1] = 0;
            bytes[2] = 0;
        }
# endif
#endif
    }
    
    
    static inline void readB8G8R8(std::byte* dst, const std::byte* src) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
    }
    
    static inline void readB8G8R8A8(std::byte* dst, const std::byte* src, uint8_t alphaMask, uint8_t defaultAlpha) {
        readB8G8R8(dst, src);
        dst[3] = static_cast<std::byte>((static_cast<uint8_t>(src[3]) & alphaMask) + defaultAlpha);
    }
    
    static inline void readB8G8R8A8_premul(std::byte* dst, const std::byte* src) {
        readB8G8R8A8(dst, src, 0xFF, 0x00);
        unmultiply(dst);
    }
    
    
    union short_pixel final {
        uint16_t shortValue;
        std::byte bytes[2];
        
        short_pixel(): shortValue(0) { }
        short_pixel(const std::byte* src) {
            bytes[0] = src[0];
            bytes[1] = src[1];
        }
    };
    
    static inline void readB5G5R5(std::byte* dst, uint16_t src) {
#if 0 //USE_SIMD
        //constexpr simd_ushort3 s = { 10, 5, 0 };
        //auto vec = simd_make_short3(src, src, src) >> s;
        auto vec = simd_make_short3(src >> 10, src >> 5, src);
        vec = vec & 0x1F;
        vec = (vec << 3) | (vec >> 2);
        dst[0] = static_cast<std::byte>(vec.x);
        dst[1] = static_cast<std::byte>(vec.y);
        dst[2] = static_cast<std::byte>(vec.z);
#else
        dst[0] = expand5To8((src >> 10) & 0x1F);
        dst[1] = expand5To8((src >>  5) & 0x1F);
        dst[2] = expand5To8((src >>  0) & 0x1F);
#endif
    }
    
    static inline void readB5G5R5(std::byte* dst, const std::byte* src) {
        readB5G5R5(dst, short_pixel(src).shortValue);
    }
    
    
    static inline void readB5G5R5A1(std::byte* dst, uint16_t src, uint8_t alphaMask, uint8_t defaultAlpha) {
        readB5G5R5(dst, src);
        dst[3] = static_cast<std::byte>((((src >> 15) * 255) & alphaMask) + defaultAlpha);
    }
    
    static inline void readB5G5R5A1(std::byte* dst, const std::byte* src, uint8_t alphaMask, uint8_t defaultAlpha) {
        readB5G5R5A1(dst, short_pixel(src).shortValue, alphaMask, defaultAlpha);
    }
    
    static inline void readB5G5R5A1_premul(std::byte* dst, const std::byte* src) {
        readB5G5R5A1(dst, src, 0xFF, 0x00);
        unmultiply(dst);
    }
};


TGAImage* fn_nullable TGAImage::load(const TGASource& source fn_noescape, TGAError* fn_nullable error fn_noescape) {
    // Open file
    auto context = TGAReaderContext::fromSource(source, error);
    if (context.file == nullptr) {
        return nullptr;
    }
    
    // Validate TGA
    TGAHeader header;
    TGAFooter footer;
    if (tga_validate(context, error, &header, &footer) == false) {
        return nullptr;
    }
    
    // Read contents
    
    // Image ID
    auto imageIdLength = static_cast<long>(header.idLength);
    char imageId[256];
    imageId[0] = 0;
    if (imageIdLength > 0) {
        auto idDataOffset = sizeof(TGAHeader);
        if (context.setPosition(idDataOffset, error) == false) {
            return nullptr;
        }
        
        if (context.read(imageId, imageIdLength, error) == false) {
            return nullptr;
        }
        imageId[imageIdLength] = 0;
        
        // Do something with imageId
        printf("Image ID: %s\n", imageId);
    }
    
    
    // Color map data
    long colorMapNumComponents = 0;
    auto numColorMapEntries = static_cast<long>(header.colorMapLength);
    auto colorMapEntrySize = static_cast<long>(header.colorMapEntrySize);
    auto colorMapSize = numColorMapEntries * (colorMapEntrySize / 8);
    union ColorMapEntry final {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
        
        uint8_t components[4];
        std::byte bytes[4];
    };
    static_assert(sizeof(ColorMapEntry) == 4, "Size of ColorMapEntry should be 4");
    ColorMapEntry* colorMapEntries = nullptr;
    if ((header.colorMapType & 0x7F) == 1) {
        // Go to color map data section
        auto colorMapOffset = sizeof(TGAHeader) + imageIdLength;
        if (context.setPosition(colorMapOffset, error) == false) {
            return nullptr;
        }
        
        // Read color map data
        auto colorMapData = static_cast<std::byte*>(std::malloc(colorMapSize));
        if (context.read(colorMapData, colorMapSize, error) == false) {
            std::free(colorMapData);
            return nullptr;
        }
        
        // Parse entries
        colorMapEntries = new ColorMapEntry[numColorMapEntries];
        if (colorMapEntrySize == 32) {
            colorMapNumComponents = 4;
            auto byteEntries = reinterpret_cast<std::byte*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                helper::readB8G8R8A8(colorMapEntries[i].bytes, byteEntries, 0xFF, 0x00);
                byteEntries += 4;
            }
        }
        else if (colorMapEntrySize == 24) {
            colorMapNumComponents = 3;
            auto byteEntries = reinterpret_cast<std::byte*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                helper::readB8G8R8(colorMapEntries[i].bytes, byteEntries);
                byteEntries += 3;
            }
        }
        else if (colorMapEntrySize == 16) {
            colorMapNumComponents = 3;
            auto uint16Entries = reinterpret_cast<uint16_t*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                //helper::readB5G5R5A1(colorMapEntries[i].bytes, uint16Entries[0], 0xFF, 0x00);
                helper::readB5G5R5(colorMapEntries[i].bytes, uint16Entries[0]);
                
                //auto entry = uint16Entries[0];
                // // TODO: Normalize values individually?
                //auto r = (entry >> 10) & 0x1F;
                //auto g = (entry >>  5) & 0x1F;
                //auto b = (entry >>  0) & 0x1F;
                //
                //colorMapEntries[i] = {
                //    .r = static_cast<uint8_t>(r),
                //    .g = static_cast<uint8_t>(g),
                //    .b = static_cast<uint8_t>(b),
                //    .a = 0
                //};
                
                uint16Entries += 1;
            }
        }
        else {
            //
        }
        
        // Clean up
        std::free(colorMapData);
    }
    
    
    // Image data
    std::byte* rawData = nullptr;
    long rawDataSize = header.width * header.height * tga_pixel_size(header.pixelDepth);
    {
        // Go to image data section
        auto imageDataOffset = sizeof(TGAHeader) + imageIdLength + colorMapSize;
        if (context.setPosition(imageDataOffset, error) == false) {
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
        
        // Read raw image data
        rawData = static_cast<std::byte*>(std::malloc(rawDataSize));
        if (context.read(rawData, rawDataSize, error) == false) {
            std::free(rawData);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    
    long numComponents = 0;
    long imageDataSize = 0;
    std::byte* imageData = nullptr;
    auto imageType = header.imageType & 0x7F;
    // Uncompressed, Color-mapped Image
    if (imageType == 1) {
        // Allocate image data
        numComponents = colorMapNumComponents;
        imageDataSize = header.width * header.height * numComponents;
        imageData = static_cast<std::byte*>(std::malloc(imageDataSize));
        
        // Parse raw image data
        if (header.pixelDepth == 8) {
            auto rowSize = header.width * numComponents;
            auto indices = reinterpret_cast<uint8_t*>(rawData);
            
            if (numComponents == 3) {
                for (auto y = 0; y < header.height; y++) {
                    auto sourceIndex = header.height - y - 1;
                    auto source = indices + sourceIndex * header.width;
                    auto destination = reinterpret_cast<uint8_t*>(imageData + y * rowSize);
                    for (auto x = 0; x < header.width; x++) {
                        auto index = source[x];
                        if (index >= numColorMapEntries) [[ unlikely ]] {
                            tga_error_other(error, "Color entry index (%d) out of bounds (%ld)", index, numColorMapEntries);
                            std::free(imageData);
                            std::free(rawData);
                            if (colorMapEntries != nullptr) {
                                delete [] colorMapEntries;
                            }
                            return nullptr;
                        }
                        
                        auto entry = colorMapEntries[index];
                        
                        destination[0] = entry.r;
                        destination[1] = entry.g;
                        destination[2] = entry.b;
                        
                        destination += 3;
                    }
                }
            }
            else if (numComponents == 4) {
                for (auto y = 0; y < header.height; y++) {
                    auto sourceIndex = header.height - y - 1;
                    auto source = indices + sourceIndex * header.width;
                    auto destination = reinterpret_cast<uint8_t*>(imageData + y * rowSize);
                    for (auto x = 0; x < header.width; x++) {
                        auto index = source[x];
                        if (index >= numColorMapEntries) [[ unlikely ]] {
                            tga_error_other(error, "Color entry index (%d) out of bounds (%ld)", index, numColorMapEntries);
                            std::free(imageData);
                            std::free(rawData);
                            if (colorMapEntries != nullptr) {
                                delete [] colorMapEntries;
                            }
                            return nullptr;
                        }
                        
                        auto entry = colorMapEntries[index];
                        
                        destination[0] = entry.r;
                        destination[1] = entry.g;
                        destination[2] = entry.b;
                        destination[3] = entry.a;
                        
                        destination += 4;
                    }
                }
            }
            else {
                tga_error_other(error, "Unsupported number of components: %ld", numComponents);
                std::free(imageData);
                std::free(rawData);
                if (colorMapEntries != nullptr) {
                    delete [] colorMapEntries;
                }
                return nullptr;
            }
        }
        else {
            tga_error_other(error, "Unsupported pixel depth for uncompressed color-mapped image: %d", header.pixelDepth);
            std::free(imageData);
            std::free(rawData);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    // Uncompressed, True-color Image
    else if (imageType == 2) {
        // Determine number of components
        auto attributeBits = header.imageDescriptor & 0b00001111;
        auto attributeBitsValid = tga_validate_attribute_bits(attributeBits);
        
        long pixelBytes = 0;
        bool hasAlpha = false;
        bool premultipliedAlpha = attributeBits == 8;
        auto pixelDepthValid = tga_inspect_true_color_pixel_depth(header.pixelDepth, attributeBits, &pixelBytes, &numComponents, &hasAlpha);
        uint8_t alphaMask = hasAlpha ? 0xFF : 0x00;
        uint8_t defaultAlpha = hasAlpha ? 0x00 : 0xFF;
        
        if (attributeBitsValid == false || pixelDepthValid == false) {
            tga_error_other(error, "Only 8-bit channel 24-bit RGB or 32-bit RGBA textures are supported. Pixel depth: %d, alpha channel bits: %d", header.pixelDepth, attributeBits);
            std::free(rawData);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
        
        // Allocate image data
        imageDataSize = header.width * header.height * numComponents;
        imageData = static_cast<std::byte*>(std::malloc(imageDataSize));
        
        // Turn image upside down
        auto sourceRowSize = header.width * pixelBytes;
        auto destinationRowSize = header.width * numComponents;
        
#define pixel_loop(_dst_inc, _src_inc, _main_body_) \
for (auto y = 0; y < header.height; y++) { \
    auto sourceIndex = header.height - y - 1; \
    auto source = rawData + sourceIndex * sourceRowSize; \
    auto destination = imageData + y * destinationRowSize; \
    for (auto x = 0; x < header.width; x++) { \
        _main_body_ \
        source += (_src_inc); \
        destination += (_dst_inc); \
    } \
}
        
        if (numComponents == 3) {
            if (pixelBytes == 2) {
                pixel_loop(3, 2, { helper::readB5G5R5(destination, source); })
            }
            else if (pixelBytes == 3) {
                pixel_loop(3, 3, { helper::readB8G8R8(destination, source); })
            }
        }
        else if (numComponents == 4) {
            if (pixelBytes == 2) {
                if (premultipliedAlpha) {
                    pixel_loop(4, 2, { helper::readB5G5R5A1_premul(destination, source); })
                }
                else {
                    pixel_loop(4, 2, { helper::readB5G5R5A1(destination, source, alphaMask, defaultAlpha); })
                }
            }
            else if (pixelBytes == 4) {
                if (premultipliedAlpha) {
                    pixel_loop(4, 4, { helper::readB8G8R8A8_premul(destination, source); })
                }
                else {
                    pixel_loop(4, 4, { helper::readB8G8R8A8(destination, source, alphaMask, defaultAlpha); })
                }
            }
        }
        else {
            tga_error_other(error, "Unsupported number of components: %ld", numComponents);
            std::free(imageData);
            std::free(rawData);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    // Uncompressed, Black-and-white Image
    else if (imageType == 3) {
        // Sanity check
        //auto attributeBits = header.imageDescriptor & 0b00001111;
        //if (attributeBits != 0) {
        //    tga_error_other(error, "Alpha channel bits are not supported in one-channel images. Alpha channel bits: %d", attributeBits);
        //    tga_close_file(file, error);
        //    std::free(rawData);
        //    if (colorMapEntries != nullptr) {
        //        delete [] colorMapEntries;
        //    }
        //    return nullptr;
        //}
        if (header.pixelDepth != 8) {
            tga_error_other(error, "Only 8-bit pixel depth is supported in one-channel images. Pixel depth: %d", header.pixelDepth);
            std::free(rawData);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
        
        numComponents = 1;
        
        // Allocate image data
        imageDataSize = header.width * header.height * numComponents;
        imageData = static_cast<std::byte*>(std::malloc(imageDataSize));
        
        // Turn image upside down
        auto rowSize = header.width * numComponents;
        for (auto y = 0; y < header.height; y++) {
            auto sourceIndex = header.height - y - 1;
            auto source = rawData + sourceIndex * rowSize;
            auto destination = imageData + y * rowSize;
            
#if 1
            std::memcpy(destination, source, rowSize);
#else
            for (auto x = 0; x < header.width; x++) {
                destination[0] = source[0];
                
                source += 1;
                destination += 1;
            }
#endif
        }
    }
    else {
        tga_error_other(error, "Unsupported image type: %d", imageType);
        if (rawData != nullptr) {
            std::free(rawData);
        }
        if (colorMapEntries != nullptr) {
            delete [] colorMapEntries;
        }
        return nullptr;
    }
    
    
    // Developer data
    //
    
    
    // Clean up raw data
    if (rawData != nullptr) {
        std::free(rawData);
    }
    
    // Clean up color map entries
    if (colorMapEntries != nullptr) {
        delete [] colorMapEntries;
    }
    
    // Return TGA image
    return new TGAImage(header.width, header.height, numComponents, imageData, true, imageDataSize);
}


TGAImage* fn_nullable TGAImage::load(const char* fn_nonnull path fn_noescape, TGAError* fn_nullable error fn_noescape) {
    return load(TGASource(path), error);
}


TGAImage* fn_nullable TGAImage::load(const void* fn_nonnull buffer fn_noescape, long bufferSize, TGAError* fn_nullable error fn_noescape) {
    return load(TGASource(buffer, bufferSize), error);
}


void* fn_nonnull TGAImage::Unsafe::transferContentsOwnership(TGAImage* fn_nonnull tga) {
    tga->_ownsContents = false;
    return tga->_contents;
}


FN_IMPLEMENT_SWIFT_INTERFACE1(TGAImage)
