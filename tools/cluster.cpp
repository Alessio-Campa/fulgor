using namespace fulgor;

int build_cluster(int argc, char** argv) {
    cmd_line_parser::parser parser(argc, argv);
    parser.add("index_filename", "The Fulgor index filename.", "-i", true);
    parser.add("output_filename", "The output filename where to write colors.", "-o", true);
    parser.add("left", "The minimum density of the list to be clustered [0, 1]. Default is 0", "-l",
               false);
    parser.add("right", "The maximum density of the list to be clustered [0, 1]. Default is 1",
               "-r", false);
    parser.add("num_threads", "Number of threads (default is 1).", "-t", false);
    parser.add("test", "Test clustering with predefined cluster ranges (0 <--> 25 <--> 75 <--> 100)",
               "--test", false, true);
    if (!parser.parse()) return 1;

    double left = 0, right = 1;
    uint64_t num_threads = 1;
    if (parser.parsed("left")) left = parser.get<double>("left");
    if (parser.parsed("right")) right = parser.get<double>("right");
    if (parser.parsed("num_threads")) num_threads = parser.get<uint64_t>("num_threads");
    bool is_test = parser.get<bool>("test");

    auto index_filename = parser.get<std::string>("index_filename");
    auto output_filename = parser.get<std::string>("output_filename");
    const uint64_t p = 5;


    if (sshash::util::ends_with(index_filename, constants::fulgor_filename_extension)) {
        index_type index;
        essentials::load(index, index_filename.c_str());
        std::vector<uint32_t> map(index.num_color_classes());
        uint32_t offset = 0;
        
        if (is_test) {
            std::vector<cluster> clusters;
            sketch_color_lists(index, p, num_threads, 0., 0.25);
            std::vector<cluster> clusters0 = cluster_sketches(index, map, offset);
            clusters.reserve(clusters.size() + clusters0.size());
            clusters.insert(clusters.end(), clusters0.begin(), clusters0.end());

            sketch_color_lists(index, p, num_threads, 0.25, 0.75);
            std::vector<cluster> clusters1 = cluster_sketches(index, map, offset);
            clusters.reserve(clusters.size() + clusters1.size());
            clusters.insert(clusters.end(), clusters1.begin(), clusters1.end());

            sketch_color_lists(index, p, num_threads, 0.75, 1.);
            std::vector<cluster> clusters2 = cluster_sketches(index, map, offset);
            clusters.reserve(clusters.size() + clusters2.size());
            clusters.insert(clusters.end(), clusters2.begin(), clusters2.end());

            cout << " ** Started testing\n";
            uint64_t num_ccs = index.num_color_classes();
            vector<uint32_t> errors;
            uint64_t num_edits = 0;
            uint64_t compressed_size = 0;

            for(uint64_t i = 0; i < num_ccs; ++i){
                uint32_t pos = map[i];
                uint32_t clst = 0;
                while (pos >= clusters[clst].edit_lists.size()){
                    pos -= clusters[clst].edit_lists.size();
                    clst++;
                }
                cout << '\r' << i+1 << "/" << num_ccs << " - " << (i+1)*100./num_ccs << "% " << i << "@"<< clst << ":" << pos;
                vector<uint32_t> resulting_colors = clusters[clst].colors(pos);
                auto it = index.colors(i);
                cout << "  *.*°*.* edit: " << resulting_colors.size() << ", real: " << it.size() << ' ';
                assert(resulting_colors.size() == it.size());
                if (resulting_colors.size() != it.size()) {
                    errors.push_back(i);
                    continue;
                }
                num_edits += clusters[clst].edit_lists[pos].size();
                compressed_size += clusters[clst].compressed_size(pos);
                for(uint64_t j = 0; j < it.size() && j < resulting_colors.size(); ++j, ++it){
                    if (*it != resulting_colors[j]){
                        // cerr << "Error while checking list " << i << "\n";
                        errors.push_back(i);
                        break;
                    }
                }
            }

            cout << "\n#Errors: " << errors.size() << '\n';
            for(auto i: errors){
                cout << i << " ";
            }
            cout << '\n';

            cout << " Num_edits: " << num_edits << '\n';
            cout << " Compressed_size: " << compressed_size << " bits\n";

            /*
            uint32_t pos = map[97];
            uint32_t clst = 0;
            while (pos >= clusters[clst].edit_lists.size()){
                pos -= clusters[clst].edit_lists.size();
                clst++;
            }
            for (auto i: clusters[clst].colors(pos)){
                cout << i << ' ';
            }
            cout << '\n';
            for (auto i: clusters[clst].reference){
                cout << i << ' ';
            }
            cout << '\n';
            for (auto i: clusters[clst].edit_lists[pos]){
                cout << i << ' ';
            }
             */

            return 0;
        }
        sketch_color_lists(index, p, num_threads, left, right);
        std::vector<cluster> clusters = cluster_sketches(index, map, offset);
    }
    return 0;
}