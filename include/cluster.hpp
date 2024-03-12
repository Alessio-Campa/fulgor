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
        for (int32_t j = 0; j < num_docs; j++) {
            if (r < reference.size() && e < edit_lists[i].size() && edit_lists[i][e] == 0 && reference[r] == 0) {
                r++;
                e++;
                continue;
            }
            if (r < reference.size() && e < edit_lists[i].size() && reference[r] == j &&
                -edit_lists[i][e] != j) {
                color_list.push_back(j);
            }
            if (r < reference.size() && e >= edit_lists[i].size() && reference[r] == j){
                color_list.push_back(j);
            }
            if (e < edit_lists[i].size() && edit_lists[i][e] == j) color_list.push_back(j);
            if (r < reference.size() && reference[r] == j) r++;
            if (e < edit_lists[i].size() && (edit_lists[i][e] == j || -edit_lists[i][e] == j)) e++;
        }
        return color_list;
    }

    uint64_t num_docs;
    std::vector<uint32_t> reference;
    std::vector<std::vector<int32_t>> edit_lists;

    std::vector<int32_t> edit_list(const std::vector<uint32_t>& L) {
        assert(std::is_sorted(L.begin(), L.end()));
        assert(std::is_sorted(reference.begin(), reference.end()));

        std::vector<int> E;

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
                E.push_back(-reference[j]);  // note: should be -R[j]
                j += 1;
            }
        }
        while (i < L.size()) {
            E.push_back(L[i]);
            i += 1;
        }

        while (j < reference.size()) {
            E.push_back(-reference[j]);  // note: should be -R[j]
            j += 1;
        }

        return E;
    }
};

}  // namespace fulgor