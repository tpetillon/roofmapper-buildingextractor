# RoofMapper building extraction tool

This tool is used to extract data from OpenStreetMap database dumps, for example [Geofrabrik extracts](http://download.geofabrik.de/).

#### Dependencies
* CMake
* [libosmium](https://github.com/osmcode/libosmium)
* Expat
* libbz2
* zlib
* Boost filesystem

#### Compilation
* `cd <build dir>`
* `cmake <path to repo clone>`
* `make`

#### Usage
`buildingextractor <osm extract file> <bin count> <min building size> <output dir>`

`bin count` tell the program to split the input data in multiple bins. It is useful to be able to handle big data sets.

All building whose area is below `min building size` will be rejected. The unit is roughly square metres. (It is an approximation though.)
