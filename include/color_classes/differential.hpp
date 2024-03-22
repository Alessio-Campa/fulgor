#pragma once

namespace fulgor {

template <typename ColorClasses>
struct differential {
    static const bool meta_colored = false;
    static const bool differential_colored = true;

    struct builder {
        builder() : m_prev_cluster_id(0) {
            // TODO: reserve bvb space
            m_list_offsets.push_back(0);
            m_reference_offsets.push_back(0);
        }

        void init_colors_builder(uint64_t num_docs){
            m_num_docs = num_docs;
        }

        void encode_reference(std::vector<uint32_t> const& reference) {
            uint64_t size = reference.size();
            util::write_delta(m_bvb, size);

            if (size == 0){
                m_reference_offsets.push_back(m_bvb.num_bits());
                m_list_offsets[0] = m_bvb.num_bits();
                return;
            }

            uint32_t prev_val = reference[0];
            util::write_delta(m_bvb, prev_val);

            for (uint64_t i = 1; i < size; ++i) {
                uint32_t val = reference[i];
                assert(val >= prev_val + 1);
                util::write_delta(m_bvb, val - (prev_val + 1));
                prev_val = val;
            }
            m_reference_offsets.push_back(m_bvb.num_bits());
            m_list_offsets[0] = m_bvb.num_bits();
        }

        void encode_list(uint64_t cluster_id, std::vector<uint32_t> const& reference,
                         typename ColorClasses::forward_iterator it) {
            std::vector<uint32_t> edit_list;
            uint64_t ref_size = reference.size();
            uint64_t it_size = it.size();
            edit_list.reserve(ref_size + it_size);

            if (cluster_id != m_prev_cluster_id) {
                m_prev_cluster_id = cluster_id;
                m_clusters.set(m_clusters.size() - 1);
            }
            m_clusters.push_back(false);

            uint64_t i = 0, j = 0;
            while (i < it_size && j < ref_size) {
                if (*it == reference[j]) {
                    i += 1;
                    ++it;
                    j += 1;
                } else if (*it < reference[j]) {
                    edit_list.push_back(*it);
                    i += 1;
                    ++it;
                } else {
                    edit_list.push_back(reference[j]);
                    j += 1;
                }
            }
            while (i < it_size) {
                edit_list.push_back(*it);
                i += 1;
                ++it;
            }
            while (j < ref_size) {
                edit_list.push_back(reference[j]);
                j += 1;
            }

            uint64_t size = edit_list.size();
            util::write_delta(m_bvb, size);

            if (size == 0) {
                m_list_offsets.push_back(m_bvb.num_bits());
                return;
            }

            uint32_t prev_val = edit_list[0];
            util::write_delta(m_bvb, prev_val);

            for (uint64_t pos = 1; pos < size; ++pos) {
                uint32_t val = edit_list[pos];
                assert(val >= prev_val + 1);
                util::write_delta(m_bvb, val - (prev_val + 1));
                prev_val = val;
            }

            m_list_offsets.push_back(m_bvb.num_bits());
        }

        void build(differential& d) {
            d.m_num_docs = m_num_docs;
            d.m_colors.swap(m_bvb.bits());
            d.m_clusters.build(&m_clusters);

            d.m_reference_offsets.swap(m_reference_offsets);
            d.m_list_offsets.swap(m_list_offsets);
        }

    private:
        bit_vector_builder m_bvb;
        pthash::bit_vector_builder m_clusters;

        uint64_t m_num_docs;
        std::vector<uint64_t> m_reference_offsets;
        std::vector<uint64_t> m_list_offsets;
        uint64_t m_prev_cluster_id;
    };

    struct forward_iterator {
        forward_iterator(differential<ColorClasses> const* ptr, uint64_t list_begin,
                         uint64_t reference_begin)
            : m_ptr(ptr), m_edit_list_begin(list_begin), m_reference_begin(reference_begin) {
            rewind();
        }

        void rewind() {
            m_edit_list_it = bit_vector_iterator((m_ptr->m_colors).data(), (m_ptr->m_colors).size(),
                                                 m_edit_list_begin);
            m_reference_it = bit_vector_iterator((m_ptr->m_colors).data(), (m_ptr->m_colors).size(),
                                                 m_reference_begin);
            m_edit_list_size = util::read_delta(m_edit_list_it);
            m_reference_size = util::read_delta(m_reference_it);

            m_curr_edit_val = m_edit_list_size == 0 ? num_docs() : util::read_delta(m_edit_list_it);
            m_prev_edit_val = 0;
            m_curr_reference_val = m_reference_size == 0 ? num_docs() : util::read_delta(m_reference_it);
            m_prev_reference_val = 0;

            m_pos_in_edit_list = 0;
            m_pos_in_reference = 0;
            update_curr_val();
        }

        uint64_t value() const { return m_curr_val; }
        uint64_t operator*() const { return value(); }

        void next() {
            if (m_pos_in_reference >= m_reference_size && m_pos_in_edit_list >= m_edit_list_size) {
                m_curr_val = num_docs();
                return;
            }
            if (m_pos_in_reference >= m_reference_size  ||
                m_curr_edit_val < m_curr_reference_val) {
                next_edit_val();
            } else if (m_pos_in_edit_list >= m_edit_list_size ||
                       m_curr_reference_val < m_curr_edit_val) {
                next_reference_val();
            }
            update_curr_val();
        }
        void operator++() { next(); }

        bool has_next() {
            return m_pos_in_reference == m_reference_size && m_pos_in_edit_list == m_edit_list_size;
        }

        uint32_t num_docs() const { return m_ptr->m_num_docs; }

    private:
        differential<ColorClasses> const* m_ptr;
        uint64_t m_edit_list_begin, m_reference_begin;
        uint64_t m_reference_size, m_edit_list_size;
        uint64_t m_pos_in_edit_list, m_pos_in_reference;
        uint32_t m_curr_reference_val, m_curr_edit_val;
        uint32_t m_prev_reference_val, m_prev_edit_val;
        uint32_t m_curr_val;
        bit_vector_iterator m_reference_it, m_edit_list_it;

        void next_reference_val() {
            m_pos_in_reference += 1;
            m_prev_reference_val = m_curr_reference_val;
            if (m_pos_in_reference < m_reference_size) {
                m_curr_reference_val = m_prev_reference_val + util::read_delta(m_reference_it) + 1;
            } else {
                m_curr_reference_val = num_docs();
            }
        }

        void next_edit_val() {
            m_pos_in_edit_list += 1;
            m_prev_edit_val = m_curr_edit_val;
            if (m_pos_in_edit_list < m_edit_list_size) {
                m_curr_edit_val = m_prev_edit_val + util::read_delta(m_edit_list_it) + 1;
            } else {
                m_curr_edit_val = num_docs();
            }
        }

        void update_curr_val() {
            while (m_curr_reference_val == m_curr_edit_val &&
                   m_pos_in_reference <= m_reference_size &&
                   m_pos_in_edit_list <= m_edit_list_size) {
                next_edit_val();
                next_reference_val();
            }
            m_curr_val = min(m_curr_edit_val, m_curr_reference_val);
        }
    };

    typedef forward_iterator iterator_type;

    forward_iterator colors(uint64_t color_id) const {
        assert(color_id < num_color_classes());
        uint64_t list_begin = m_list_offsets[color_id];
        uint64_t reference_begin = m_reference_offsets[m_clusters.rank(color_id)];
        return forward_iterator(this, list_begin, reference_begin);
    }

    uint64_t num_color_classes() const { return m_list_offsets.size() - 1; }
    uint64_t num_docs() const { return m_num_docs; }

private:
    uint32_t m_num_docs;
    std::vector<uint64_t> m_reference_offsets, m_list_offsets;

    std::vector<uint64_t> m_colors;
    ranked_bit_vector m_clusters;
};

}  // namespace fulgor