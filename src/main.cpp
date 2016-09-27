#include <cstdlib>
#include <iostream>
#include <limits>
#include <algorithm>

#include <osmium/io/any_input.hpp>

#include <osmium/util/file.hpp>
#include <osmium/util/progress_bar.hpp>

#include <osmium/visitor.hpp>

#include <osmium/osm.hpp>

#include <osmium/area/assembler.hpp>
#include <osmium/area/multipolygon_collector.hpp>

#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>

using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

int building_way_count = 0;
int building_area_count = 0;
int building_count = 0;

osmium::Box mean_location(const osmium::NodeRefList& way) {
    double min_lon = std::numeric_limits<double>::max();
    double min_lat = min_lon;
    double max_lon = std::numeric_limits<double>::lowest();
    double max_lat = max_lon;

    for (auto& node : way) {
        auto location = node.location();
        double lon = location.lon();
        double lat = location.lat();
        min_lon = std::min(min_lon, lon);
        min_lat = std::min(min_lat, lat);
        max_lon = std::max(max_lon, lon);
        max_lat = std::max(max_lat, lat);
    }

    return osmium::Box(min_lon, min_lat, max_lon, max_lat);
}

osmium::Box mean_location(const osmium::Way& way) {
    return mean_location(way.nodes());
}

osmium::Location box_center(const osmium::Box& box) {
    return osmium::Location(
        (box.bottom_left().lon() + box.top_right().lon()) / 2.0,
        (box.bottom_left().lat() + box.top_right().lat()) / 2.0
    );
}

struct BuildingWayHandler : public osmium::handler::Handler {

    void way(const osmium::Way& way) {
        if (way.tags().has_key("building")) {
            if (way.tags().has_key("roof:material")) {
                return;
            }

            building_way_count++;
            building_count++;

            osmium::Location location = box_center(mean_location(way));
            std::cout << "way," << way.id() << ',' << way.version() << ',' <<
                location.lon() << ',' << location.lat() << '\n';

            if (building_count % 100 == 0) {
                std::cout << std::flush;
            }
        }
    }
};

struct BuildingAreaHandler : public osmium::handler::Handler {

    void area(const osmium::Area& area) {
        if (!area.is_multipolygon()) {
            return;
        }

        if (area.tags().has_key("building")) {
            if (area.tags().has_key("roof:material")) {
                return;
            }

            building_area_count++;
            building_count++;

            osmium::Box box;
            for (auto& ring : area.outer_rings()) {
                box.extend(mean_location(ring));
            }
            osmium::Location location = box_center(box);
            std::cout << "relation," << area.orig_id() << ',' << area.version() << ',' <<
                location.lon() << ',' << location.lat() << '\n';

            if (building_count % 100 == 0) {
                std::cout << std::flush;
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " OSMFILE\n";
        std::exit(1);
    }

    std::cout.precision(7);
    std::cout << std::fixed;

    osmium::io::File input_file{argv[1]};

    osmium::area::Assembler::config_type assembler_config;
    osmium::area::MultipolygonCollector<osmium::area::Assembler> collector{assembler_config};

    std::cerr << "Pass 1...\n";
    osmium::io::Reader reader1{input_file, osmium::osm_entity_bits::relation};
    collector.read_relations(reader1);
    reader1.close();
    std::cerr << "Pass 1 done\n";

    index_type index;

    location_handler_type location_handler{index};
    location_handler.ignore_errors();

    BuildingWayHandler building_way_handler;
    BuildingAreaHandler building_area_handler;

    std::cout << "object_type,id,version,longitude,latitude" << std::endl;

    std::cerr << "Pass 2...\n";
    osmium::io::Reader reader2{input_file};
    osmium::apply(reader2,
        location_handler,
        building_way_handler,
        collector.handler([&building_area_handler](osmium::memory::Buffer&& buffer) {
            osmium::apply(buffer, building_area_handler);
        })
    );
    reader2.close();
    std::cerr << "Pass 2 done\n";

    std::cerr << building_count << " buildings (" <<
        building_way_count << " ways, " <<
        building_area_count << " relations)" << std::endl;
}
