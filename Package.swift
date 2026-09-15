// swift-tools-version: 6.4
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "FastTGA",
    platforms: [
        .macOS(.v12),
        .iOS(.v15),
        .tvOS(.v15),
        .watchOS(.v9),
        .visionOS(.v1),
    ],
    products: [
        .library(
            name: "FastTGAC",
            targets: ["FastTGAC"]
        ),
        .library(
            name: "FastTGA",
            targets: ["FastTGA"]
        ),
    ],
    targets: [
        .target(
            name: "FastTGAC",
            cxxSettings: [
                .enableWarning("all")
            ]
        ),
        .target(
            name: "FastTGA",
            dependencies: [
                .target(name: "FastTGAC")
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx),
                .strictMemorySafety()
            ]
        ),
    ],
    cLanguageStandard: .c17,
    cxxLanguageStandard: .cxx20
)
