//
//  TGAImage2.swift
//  FastTGA
//
//  Created by Evgenij Lutz on 18.06.26.
//

import Foundation


public enum TGAImage2Error: Error, CustomStringConvertible, Sendable {
    case invalidTGA(_ message: String)
    case unsupported(_ message: String)
    case fileReadFailed(_ message: String)
    
    public var description: String {
        switch self {
        case .invalidTGA(let message), .unsupported(let message), .fileReadFailed(let message):
            return message
        }
    }
}


/// TGA image decoded into tightly packed top-left-origin component bytes. It's a test Swift implementation of C++ version of the ``TGAImage`` class. At the moment it has terrible performance.
///
/// Supported formats match the original C++ decoder: uncompressed color-mapped,
/// true-color, and greyscale TGA images with 8-bit components.
public final class TGAImage2: Sendable {
    public let width: Int
    public let height: Int
    
    /// Number of pixel components. It's either `1`, `3`, or `4`.
    public let numComponents: Int
    
    /// Contents of the image. The size is always multiple of ``width``, ``height`` and ``numComponents``.
    public let contents: Data
    
    public var size: Int {
        contents.count
    }
    
    private init(width: Int, height: Int, numComponents: Int, contents: Data) {
        self.width = width
        self.height = height
        self.numComponents = numComponents
        self.contents = contents
    }
}


@available(macOS 10.14.4, iOS 12.2, watchOS 5.2, tvOS 12.2, visionOS 1.0, *)
public extension TGAImage2 {
    static func isTGA(path: String) -> Bool {
        do {
            let data = try loadData(path: path)
            _ = try validate(span: data.span)
            return true
        }
        catch {
            return false
        }
    }
    
    static func load(path: String) throws -> TGAImage2 {
        let data = try loadData(path: path)
        return try decode(span: data.span)
    }
}


@available(macOS 10.14.4, iOS 12.2, watchOS 5.2, tvOS 12.2, visionOS 1.0, *)
private extension TGAImage2 {
    struct Header {
        let idLength: UInt8
        let colorMapType: UInt8
        let imageType: UInt8
        let colorMapFirstEntryIndex: UInt16
        let colorMapLength: UInt16
        let colorMapEntrySize: UInt8
        let xOrigin: UInt16
        let yOrigin: UInt16
        let width: UInt16
        let height: UInt16
        let pixelDepth: UInt8
        let imageDescriptor: UInt8
        
        static let byteCount = 18
    }
    
    struct Footer {
        let extensionAreaOffset: UInt32
        let developerAreaOffset: UInt32
        let signature: [UInt8]
        let point: UInt8
        let zero: UInt8
        
        static let byteCount = 26
    }
    
    struct ColorMapEntry {
        let r: UInt8
        let g: UInt8
        let b: UInt8
        let a: UInt8
    }
    
    static func loadData(path: String) throws -> Data {
        do {
            return try Data(contentsOf: URL(fileURLWithPath: path), options: [.mappedIfSafe])
        }
        catch {
            throw TGAImage2Error.fileReadFailed("Could not open file: \(error.localizedDescription)")
        }
    }
    
    static func decode(span: Span<UInt8>) throws -> TGAImage2 {
        let header = try validate(span: span)
        let width = Int(header.width)
        let height = Int(header.height)
        let imageIDLength = Int(header.idLength)
        let colorMapSize = Int(header.colorMapLength) * (Int(header.colorMapEntrySize) / 8)
        let imageDataOffset = Header.byteCount + imageIDLength + colorMapSize
        let bytesPerPixel = Int(header.pixelDepth) / 8
        let rawDataSize = width * height * bytesPerPixel
        
        guard span.count >= imageDataOffset + rawDataSize else {
            throw TGAImage2Error.invalidTGA("Could not read image data")
        }
        
        let colorMapEntries = try readColorMapEntries(from: span, header: header)
        let rawData = span.extracting(imageDataOffset..<(imageDataOffset + rawDataSize))
        let imageType = Int(header.imageType & 0x7F)
        
        switch imageType {
        case 1:
            return try decodeColorMappedImage(header: header, rawData: rawData, colorMapEntries: colorMapEntries)
        case 2:
            return try decodeTrueColorImage(header: header, rawData: rawData)
        case 3:
            return try decodeGrayscaleImage(header: header, rawData: rawData)
        default:
            throw TGAImage2Error.unsupported("Unsupported image type: \(imageType)")
        }
    }
    
    /// Reads a TGA image header and validates it.
    ///
    /// - Throws: an error if the header is invalid.
    ///
    /// - Returns: a valid TGA image header.
    static func validate(span: Span<UInt8>) throws -> Header {
        guard span.count >= Header.byteCount else {
            throw TGAImage2Error.invalidTGA("Could not read the header")
        }
        guard span.count >= Footer.byteCount else {
            throw TGAImage2Error.invalidTGA("Could not read the footer")
        }
        
        _ = try readFooter(from: span)
        let header = try readHeader(from: span)
        
        let colorMapType = Int(header.colorMapType & 0x7F)
        guard colorMapType == 0 || colorMapType == 1 else {
            throw TGAImage2Error.invalidTGA("Unknown color map type (\(colorMapType)), probably not TGA")
        }
        
        let imageType = Int(header.imageType & 0x7F)
        guard [0, 1, 2, 3, 9, 10, 11].contains(imageType) else {
            throw TGAImage2Error.invalidTGA("Unknown image type (\(imageType)), probably not TGA")
        }
        guard header.width > 0 && header.height > 0 else {
            throw TGAImage2Error.invalidTGA("Suspicious image size \(header.width)x\(header.height), probably not TGA")
        }
        guard header.width <= 16384 && header.height <= 16384 else {
            throw TGAImage2Error.invalidTGA("Suspiciously big image size \(header.width)x\(header.height), probably not TGA")
        }
        guard imageType == 1 || imageType == 2 || imageType == 3 else {
            throw TGAImage2Error.unsupported("Only uncompressed images are supported")
        }
        guard header.colorMapFirstEntryIndex == 0 else {
            throw TGAImage2Error.unsupported("Color map first entry index is not supported")
        }
        guard header.colorMapEntrySize % 8 == 0 else {
            throw TGAImage2Error.unsupported("Only color map entry size multiple of 8 bits is supported")
        }
        guard [0, 16, 24, 32].contains(header.colorMapEntrySize) else {
            throw TGAImage2Error.unsupported("Unsupported color map entry size: \(header.colorMapEntrySize)")
        }
        guard header.xOrigin == 0 && header.yOrigin == 0 else {
            throw TGAImage2Error.unsupported("Image origin is not supported")
        }
        guard header.pixelDepth % 8 == 0 else {
            throw TGAImage2Error.unsupported("Only pixel depths multiple of 8 bits are supported")
        }
        guard (header.imageDescriptor & 0b00110000) == 0 else {
            throw TGAImage2Error.unsupported("Image origin direction is not supported")
        }
        
        let alphaChannelBits = header.imageDescriptor & 0b00001111
        guard alphaChannelBits == 0 || alphaChannelBits == 8 else {
            throw TGAImage2Error.unsupported("Either no alpha channel or alpha channels with 8 bits are supported")
        }
        guard !(imageType == 1 && alphaChannelBits != 0) else {
            throw TGAImage2Error.unsupported("Alpha channel info in the image descriptor is not supported when image is color-mapped")
        }
        
        return header
    }
    
    static func readColorMapEntries(from span: Span<UInt8>, header: Header) throws -> [ColorMapEntry] {
        guard (header.colorMapType & 0x7F) == 1 else {
            return []
        }
        
        let imageIDLength = Int(header.idLength)
        let entryCount = Int(header.colorMapLength)
        let entrySize = Int(header.colorMapEntrySize)
        let offset = Header.byteCount + imageIDLength
        let colorMapSize = entryCount * (entrySize / 8)
        
        guard span.count >= offset + colorMapSize else {
            throw TGAImage2Error.invalidTGA("Could not read image data")
        }
        
        let colorMap = span.extracting(offset..<(offset + colorMapSize))
        var entries: [ColorMapEntry] = []
        entries.reserveCapacity(entryCount)
        
        switch entrySize {
        case 32:
            for i in 0..<entryCount {
                let entryOffset = i * 4
                entries.append(ColorMapEntry(
                    r: colorMap[entryOffset + 2],
                    g: colorMap[entryOffset + 1],
                    b: colorMap[entryOffset + 0],
                    a: colorMap[entryOffset + 3]
                ))
            }
        case 24:
            for i in 0..<entryCount {
                let entryOffset = i * 3
                entries.append(ColorMapEntry(
                    r: colorMap[entryOffset + 2],
                    g: colorMap[entryOffset + 1],
                    b: colorMap[entryOffset + 0],
                    a: 0
                ))
            }
        case 16:
            for i in 0..<entryCount {
                let entry = colorMap.littleEndianUInt16(at: i * 2)
                entries.append(ColorMapEntry(
                    r: UInt8((entry >> 10) & 0x1F),
                    g: UInt8((entry >> 5) & 0x1F),
                    b: UInt8(entry & 0x1F),
                    a: 0
                ))
            }
        default:
            break
        }
        
        return entries
    }
    
    static func decodeColorMappedImage(header: Header, rawData: Span<UInt8>, colorMapEntries: [ColorMapEntry]) throws -> TGAImage2 {
        guard header.pixelDepth == 8 else {
            throw TGAImage2Error.unsupported("Unsupported pixel depth for uncompressed color-mapped image: \(header.pixelDepth)")
        }
        
        let width = Int(header.width)
        let height = Int(header.height)
        let numComponents = Int(header.colorMapEntrySize) == 32 ? 4 : 3
        let rowSize = width * numComponents
        var imageData = Data(count: width * height * numComponents)
        
        for y in 0..<height {
            let sourceY = height - y - 1
            var destinationOffset = y * rowSize
            
            for x in 0..<width {
                let colorMapIndex = Int(rawData[sourceY * width + x])
                guard colorMapIndex < colorMapEntries.count else {
                    throw TGAImage2Error.invalidTGA("Color entry index (\(colorMapIndex)) out of bounds (\(colorMapEntries.count))")
                }
                
                let entry = colorMapEntries[colorMapIndex]
                imageData[destinationOffset + 0] = entry.r
                imageData[destinationOffset + 1] = entry.g
                imageData[destinationOffset + 2] = entry.b
                if numComponents == 4 {
                    imageData[destinationOffset + 3] = entry.a
                }
                destinationOffset += numComponents
            }
        }
        
        return TGAImage2(width: width, height: height, numComponents: numComponents, contents: imageData)
    }
    
    static func decodeTrueColorImage(header: Header, rawData: Span<UInt8>) throws -> TGAImage2 {
        let alphaChannelBits = header.imageDescriptor & 0b00001111
        let numComponents = alphaChannelBits == 0 ? 3 : 4
        let pixelWithoutAlphaSize = header.pixelDepth - alphaChannelBits
        let componentSize = pixelWithoutAlphaSize / 3
        
        guard pixelWithoutAlphaSize == 24 && componentSize == 8 && (alphaChannelBits == 0 || alphaChannelBits == 8) else {
            throw TGAImage2Error.unsupported("Only 8-bit channel 24-bit RGB or 32-bit RGBA textures are supported. Pixel depth: \(header.pixelDepth), alpha channel bits: \(alphaChannelBits)")
        }
        
        let width = Int(header.width)
        let height = Int(header.height)
        let rowSize = width * numComponents
        var imageData = Data(count: width * height * numComponents)
        
        #if true
        // Access imageData directly
        for y in 0 ..< height {
            let sourceY = height - y - 1
            var destinationOffset = y * rowSize
            var sourceOffset = sourceY * rowSize
            
            for _ in 0 ..< width {
                imageData[destinationOffset + 0] = rawData[sourceOffset + 2]
                imageData[destinationOffset + 1] = rawData[sourceOffset + 1]
                imageData[destinationOffset + 2] = rawData[sourceOffset + 0]
                if numComponents == 4 {
                    imageData[destinationOffset + 3] = rawData[sourceOffset + 3]
                }
                
                sourceOffset += numComponents
                destinationOffset += numComponents
            }
        }
        #elseif false
        // Access imageData through a mutable span
        var funnel = imageData.mutableSpan
        for y in 0 ..< height {
            let sourceY = height - y - 1
            var destinationOffset = y * rowSize
            var sourceOffset = sourceY * rowSize
            
            for _ in 0 ..< width {
                funnel[destinationOffset + 0] = rawData[sourceOffset + 2]
                funnel[destinationOffset + 1] = rawData[sourceOffset + 1]
                funnel[destinationOffset + 2] = rawData[sourceOffset + 0]
                if numComponents == 4 {
                    funnel[destinationOffset + 3] = rawData[sourceOffset + 3]
                }
                
                sourceOffset += numComponents
                destinationOffset += numComponents
            }
        }
        #else
        // Access imageData through an unsafe pointer
        unsafe imageData.withUnsafeMutableBytes { pointer in
            unsafe rawData.withUnsafeBytes { rawBytes in
                let _ = pointer.baseAddress
                let _ = rawBytes.baseAddress
                
                for y in 0 ..< height {
                    let sourceY = height - y - 1
                    var destinationOffset = y * rowSize
                    var sourceOffset = sourceY * rowSize
                    
                    for _ in 0 ..< width {
                        unsafe pointer[destinationOffset + 0] = rawBytes[sourceOffset + 2]
                        unsafe pointer[destinationOffset + 1] = rawBytes[sourceOffset + 1]
                        unsafe pointer[destinationOffset + 2] = rawBytes[sourceOffset + 0]
                        if numComponents == 4 {
                            unsafe pointer[destinationOffset + 3] = rawBytes[sourceOffset + 3]
                        }
                        
                        sourceOffset += numComponents
                        destinationOffset += numComponents
                    }
                }
            }
        }
        #endif
        
        return TGAImage2(width: width, height: height, numComponents: numComponents, contents: imageData)
    }
    
    static func decodeGrayscaleImage(header: Header, rawData: Span<UInt8>) throws -> TGAImage2 {
        guard header.pixelDepth == 8 else {
            throw TGAImage2Error.unsupported("Only 8-bit pixel depth is supported in one-channel images. Pixel depth: \(header.pixelDepth)")
        }
        
        let width = Int(header.width)
        let height = Int(header.height)
        let rowSize = width
        var imageData = Data(count: width * height)
        
        for y in 0..<height {
            let sourceY = height - y - 1
            let destinationOffset = y * rowSize
            let sourceOffset = sourceY * rowSize
            
            for x in 0..<rowSize {
                imageData[destinationOffset + x] = rawData[sourceOffset + x]
            }
        }
        
        return TGAImage2(width: width, height: height, numComponents: 1, contents: imageData)
    }
    
    static func readHeader(from span: Span<UInt8>) throws -> Header {
        guard span.count >= Header.byteCount else {
            throw TGAImage2Error.invalidTGA("Could not read the header")
        }
        
        return Header(
            idLength: span[0],
            colorMapType: span[1],
            imageType: span[2],
            colorMapFirstEntryIndex: span.littleEndianUInt16(at: 3),
            colorMapLength: span.littleEndianUInt16(at: 5),
            colorMapEntrySize: span[7],
            xOrigin: span.littleEndianUInt16(at: 8),
            yOrigin: span.littleEndianUInt16(at: 10),
            width: span.littleEndianUInt16(at: 12),
            height: span.littleEndianUInt16(at: 14),
            pixelDepth: span[16],
            imageDescriptor: span[17]
        )
    }
    
    static func readFooter(from span: Span<UInt8>) throws -> Footer {
        guard span.count >= Footer.byteCount else {
            throw TGAImage2Error.invalidTGA("Could not read the footer")
        }
        
        let offset = span.count - Footer.byteCount
        var signature: [UInt8] = []
        signature.reserveCapacity(16)
        for i in 0..<16 {
            signature.append(span[offset + 8 + i])
        }
        
        return Footer(
            extensionAreaOffset: span.littleEndianUInt32(at: offset),
            developerAreaOffset: span.littleEndianUInt32(at: offset + 4),
            signature: signature,
            point: span[offset + 24],
            zero: span[offset + 25]
        )
    }
}


@available(macOS 10.14.4, iOS 12.2, watchOS 5.2, tvOS 12.2, visionOS 1.0, *)
private extension Span<UInt8> {
    func littleEndianUInt16(at offset: Int) -> UInt16 {
        let low = UInt16(self[offset])
        let high = UInt16(self[offset + 1]) << 8
        return low | high
    }
    
    func littleEndianUInt32(at offset: Int) -> UInt32 {
        let byte0 = UInt32(self[offset])
        let byte1 = UInt32(self[offset + 1]) << 8
        let byte2 = UInt32(self[offset + 2]) << 16
        let byte3 = UInt32(self[offset + 3]) << 24
        return byte0 | byte1 | byte2 | byte3
    }
}
