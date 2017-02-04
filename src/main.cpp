#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <algorithm>
#include <map>

#include <osmium/io/any_input.hpp>

#include <osmium/util/file.hpp>
#include <osmium/util/progress_bar.hpp>

#include <osmium/visitor.hpp>

#include <osmium/osm.hpp>

#include <osmium/handler.hpp>

int building_way_count = 0;
int building_area_count = 0;
int building_count = 0;

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
};

struct BuildingRelationFilter : public osmium::handler::Handler {
    BuildingRelationFilter(std::map<osmium::object_id_type, BuildingRelation>& buildingRelations) :
        m_buildingRelations(buildingRelations) {
    }

    void relation(const osmium::Relation& relation) {
        if (relation_is_multipolygon(relation) &&
            relation.tags().has_key("building") &&
            !relation.tags().has_key("roof:material") &&
            relation.members().size() > 0)
        {
            for (auto& member : relation.members()) {
                if (member.type() == osmium::item_type::way) {
                    BuildingRelation buildingRelation;
                    buildingRelation.id = relation.id();
                    buildingRelation.version = relation.version();
                    buildingRelation.first_way_id = member.ref();
                    m_buildingRelations[buildingRelation.first_way_id] = buildingRelation;
                    break; // only keep the first way
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
    std::map<osmium::object_id_type, BuildingRelation>& m_buildingRelations;
};

struct BuildingWayFilter : public osmium::handler::Handler {
    BuildingWayFilter(int bin, int bin_count,
        std::map<osmium::object_id_type, BuildingWay>& buildingWays,
        const std::map<osmium::object_id_type, BuildingRelation>& buildingRelations) :
        m_bin(bin), m_bin_count(bin_count), m_buildingWays(buildingWays), m_buildingRelations(buildingRelations) {
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

                    m_buildingWays[buildingWay.first_node_id] = buildingWay;
                }
            }
        }
    }

private:
    int m_bin;
    int m_bin_count;
    std::map<osmium::object_id_type, BuildingWay>& m_buildingWays;
    const std::map<osmium::object_id_type, BuildingRelation>& m_buildingRelations;
};

struct BuildingPositionFinder : public osmium::handler::Handler {
    BuildingPositionFinder(
        std::map<osmium::object_id_type, BuildingWay>& buildingWays,
        std::ostream& output_stream) :
        m_buildingWays(buildingWays), m_output_stream(output_stream) {
    }

    void node(const osmium::Node& node) {
        auto it = m_buildingWays.find(node.id());
        if (it != m_buildingWays.end()) {
            BuildingWay buildingWay = it->second;

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
    std::map<osmium::object_id_type, BuildingWay>& m_buildingWays;
    std::ostream& m_output_stream;
};

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " osm_file bin_count output_dir\n";
        std::exit(1);
    }

    std::cout.precision(7);
    std::cout << std::fixed;

    osmium::io::File input_file{argv[1]};
    int bin_count = std::stoi(argv[2]);
    std::string output_dir = std::string(argv[3]);

    std::map<osmium::object_id_type, BuildingRelation> buildingRelations;

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

        std::map<osmium::object_id_type, BuildingWay> buildingWays;

        BuildingWayFilter buildingWayFilter{bin, bin_count, buildingWays, buildingRelations};

        std::cerr << "\tPass 1...\n";
        osmium::io::Reader reader1{input_file};
        osmium::apply(reader1, buildingWayFilter);
        reader1.close();
        std::cerr << "\tPass 1 done\n";

        std::cerr << '\t' << buildingWays.size() << " building ways selected in bin " << bin << '\n';

        output_file << "object_type,id,version,longitude,latitude\n";

        BuildingPositionFinder buildingPositionFinder{buildingWays, output_file};

        std::cerr << "\tPass 2...\n";
        osmium::io::Reader reader2{input_file};
        osmium::apply(reader2, buildingPositionFinder);
        reader2.close();
        std::cerr << "\tPass 2 done\n";

        output_file.close();
    }

    std::cerr << building_count << " buildings (" <<
        building_way_count << " ways, " <<
        building_area_count << " relations)" << std::endl;
}
