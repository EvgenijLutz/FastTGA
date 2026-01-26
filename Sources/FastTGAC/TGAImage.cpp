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
    
    auto newTGAFormat = std::memcmp(footer.signature, "TRUEVISION-XFILE", 16) == 0;
#if DEBUG
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
    auto colorMapType = static_cast<int>(header.colorMapType & 0xF);
    if (colorMapType != 0 && colorMapType != 1) {
        tga_error_other(error, "Unknown color map type (%d), probably not TGA", colorMapType);
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    
    // Image type
    if (header.imageType != 0 &&
        header.imageType != 1 &&
        header.imageType != 2 &&
        header.imageType != 3 &&
        header.imageType != 9 &&
        header.imageType != 10 &&
        header.imageType != 11) {
        tga_error_other(error, "Unknown image type (%d), probably not TGA", static_cast<int>(header.imageType));
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
    if (colorMapType != 0) {
        tga_error_other(error, "No color map is supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.imageType != 2) {
        tga_error_other(error, "Only \"Uncompressed, True-color Image\" is supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
    if (header.colorMapFirstEntryIndex != 0 || header.colorMapLength != 0 || header.colorMapEntrySize != 0) {
        tga_error_other(error, "Color map is not supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
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
    if (std::min(header.pixelDepth / 3, 8) != 8) {
        tga_error_other(error, "Only 8-bit pixel components are supported");
        tga_close_file_if_not_equal(file, inFile, error);
        return false;
    }
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
    
    // 3. Image data
    
    
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
    
    // Color map data
    
    // Image data
    
    auto imageDataOffset = sizeof(TGAHeader) + header.idLength + header.colorMapLength * (header.colorMapEntrySize / 8);
    auto result = std::fseek(file, imageDataOffset, SEEK_SET);
    if (result != 0) {
        tga_error_other(error, "Could not set the file position: %s", strerror(errno));
        tga_close_file(file, error);
        return nullptr;
    }
    
    auto numComponents = header.pixelDepth / 8;
    auto imageDataSize = header.width * header.height * numComponents;
    auto rawData = new char[imageDataSize];
    auto imageData = new char[imageDataSize];
    auto read = std::fread(rawData, imageDataSize, 1, file);
    if (read != 1) {
        tga_error_other(error, "Could not read image data: %s", strerror(errno));
        tga_close_file(file, error);
        delete [] rawData;
        return nullptr;
    }
    
    // Turn image upside down
    auto numSwapComponents = std::min(numComponents, 3);
    auto add = numComponents > 3 ? 1 : 0;
    auto rowSize = header.width * numComponents;
    for (auto y = 0; y < header.height; y++) {
        auto sourceIndex = header.height - y - 1;
        auto source = rawData + sourceIndex * rowSize;
        auto destination = imageData + y * rowSize;
        
#if 0
        memcpy(destination, source, rowSize);
#else
        for (auto x = 0; x < header.width; x++) {
            auto v = x * numComponents;
            auto s = source + v;
            auto d = destination + v;
            for (auto c = 0; c < numSwapComponents; c++) {
                d[c] = s[numComponents - c - 1 - add];
            }
            if (add) {
                d[3] = s[3];
            }
        }
#endif
    }
    
    // Clean up
    delete [] rawData;
    
    // Close file
    tga_close_file(file, error);
    
    // Return TGA image
    return new TGAImage(header.width, header.height, numComponents, imageData, imageDataSize);
}


FN_IMPLEMENT_SWIFT_INTERFACE1(TGAImage)
