#ifndef LIBNEST2D_HPP
#define LIBNEST2D_HPP

#include <memory>
#include <vector>
#include <map>
#include <array>
#include <algorithm>
#include <functional>

#include "geometry_traits.hpp"
//#include "optimizers/subplex.hpp"
//#include "optimizers/simplex.hpp"
#include "optimizers/genetic.hpp"

namespace libnest2d {

/**
 * \brief An item to be placed on a bin.
 *
 * It holds a copy of the original shape object but supports move construction
 * from the shape objects if its an rvalue reference. This way we can construct
 * the items without the cost of copying a potentially large amount of input.
 *
 * The results of some calculations are cached for maintaining fast run times.
 * For this reason, memory demands are much higher but this should pay off.
 */
template<class RawShape>
class _Item {
    using Coord = TCoord<TPoint<RawShape>>;
    using Vertex = TPoint<RawShape>;
    using Box = _Box<Vertex>;

    // The original shape that gets encapsulated.
    RawShape sh_;

    // Transformation data
    Vertex translation_;
    Radians rotation_;
    Coord offset_distance_;

    // Info about whether the tranformations will have to take place
    // This is needed because if floating point is used, it is hard to say
    // that a zero angle is not a rotation because of testing for equality.
    bool has_rotation_ = false, has_translation_ = false, has_offset_ = false;

    // For caching the calculations as they can get pretty expensive.
    mutable RawShape tr_cache_;
    mutable bool tr_cache_valid_ = false;
    mutable double area_cache_ = 0;
    mutable bool area_cache_valid_ = false;
    mutable RawShape offset_cache_;
    mutable bool offset_cache_valid_ = false;

public:

    /// The type of the shape which was handed over as the template argument.
    using ShapeType = RawShape;

    /**
     * \brief Iterator type for the outer vertices.
     *
     * Only const iterators can be used. The _Item type is not intended to
     * modify the carried shapes from the outside. The main purpose of this type
     * is to cache the calculation results from the various operators it
     * supports. Giving out a non const iterator would make it impossible to
     * perform correct cache invalidation.
     */
    using Iterator = TVertexConstIterator<RawShape>;

    /**
     * @brief Get the orientation of the polygon.
     *
     * The orientation have to be specified as a specialization of the
     * OrientationType struct which has a Value constant.
     *
     * @return The orientation type identifier for the _Item type.
     */
    static BP2D_CONSTEXPR Orientation orientation() {
        return OrientationType<RawShape>::Value;
    }

    /**
     * @brief Constructing an _Item form an existing raw shape. The shape will
     * be copied into the _Item object.
     * @param sh The original shape object.
     */
    explicit inline _Item(const RawShape& sh): sh_(sh) {}

    /**
     * @brief Construction of an item by moving the content of the raw shape,
     * assuming that it supports move semantics.
     * @param sh The original shape object.
     */
    explicit inline _Item(RawShape&& sh): sh_(std::move(sh)) {}

    /**
     * @brief Create an item from an initilizer list.
     * @param il The initializer list of vertices.
     */
    inline _Item(const std::initializer_list< Vertex >& il):
        sh_(ShapeLike::create<RawShape>(il)) {}

    inline _Item(const TContour<RawShape>& contour):
        sh_(ShapeLike::create<RawShape>(contour)) {}

    inline _Item(TContour<RawShape>&& contour):
        sh_(ShapeLike::create<RawShape>(std::move(contour))) {}

    /**
     * @brief Convert the polygon to string representation. The format depends
     * on the implementation of the polygon.
     * @return
     */
    inline std::string toString() const
    {
        return ShapeLike::toString(sh_);
    }

    /// Iterator tho the first vertex in the polygon.
    inline Iterator begin() const
    {
        return ShapeLike::cbegin(sh_);
    }

    /// Alias to begin()
    inline Iterator cbegin() const
    {
        return ShapeLike::cbegin(sh_);
    }

    /// Iterator to the last element.
    inline Iterator end() const
    {
        return ShapeLike::cend(sh_);
    }

    /// Alias to end()
    inline Iterator cend() const
    {
        return ShapeLike::cend(sh_);
    }

    /**
     * @brief Get a copy of an outer vertex whithin the carried shape.
     *
     * Note that the vertex considered here is taken from the original shape
     * that this item is constructed from. This means that no transformation is
     * applied to the shape in this call.
     *
     * @param idx The index of the requested vertex.
     * @return A copy of the requested vertex.
     */
    inline Vertex vertex(unsigned long idx) const
    {
        return ShapeLike::vertex(sh_, idx);
    }

    /**
     * @brief Modify a vertex.
     *
     * Note that this method will invalidate every cached calculation result
     * including polygon offset and transformations.
     *
     * @param idx The index of the requested vertex.
     * @param v The new vertex data.
     */
    inline void setVertex(unsigned long idx,
                          const Vertex& v )
    {
        invalidateCache();
        ShapeLike::vertex(sh_, idx) = v;
    }

    /**
     * @brief Calculate the shape area.
     *
     * The method returns absolute value and does not reflect polygon
     * orientation. The result is cached, subsequent calls will have very little
     * cost.
     * @return The shape area in floating point double precision.
     */
    inline double area() const {
        double ret ;
        if(area_cache_valid_) ret = area_cache_;
        else {
            ret = ShapeLike::area(offsettedShape());
            area_cache_ = ret;
            area_cache_valid_ = true;
        }
        return ret;
    }

    /// The number of the outer ring vertices.
    inline size_t vertexCount() const {
        return ShapeLike::contourVertexCount(sh_);
    }

    /**
     * @brief isPointInside
     * @param p
     * @return
     */
    inline bool isPointInside(const Vertex& p)
    {
        return ShapeLike::isInside(p, sh_);
    }

    inline bool isInside(const _Item& sh) const
    {
        return ShapeLike::isInside(transformedShape(), sh.transformedShape());
    }

    inline bool isInside(const _Box<TPoint<RawShape>>& box);

    inline void translate(const Vertex& d) BP2D_NOEXCEPT
    {
        translation_ += d; has_translation_ = true;
        tr_cache_valid_ = false;
    }

    inline void rotate(const Radians& rads) BP2D_NOEXCEPT
    {
        rotation_ += rads;
        has_rotation_ = true;
        tr_cache_valid_ = false;
    }

    inline void addOffset(Coord distance) BP2D_NOEXCEPT
    {
        offset_distance_ = distance;
        has_offset_ = true;
        offset_cache_valid_ = false;
    }

    inline void removeOffset() BP2D_NOEXCEPT {
        has_offset_ = false;
        invalidateCache();
    }

    inline Radians rotation() const BP2D_NOEXCEPT
    {
        return rotation_;
    }

    inline TPoint<RawShape> translation() const BP2D_NOEXCEPT
    {
        return translation_;
    }

    inline void rotation(Radians rot) BP2D_NOEXCEPT
    {
        if(rotation_ != rot) {
            rotation_ = rot; has_rotation_ = true; tr_cache_valid_ = false;
        }
    }

    inline void translation(const TPoint<RawShape>& tr) BP2D_NOEXCEPT
    {
        if(translation_ != tr) {
            translation_ = tr; has_translation_ = true; tr_cache_valid_ = false;
        }
    }

    inline RawShape transformedShape() const
    {
        if(tr_cache_valid_) return tr_cache_;

        RawShape cpy = offsettedShape();
        if(has_rotation_) ShapeLike::rotate(cpy, rotation_);
        if(has_translation_) ShapeLike::translate(cpy, translation_);
        tr_cache_ = cpy; tr_cache_valid_ = true;

        return cpy;
    }

    inline operator RawShape() const
    {
        return transformedShape();
    }

    inline const RawShape& rawShape() const BP2D_NOEXCEPT
    {
        return sh_;
    }

    inline void resetTransformation() BP2D_NOEXCEPT
    {
        has_translation_ = false; has_rotation_ = false; has_offset_ = false;
    }

    inline Box boundingBox() const {
        return ShapeLike::boundingBox(transformedShape());
    }

    //Static methods:

    inline static bool intersects(const _Item& sh1, const _Item& sh2)
    {
        return ShapeLike::intersects(sh1.transformedShape(),
                                     sh2.transformedShape());
    }

    inline static bool touches(const _Item& sh1, const _Item& sh2)
    {
        return ShapeLike::touches(sh1.transformedShape(),
                                  sh2.transformedShape());
    }

private:

    inline const RawShape& offsettedShape() const {
        if(has_offset_ ) {
            if(offset_cache_valid_) return offset_cache_;
            else {
                offset_cache_ = sh_;
                ShapeLike::offset(offset_cache_, offset_distance_);
                offset_cache_valid_ = true;
                return offset_cache_;
            }
        }
        return sh_;
    }

    inline void invalidateCache() const BP2D_NOEXCEPT
    {
        tr_cache_valid_ = false;
        area_cache_valid_ = false;
        offset_cache_valid_ = false;
    }
};

/**
 * \brief Subclass of _Item for regular rectangle items.
 */
template<class RawShape>
class _Rectangle: public _Item<RawShape> {
    RawShape sh_;
    using _Item<RawShape>::vertex;
    using TO = Orientation;
public:

    using Unit = TCoord<TPoint<RawShape>>;

    template<TO o = OrientationType<RawShape>::Value>
    inline _Rectangle(Unit width, Unit height,
                      // disable this ctor if o != CLOCKWISE
                      enable_if_t< o == TO::CLOCKWISE, int> = 0 ):
        _Item<RawShape>( ShapeLike::create<RawShape>( {
                                                        {0, 0},
                                                        {0, height},
                                                        {width, height},
                                                        {width, 0},
                                                        {0, 0}
                                                      } ))
    {
    }

    template<TO o = OrientationType<RawShape>::Value>
    inline _Rectangle(Unit width, Unit height,
                      // disable this ctor if o != COUNTER_CLOCKWISE
                      enable_if_t< o == TO::COUNTER_CLOCKWISE, int> = 0 ):
        _Item<RawShape>( ShapeLike::create<RawShape>( {
                                                        {0, 0},
                                                        {width, 0},
                                                        {width, height},
                                                        {0, height},
                                                        {0, 0}
                                                      } ))
    {
    }

    inline Unit width() const BP2D_NOEXCEPT {
        return getX(vertex(2));
    }

    inline Unit height() const BP2D_NOEXCEPT {
        return getY(vertex(2));
    }
};

template<class RawShape>
inline bool _Item<RawShape>::isInside(const _Box<TPoint<RawShape>>& box) {
    _Rectangle<RawShape> rect(box.width(), box.height());
    return _Item<RawShape>::isInside(rect);
}

/**
 * \brief A wrapper interface (trait) class for any placement strategy provider.
 *
 * If a client want's to use its own placement algorithm, all it has to do is to
 * specialize this class template and define all the ten methods it has. It can
 * use the strategies::PlacerBoilerplace class for creating a new placement
 * strategy where only the constructor and the trypack method has to be provided
 * and it will work out of the box.
 */
template<class PlacementStrategy>
class PlacementStrategyLike {
    PlacementStrategy impl_;
public:

    /// The item type that the placer works with.
    using Item = typename PlacementStrategy::Item;

    /// The placer's config type. Should be a simple struct but can be anything.
    using Config = typename PlacementStrategy::Config;

    /**
     * \brief The type of the bin that the placer works with.
     *
     * Can be a box or an arbitrary shape or just a width or height without a
     * second dimension if an infinite bin is considered.
     */
    using BinType = typename PlacementStrategy::BinType;

    /**
     * \brief Pack result that can be used to accept or discard it. See trypack
     * method.
     */
    using PackResult = typename PlacementStrategy::PackResult;

    using ItemRef = std::reference_wrapper<Item>;
    using ItemGroup = std::vector<ItemRef>;

    /**
     * @brief Constructor taking the bin and an optional configuration.
     * @param bin The bin object whose type is defined by the placement strategy.
     * @param config The configuration for the particular placer.
     */
    explicit PlacementStrategyLike(const BinType& bin,
                                   const Config& config = Config()):
        impl_(bin)
    {
        configure(config);
    }

    /**
     * @brief Provide a different configuration for the placer.
     *
     * Note that it depends on the particular placer implementation how it
     * reacts to config changes in the middle of a calculation.
     *
     * @param config The configuration object defined by the placement startegy.
     */
    inline void configure(const Config& config) { impl_.configure(config); }

    /**
     * @brief A method that tries to pack an item and returns an object
     * describing the pack result.
     *
     * The result can be casted to bool and used as an argument to the accept
     * method to accept a succesfully packed item. This way the next packing
     * will consider the accepted item as well. The PackResult should carry the
     * transformation info so that if the tried item is later modified or tried
     * multiple times, the result object should set it to the originally
     * determied position. An implementation can be found in the
     * strategies::PlacerBoilerplate::PackResult class.
     *
     * @param item Ithe item to be packed.
     * @return The PackResult object that can be implicitly casted to bool.
     */
    inline PackResult trypack(Item& item) { return impl_.trypack(item); }

    /**
     * @brief A method to accept a previously tried item.
     *
     * If the pack result is a failure the method should ignore it.
     * @param r The result of a previous trypack call.
     */
    inline void accept(PackResult& r) { impl_.accept(r); }

    /**
     * @brief pack Try to pack an item and immediately accept it on success.
     *
     * A default implementation would be to call
     * { auto&& r = trypack(item); accept(r); return r; } but we should let the
     * implementor of the placement strategy to harvest any optimizations from
     * the absence of an intermadiate step. The above version can still be used
     * in the implementation.
     *
     * @param item The item to pack.
     * @return Returns true if the item was packed or false if it could not be
     * packed.
     */
    inline bool pack(Item& item) { return impl_.pack(item); }

    /// Unpack the last element (remove it from the list of packed items).
    inline void unpackLast() { impl_.unpackLast(); }

    /// Get the bin object.
    inline const BinType& bin() const { return impl_.bin(); }

    /// Set a new bin object.
    inline void bin(const BinType& bin) { impl_.bin(bin); }

    /// Get the packed items.
    inline ItemGroup getItems() { return impl_.getItems(); }

    /// Clear the packed items so a new session can be started.
    inline void clearItems() { impl_.clearItems(); }

    inline double filledArea() const { return impl_.filledArea(); }

#ifndef NDEBUG
    inline auto getDebugItems() -> decltype(impl_.debug_items_)&
    {
        return impl_.debug_items_;
    }
#endif

};

// The progress function will be called with the number of placed items
using ProgressFunction = std::function<void(unsigned)>;

/**
 * A wrapper interface (trait) class for any selections strategy provider.
 */
template<class SelectionStrategy>
class SelectionStrategyLike {
    SelectionStrategy impl_;
public:
    using Item = typename SelectionStrategy::Item;
    using Config = typename SelectionStrategy::Config;

    using ItemRef = std::reference_wrapper<Item>;
    using ItemGroup = std::vector<ItemRef>;

    /**
     * @brief Provide a different configuration for the selection strategy.
     *
     * Note that it depends on the particular placer implementation how it
     * reacts to config changes in the middle of a calculation.
     *
     * @param config The configuration object defined by the selection startegy.
     */
    inline void configure(const Config& config) {
        impl_.configure(config);
    }

    /**
     * @brief A function callback which should be called whenewer an item or
     * a group of items where succesfully packed.
     * @param fn A function callback object taking one unsigned integer as the
     * number of the remaining items to pack.
     */
    void progressIndicator(ProgressFunction fn) { impl_.progressIndicator(fn); }

    /**
     * \brief A method to start the calculation on the input sequence.
     *
     * \tparam TPlacer The only mandatory template parameter is the type of
     * placer compatible with the PlacementStrategyLike interface.
     *
     * \param first, last The first and last iterator if the input sequence. It
     * can be only an iterator of a type converitible to Item.
     * \param bin. The shape of the bin. It has to be supported by the placement
     * strategy.
     * \param An optional config object for the placer.
     */
    template<class TPlacer, class TIterator,
             class TBin = typename PlacementStrategyLike<TPlacer>::BinType,
             class PConfig = typename PlacementStrategyLike<TPlacer>::Config>
    inline void packItems(
            TIterator first,
            TIterator last,
            TBin&& bin,
            PConfig&& config = PConfig() )
    {
        impl_.template packItems<TPlacer>(first, last,
                                 std::forward<TBin>(bin),
                                 std::forward<PConfig>(config));
    }

    /**
     * \brief Get the number of bins opened by the selection algorithm.
     *
     * Initially it is zero and after the call to packItems it will return
     * the number of bins opened by the packing procedure.
     *
     * \return The number of bins opened.
     */
    inline size_t binCount() const { return impl_.binCount(); }

    /**
     * @brief Get the items for a particular bin.
     * @param binIndex The index of the requested bin.
     * @return Returns a list of allitems packed into the requested bin.
     */
    inline ItemGroup itemsForBin(size_t binIndex) {
        return impl_.itemsForBin(binIndex);
    }

    /// Same as itemsForBin but for a const context.
    inline const ItemGroup itemsForBin(size_t binIndex) const {
        return impl_.itemsForBin(binIndex);
    }
};


/**
 * \brief A list of packed item vectors. Each vector represents a bin.
 */
template<class RawShape>
using _PackGroup = std::vector<
                        std::vector<
                            std::reference_wrapper<_Item<RawShape>>
                        >
                   >;

/**
 * \brief A list of packed (index, item) pair vectors. Each vector represents a
 * bin.
 *
 * The index is points to the position of the item in the original input
 * sequence. This way the caller can use the items as a transformation data
 * carrier and transform the original objects manually.
 */
template<class RawShape>
using _IndexedPackGroup = std::vector<
                               std::vector<
                                   std::pair<
                                       unsigned,
                                       std::reference_wrapper<_Item<RawShape>>
                                   >
                               >
                          >;

/**
 * The Arranger is the frontend class for the binpack2d library. It takes the
 * input items and outputs the items with the proper transformations to be
 * inside the provided bin.
 */
template<class PlacementStrategy, class SelectionStrategy >
class Arranger {
    using TSel = SelectionStrategyLike<SelectionStrategy>;
    TSel selector_;
    bool use_min_bb_rotation_ = false;
public:
    using Item = typename PlacementStrategy::Item;
    using ItemRef = std::reference_wrapper<Item>;
    using TPlacer = PlacementStrategyLike<PlacementStrategy>;
    using BinType = typename TPlacer::BinType;
    using PlacementConfig = typename TPlacer::Config;
    using SelectionConfig = typename TSel::Config;

    using Unit = TCoord<TPoint<typename Item::ShapeType>>;

    using IndexedPackGroup = _IndexedPackGroup<typename Item::ShapeType>;
    using PackGroup = _PackGroup<typename Item::ShapeType>;

private:
    BinType bin_;
    PlacementConfig pconfig_;
    Unit min_obj_distance_;

    using SItem =  typename SelectionStrategy::Item;
    using TPItem = remove_cvref_t<Item>;
    using TSItem = remove_cvref_t<SItem>;

    std::vector<TPItem> item_cache_;

public:

    /**
     * \brief Constructor taking the bin as the only mandatory parameter.
     *
     * \param bin The bin shape that will be used by the placers. The type
     * of the bin should be one that is supported by the placer type.
     */
    template<class TBinType = BinType,
             class PConf = PlacementConfig,
             class SConf = SelectionConfig>
    Arranger( TBinType&& bin,
              Unit min_obj_distance = 0,
              PConf&& pconfig = PConf(),
              SConf&& sconfig = SConf()):
        bin_(std::forward<TBinType>(bin)),
        pconfig_(std::forward<PlacementConfig>(pconfig)),
        min_obj_distance_(min_obj_distance)
    {
        static_assert( std::is_same<TPItem, TSItem>::value,
                       "Incompatible placement and selection strategy!");

        selector_.configure(std::forward<SelectionConfig>(sconfig));
    }

    /**
     * \brief Arrange an input sequence and return a PackGroup object with
     * the packed groups corresponding to the bins.
     *
     * The number of groups in the pack group is the number of bins opened by
     * the selection algorithm.
     */
    template<class TIterator>
    inline PackGroup arrange(TIterator from, TIterator to)
    {
        return _arrange(from, to);
    }

    /**
     * A version of the arrange method returning an IndexedPackGroup with
     * the item indexes into the original input sequence.
     *
     * Takes a little longer to collect the indices. Scales linearly with the
     * input sequence size.
     */
    template<class TIterator>
    inline IndexedPackGroup arrangeIndexed(TIterator from, TIterator to)
    {
        return _arrangeIndexed(from, to);
    }

    /// Shorthand to normal arrange method.
    template<class TIterator>
    inline PackGroup operator() (TIterator from, TIterator to)
    {
        return _arrange(from, to);
    }

    /// Set a progress indicatior function object for the selector.
    inline Arranger& progressIndicator(ProgressFunction func)
    {
        selector_.progressIndicator(func); return *this;
    }

    inline PackGroup lastResult() {
        PackGroup ret;
        for(size_t i = 0; i < selector_.binCount(); i++) {
            auto items = selector_.itemsForBin(i);
            ret.push_back(items);
        }
        return ret;
    }

    inline Arranger& useMinimumBoundigBoxRotation(bool s = true) {
        use_min_bb_rotation_ = s; return *this;
    }

private:

    template<class TIterator,
             class IT = remove_cvref_t<typename TIterator::value_type>,

             // This funtion will be used only if the iterators are pointing to
             // a type compatible with the binpack2d::_Item template.
             // This way we can use references to input elements as they will
             // have to exist for the lifetime of this call.
             class T = enable_if_t< std::is_convertible<IT, TPItem>::value, IT>
             >
    inline PackGroup _arrange(TIterator from, TIterator to, bool = false)
    {
        __arrange(from, to);
        return lastResult();
    }

    template<class TIterator,
             class IT = remove_cvref_t<typename TIterator::value_type>,
             class T = enable_if_t<!std::is_convertible<IT, TPItem>::value, IT>
             >
    inline PackGroup _arrange(TIterator from, TIterator to, int = false)
    {
        item_cache_ = {from, to};

        __arrange(item_cache_.begin(), item_cache_.end());
        return lastResult();
    }

    template<class TIterator,
             class IT = remove_cvref_t<typename TIterator::value_type>,

             // This funtion will be used only if the iterators are pointing to
             // a type compatible with the binpack2d::_Item template.
             // This way we can use references to input elements as they will
             // have to exist for the lifetime of this call.
             class T = enable_if_t< std::is_convertible<IT, TPItem>::value, IT>
             >
    inline IndexedPackGroup _arrangeIndexed(TIterator from,
                                            TIterator to,
                                            bool = false)
    {
        __arrange(from, to);
        return createIndexedPackGroup(from, to, selector_);
    }

    template<class TIterator,
             class IT = remove_cvref_t<typename TIterator::value_type>,
             class T = enable_if_t<!std::is_convertible<IT, TPItem>::value, IT>
             >
    inline IndexedPackGroup _arrangeIndexed(TIterator from,
                                            TIterator to,
                                            int = false)
    {
        item_cache_ = {from, to};
        __arrange(item_cache_.begin(), item_cache_.end());
        return createIndexedPackGroup(from, to, selector_);
    }

    template<class TIterator>
    static IndexedPackGroup createIndexedPackGroup(TIterator from,
                                                   TIterator to,
                                                   TSel& selector)
    {
        IndexedPackGroup pg;
        pg.reserve(selector.binCount());

        for(size_t i = 0; i < selector.binCount(); i++) {
            auto items = selector.itemsForBin(i);
            pg.push_back({});
            pg[i].reserve(items.size());

            for(Item& itemA : items) {
                auto it = from;
                unsigned idx = 0;
                while(it != to) {
                    Item& itemB = *it;
                    if(&itemB == &itemA) break;
                    it++; idx++;
                }
                pg[i].emplace_back(idx, itemA);
            }
        }

        return pg;
    }

    Radians findBestRotation(Item& item) {
        opt::StopCriteria stopcr;
        stopcr.stoplimit = 0.01;
        stopcr.max_iterations = 10000;
        stopcr.type = opt::StopLimitType::RELATIVE;
        opt::TOptimizer<opt::Method::G_GENETIC> solver(stopcr);

        auto orig_rot = item.rotation();

        auto result = solver.optimize_min([&item, &orig_rot](Radians rot){
            item.rotation(orig_rot + rot);
            auto bb = item.boundingBox();
            return std::sqrt(bb.height()*bb.width());
        }, opt::initvals(Radians(0)), opt::bound<Radians>(-Pi/2, Pi/2));

        item.rotation(orig_rot);

        return std::get<0>(result.optimum);
    }

    template<class TIter> inline void __arrange(TIter from, TIter to)
    {
        if(min_obj_distance_ > 0) std::for_each(from, to, [this](Item& item) {
            item.addOffset(static_cast<Unit>(std::ceil(min_obj_distance_/2.0)));
        });

        if(use_min_bb_rotation_)
            std::for_each(from, to, [this](Item& item){
                Radians rot = findBestRotation(item);
                item.rotate(rot);
            });

        selector_.template packItems<PlacementStrategy>(
                    from, to, bin_, pconfig_);

        if(min_obj_distance_ > 0) std::for_each(from, to, [](Item& item) {
            item.removeOffset();
        });

    }
};

}

#endif // LIBNEST2D_HPP
