#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cmath>

#include <osmium/io/any_input.hpp>

#include <osmium/util/file.hpp>
#include <osmium/util/progress_bar.hpp>

#include <osmium/visitor.hpp>

#include <osmium/osm.hpp>

#include <osmium/handler.hpp>

int building_way_count = 0;
int building_area_count = 0;
int building_count = 0;
int skipped_building_count = 0;

struct BuildingNode {
    osmium::object_id_type id;
    osmium::Location location;
};

struct BuildingRelation {
    osmium::object_id_type id;
    osmium::object_version_type version;
    osmium::object_id_type first_way_id;
};

struct BuildingWay {
    osmium::item_type type;
    osmium::object_id_type id;
    osmium::object_version_type version;
    osmium::object_id_type first_node_id;
    std::vector<osmium::object_id_type> nodes;
};

struct BuildingRelationFilter : public osmium::handler::Handler {
    BuildingRelationFilter(std::unordered_map<osmium::object_id_type, BuildingRelation>& buildingRelations) :
        m_buildingRelations(buildingRelations) {
    }

    void relation(const osmium::Relation& relation) {
        if (relation_is_multipolygon(relation) &&
            relation.tags().has_key("building") &&
            !relation.tags().has_key("roof:material") &&
            relation.members().size() > 0)
        {
            for (auto& member : relation.members()) {
                if (member.type() == osmium::item_type::way && strcmp(member.role(), "outer") == 0) {
                    BuildingRelation buildingRelation;
                    buildingRelation.id = relation.id();
                    buildingRelation.version = relation.version();
                    buildingRelation.first_way_id = member.ref();
                    m_buildingRelations[buildingRelation.first_way_id] = buildingRelation;
                    break; // only keep the first outer way
                }
            }
        }
    }

    bool relation_is_multipolygon(const osmium::Relation& relation) const {
        const char* type = relation.tags().get_value_by_key("type");

        // ignore relations without "type" tag
        if (!type) {
            return false;
        }

        if (!std::strcmp(type, "multipolygon")) {
            return true;
        }

        return false;
    }

private:
    std::unordered_map<osmium::object_id_type, BuildingRelation>& m_buildingRelations;
};

struct BuildingWayFilter : public osmium::handler::Handler {
    BuildingWayFilter(int bin, int bin_count,
        std::unordered_map<osmium::object_id_type, BuildingWay>& buildingWays,
        const std::unordered_map<osmium::object_id_type, BuildingRelation>& buildingRelations,
        std::unordered_map<osmium::object_id_type, osmium::object_id_type>& buildingWayNodeIds) :
        m_bin(bin), m_bin_count(bin_count), m_buildingWays(buildingWays),
        m_buildingRelations(buildingRelations), m_buildingWayNodeIds(buildingWayNodeIds) {
    }

    void way(const osmium::Way& way) {
        if (way.id() % m_bin_count == m_bin && way.nodes().size() > 0) {
            if (way.tags().has_key("building") &&
                !way.tags().has_key("roof:material")) {
                BuildingWay buildingWay;
                buildingWay.type = osmium::item_type::way;
                buildingWay.id = way.id();
                buildingWay.version = way.version();
                buildingWay.first_node_id = way.nodes()[0].ref();

                add_nodes(way.nodes(), buildingWay);

                m_buildingWays[buildingWay.first_node_id] = buildingWay;
            } else {
                auto it = m_buildingRelations.find(way.id());
                if (it != m_buildingRelations.end()) {
                    BuildingRelation buildingRelation = it->second;
                    BuildingWay buildingWay;
                    buildingWay.type = osmium::item_type::relation;
                    buildingWay.id = buildingRelation.id;
                    buildingWay.version = buildingRelation.version;
                    buildingWay.first_node_id = way.nodes()[0].ref();

                    add_nodes(way.nodes(), buildingWay);

                    m_buildingWays[buildingWay.first_node_id] = buildingWay;
                }
            }
        }
    }

private:
    int m_bin;
    int m_bin_count;
    std::unordered_map<osmium::object_id_type, BuildingWay>& m_buildingWays;
    const std::unordered_map<osmium::object_id_type, BuildingRelation>& m_buildingRelations;
    std::unordered_map<osmium::object_id_type, osmium::object_id_type>& m_buildingWayNodeIds;

    void add_nodes(const osmium::WayNodeList& nodeList, BuildingWay& buildingWay) {
        buildingWay.nodes.reserve(nodeList.size());

        for (auto node : nodeList) {
            buildingWay.nodes.push_back(node.ref());
            m_buildingWayNodeIds[node.ref()] = buildingWay.id;
        }
    }
};

struct NodeLocationFinder : public osmium::handler::Handler {
    NodeLocationFinder(
        const std::unordered_map<osmium::object_id_type, osmium::object_id_type>& buildingWayNodeIds,
        std::unordered_map<osmium::object_id_type, osmium::Location>& nodeLocations) :
        m_buildingWayNodeIds(buildingWayNodeIds), m_nodeLocations(nodeLocations) {
    }

    void node(const osmium::Node& node) {
        auto it = m_buildingWayNodeIds.find(node.id());
        if (it != m_buildingWayNodeIds.end()) {
            m_nodeLocations[node.id()] = node.location();
        }
    }

private:
    const std::unordered_map<osmium::object_id_type, osmium::object_id_type>& m_buildingWayNodeIds;
    std::unordered_map<osmium::object_id_type, osmium::Location>& m_nodeLocations;
};

struct BuildingPositionFinder : public osmium::handler::Handler {
    BuildingPositionFinder(
        std::unordered_map<osmium::object_id_type, BuildingWay>& buildingWays,
        std::unordered_map<osmium::object_id_type, double>& buildingWayAreas,
        double minBuildingArea, std::ostream& output_stream) :
        m_buildingWays(buildingWays), m_buildingWayAreas(buildingWayAreas),
        m_minBuildingArea(minBuildingArea), m_output_stream(output_stream) {
    }

    void node(const osmium::Node& node) {
        auto it = m_buildingWays.find(node.id());
        if (it != m_buildingWays.end()) {
            BuildingWay buildingWay = it->second;

            double wayArea = m_buildingWayAreas.at(buildingWay.id);
            if (wayArea < m_minBuildingArea) {
                // skip small buildings
                skipped_building_count++;
                return;
            }

            if (buildingWay.type == osmium::item_type::relation) {
                building_area_count++;
            } else {
                building_way_count++;
            }
            building_count++;

            m_output_stream <<
                (buildingWay.type == osmium::item_type::relation ? "relation," : "way,") <<
                buildingWay.id << ',' << buildingWay.version << ',' <<
                node.location().lon() << ',' << node.location().lat() << '\n';

            if (building_count % 1000 == 0) {
                m_output_stream << std::flush;
            }
        }
    }

private:
    std::unordered_map<osmium::object_id_type, BuildingWay>& m_buildingWays;
    std::unordered_map<osmium::object_id_type, double>& m_buildingWayAreas;
    double m_minBuildingArea;
    std::ostream& m_output_stream;
};

struct SinusoidalCoords {
    double x;
    double y;
};

SinusoidalCoords location_to_sinusoidal(osmium::Location location) {
    const double PI = 3.141592653589793238463;
    double earthRadius = 6371009.0; // metres
    double oneDegreeLength = (PI * earthRadius) / 180.0;

    SinusoidalCoords coords;
    coords.y = location.lat() * oneDegreeLength;
    coords.x = location.lon() * oneDegreeLength * std::cos((location.lat() * PI) / 180.0);

    return coords;
}

double compute_polygon_area(std::vector<SinusoidalCoords> coordList) {
    double area = 0.0;

    for (int i = 0;  i < coordList.size(); i++) {
        auto& coords = coordList[i];
        auto& nextCoords = coordList[(i + 1) % coordList.size()];
        area += (coords.x + nextCoords.x) * (coords.y - nextCoords.y);
    }

    return std::abs(area) * 0.5;
}

void compute_way_areas(
    const std::unordered_map<osmium::object_id_type, BuildingWay>& buildingWays,
    const std::unordered_map<osmium::object_id_type, osmium::Location>& nodeLocations,
    std::unordered_map<osmium::object_id_type, double>& buildingWayAreas) {
    for (auto& buildingIdAndWay : buildingWays) {
        auto& way = buildingIdAndWay.second;

        std::vector<SinusoidalCoords> coordList;

        for (auto& nodeId : way.nodes) {
            osmium::Location location = nodeLocations.at(nodeId);
            coordList.push_back(location_to_sinusoidal(location));
        }

        buildingWayAreas[way.id] = compute_polygon_area(coordList);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " osm_file bin_count min_building_size output_dir\n";
        std::exit(1);
    }

    std::cout.precision(7);
    std::cout << std::fixed;

    osmium::io::File input_file{argv[1]};
    int bin_count = std::stoi(argv[2]);
    double min_building_area = std::stod(argv[3]);
    std::string output_dir = std::string(argv[4]);

    std::unordered_map<osmium::object_id_type, BuildingRelation> buildingRelations;

    BuildingRelationFilter buildingRelationFilter{buildingRelations};

    std::cerr << "Pass 0...\n";
    osmium::io::Reader reader0{input_file};
    osmium::apply(reader0, buildingRelationFilter);
    reader0.close();
    std::cerr << "Pass 0 done\n";
    std::cerr << buildingRelations.size() << " relations selected\n";

    for (int bin = 0; bin < bin_count; bin++) {
        std::cout << "Bin " << (bin + 1) << '/' << bin_count << '\n';

        std::string file_path = output_dir;
        file_path += '/' + std::to_string(bin) + ".csv";

        std::ofstream output_file(file_path, std::ofstream::out | std::ofstream::trunc);

        std::unordered_map<osmium::object_id_type, BuildingWay> buildingWays;
        std::unordered_map<osmium::object_id_type, osmium::object_id_type> buildingWayNodeIds;

        BuildingWayFilter buildingWayFilter{bin, bin_count, buildingWays, buildingRelations, buildingWayNodeIds};

        std::cerr << "\tPass 1...\n";
        osmium::io::Reader reader1{input_file};
        osmium::apply(reader1, buildingWayFilter);
        reader1.close();
        std::cerr << "\tPass 1 done\n";

        std::cerr << '\t' << buildingWays.size() << " building ways selected in bin " << bin << '\n';

        std::unordered_map<osmium::object_id_type, osmium::Location> nodeLocations;

        NodeLocationFinder nodeLocationFinder{buildingWayNodeIds, nodeLocations};

        std::cerr << "\tPass 2...\n";
        osmium::io::Reader reader2{input_file};
        osmium::apply(reader2, nodeLocationFinder);
        reader2.close();
        std::cerr << "\tPass 2 done\n";

        std::unordered_map<osmium::object_id_type, double> buildingWayAreas;

        compute_way_areas(buildingWays, nodeLocations, buildingWayAreas);

        output_file << "object_type,id,version,longitude,latitude\n";

        BuildingPositionFinder buildingPositionFinder{buildingWays, buildingWayAreas, min_building_area, output_file};

        std::cerr << "\tPass 3...\n";
        osmium::io::Reader reader3{input_file};
        osmium::apply(reader3, buildingPositionFinder);
        reader3.close();
        std::cerr << "\tPass 3 done\n";

        output_file.close();
    }

    std::cerr << building_count << " buildings (" <<
        building_way_count << " ways, " <<
        building_area_count << " relations)" << std::endl;
    std::cerr << "skipped " << skipped_building_count << " buildings (" <<
        int(100 * (skipped_building_count + building_count) / float(skipped_building_count)) << "%" << std::endl;
}
