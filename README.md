# FastTGA

TGA image loader. [TGA File Format specification](http://tfc.duke.free.fr/coding/tga_specs.pdf) is used. Public API is available in C++ and Swift.

An uncompressed `RGB`(+ unused `A`) 4096x4096 image takes 0.047s to load in Debug and 0.023s in Release mode.

Supported types:
- Uncompressed, Color-mapped Image (24 and 32-bit without alpha)
- Uncompressed, True-color Image (15, 16, 24 and 32-bit with alpha if specified)
- Uncompressed, Black-and-white Image (8-bit)

To be supported in the future releases:
- Alpha channel for all uncompressed types
- Premultiplied alpha
- Run-length encoded, Color-mapped Image
- Run-length encoded, True-color Image
- Run-length encoded, Black-and-white Image
- TGA image serialization


## Installing FastTGA

Add the following dependency to your Package.swift:

```Swift
.package(url: "https://github.com/EvgenijLutz/FastTGA.git", from: "1.2.0")
```

Access the API in `C++`:
```cpp
#include <FastTGAC/FastTGAC.hpp>
```

And `Swift`:
```Swift
import FastTGA
```

And you're good to go!


## Swift implementation

As an experiment in porting the codebase entirely to `Swift`, there is also an experimental class called `TGAImage2` that mirrors the logic of `TGAImage`. At the moment, its implementation is at least twice as slow, so you will probably still want to use `TGAImage` (the `C++` implementation) instead.
