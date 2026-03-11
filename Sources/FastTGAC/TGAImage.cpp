//
//  TGAImage.cpp
//  FastTGA
//
//  Created by Evgenij Lutz on 26.01.26.
//

#include <FastTGAC/TGAImage.hpp>


void tga_error_reset(TGAError* fn_nullable error fn_noescape) {
    if (error == nullptr) {
        return;
    }
    
    memset(error, 0, sizeof(TGAError));
}

void tga_error_notImplemented(TGAError* fn_nullable error fn_noescape) {
    if (error == nullptr) {
        return;
    }
    
    error->error = true;
    error->code = TGAErrorCode::notImplemented;
}

void tga_error_other(TGAError* fn_nullable error fn_noescape, const char* fn_nonnull __restrict format, ...) __printflike(2, 3);
void tga_error_other(TGAError* fn_nullable error fn_noescape, const char* fn_nonnull __restrict format, ...) {
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


TGAImage::TGAImage(long width, long height, long numComponents, char* fn_nonnull contents, long size):
_referenceCounter(1),
_width(width),
_height(height),
_numComponents(numComponents),
_contents(contents),
_size(size) {
    //
}


TGAImage::~TGAImage() {
    delete [] _contents;
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


static bool tga_close_file_if_not_equal(FILE* fn_nullable file, FILE* fn_nullable other, TGAError* fn_nullable error fn_noescape) {
    if (file == other) {
        return true;
    }
    
    return tga_close_file(file, error);
}


static bool tga_validate(const char* fn_nonnull filePath fn_noescape,
                         TGAError* fn_nullable error fn_noescape,
                         FILE* inFile,
                         TGAHeader* outHeader,
                         TGAFooter* outFooter) {
    tga_error_reset(error);
    
    // Open file
    auto file = inFile;
    if (file == nullptr) {
        file = tga_open_file(filePath, error);
        if (file == nullptr) {
            return false;
        }
    }
    
    
    // 1. Read the footer
    
    // Go to the end of file
    auto result = std::fseek(file, 0, SEEK_END);
    if (result != 0) {
        tga_error_other(error, "Could not reach the end of the file: %s", strerror(errno));
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    // Get file size
    auto size = ftell(file);
    
    // Read the footer to determine if it's the "New TGA Format", otherwise it may be the old TGA
    result = std::fseek(file, size - sizeof(TGAFooter), SEEK_SET);
    if (result != 0) {
        tga_error_other(error, "Could not set the file position: %s", strerror(errno));
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    TGAFooter footer;
    auto read = std::fread(&footer, sizeof(TGAFooter), 1, file);
    if (read != 1) {
        tga_error_other(error, "Could not read the footer: %s", strerror(errno));
        tga_close_file_if_not_equal(file, inFile, error);
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
    result = std::fseek(file, 0, SEEK_SET);
    if (result != 0) {
        tga_error_other(error, "Could not set the file position: %s", strerror(errno));
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    TGAHeader header;
    read = std::fread(&header, sizeof(TGAHeader), 1, file);
    if (read != 1) {
        tga_error_other(error, "Could not read the header: %s", strerror(errno));
        tga_close_file_if_not_equal(file, inFile, error);
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
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    // Image type
    auto imageType = static_cast<int>(header.imageType & 0x7F);
    if (imageType != 0 &&
        imageType != 1 && imageType != 2 && imageType != 3 &&
        imageType != 9 && imageType != 10 && imageType != 11) {
        tga_error_other(error, "Unknown image type (%d), probably not TGA", static_cast<int>(imageType));
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    // Size
    if (header.width == 0 || header.height == 0) {
        tga_error_other(error, "Suspicious image size %dx%d, probably not TGA", header.width, header.height);
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.width > 16384 || header.height > 16384) {
        tga_error_other(error, "Suspiciously big image size %dx%d, probably not TGA", header.width, header.height);
        tga_close_file_if_not_equal(file, inFile, error);
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
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.colorMapFirstEntryIndex != 0) {
        tga_error_other(error, "Color map first entry index is not supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.colorMapEntrySize % 8 != 0) {
        tga_error_other(error, "Only color map entry size multiple of 8 bits is supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.colorMapEntrySize != 0 && header.colorMapEntrySize != 16 && header.colorMapEntrySize != 24 && header.colorMapEntrySize != 32) {
        tga_error_other(error, "Unsupported color map entry size: %d", header.colorMapEntrySize);
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    //if (header.colorMapFirstEntryIndex != 0 || header.colorMapLength != 0 || header.colorMapEntrySize != 0) {
    //    tga_error_other(error, "Color map is not supported");
    //    tga_close_file_if_not_equal(file, inFile, error);
    //    return false;
    //}
    if (header.xOrigin != 0 || header.yOrigin != 0) {
        tga_error_other(error, "Image origin is not supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.pixelDepth % 8 != 0) {
        tga_error_other(error, "Only pixel depths multiple of 8 bits are supported");
        tga_close_file_if_not_equal(file, inFile, error);
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
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    auto alphaChannelBits = header.imageDescriptor & 0b00001111;
    if (alphaChannelBits != 0 && alphaChannelBits != 8) {
        tga_error_other(error, "Either no alpha channel or alpha channels with 8 bits are supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if ((imageType == 1 || imageType == 9) && (alphaChannelBits != 0)) {
        tga_error_other(error, "Aplha channel info in the image descriptior is not supported when image is color-mapped");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    
    // Close file if needed
    tga_close_file_if_not_equal(file, inFile, error);
    
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


bool TGAImage::isTGA(const char* fn_nonnull filePath fn_noescape, TGAError* fn_nullable error fn_noescape) {
    return tga_validate(filePath, error, nullptr, nullptr, nullptr);
}


TGAImage* fn_nullable TGAImage::load(const char* fn_nonnull filePath fn_noescape, TGAError* fn_nullable error fn_noescape) {
    // Open file
    auto file = tga_open_file(filePath, error);
    if (file == nullptr) {
        return nullptr;
    }
    
    // Validate TGA
    TGAHeader header;
    TGAFooter footer;
    if (tga_validate(filePath, error, file, &header, &footer) == false) {
        tga_close_file(file, error);
        return nullptr;
    }
    
    // Read contents
    
    // Image ID
    auto imageIdLength = static_cast<long>(header.idLength);
    char imageId[256];
    imageId[0] = 0;
    if (imageIdLength > 0) {
        auto idDataOffset = sizeof(TGAHeader);
        auto result = std::fseek(file, idDataOffset, SEEK_SET);
        if (result != 0) {
            tga_error_other(error, "Could not set the file position: %s", strerror(errno));
            tga_close_file(file, error);
            return nullptr;
        }
        
        auto read = std::fread(imageId, imageIdLength, 1, file);
        if (read != 1) {
            tga_error_other(error, "Could not read image data: %s", strerror(errno));
            tga_close_file(file, error);
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
    struct ColorMapEntry {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };
    ColorMapEntry* colorMapEntries = nullptr;
    if ((header.colorMapType & 0x7F) == 1) {
        // Go to color map data section
        auto colorMapOffset = sizeof(TGAHeader) + imageIdLength;
        auto result = std::fseek(file, colorMapOffset, SEEK_SET);
        if (result != 0) {
            tga_error_other(error, "Could not set the file position: %s", strerror(errno));
            tga_close_file(file, error);
            return nullptr;
        }
        
        // Read color map data
        auto colorMapData = new char[colorMapSize];
        auto read = std::fread(colorMapData, colorMapSize, 1, file);
        if (read != 1) {
            tga_error_other(error, "Could not read image data: %s", strerror(errno));
            tga_close_file(file, error);
            delete [] colorMapData;
            return nullptr;
        }
        
        // Parse entries
        colorMapEntries = new ColorMapEntry[numColorMapEntries];
        if (colorMapEntrySize == 32) {
            colorMapNumComponents = 4;
            auto uint8Entries = reinterpret_cast<uint8_t*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                colorMapEntries[i] = {
                    .r = uint8Entries[2],
                    .g = uint8Entries[1],
                    .b = uint8Entries[0],
                    .a = uint8Entries[3]
                };
                uint8Entries += 4;
            }
        }
        else if (colorMapEntrySize == 24) {
            colorMapNumComponents = 3;
            auto uint8Entries = reinterpret_cast<uint8_t*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                colorMapEntries[i] = {
                    .r = uint8Entries[2],
                    .g = uint8Entries[1],
                    .b = uint8Entries[0],
                    .a = 0
                };
                uint8Entries += 3;
            }
        }
        else if (colorMapEntrySize == 16) {
            colorMapNumComponents = 3;
            auto uint16Entries = reinterpret_cast<uint16_t*>(colorMapData);
            for (auto i = 0; i < numColorMapEntries; i++) {
                auto entry = uint16Entries[0];
                // TODO: Normalize values individually?
                auto r = (entry >> 10) & 0x1F;
                auto g = (entry >>  5) & 0x1F;
                auto b = (entry >>  0) & 0x1F;
                
                colorMapEntries[i] = {
                    .r = static_cast<uint8_t>(r),
                    .g = static_cast<uint8_t>(g),
                    .b = static_cast<uint8_t>(b),
                    .a = 0
                };
                uint16Entries += 1;
            }
        }
        else {
            //
        }
        
        // Clean up
        delete [] colorMapData;
    }
    
    
    // Image data
    char* rawData = nullptr;
    long rawDataSize = header.width * header.height * (header.pixelDepth / 8);
    {
        // Go to image data section
        auto imageDataOffset = sizeof(TGAHeader) + imageIdLength + colorMapSize;
        auto result = std::fseek(file, imageDataOffset, SEEK_SET);
        if (result != 0) {
            tga_error_other(error, "Could not set the file position: %s", strerror(errno));
            tga_close_file(file, error);
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
        
        // Read raw image data
        rawData = new char[rawDataSize];
        auto read = std::fread(rawData, rawDataSize, 1, file);
        if (read != 1) {
            tga_error_other(error, "Could not read image data: %s", strerror(errno));
            tga_close_file(file, error);
            delete [] rawData;
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    
    long numComponents = 0;
    long imageDataSize = 0;
    char* imageData = nullptr;
    auto imageType = header.imageType & 0x7F;
    // Uncompressed, Color-mapped Image
    if (imageType == 1) {
        // Allocate image data
        numComponents = colorMapNumComponents;
        imageDataSize = header.width * header.height * numComponents;
        imageData = new char[imageDataSize];
        
        // Parse raw image data
        if (header.pixelDepth == 8) {
            auto rowSize = header.width * numComponents;
            auto indices = reinterpret_cast<uint8_t*>(rawData);
            
            if (numComponents == 3) {
                for (auto y = 0; y < header.height; y++) {
                    auto sourceIndex = header.height - y - 1;
                    auto source = indices + sourceIndex * header.width;
                    auto destination = imageData + y * rowSize;
                    for (auto x = 0; x < header.width; x++) {
                        auto index = source[x];
                        if (index >= numColorMapEntries) {
                            tga_error_other(error, "Color entry index (%d) out of bounds (%ld)", index, numColorMapEntries);
                            tga_close_file(file, error);
                            delete [] imageData;
                            delete [] rawData;
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
                    auto destination = imageData + y * rowSize;
                    for (auto x = 0; x < header.width; x++) {
                        auto index = source[x];
                        if (index >= numColorMapEntries) {
                            tga_error_other(error, "Color entry index (%d) out of bounds (%ld)", index, numColorMapEntries);
                            tga_close_file(file, error);
                            delete [] imageData;
                            delete [] rawData;
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
                tga_close_file(file, error);
                delete [] imageData;
                delete [] rawData;
                if (colorMapEntries != nullptr) {
                    delete [] colorMapEntries;
                }
                return nullptr;
            }
        }
        else {
            tga_error_other(error, "Unsupported pixel depth for uncompressed color-mapped image: %d", header.pixelDepth);
            tga_close_file(file, error);
            delete [] imageData;
            delete [] rawData;
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    // Uncompressed, True-color Image
    else if (imageType == 2) {
        // Determine number of components
#if 1
        auto alphaChannelBits = header.imageDescriptor & 0b00001111;
        numComponents = alphaChannelBits ? 4 : 3;
        auto pixelWithoutAlphaSize = header.pixelDepth - alphaChannelBits;
        auto componentSize = pixelWithoutAlphaSize / 3;
        if (pixelWithoutAlphaSize != 24 || componentSize != 8 || (alphaChannelBits != 0 && alphaChannelBits != 8)) {
            tga_error_other(error, "Only 8-bit channel 24-bit RGB or 32-bit RGBA textures are supported. Pixel depth: %d, alpha channel bits: %d", header.pixelDepth, alphaChannelBits);
            tga_close_file(file, error);
            delete [] rawData;
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
#else
        // Actually wrong formula
        numComponents = header.pixelDepth / 8;
#endif
        
        // Allocate image data
        imageDataSize = header.width * header.height * numComponents;
        imageData = new char[imageDataSize];
        
        // Turn image upside down
        auto rowSize = header.width * numComponents;
        if (numComponents == 3) {
            for (auto y = 0; y < header.height; y++) {
                auto sourceIndex = header.height - y - 1;
                auto source = rawData + sourceIndex * rowSize;
                auto destination = imageData + y * rowSize;
                for (auto x = 0; x < header.width; x++) {
                    destination[0] = source[2];
                    destination[1] = source[1];
                    destination[2] = source[0];
                    
                    source += 3;
                    destination += 3;
                }
            }
        }
        else if (numComponents == 4) {
            for (auto y = 0; y < header.height; y++) {
                auto sourceIndex = header.height - y - 1;
                auto source = rawData + sourceIndex * rowSize;
                auto destination = imageData + y * rowSize;
                
                for (auto x = 0; x < header.width; x++) {
                    destination[0] = source[2];
                    destination[1] = source[1];
                    destination[2] = source[0];
                    destination[3] = source[3];
                    
                    source += 4;
                    destination += 4;
                }
            }
        }
        else {
            tga_error_other(error, "Unsupported number of components: %ld", numComponents);
            tga_close_file(file, error);
            delete [] imageData;
            delete [] rawData;
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
    }
    else if (imageType == 3) {
        // Sanity check
        //auto alphaChannelBits = header.imageDescriptor & 0b00001111;
        //if (alphaChannelBits != 0) {
        //    tga_error_other(error, "Alpha channel bits are not supported in one-channel images. Alpha channel bits: %d", alphaChannelBits);
        //    tga_close_file(file, error);
        //    delete [] rawData;
        //    if (colorMapEntries != nullptr) {
        //        delete [] colorMapEntries;
        //    }
        //    return nullptr;
        //}
        if (header.pixelDepth != 8) {
            tga_error_other(error, "Only 8-bit pixel depth is supported in one-channel images. Pixel depth: %d", header.pixelDepth);
            tga_close_file(file, error);
            delete [] rawData;
            if (colorMapEntries != nullptr) {
                delete [] colorMapEntries;
            }
            return nullptr;
        }
        
        numComponents = 1;
        
        // Allocate image data
        imageDataSize = header.width * header.height * numComponents;
        imageData = new char[imageDataSize];
        
        // Turn image upside down
        auto rowSize = header.width * numComponents;
        for (auto y = 0; y < header.height; y++) {
            auto sourceIndex = header.height - y - 1;
            auto source = rawData + sourceIndex * rowSize;
            auto destination = imageData + y * rowSize;
            
#if 1
            memcpy(destination, source, rowSize);
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
        tga_close_file(file, error);
        if (rawData != nullptr) {
            delete [] rawData;
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
        delete [] rawData;
    }
    
    // Clean up color map entries
    if (colorMapEntries != nullptr) {
        delete [] colorMapEntries;
    }
    
    // Close file
    tga_close_file(file, error);
    
    // Return TGA image
    return new TGAImage(header.width, header.height, numComponents, imageData, imageDataSize);
}


FN_IMPLEMENT_SWIFT_INTERFACE1(TGAImage)
