#pragma once

namespace fulgor {

struct cluster {
    cluster() {}
    cluster(uint32_t n_docs, std::vector<uint32_t> const& ref) {
        num_docs = n_docs;
        reference = ref;
    }

    void append_color_list(const std::vector<uint32_t>& list) {
        edit_lists.push_back(edit_list(list));
    }

    std::vector<uint32_t> colors(uint32_t i) {
        std::vector<uint32_t> color_list;
        uint32_t r = 0, e = 0;
        while (r != reference.size() && e != edit_lists[i].size()) {
            if (reference[r] == edit_lists[i][e]) {
                r++;
                e++;
            } else if (reference[r] < edit_lists[i][e]){
                color_list.push_back(reference[r++]);
            } else {
                color_list.push_back(edit_lists[i][e++]);
            }
        }
        while (r != reference.size()){
            color_list.push_back(reference[r++]);
        }
        while (e != edit_lists[i].size()){
            color_list.push_back(edit_lists[i][e++]);
        }
        return color_list;
    }

    uint64_t compressed_size(uint64_t color_id) {
        bit_vector_builder m_bvb;
        uint64_t list_size = edit_lists[color_id].size();

        util::write_delta(m_bvb, list_size);
        if (list_size != 0) {
            uint32_t prev_val = edit_lists[color_id][0];
            util::write_delta(m_bvb, prev_val);
            for (uint64_t i = 1; i != list_size; ++i) {
                uint32_t val = edit_lists[color_id][i];
                assert(val >= prev_val + 1);
                util::write_delta(m_bvb, val - (prev_val + 1));
                prev_val = val;
            }
        }
        return m_bvb.num_bits();
    }

    uint64_t num_docs;
    std::vector<uint32_t> reference;
    std::vector<std::vector<uint32_t>> edit_lists;

    std::vector<uint32_t> edit_list(
        const std::vector<uint32_t>& L) {  // TODO: user set_symmetric_difference
        assert(std::is_sorted(L.begin(), L.end()));
        assert(std::is_sorted(reference.begin(), reference.end()));

        std::vector<uint32_t> E;

        /* at most (when L and R have empty intersection) */
        E.reserve(L.size() + reference.size());

        uint64_t i = 0, j = 0;
        while (i < L.size() and j < reference.size()) {
            if (L[i] == reference[j]) {
                i += 1;
                j += 1;
            } else if (L[i] < reference[j]) {
                E.push_back(L[i]);
                i += 1;
            } else {
                E.push_back(reference[j]);  // note: should be -R[j]
                j += 1;
            }
        }
        while (i < L.size()) {
            E.push_back(L[i]);
            i += 1;
        }

        while (j < reference.size()) {
            E.push_back(reference[j]);  // note: should be -R[j]
            j += 1;
        }

        return E;
    }
};

}  // namespace fulgor