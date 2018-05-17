#ifndef SELECTION_BOILERPLATE_HPP
#define SELECTION_BOILERPLATE_HPP

#include "../libnest2d.hpp"

namespace libnest2d {
namespace strategies {

template<class RawShape>
class SelectionBoilerplate {
public:
    using Item = _Item<RawShape>;
    using ItemRef = std::reference_wrapper<Item>;
    using ItemGroup = std::vector<ItemRef>;
    using PackGroup = std::vector<ItemGroup>;

    size_t binCount() const { return packed_bins_.size(); }

    ItemGroup itemsForBin(size_t binIndex) {
        assert(binIndex < packed_bins_.size());
        return packed_bins_[binIndex];
    }

    inline const ItemGroup itemsForBin(size_t binIndex) const {
        assert(binIndex < packed_bins_.size());
        return packed_bins_[binIndex];
    }

protected:
    PackGroup packed_bins_;
};

}
}

#endif // SELECTION_BOILERPLATE_HPP
