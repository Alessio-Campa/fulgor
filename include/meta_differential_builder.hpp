#pragma once

#include "index.hpp"
#include "build_util.hpp"
#include "color_classes/meta.hpp"

namespace fulgor {

struct md_diff_permuter {
    md_diff_permuter(build_configuration const& build_config)
        : m_build_config(build_config), m_num_partitions(0) {}

    void permute_meta(meta<hybrid> const& m, uint64_t num_docs) {
        essentials::timer<std::chrono::high_resolution_clock, std::chrono::seconds> timer;

        {
            essentials::logger("step 2. build sketches");
            timer.start();

            constexpr uint64_t p = 10;
            build_differential_sketches_from_meta(
                m, num_docs, m.num_color_classes(), p, m_build_config.num_threads,
                m_build_config.tmp_dirname + "/sketches.bin");

            timer.stop();
            std::cout << "** building sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        {
            essentials::logger("step 3. clustering sketches");
            timer.start();

            std::ifstream in(m_build_config.tmp_dirname + "/sketches.bin", std::ios::binary);
            if (!in.is_open()) throw std::runtime_error("error in opening file");

            std::vector<kmeans::point> points;
            std::vector<uint64_t> color_ids;
            uint64_t num_bytes_per_point = 0;
            uint64_t num_points = 0;
            uint64_t num_docs = 0;
            in.read(reinterpret_cast<char*>(&num_bytes_per_point), sizeof(uint64_t));
            in.read(reinterpret_cast<char*>(&num_docs), sizeof(uint64_t));
            in.read(reinterpret_cast<char*>(&num_points), sizeof(uint64_t));
            m_num_docs = num_docs;
            points.resize(num_points, kmeans::point(num_bytes_per_point));
            color_ids.resize(num_points);
            for (uint64_t i = 0; i != num_points; ++i) {
                in.read(reinterpret_cast<char*>(&color_ids[i]), sizeof(uint64_t));
            }
            for (auto& point : points) {
                in.read(reinterpret_cast<char*>(point.data()), num_bytes_per_point);
            }
            in.close();

            std::remove((m_build_config.tmp_dirname + "/sketches.bin").c_str());

            kmeans::clustering_parameters params;
            constexpr float min_delta = 0.0001;
            constexpr float max_iteration = 10;
            constexpr uint64_t min_cluster_size = 50;
            constexpr uint64_t seed = 42;
            params.set_min_delta(min_delta);
            params.set_max_iteration(max_iteration);
            params.set_min_cluster_size(min_cluster_size);
            params.set_random_seed(seed);
            auto clustering_data = kmeans::kmeans_divisive(points.begin(), points.end(), params);

            timer.stop();
            std::cout << "** clustering sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();

            m_num_partitions = clustering_data.num_clusters;
            m_partition_size.resize(m_num_partitions + 1, 0);
            for (auto c : clustering_data.clusters) { m_partition_size[c] += 1; }

            /* prefix sum */
            {
                uint64_t val = 0;
                for (auto& size : m_partition_size) {
                    uint64_t tmp = size;
                    size = val;
                    val += tmp;
                }
            }

            const uint64_t num_color_classes = num_points;

            auto clusters_pos = m_partition_size;
            std::vector<uint32_t> permutation(num_color_classes);
            assert(clustering_data.clusters.size() == num_color_classes);
            for (uint64_t i = 0; i != num_color_classes; ++i) {
                uint64_t cluster_id = clustering_data.clusters[i];
                permutation[i] = clusters_pos[cluster_id];
                clusters_pos[cluster_id] += 1;
            }

            m_color_classes_ids.resize(num_color_classes);
            for (uint64_t i = 0; i != num_color_classes; ++i) {
                m_color_classes_ids[permutation[i]] = color_ids[i];
            }

            std::cout << "Computed " << m_num_partitions << " partitions\n";
            std::vector<uint32_t> distribution_stats(num_docs, 0);

            m_permutation.resize(num_color_classes);
            m_references.resize(m_num_partitions);
            std::vector<uint32_t> distribution(num_docs, 0);
            uint64_t cluster_size = 0;
            for (uint64_t color_id = 0, cluster_id = 0; color_id != num_color_classes + 1;
                 ++color_id, ++cluster_size) {
                if (color_id == m_partition_size[cluster_id + 1]) {
                    auto& reference = m_references[cluster_id];
                    for (uint32_t i = 0; i != num_docs; ++i) {
                        if (distribution[i] >= ceil(1. * cluster_size / 2.))
                            reference.emplace_back(i);
                    }
                    fill(distribution.begin(), distribution.end(), 0);
                    cluster_id++;
                    cluster_size = 0;
                    if (color_id == num_color_classes) break;
                }
                meta<hybrid>::forward_iterator it = m.colors(m_color_classes_ids[color_id]);
                uint64_t size = it.meta_color_list_size();
                while (size-- > 0) {
                    uint64_t val = it.partition_id();
                    distribution[val]++;
                    distribution_stats[val]++;
                    it.next_partition_id();
                }
                m_permutation[color_id] = {cluster_id, m_color_classes_ids[color_id]};
            }
            /*
            for (uint64_t i = 0; i < num_docs; i++) {
                cout << i << ": " << distribution_stats[i] << ", ";
                if ((i + 1) % 10 == 0) cout << endl;
            }
            */
            cout << "FINISHED PERMUTING;" << endl;
        }
    }

    void permute(hybrid const& h) {
        essentials::timer<std::chrono::high_resolution_clock, std::chrono::seconds> timer;

        {
            essentials::logger("step 2. build sketches");
            timer.start();

            constexpr uint64_t p = 10;
            build_differential_sketches_from_hybrid(h, h.num_docs(), h.num_color_classes(), p,
                                                    m_build_config.num_threads,
                                                    m_build_config.tmp_dirname + "/sketches.bin");

            timer.stop();
            std::cout << "** building sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        {
            essentials::logger("step 3. clustering sketches");
            timer.start();

            std::ifstream in(m_build_config.tmp_dirname + "/sketches.bin", std::ios::binary);
            if (!in.is_open()) throw std::runtime_error("error in opening file");

            std::vector<kmeans::point> points;
            std::vector<uint64_t> color_ids;
            uint64_t num_bytes_per_point = 0;
            uint64_t num_points = 0;
            uint64_t num_docs = 0;
            in.read(reinterpret_cast<char*>(&num_bytes_per_point), sizeof(uint64_t));
            in.read(reinterpret_cast<char*>(&num_docs), sizeof(uint64_t));
            in.read(reinterpret_cast<char*>(&num_points), sizeof(uint64_t));
            m_num_docs = num_docs;
            points.resize(num_points, kmeans::point(num_bytes_per_point));
            color_ids.resize(num_points);
            for (uint64_t i = 0; i != num_points; ++i) {
                in.read(reinterpret_cast<char*>(&color_ids[i]), sizeof(uint64_t));
            }
            for (auto& point : points) {
                in.read(reinterpret_cast<char*>(point.data()), num_bytes_per_point);
            }
            in.close();

            std::remove((m_build_config.tmp_dirname + "/sketches.bin").c_str());

            kmeans::clustering_parameters params;
            constexpr float min_delta = 0.0001;
            constexpr float max_iteration = 10;
            constexpr uint64_t min_cluster_size = 50;
            constexpr uint64_t seed = 42;
            params.set_min_delta(min_delta);
            params.set_max_iteration(max_iteration);
            params.set_min_cluster_size(min_cluster_size);
            params.set_random_seed(seed);
            auto clustering_data = kmeans::kmeans_divisive(points.begin(), points.end(), params);

            timer.stop();
            std::cout << "** clustering sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();

            m_num_partitions = clustering_data.num_clusters;
            m_partition_size.resize(m_num_partitions + 1, 0);
            for (auto c : clustering_data.clusters) { m_partition_size[c] += 1; }

            /* prefix sum */
            {
                uint64_t val = 0;
                for (auto& size : m_partition_size) {
                    uint64_t tmp = size;
                    size = val;
                    val += tmp;
                }
            }

            const uint64_t num_color_classes = num_points;

            auto clusters_pos = m_partition_size;
            std::vector<uint32_t> permutation(num_color_classes);
            assert(clustering_data.clusters.size() == num_color_classes);
            for (uint64_t i = 0; i != num_color_classes; ++i) {
                uint64_t cluster_id = clustering_data.clusters[i];
                permutation[i] = clusters_pos[cluster_id];
                clusters_pos[cluster_id] += 1;
            }

            m_color_classes_ids.resize(num_color_classes);
            for (uint64_t i = 0; i != num_color_classes; ++i) {
                m_color_classes_ids[permutation[i]] = color_ids[i];
            }

            std::cout << "Computed " << m_num_partitions << " partitions\n";

            m_permutation.resize(num_color_classes);
            m_references.resize(m_num_partitions);
            std::vector<uint32_t> distribution(num_docs, 0);
            uint64_t cluster_size = 0;
            for (uint64_t color_id = 0, cluster_id = 0; color_id != num_color_classes + 1;
                 ++color_id, ++cluster_size) {
                if (color_id == m_partition_size[cluster_id + 1]) {
                    auto& reference = m_references[cluster_id];
                    for (uint32_t i = 0; i != num_docs; ++i) {
                        if (distribution[i] >= ceil(1. * cluster_size / 2.))
                            reference.emplace_back(i);
                    }
                    fill(distribution.begin(), distribution.end(), 0);
                    cluster_id++;
                    cluster_size = 0;
                    if (color_id == num_color_classes) break;
                }
                auto it = h.colors(m_color_classes_ids[color_id]);
                for (uint32_t i = 0; i != it.size(); ++i, ++it) { distribution[*it]++; }
                m_permutation[color_id] = {cluster_id, m_color_classes_ids[color_id]};
            }
        }
    }

    uint64_t num_partitions() const { return m_num_partitions; }
    uint64_t num_docs() const { return m_num_docs; }
    std::vector<std::pair<uint32_t, uint32_t>> permutation() const { return m_permutation; }
    std::vector<uint32_t> color_classes_ids() const { return m_color_classes_ids; }
    std::vector<std::vector<uint32_t>> references() const { return m_references; }

private:
    build_configuration m_build_config;
    uint64_t m_num_partitions;
    uint64_t m_num_docs;
    std::vector<std::pair<uint32_t, uint32_t>> m_permutation;
    std::vector<std::vector<uint32_t>> m_references;
    std::vector<uint32_t> m_partition_size;
    std::vector<uint32_t> m_color_classes_ids;
};

struct md_permuter {
    md_permuter(build_configuration const& build_config)
        : m_build_config(build_config), m_num_partitions(0), m_max_partition_size(0) {}

    void permute(index_type const& index) {
        essentials::timer<std::chrono::high_resolution_clock, std::chrono::seconds> timer;

        {
            essentials::logger("step 2. build sketches");
            timer.start();
            constexpr uint64_t p = 10;  // use 2^p bytes per HLL sketch
            build_reference_sketches(index, p, m_build_config.num_threads,
                                     m_build_config.tmp_dirname + "/sketches.bin");
            timer.stop();
            std::cout << "** building sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        {
            essentials::logger("step 3. clustering sketches");
            timer.start();

            std::ifstream in(m_build_config.tmp_dirname + "/sketches.bin", std::ios::binary);
            if (!in.is_open()) throw std::runtime_error("error in opening file");

            std::vector<kmeans::point> points;
            uint64_t num_bytes_per_point = 0;
            uint64_t num_points = 0;
            in.read(reinterpret_cast<char*>(&num_bytes_per_point), sizeof(uint64_t));
            in.read(reinterpret_cast<char*>(&num_points), sizeof(uint64_t));
            points.resize(num_points, kmeans::point(num_bytes_per_point));
            for (auto& point : points) {
                in.read(reinterpret_cast<char*>(point.data()), num_bytes_per_point);
            }
            in.close();

            std::remove((m_build_config.tmp_dirname + "/sketches.bin").c_str());

            kmeans::clustering_parameters params;

            /* kmeans_divisive */
            constexpr float min_delta = 0.0001;
            constexpr float max_iteration = 10;
            constexpr uint64_t min_cluster_size = 50;
            constexpr uint64_t seed = 0;
            params.set_min_delta(min_delta);
            params.set_max_iteration(max_iteration);
            params.set_min_cluster_size(min_cluster_size);
            params.set_random_seed(seed);
            auto clustering_data = kmeans::kmeans_divisive(points.begin(), points.end(), params);

            timer.stop();
            std::cout << "** clustering sketches took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();

            m_num_partitions = clustering_data.num_clusters;

            m_partition_size.resize(m_num_partitions + 1, 0);
            for (auto c : clustering_data.clusters) m_partition_size[c] += 1;

            /* take prefix sums */
            uint64_t val = 0;
            for (auto& size : m_partition_size) {
                if (size > m_max_partition_size) m_max_partition_size = size;
                uint64_t tmp = size;
                size = val;
                val += tmp;
            }

            const uint64_t num_docs = index.num_docs();

            /* build permutation */
            auto counts = m_partition_size;  // copy
            m_permutation.resize(num_docs);
            assert(clustering_data.clusters.size() == num_docs);
            for (uint64_t i = 0; i != num_docs; ++i) {
                uint32_t cluster_id = clustering_data.clusters[i];
                m_permutation[i] = counts[cluster_id];
                counts[cluster_id] += 1;
            }

            /* permute filenames */
            m_filenames.resize(num_docs);
            for (uint64_t i = 0; i != num_docs; ++i) {
                m_filenames[m_permutation[i]] = index.filename(i);
            }
        }
    }

    partition_endpoint partition_endpoints(uint64_t partition_id) const {
        assert(partition_id + 1 < m_partition_size.size());
        return {m_partition_size[partition_id], m_partition_size[partition_id + 1]};
    }

    uint64_t num_partitions() const { return m_num_partitions; }
    uint64_t max_partition_size() const { return m_max_partition_size; }
    std::vector<uint32_t> permutation() const { return m_permutation; }
    std::vector<uint32_t> partition_size() const { return m_partition_size; }
    std::vector<std::string> filenames() const { return m_filenames; }

private:
    build_configuration m_build_config;
    uint64_t m_num_partitions;
    uint64_t m_max_partition_size;
    std::vector<uint32_t> m_permutation;
    std::vector<uint32_t> m_partition_size;
    std::vector<std::string> m_filenames;
};

template <typename ColorClasses>
struct index<ColorClasses>::meta_differential_builder {
    meta_differential_builder() {}

    meta_differential_builder(build_configuration const& build_config)
        : m_build_config(build_config) {}

    void build(index& idx) {
        if (idx.m_k2u.size() != 0) throw std::runtime_error("index already built");

        index_type index;
        essentials::logger("step 1. loading index to be mega-partitioned");
        essentials::load(index, m_build_config.index_filename_to_partition.c_str());
        essentials::logger("DONE");

        const uint64_t num_docs = index.num_docs();
        const uint64_t num_color_classes = index.num_color_classes();

        essentials::timer<std::chrono::high_resolution_clock, std::chrono::seconds> timer;

        md_permuter p(m_build_config);
        p.permute(index);
        auto const& permutation = p.permutation();

        const uint64_t num_partitions = p.num_partitions();
        const uint64_t max_partition_size = p.max_partition_size();
        std::cout << "num_partitions = " << num_partitions << std::endl;
        std::cout << "max_partition_size = " << max_partition_size << std::endl;

        {
            essentials::logger("step 4. building partial/meta colors");
            timer.start();

            std::ofstream metacolors_out(m_build_config.tmp_dirname + "/metacolors.bin",
                                         std::ios::binary);
            if (!metacolors_out.is_open()) throw std::runtime_error("error in opening file");

            uint64_t num_integers_in_metacolors = 0;
            uint64_t num_partial_colors = 0;

            std::vector<uint32_t> partial_color;
            std::vector<uint32_t> permuted_list;
            partial_color.reserve(max_partition_size);
            permuted_list.reserve(num_docs);

            meta<hybrid>::builder colors_builder;

            colors_builder.init_colors_builder(num_docs, num_partitions);
            for (uint64_t partition_id = 0; partition_id != num_partitions; ++partition_id) {
                auto endpoints = p.partition_endpoints(partition_id);
                uint64_t num_docs_in_partition = endpoints.end - endpoints.begin;
                colors_builder.init_color_partition(partition_id, num_docs_in_partition);
            }

            uint64_t partition_id = 0;
            uint32_t meta_color_list_size = 0;

            std::vector<std::unordered_map<__uint128_t,            // key
                                           uint32_t,               // value
                                           util::hasher_uint128_t  // key's hasher
                                           >>
                hashes;  // (hash, id)
            hashes.resize(num_partitions);

            auto hash_and_compress = [&]() {
                assert(!partial_color.empty());
                auto hash = util::hash128(reinterpret_cast<char const*>(partial_color.data()),
                                          partial_color.size() * sizeof(uint32_t));
                uint32_t partial_color_id = 0;
                auto it = hashes[partition_id].find(hash);
                if (it == hashes[partition_id].cend()) {  // new partial color
                    partial_color_id = hashes[partition_id].size();
                    hashes[partition_id].insert({hash, partial_color_id});
                    colors_builder.process_colors(partition_id, partial_color.data(),
                                                  partial_color.size());
                } else {
                    partial_color_id = (*it).second;
                }

                /*  write meta color: (partition_id, partial_color_id)
                    Note: at this stage, partial_color_id is relative
                          to its partition (is not global yet).
                */
                metacolors_out.write(reinterpret_cast<char const*>(&partition_id),
                                     sizeof(uint32_t));
                metacolors_out.write(reinterpret_cast<char const*>(&partial_color_id),
                                     sizeof(uint32_t));

                partial_color.clear();
                meta_color_list_size += 1;
            };

            for (uint64_t color_class_id = 0; color_class_id != num_color_classes;
                 ++color_class_id) {
                /* permute list */
                permuted_list.clear();
                auto it = index.colors(color_class_id);
                uint64_t list_size = it.size();
                for (uint64_t i = 0; i != list_size; ++i, ++it) {
                    uint32_t ref_id = *it;
                    permuted_list.push_back(permutation[ref_id]);
                }
                std::sort(permuted_list.begin(), permuted_list.end());

                /* partition list */
                meta_color_list_size = 0;
                partition_id = 0;
                partition_endpoint curr_partition = p.partition_endpoints(0);
                assert(partial_color.empty());

                /* reserve space to hold the size of the meta color list */
                metacolors_out.write(reinterpret_cast<char const*>(&meta_color_list_size),
                                     sizeof(uint32_t));

                for (uint64_t i = 0; i != list_size; ++i) {
                    uint32_t ref_id = permuted_list[i];
                    while (ref_id >= curr_partition.end) {
                        if (!partial_color.empty()) hash_and_compress();
                        partition_id += 1;
                        curr_partition = p.partition_endpoints(partition_id);
                    }
                    assert(ref_id >= curr_partition.begin);
                    partial_color.push_back(ref_id - curr_partition.begin);
                }
                if (!partial_color.empty()) hash_and_compress();

                num_integers_in_metacolors += meta_color_list_size;

                /* write size of meta color list */
                uint64_t current_pos = metacolors_out.tellp();
                uint64_t num_bytes_in_meta_color_list =
                    2 * meta_color_list_size * sizeof(uint32_t) + sizeof(uint32_t);
                assert(current_pos >= num_bytes_in_meta_color_list);
                uint64_t pos = current_pos - num_bytes_in_meta_color_list;
                metacolors_out.seekp(pos);
                metacolors_out.write(reinterpret_cast<char const*>(&meta_color_list_size),
                                     sizeof(uint32_t));
                metacolors_out.seekp(current_pos);
            }

            metacolors_out.close();

            std::vector<uint64_t> num_partial_colors_before;
            std::vector<uint32_t> num_lists_in_partition;
            num_partial_colors_before.reserve(num_partitions);
            num_lists_in_partition.reserve(num_partitions);
            num_partial_colors = 0;
            for (partition_id = 0; partition_id != num_partitions; ++partition_id) {
                num_partial_colors_before.push_back(num_partial_colors);
                uint64_t num_partial_colors_in_partition = hashes[partition_id].size();
                num_partial_colors += num_partial_colors_in_partition;
                num_lists_in_partition.push_back(num_partial_colors_in_partition);
                std::cout << "num_partial_colors_in_partition-" << partition_id << ": "
                          << num_partial_colors_in_partition << std::endl;
            }

            std::cout << "total num. partial colors = " << num_partial_colors << std::endl;

            colors_builder.init_meta_colors_builder(num_integers_in_metacolors + num_color_classes,
                                                    num_partial_colors, p.partition_size(),
                                                    num_lists_in_partition);

            std::vector<uint32_t> metacolors;
            metacolors.reserve(num_partitions);  // at most

            std::ifstream metacolors_in(m_build_config.tmp_dirname + "/metacolors.bin",
                                        std::ios::binary);
            if (!metacolors_in.is_open()) throw std::runtime_error("error in opening file");

            for (uint64_t color_class_id = 0; color_class_id != num_color_classes;
                 ++color_class_id) {
                assert(metacolors.empty());
                uint32_t meta_color_list_size = 0;
                metacolors_in.read(reinterpret_cast<char*>(&meta_color_list_size),
                                   sizeof(uint32_t));
                for (uint32_t i = 0; i != meta_color_list_size; ++i) {
                    uint32_t partition_id = 0;
                    uint32_t partial_color_id = 0;
                    metacolors_in.read(reinterpret_cast<char*>(&partition_id), sizeof(uint32_t));
                    metacolors_in.read(reinterpret_cast<char*>(&partial_color_id),
                                       sizeof(uint32_t));
                    /* transform the partial_color_id into a global id */
                    metacolors.push_back(partial_color_id +
                                         num_partial_colors_before[partition_id]);
                }
                colors_builder.process_metacolors(metacolors.data(), metacolors.size());
                metacolors.clear();
            }

            metacolors_in.close();
            std::remove((m_build_config.tmp_dirname + "/metacolors.bin").c_str());
            meta<hybrid> temp_meta;
            colors_builder.build(temp_meta);

            std::vector<hybrid> pc = temp_meta.partial_colors();
            assert(pc.size() == num_partitions);

            for (uint64_t i = 0; i < num_partitions; i++) {
                md_diff_permuter dp(m_build_config);
                dp.permute(pc[i]);

                differential::builder diff_builder;
                diff_builder.init_colors_builder(dp.num_docs());

                auto const& permutation = dp.permutation();
                auto const& references = dp.references();

                for (auto& reference : references) { diff_builder.encode_reference(reference); }
                for (auto& [cluster_id, color_id] : permutation) {
                    diff_builder.encode_list(cluster_id, references[cluster_id],
                                             pc[i].colors(color_id));
                }
                differential d;
                diff_builder.build(d);
                idx.m_ccs.m_diff_colors.push_back(d);
                d.print_stats();
            }

            {
                essentials::logger("step infty. build differential-meta colors");
                md_diff_permuter dp(m_build_config);
                dp.permute_meta(temp_meta, num_partial_colors);

                differential::builder diff_builder;
                diff_builder.init_colors_builder(dp.num_docs());

                auto const& permutation = dp.permutation();
                auto const& references = dp.references();


                for (auto& reference : references) { diff_builder.encode_reference(reference); }
                for (auto& [cluster_id, color_id] : permutation) {
                    auto meta_color = temp_meta.colors(color_id);
                    diff_builder.encode_list(cluster_id, references[cluster_id], meta_color);
                }
                differential d;
                diff_builder.build(d);
                idx.m_ccs.m_diff_partitions = d;
                d.print_stats();

                pthash::compact_vector::builder partial_colors_ids_builder(
                    num_integers_in_metacolors + num_color_classes,
                    std::ceil(std::log2(temp_meta.num_max_lists_in_partition()))
                );
                for (auto& [cluster_id, color_id] : permutation) {
                    auto it = temp_meta.colors(color_id);
                    partial_colors_ids_builder.push_back(it.meta_color_list_size());
                    for(uint64_t i = 0; i < it.meta_color_list_size(); i++, it.next_partition_id()){
                        it.update_partition();
                        partial_colors_ids_builder.push_back(it.meta_color() - it.num_lists_before());
                    }
                }
                pthash::compact_vector partial_colors_ids;
                partial_colors_ids_builder.build(partial_colors_ids);
                cout << "  PARTIAL COLORS IDS SIZE: " << partial_colors_ids.bytes() << endl; 



/*
                essentials::logger("step 7. check correctness...");

                for (uint64_t color_id = 0; color_id < num_color_classes; color_id++) {
                    uint64_t meta_list_start =
                        idx.m_ccs.m_meta_colors_offsets.access(permutation[color_id].second);
                    pthash::compact_vector::iterator exp_it =
                        idx.m_ccs.m_meta_colors.at(meta_list_start);
                    uint64_t exp_it_size = *exp_it;
                    auto res_it = d.colors(color_id);
                    if (res_it.size() != exp_it_size) {
                        cout << "Error while checking color " << color_id
                             << ", different sizes: expected " << exp_it_size << " but got "
                             << res_it.size() << ")\n";
                        continue;
                    }
                    for (uint64_t j = 0; j < exp_it_size; ++j, ++res_it) {
                        auto exp = *exp_it;
                        auto got = *res_it;
                        if (exp != got) {
                            cout << "Error while checking color " << color_id
                                 << ", mismatch at position " << j << ": expected " << exp
                                 << " but got " << got << std::endl;
                        }
                    }
                }
                
                cout << " META-COLORS DONE.\n";
*/
            }

            timer.stop();
            std::cout << "** building partial/meta colors took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        {
            essentials::logger("step 5. copy u2c and k2u");
            timer.start();
            idx.m_u2c = index.get_u2c();
            idx.m_k2u = index.get_k2u();
            timer.stop();
            std::cout << "** copying u2c and k2u took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        {
            essentials::logger("step 6. building filenames");
            timer.start();
            idx.m_filenames.build(p.filenames());
            timer.stop();
            std::cout << "** building filenames took " << timer.elapsed() << " seconds / "
                      << timer.elapsed() / 60 << " minutes" << std::endl;
            timer.reset();
        }

        if (m_build_config.check) {
            essentials::logger("step 7. check correctness...");

            std::vector<uint32_t> permuted_list;
            permuted_list.reserve(num_docs);

            for (uint64_t color_class_id = 0; color_class_id != num_color_classes;
                 ++color_class_id) {
                auto it_exp = index.colors(color_class_id);
                auto it_got = idx.colors(color_class_id);
                const uint64_t exp_size = it_exp.size();
                const uint64_t got_size = it_got.size();

                if (exp_size != got_size) {
                    std::cout << "got colors list of size " << got_size << " but expected "
                              << exp_size << std::endl;
                    return;
                }

                permuted_list.clear();
                for (uint64_t i = 0; i != exp_size; ++i, ++it_exp) {
                    uint32_t ref_id = *it_exp;
                    permuted_list.push_back(permutation[ref_id]);
                }
                std::sort(permuted_list.begin(), permuted_list.end());

                for (uint64_t i = 0; i != got_size; ++i, ++it_got) {
                    if (permuted_list[i] != *it_got) {
                        std::cout << "got ref " << *it_got << " BUT expected " << permuted_list[i]
                                  << std::endl;
                        return;
                    }
                }
            }
            essentials::logger("DONE!");
        }
    }

private:
    build_configuration m_build_config;
};

}  // namespace fulgor
